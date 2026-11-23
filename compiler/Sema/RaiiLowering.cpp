//===- RaiiLowering.cpp - C+ deterministic cleanup lowering -----*- C++ -*-===//

#include "Sema/RaiiLowering.h"

#include "Sema/CPlusLexer.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace cplus {

namespace {

constexpr size_t kNPos = static_cast<size_t>(-1);
const char *kOwnPrefix = "/*cplus$own:";
const char *kMemPrefix = "/*cplus$mem:";

struct OwnedVar {
  std::string name;
  std::string type;   // resource type (for drop generation)
  std::string drop;   // destructor symbol
  bool managed = false;
  bool isDefer = false;
  std::string deferText;
  unsigned line = 0;
  unsigned column = 0;
};

struct Scope {
  size_t closeOffset = 0;
  unsigned closeLine = 0;
  std::vector<OwnedVar> vars;
  bool functionBody = false;
};

struct LoopInfo {
  /// Index in `scopes` of the loop body block, or -1 when the body is not a
  /// compound statement.
  int bodyPos = -1;
  size_t stackSizeAtKeyword = 0;
};

struct Edit {
  size_t offset = 0;
  size_t length = 0;
  std::string text;
  unsigned seq = 0;
};

bool startsWith(const std::string &s, const char *prefix) {
  size_t n = std::char_traits<char>::length(prefix);
  return s.size() >= n && s.compare(0, n, prefix) == 0;
}

std::string quoteFile(const std::string &file) {
  std::string out;
  for (char c : file) {
    if (c == '\\' || c == '"')
      out += '\\';
    out += c;
  }
  return out;
}

class Lowering {
public:
  Lowering(const std::string &source, std::vector<std::string> resourceTypes,
           DiagnosticEngine &diags, const std::string &filename, Mode mode,
           bool strict = false)
      : source_(source), toks_(lexCPlus(source)), diags_(diags),
        filename_(filename), mode_(mode), strict_(strict) {
    for (auto &t : resourceTypes)
      resourceTypes_.insert(t);
  }

  LoweredSource run() {
    // Plain-C raw-allocation / FILE lifetime checks.  Kept separate from the
    // ownership-lowering scan below: these patterns carry no C+ markers, so
    // they are diagnosed directly from the token stream.
    analyzePlainC();
    scan();
    applyEdits();

    LoweredSource result;
    result.text = std::move(output_);
    result.changed = !edits_.empty() || removedMarkers_ > 0;
    result.obligations = obligations_;
    return result;
  }

private:
  // --- token helpers --------------------------------------------------------

  size_t nextSig(size_t i) const {
    ++i;
    while (i < toks_.size() && isTrivia(i))
      ++i;
    return i;
  }

  size_t prevSig(size_t i) const {
    while (i > 0) {
      --i;
      if (!isTrivia(i))
        return i;
    }
    return kNPos;
  }

  bool isTrivia(size_t i) const {
    return toks_[i].kind == TokKind::Whitespace ||
           toks_[i].kind == TokKind::Comment;
  }

  bool isPunct(size_t i, const char *p) const {
    return i < toks_.size() && toks_[i].kind == TokKind::Punct &&
           toks_[i].text == p;
  }

  bool isIdent(size_t i, const char *s) const {
    return i < toks_.size() && toks_[i].kind == TokKind::Identifier &&
           toks_[i].text == s;
  }

  size_t statementEnd(size_t i) const {
    int paren = 0, bracket = 0, brace = 0;
    for (; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Punct)
        continue;
      const std::string &p = toks_[i].text;
      if (p == "(")
        ++paren;
      else if (p == ")")
        --paren;
      else if (p == "[")
        ++bracket;
      else if (p == "]")
        --bracket;
      else if (p == "{")
        ++brace;
      else if (p == "}")
        --brace;
      else if (p == ";" && paren == 0 && bracket == 0 && brace == 0)
        return i;
    }
    return kNPos;
  }

  std::string joinTokens(size_t from, size_t to) const {
    std::string out;
    for (size_t k = from; k < to; ++k) {
      if (isTrivia(k))
        continue;
      if (!out.empty())
        out += " ";
      out += toks_[k].text;
    }
    return out;
  }

  void handleDefer(size_t i) {
    size_t semi = statementEnd(i + 1);
    if (semi == kNPos || semi == i + 1) {
      errorAt("defer requires a statement", toks_[i]);
      return;
    }
    for (size_t k = i + 1; k < semi; ++k) {
      if (toks_[k].kind == TokKind::Identifier &&
          (toks_[k].text == "return" || toks_[k].text == "break" ||
           toks_[k].text == "continue" || toks_[k].text == "goto")) {
        errorAt("'" + toks_[k].text + "' cannot jump out of a defer cleanup",
                toks_[k]);
        return;
      }
    }
    OwnedVar v;
    v.name = "__cplus_defer_" + std::to_string(deferCounter_++);
    v.isDefer = true;
    v.deferText = joinTokens(i + 1, semi);
    v.line = toks_[i].line;
    v.column = toks_[i].column;
    registerOwned(v);
    edits_.push_back(Edit{toks_[i].offset,
                          toks_[semi].offset - toks_[i].offset,
                          std::string(), seq_++});
    atStmtStart_ = true;
  }

  // --- diagnostics ----------------------------------------------------------

  void errorAt(const std::string &msg, const Token &t,
               const std::string &help = std::string()) {
    Diag d;
    d.level = DiagLevel::Error;
    d.message = msg;
    d.file = filename_;
    d.line = t.line;
    d.column = t.column;
    d.help = help;
    diags_.report(std::move(d));
  }

  void warningAt(const std::string &msg, const Token &t,
                 const std::string &help = std::string()) {
    if (mode_ == Mode::LegacyC)
      return;
    Diag d;
    d.level = DiagLevel::Warning;
    d.message = msg;
    d.file = filename_;
    d.line = t.line;
    d.column = t.column;
    d.help = help;
    diags_.report(std::move(d));
  }

  /// Diagnostics for plain-C lifetime bugs that are "strict" findings: an
  /// error in STRICT mode, a warning in the default (Safe) mode, and nothing
  /// at all in LegacyC mode.
  void strictDiag(const std::string &msg, const Token &t,
                  const std::string &help = std::string()) {
    if (mode_ == Mode::LegacyC)
      return;
    if (strict_)
      errorAt(msg, t, help);
    else
      warningAt(msg, t, help);
  }

  /// Possible leaks are reported as warnings even in STRICT: the linear scan
  /// cannot prove a leak on every path, so rejecting would create false
  /// positives on valid C with early returns or complex control flow.
  void plainWarn(const std::string &msg, const Token &t,
                 const std::string &help = std::string()) {
    if (mode_ == Mode::LegacyC)
      return;
    warningAt(msg, t, help);
  }

  /// Diagnostics for plain-C lifetime bugs that are always rejected in safe
  /// modes (double close, use-after-close, escaping stack buffers).  LegacyC
  /// mode is left untouched.
  void plainError(const std::string &msg, const Token &t,
                  const std::string &help = std::string()) {
    if (mode_ == Mode::LegacyC)
      return;
    errorAt(msg, t, help);
  }

  // --- markers --------------------------------------------------------------

  bool parseMarker(const std::string &text, OwnedVar *out) {
    if (startsWith(text, kOwnPrefix)) {
      // /*cplus$own:NAME:DROP*/
      size_t start = std::char_traits<char>::length(kOwnPrefix);
      size_t end = text.rfind("*/");
      if (end == std::string::npos || end <= start)
        return false;
      std::string body = text.substr(start, end - start);
      size_t colon = body.find(':');
      if (colon == std::string::npos)
        return false;
      out->name = body.substr(0, colon);
      out->drop = body.substr(colon + 1);
      out->managed = false;
      if (out->drop.size() > 5 &&
          out->drop.compare(out->drop.size() - 5, 5, "_drop") == 0)
        out->type = out->drop.substr(0, out->drop.size() - 5);
      return !out->name.empty() && !out->drop.empty();
    }
    if (startsWith(text, kMemPrefix)) {
      size_t start = std::char_traits<char>::length(kMemPrefix);
      size_t end = text.find("*/", start);
      if (end == std::string::npos)
        return false;
      out->name = text.substr(start, end - start);
      out->managed = true;
      return !out->name.empty();
    }
    return false;
  }

  // --- main scan ------------------------------------------------------------

  void scan() {
    bool inDirective = false;

    for (size_t i = 0; i < toks_.size(); ++i) {
      const Token &t = toks_[i];

      if (isPunct(i, "#")) {
        inDirective = true;
        continue;
      }
      if (inDirective) {
        if (t.kind == TokKind::Whitespace && t.text.find('\n') != std::string::npos)
          inDirective = false;
        continue;
      }
      if (t.kind == TokKind::Comment) {
        handleMarker(i);
        continue;
      }
      if (t.kind == TokKind::Whitespace || t.kind == TokKind::EndOfFile)
        continue;
      if (t.kind == TokKind::Punct) {
        handlePunct(i, t);
        continue;
      }
      if (t.kind == TokKind::Identifier) {
        handleIdentifier(i, t);
        continue;
      }
    }

    if (scopes_.size() != 0) {
      // Unbalanced braces: the C compiler will report it, but make sure we
      // did not silently lose an ownership obligation.
      for (const auto &s : scopes_)
        for (const auto &v : s.vars)
          warningAt("cleanup for '" + v.name +
                        "' could not be placed in an unbalanced scope",
                    toks_[0]);
    }
  }

  void handleMarker(size_t i) {
    OwnedVar v;
    if (!parseMarker(toks_[i].text, &v))
      return;
    v.line = toks_[i].line;
    v.column = toks_[i].column;
    if (scopes_.empty()) {
      errorAt("ownership marker outside a function scope", toks_[i]);
      return;
    }
    registerOwned(v);
    edits_.push_back(
        Edit{toks_[i].offset, toks_[i].text.size(), std::string(), seq_++});
    ++removedMarkers_;
  }

  void registerOwned(const OwnedVar &v) {
    // A declaration can be discovered twice: once by the resource-type
    // declaration scan and once by its ownership marker.  Keep exactly one
    // cleanup obligation per variable per scope.
    for (auto &existing : scopes_.back().vars) {
      if (existing.name == v.name) {
        existing = v;
        owned_[v.name] = v;
        return;
      }
    }
    scopes_.back().vars.push_back(v);
    owned_[v.name] = v;
    ++obligations_;
  }

  void handlePunct(size_t i, const Token &t) {
    if (t.text == "(") {
      ++parenDepth_;
      atStmtStart_ = false;
      return;
    }
    if (t.text == ")") {
      if (parenDepth_ > 0)
        --parenDepth_;
      atStmtStart_ = false;
      return;
    }
    if (t.text == "{") {
      openScope(i, t);
      return;
    }
    if (t.text == "}") {
      closeScope(i, t);
      return;
    }
    if (t.text == ";") {
      atStmtStart_ = true;
      return;
    }
    if (t.text == ":") {
      atStmtStart_ = true;
      return;
    }
    if (t.text == "=") {
      checkAssignment(i);
      atStmtStart_ = false;
      return;
    }
    atStmtStart_ = false;
  }

  void openScope(size_t i, const Token &t) {
    bool isFunction = false;
    if (!inFunction_) {
      size_t p = prevSig(i);
      if (p != kNPos && isPunct(p, ")")) {
        isFunction = true;
        inFunction_ = true;
        owned_.clear();
        borrows_.clear();
        moved_.clear();
        movedOwners_.clear();
        explicitlyDropped_.clear();
      }
    }

    Scope s;
    s.closeOffset = t.offset;
    s.closeLine = t.line;
    s.functionBody = isFunction;
    scopes_.push_back(std::move(s));

    if (loops_.size() > 0 && loops_.back().bodyPos < 0)
      loops_.back().bodyPos = static_cast<int>(scopes_.size()) - 1;

    ++functionDepth_;
    atStmtStart_ = true;
  }

  void closeScope(size_t i, const Token &t) {
    if (scopes_.empty())
      return;
    Scope &s = scopes_.back();
    std::string drops = dropsFor(scopes_.size() - 1, scopes_.size(), {});
    if (!drops.empty())
      insertBefore(t, drops);

    if (loops_.size() > 0 &&
        loops_.back().bodyPos == static_cast<int>(scopes_.size()) - 1)
      loops_.pop_back();

    // A name declared in this scope no longer suppresses cleanup for a
    // later, unrelated variable with the same name.
    for (const auto &v : s.vars)
      explicitlyDropped_.erase(v.name);
    bool wasFunction = s.functionBody;
    scopes_.pop_back();
    if (functionDepth_ > 0)
      --functionDepth_;
    if (wasFunction)
      inFunction_ = false;
    atStmtStart_ = true;
  }

  void handleIdentifier(size_t i, const Token &t) {
    if (t.text == "defer") {
      handleDefer(i);
      return;
    }
    // Track explicit moves so a borrow of the moved owner can be rejected.
    // An explicit T_drop(&owner) consumes the automatic cleanup obligation.
    if (t.text.size() > 5 &&
        t.text.compare(t.text.size() - 5, 5, "_drop") == 0) {
      size_t lp = nextSig(i);
      if (isPunct(lp, "(")) {
        size_t amp = nextSig(lp);
        size_t target = nextSig(amp);
        if (isPunct(amp, "&") && target < toks_.size() &&
            toks_[target].kind == TokKind::Identifier &&
            owned_.count(toks_[target].text))
          explicitlyDropped_.insert(toks_[target].text);
      }
    }
    if (t.text == "cplus_move") {
      size_t lp = nextSig(i);
      if (isPunct(lp, "(")) {
        size_t arg = nextSig(lp);
        if (arg < toks_.size() && toks_[arg].kind == TokKind::Identifier &&
            owned_.count(toks_[arg].text)) {
          if (movedOwners_.count(toks_[arg].text))
            warningAt("resource '" + toks_[arg].text +
                          "' is moved more than once",
                      toks_[arg]);
          movedOwners_.insert(toks_[arg].text);
          moved_.insert(toks_[arg].text);
          skipMoveArg_ = arg;
        }
      }
      atStmtStart_ = false;
      return;
    }
    if (i == skipMoveArg_) {
      skipMoveArg_ = kNPos;
      atStmtStart_ = false;
      return;
    }
    if (movedOwners_.count(t.text) && mode_ != Mode::LegacyC) {
      if (strict_)
        errorAt("use-after-move: use of moved-from resource '" + t.text + "'",
                t, "the handle was zeroed by cplus_move");
      else
        warningAt("use of moved-from resource '" + t.text + "'", t,
                  "the handle was zeroed by cplus_move");
    }
    auto borrow = borrows_.find(t.text);
    if (borrow != borrows_.end() && moved_.count(borrow->second)) {
      errorAt("borrow '" + t.text + "' of '" + borrow->second +
                  "' is used after that resource was moved",
              t, "re-borrow the new owner instead");
    }
    if (t.text == "return") {
      std::string exclude;
      checkReturnValue(i, &exclude);
      std::string drops = dropsFor(0, scopes_.size(), exclude);
      if (!drops.empty())
        insertReturnCleanup(i, drops);
      atStmtStart_ = false;
      return;
    }
    if (t.text == "break" || t.text == "continue") {
      std::string drops = loopExitDrops();
      if (!drops.empty())
        insertStatementCleanup(i, drops);
      atStmtStart_ = false;
      return;
    }
    if (t.text == "goto") {
      // STRICT fails closed only when a managed/resource owner is live at the
      // jump; a plain goto with no cleanup obligations is safe.
      if (strict_ && !owned_.empty())
        errorAt("'goto' bypasses C+ cleanup; destructors may not run", t,
                "use structured control flow or an explicit cleanup call");
      else
        warningAt("'goto' bypasses C+ cleanup; destructors may not run", t,
                  "use structured control flow or an explicit cleanup call");
      atStmtStart_ = false;
      return;
    }
    if (t.text == "for" || t.text == "while" || t.text == "do" ||
        t.text == "switch") {
      LoopInfo l;
      l.stackSizeAtKeyword = scopes_.size();
      loops_.push_back(l);
      atStmtStart_ = false;
      return;
    }

    if (atStmtStart_ && inFunction_ && parenDepth_ == 0) {
      size_t ownedBefore = obligations_;
      tryResourceDeclaration(i);
      if (obligations_ == ownedBefore)
        tryBorrowDeclaration(i);
    }
    atStmtStart_ = false;
  }

  void insertBefore(const Token &t, const std::string &drops) {
    std::string text = drops;
    text += "\n#line " + std::to_string(t.line) + " \"" +
            quoteFile(filename_) + "\"\n";
    edits_.push_back(Edit{t.offset, 0, text, seq_++});
  }

  /// Wrap `return`/`break`/`continue` in a compound statement so that
  /// injecting cleanup cannot change how an enclosing `if`/`else` binds.
  /// Evaluate the return expression before running cleanup: cleanup may null
  /// owning locals, so `return xs[0];` must read xs first.
  void insertReturnCleanup(size_t keywordIdx, const std::string &drops) {
    size_t semi = statementEnd(keywordIdx);
    if (semi == kNPos) {
      warningAt("cannot place cleanup before this statement",
                toks_[keywordIdx]);
      return;
    }
    size_t exprStart = nextSig(keywordIdx);
    if (exprStart >= semi) { // `return;`
      insertStatementCleanup(keywordIdx, drops);
      return;
    }
    std::string expr = joinTokens(exprStart, semi);
    std::string replacement = "{ __typeof__(" + expr +
                              ") __cplus_ret = (" + expr + "); " + drops +
                              " return __cplus_ret;";
    edits_.push_back(Edit{toks_[keywordIdx].offset,
                          toks_[semi].offset - toks_[keywordIdx].offset,
                          replacement, seq_++});
    edits_.push_back(
        Edit{toks_[semi].offset + toks_[semi].text.size(), 0, " }", seq_++});
  }

  void insertStatementCleanup(size_t keywordIdx, const std::string &drops) {
    size_t semi = statementEnd(keywordIdx);
    if (semi == kNPos) {
      warningAt("cannot place cleanup before this statement",
                toks_[keywordIdx]);
      return;
    }
    edits_.push_back(
        Edit{toks_[keywordIdx].offset, 0, "{ " + drops, seq_++});
    edits_.push_back(
        Edit{toks_[semi].offset + toks_[semi].text.size(), 0, " }", seq_++});
  }

  /// Cleanup calls for scopes [from, to), innermost first, and within one
  /// scope in reverse declaration order.  `exclude` names a variable whose
  /// ownership has been transferred and must not be destroyed here.
  std::string dropsFor(size_t from, size_t to,
                       const std::string &exclude) const {
    std::string out;
    for (size_t p = to; p > from; --p) {
      const auto &vars = scopes_[p - 1].vars;
      for (size_t k = vars.size(); k > 0; --k) {
        const OwnedVar &v = vars[k - 1];
        if (v.name == exclude || explicitlyDropped_.count(v.name))
          continue;
        if (v.isDefer)
          out += v.deferText + "; ";
        else if (v.managed)
          out += "cplus_release(" + v.name + "); " + v.name + " = NULL; ";
        else
          out += v.drop + "(&" + v.name + "); ";
      }
    }
    return out;
  }

  std::string loopExitDrops() {
    if (loops_.empty())
      return std::string();
    const LoopInfo &l = loops_.back();
    size_t from = l.bodyPos >= 0 ? static_cast<size_t>(l.bodyPos)
                                 : l.stackSizeAtKeyword;
    if (from > scopes_.size())
      from = scopes_.size();
    return dropsFor(from, scopes_.size(), {});
  }

  void checkReturnValue(size_t i, std::string *excluded) {
    size_t v = nextSig(i);
    if (v >= toks_.size() || toks_[v].kind != TokKind::Identifier)
      return;
    // Only a direct `return x;` transfers the object.  An expression such as
    // `return x[0] == 1;` merely reads through it and is safe.
    size_t after = nextSig(v);
    if (after < toks_.size() && !isPunct(after, ";"))
      return;
    auto borrow = borrows_.find(toks_[v].text);
    if (borrow != borrows_.end()) {
      errorAt("borrow '" + borrow->first + "' of local '" + borrow->second +
                  "' cannot escape the function",
              toks_[v], "return the owner instead, or copy the borrowed data");
      return;
    }
    auto it = owned_.find(toks_[v].text);
    if (it == owned_.end())
      return;

    if (it->second.managed) {
      Diag d;
      d.level = DiagLevel::Error;
      d.message = "managed pointer '" + it->first +
                  "' cannot escape its owning scope";
      d.file = filename_;
      d.line = toks_[v].line;
      d.column = toks_[v].column;
      d.help = "managed memory is released at scope exit; copy the data or "
               "use an explicit resource";
      diags_.report(std::move(d));
      return;
    }

    // Returning a resource local transfers ownership to the caller.  The
    // local's normal scope-exit cleanup is unreachable after the return, so
    // no move expression is needed; emitting one would only add a memset on
    // every returned value and block optimisation of the caller's loop.
    *excluded = it->first;
    moved_.insert(it->first);
  }

  void checkAssignment(size_t i) {
    size_t lhs = prevSig(i);
    if (lhs == kNPos || toks_[lhs].kind != TokKind::Identifier)
      return;
    auto it = owned_.find(toks_[lhs].text);
    if (it == owned_.end())
      return;

    // `*p = ...` and `p->field = ...` modify the pointee, not the owner.
    size_t before = prevSig(lhs);
    if (before != kNPos &&
        (isPunct(before, "*") || isPunct(before, ".") ||
         isPunct(before, "->")))
      return;

    size_t rhs = nextSig(i);
    if (rhs < toks_.size() && toks_[rhs].kind == TokKind::Identifier) {
      auto rit = owned_.find(toks_[rhs].text);
      if (rit != owned_.end()) {
        errorAt("resource '" + it->first + "' cannot be copied from '" +
                    rit->first + "'",
                toks_[lhs],
                "transfer ownership with cplus_move(" + rit->first + ")");
        return;
      }
    }
    if (it->second.managed) {
      errorAt("managed pointer '" + it->first + "' cannot be reassigned",
              toks_[lhs],
              "managed memory is released exactly once at scope exit");
    }
  }

  void tryResourceDeclaration(size_t i) {
    size_t typeIdx = i;
    if (isIdent(i, "struct") || isIdent(i, "union"))
      typeIdx = nextSig(i);
    if (typeIdx >= toks_.size() || toks_[typeIdx].kind != TokKind::Identifier)
      return;
    if (!resourceTypes_.count(toks_[typeIdx].text))
      return;

    size_t nameIdx = nextSig(typeIdx);
    if (nameIdx >= toks_.size() || toks_[nameIdx].kind != TokKind::Identifier)
      return;
    size_t after = nextSig(nameIdx);
    if (isPunct(after, "("))
      return; // function returning a resource type; not a local

    // Borrowed pointers and arrays are not owned by this scope.
    if (isPunct(after, "*") || isPunct(after, "["))
      return;

    size_t semi = statementEnd(nameIdx);
    if (semi == kNPos)
      return;

    const std::string name = toks_[nameIdx].text;
    if (owned_.count(name))
      return;

    // Multi-declarator statements are rejected: each owning variable must
    // have its own declaration so cleanup order is unambiguous.
    int paren = 0, bracket = 0;
    size_t eq = kNPos;
    for (size_t k = nameIdx + 1; k < semi; ++k) {
      if (isPunct(k, "("))
        ++paren;
      else if (isPunct(k, ")"))
        --paren;
      else if (isPunct(k, "["))
        ++bracket;
      else if (isPunct(k, "]"))
        --bracket;
      else if (isPunct(k, "=") && paren == 0 && bracket == 0) {
        eq = k;
        break;
      } else if (isPunct(k, ",") && paren == 0 && bracket == 0) {
        errorAt("declare one owning resource per statement", toks_[k]);
        return;
      }
    }

    if (eq == kNPos) {
      // A resource local without an initialiser is zero-initialised so that
      // its destructor observes a well-defined empty handle.
      edits_.push_back(Edit{toks_[semi].offset, 0, " = {0}", seq_++});
    }

    OwnedVar v;
    v.name = name;
    v.type = toks_[typeIdx].text;
    v.drop = v.type + "_drop";
    v.line = toks_[nameIdx].line;
    v.column = toks_[nameIdx].column;
    registerOwned(v);
  }

  /// Record `T *p = &owner;` and `T *p = managed_owner;` as borrows of a
  /// local resource or managed allocation.  This is a deliberately small
  /// borrow checker: it catches dangling escapes and use-after-move, not
  /// aliasing or general lifetime relationships.
  bool tryBorrowDeclaration(size_t i) {
    size_t star = nextSig(i);
    if (!isPunct(star, "*"))
      return false;
    size_t name = nextSig(star);
    if (name >= toks_.size() || toks_[name].kind != TokKind::Identifier)
      return false;
    size_t eq = nextSig(name);
    if (!isPunct(eq, "="))
      return false;
    size_t init = nextSig(eq);
    bool addressOf = isPunct(init, "&");
    size_t ownerIdx = addressOf ? nextSig(init) : init;
    if (ownerIdx >= toks_.size() ||
        toks_[ownerIdx].kind != TokKind::Identifier)
      return false;
    if (!owned_.count(toks_[ownerIdx].text))
      return false;
    borrows_[toks_[name].text] = toks_[ownerIdx].text;
    return true;
  }

  // --- plain-C lifetime checks ----------------------------------------------
  //
  // The checks below cover the raw `malloc`/`fopen` patterns that occur in
  // ordinary C, where no C+ ownership marker exists.  They are deliberately
  // token-level and conservative so that valid C is never rejected:
  //
  //   * a leak is only reported at an explicit `return` that is reached while
  //     an allocation is still live on the linear token path (free/fclose
  //     removes it from the live set; a null-check guard suppresses the
  //     report), and only when the pointer is a local that neither escapes
  //     via return nor via an out-parameter;
  //   * `longjmp` is only escalated when an allocation is live;
  //   * a `FILE*` is only flagged for fclose/use when it came from `fopen`
  //     in this function and was actually tracked.
  //
  // Everything here is disabled in LegacyC mode.  STRICT mode turns the
  // leak/longjmp findings into errors; the definite-UB findings are always
  // errors in safe modes.

  enum class AllocKind { Malloc, Fopen };

  struct RawAlloc {
    AllocKind kind = AllocKind::Malloc;
    bool possiblyNull = false;
    bool escaped = false;   // returned or stored through an out-parameter
    bool reported = false;  // avoid a cascade of leak reports for one alloc
    unsigned line = 0;
    unsigned column = 0;
  };

  struct LocalVar {
    int scopeIndex = 0;  // index into the scope stack where it was declared
    bool isPointer = false;
    bool isArray = false;
    bool isStatic = false;
  };

  size_t matchingParen(size_t open) const {
    int depth = 0;
    for (size_t k = open; k < toks_.size(); ++k) {
      if (isPunct(k, "("))
        ++depth;
      else if (isPunct(k, ")")) {
        if (--depth == 0)
          return k;
      }
    }
    return kNPos;
  }

  size_t matchingParenBackward(size_t close) const {
    int depth = 0;
    for (size_t k = close + 1; k > 0;) {
      --k;
      if (isPunct(k, ")"))
        ++depth;
      else if (isPunct(k, "(")) {
        if (--depth == 0)
          return k;
      }
    }
    return kNPos;
  }

  size_t matchingBrace(size_t open) const {
    int depth = 0;
    for (size_t k = open; k < toks_.size(); ++k) {
      if (isPunct(k, "{"))
        ++depth;
      else if (isPunct(k, "}")) {
        if (--depth == 0)
          return k;
      }
    }
    return kNPos;
  }

  /// Walk backwards from `from` to the `=` that assigns to a variable.
  /// Cast spellings such as `(int*)malloc(...)` and parenthesised sub
  /// expressions are skipped so the assignment target is not hidden.
  size_t findEqBefore(size_t from) const {
    for (size_t j = from; j > 0;) {
      size_t p = prevSig(j);
      if (p == kNPos)
        return kNPos;
      if (isPunct(p, ";") || isPunct(p, "{") || isPunct(p, "}"))
        return kNPos;
      if (isPunct(p, "="))
        return p;
      if (isPunct(p, ")")) {
        // Jump over a complete parenthesised group (a cast in practice).
        // Start at depth 1 for the closing paren we are already standing on.
        size_t k = p;
        int depth = 1;
        for (; k > 0;) {
          size_t q = prevSig(k);
          if (q == kNPos)
            break;
          if (isPunct(q, ")"))
            ++depth;
          else if (isPunct(q, "(")) {
            if (--depth == 0)
              break;
          }
          k = q;
        }
        j = k;
        continue;
      }
      j = p;
    }
    return kNPos;
  }

  size_t findAssignmentLhs(size_t eq) const {
    size_t p = prevSig(eq);
    if (p == kNPos || toks_[p].kind != TokKind::Identifier)
      return kNPos;
    return p;
  }

  /// True when the identifier at `lhs` is a dereference/member/subscript
  /// (`*p`, `p->x`, `x[i]`) rather than a plain variable name.  A declaration
  /// pointer (`int *p = ...`) is *not* a dereference: the token before `*`
  /// is a type identifier.
  bool isDerefLhs(size_t lhs) const {
    size_t b = prevSig(lhs);
    if (b == kNPos)
      return false;
    if (isPunct(b, "*")) {
      size_t beforeStar = prevSig(b);
      if (beforeStar == kNPos || toks_[beforeStar].kind != TokKind::Identifier)
        return true; // `*p = ...`
      return false; // `int *p = ...`
    }
    if (isPunct(b, ".") || isPunct(b, "->") || isPunct(b, "["))
      return true;
    if (isPunct(nextSig(lhs), "["))
      return true;
    return false;
  }

  /// Recognise `!x`, `x == NULL`, `NULL == x`, `x == 0` as a null test used to
  /// guard an early return.  `x != NULL` is intentionally not a guard: a
  /// return on that branch would really leak a non-null allocation.
  bool isNullCheck(size_t from, size_t to, const std::string &name) const {
    std::vector<size_t> sig;
    for (size_t k = from; k < to; ++k)
      if (!isTrivia(k))
        sig.push_back(k);
    auto isNullConst = [&](size_t x) {
      return (toks_[x].kind == TokKind::Identifier &&
              toks_[x].text == "NULL") ||
             (toks_[x].kind == TokKind::Number && toks_[x].text == "0");
    };
    if (sig.size() == 2 && isPunct(sig[0], "!") &&
        isIdent(sig[1], name.c_str()))
      return true;
    if (sig.size() == 3 && isPunct(sig[1], "==")) {
      if (isIdent(sig[0], name.c_str()) && isNullConst(sig[2]))
        return true;
      if (isNullConst(sig[0]) && isIdent(sig[2], name.c_str()))
        return true;
    }
    return false;
  }

  /// Collect file-scope declarations so a global pointer assigned in a
  /// function is not mistaken for a leaking local allocation.
  std::set<std::string> collectGlobals() const {
    std::set<std::string> out;
    int depth = 0;
    bool atStart = true;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (isTrivia(i))
        continue;
      if (isPunct(i, "{")) {
        ++depth;
        atStart = true;
        continue;
      }
      if (isPunct(i, "}")) {
        if (depth > 0)
          --depth;
        atStart = true;
        continue;
      }
      if (isPunct(i, ";") || isPunct(i, ":")) {
        atStart = true;
        continue;
      }
      if (depth == 0 && atStart) {
        size_t end = statementEnd(i);
        if (end != kNPos) {
          std::map<std::string, LocalVar> dummy;
          recordLocalDecl(i, end, dummy, &out);
        }
      }
      atStart = false;
    }
    return out;
  }

  /// Record a simple `type *name` / `type name[N]` declaration.  Names found
  /// are added to `scope` (for locals) and, when non-null, to `out` (for
  /// file-scope declarations).
  void recordLocalDecl(size_t s, size_t e,
                       std::map<std::string, LocalVar> &scope,
                       std::set<std::string> *out = nullptr,
                       int scopeIndex = 0) const {
    if (s >= e)
      return;
    static const std::set<std::string> kNonDecl = {
        "return", "if",   "for",     "while",   "do",      "switch",
        "case",   "else", "goto",    "break",   "continue"};
    size_t k = s;
    bool isStatic = false;
    while (k < e) {
      if (isIdent(k, "static")) {
        isStatic = true;
        k = nextSig(k);
      } else if (isIdent(k, "const") || isIdent(k, "volatile") ||
                 isIdent(k, "unsigned") || isIdent(k, "signed") ||
                 isIdent(k, "struct") || isIdent(k, "union") ||
                 isIdent(k, "enum") || isIdent(k, "long") ||
                 isIdent(k, "short") || isIdent(k, "register") ||
                 isIdent(k, "extern") || isIdent(k, "inline")) {
        k = nextSig(k);
      } else {
        break;
      }
    }
    if (k >= e || toks_[k].kind != TokKind::Identifier ||
        kNonDecl.count(toks_[k].text))
      return;
    k = nextSig(k);
    bool sawStar = false;
    while (k < e && isPunct(k, "*")) {
      sawStar = true;
      k = nextSig(k);
    }
    if (k >= e || toks_[k].kind != TokKind::Identifier)
      return;
    const std::string name = toks_[k].text;
    size_t after = nextSig(k);
    bool isArray = after < e && isPunct(after, "[");
    if (out)
      out->insert(name);
    else
      scope[name] = LocalVar{scopeIndex, sawStar, isArray, isStatic};
  }

  void analyzePlainC() {
    if (mode_ == Mode::LegacyC)
      return;
    const std::set<std::string> globals = collectGlobals();

    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isPunct(i, "{"))
        continue;
      size_t before = prevSig(i);
      if (before == kNPos || !isPunct(before, ")"))
        continue; // not a function body (struct/initialiser/control block)
      // Functions explicitly opted out of cleanup are left alone.
      bool noCleanup = false;
      for (size_t k = before, steps = 0;
           k != kNPos && k > 0 && steps < 32; ++steps) {
        size_t p = prevSig(k);
        if (p == kNPos || isPunct(p, ";") || isPunct(p, "}"))
          break;
        if (isIdent(p, "cplus_no_cleanup")) {
          noCleanup = true;
          break;
        }
        k = p;
      }
      size_t end = matchingBrace(i);
      if (end == kNPos)
        continue;
      if (!noCleanup)
        analyzeFunction(i, end, globals);
      i = end;
    }
  }

  void analyzeFunction(size_t openIdx, size_t closeIdx,
                       const std::set<std::string> &globals) {
    // Parameter names: an allocation stored into a parameter is an
    // out-parameter/escaping store, not a provable local leak.
    std::set<std::string> params;
    size_t closeParen = prevSig(openIdx);
    size_t openParen =
        closeParen == kNPos ? kNPos : matchingParenBackward(closeParen);
    if (openParen != kNPos) {
      for (size_t k = openParen + 1; k < closeParen; ++k)
        if (toks_[k].kind == TokKind::Identifier)
          params.insert(toks_[k].text);
    }

    std::vector<std::map<std::string, LocalVar>> scopes(1);
    std::map<std::string, RawAlloc> live;          // live on the linear path
    std::set<std::string> closedFiles;             // fopen results already closed
    std::map<std::string, size_t> closeTokenIdx;
    std::map<std::string, std::set<std::string>> fileAliases; // FILE* copies
    std::set<std::string> nullChecked;            // fopen vars with a null test
    // `p -> (inner local, scope index)` for `p = &innerLocal`.
    std::map<std::string, std::pair<std::string, int>> dangling;
    std::set<std::string> expired;                 // pointers to dead locals
    std::set<std::string> danglingReported;
    std::set<std::string> longjmpReported;
    std::set<std::string> gotoReported;
    // A `free(p)` after `longjmp` is only a resume path when this function
    // also calls setjmp; otherwise it is unreachable after the jump.
    bool hasSetjmp = false;
    for (size_t k = openIdx; k <= closeIdx; ++k)
      if (isIdent(k, "setjmp")) { hasSetjmp = true; break; }

    auto lookupLocal = [&](const std::string &n) -> LocalVar * {
      for (int s = static_cast<int>(scopes.size()) - 1; s >= 0; --s) {
        auto it = scopes[s].find(n);
        if (it != scopes[s].end())
          return &it->second;
      }
      return nullptr;
    };

    auto reportDanglingUse = [&](const Token &at, const std::string &name) {
      if (danglingReported.count(name))
        return;
      danglingReported.insert(name);
      plainError("use of pointer '" + name +
                     "' whose target local has left its scope",
                 at, "the pointed-to stack object no longer exists");
    };

    // Whether `retIdx` is immediately guarded by an `if (!x)`-style null
    // check for `name`.  Scanning backwards stops at the first statement
    // boundary so only the directly enclosing condition is considered.
    auto nullGuardedReturn = [&](size_t retIdx, const std::string &name) {
      for (size_t j = retIdx; j > openIdx;) {
        size_t p = prevSig(j);
        if (p == kNPos || p <= openIdx)
          break;
        if (isIdent(p, "return") || isIdent(p, "else"))
          break;
        // Skip over earlier statements in the guarded block (e.g. an fprintf
        // before the return) and the block's opening brace.
        if (isPunct(p, ";") || isPunct(p, "{")) {
          j = p;
          continue;
        }
        if (isIdent(p, "if")) {
          size_t lp = nextSig(p);
          if (!isPunct(lp, "("))
            return false;
          size_t rp = matchingParen(lp);
          if (rp == kNPos || rp >= retIdx)
            return false;
          return isNullCheck(lp + 1, rp, name);
        }
        j = p;
      }
      return false;
    };

    auto noteFopenNullChecks = [&](size_t ifIdx) {
      size_t lp = nextSig(ifIdx);
      if (!isPunct(lp, "("))
        return;
      size_t rp = matchingParen(lp);
      if (rp == kNPos)
        return;
      for (size_t k = lp + 1; k < rp; ++k) {
        if (toks_[k].kind == TokKind::Identifier) {
          auto it = live.find(toks_[k].text);
          if (it != live.end() && it->second.kind == AllocKind::Fopen)
            nullChecked.insert(toks_[k].text);
        }
      }
    };

    static const std::set<std::string> kFileUseFuncs = {
        "fprintf", "fputs",  "fputc",   "fwrite", "fread",  "fseek",
        "ftell",   "fflush", "rewind",  "fileno", "ferror", "feof",
        "clearerr", "setbuf", "setvbuf"};

    bool atStmtStart = true;
    for (size_t i = openIdx + 1; i < closeIdx; ++i) {
      const Token &t = toks_[i];
      if (isTrivia(i))
        continue;

      if (atStmtStart) {
        size_t end = statementEnd(i);
        if (end == kNPos || end > closeIdx)
          end = closeIdx;
        recordLocalDecl(i, end, scopes.back(), nullptr,
                        static_cast<int>(scopes.size()) - 1);
      }

      if (isPunct(i, "{")) {
        scopes.push_back(std::map<std::string, LocalVar>());
        atStmtStart = true;
        continue;
      }
      if (isPunct(i, "}")) {
        int closing = static_cast<int>(scopes.size()) - 1;
        for (const auto &kv : dangling)
          if (kv.second.second == closing)
            expired.insert(kv.first);
        if (scopes.size() > 1)
          scopes.pop_back();
        atStmtStart = true;
        continue;
      }
      if (isPunct(i, ";") || isPunct(i, ":")) {
        atStmtStart = true;
        continue;
      }
      if (isPunct(i, "=")) {
        size_t lhs = findAssignmentLhs(i);
        std::string lhsName =
            lhs == kNPos ? std::string() : toks_[lhs].text;
        size_t rhs = nextSig(i);

        // `*out = p` / `obj.field = p` / `out = p` (out a pointer parameter)
        // transfers the pointer to the caller: not a provable local leak.
        // `head = n` moves ownership into another local container/list; the
        // linear scan cannot follow it, so treat it as escaped too.
        if (!lhsName.empty() && rhs < closeIdx &&
            toks_[rhs].kind == TokKind::Identifier && lhsName != toks_[rhs].text) {
          auto rit = live.find(toks_[rhs].text);
          if (rit != live.end()) {
            // `FILE *f2 = f1;` is an alias, not an ownership transfer. Track
            // the copy so fclose through either name invalidates both.
            LocalVar *lv = lookupLocal(lhsName);
            if (rit->second.kind == AllocKind::Fopen && lv && lv->isPointer) {
              fileAliases[lhsName].insert(toks_[rhs].text);
              fileAliases[toks_[rhs].text].insert(lhsName);
            }
            rit->second.escaped = true;
          }
        }

        if (!lhsName.empty() && (isDerefLhs(lhs) || params.count(lhsName))) {
          if (rhs < closeIdx && toks_[rhs].kind == TokKind::Identifier) {
            auto it = live.find(toks_[rhs].text);
            if (it != live.end())
              it->second.escaped = true;
          }
        }

        // `p = &innerLocal` sets up a possible dangling pointer once the
        // inner scope closes.  Any other reassignment clears that state.
        if (!lhsName.empty() && isPunct(rhs, "&")) {
          size_t target = nextSig(rhs);
          if (target < closeIdx && toks_[target].kind == TokKind::Identifier) {
            LocalVar *pv = lookupLocal(lhsName);
            LocalVar *tv = lookupLocal(toks_[target].text);
            if (pv && tv && !tv->isStatic && !pv->isStatic &&
                tv->scopeIndex > pv->scopeIndex) {
              dangling[lhsName] = {toks_[target].text, tv->scopeIndex};
              expired.erase(lhsName);
            } else {
              expired.erase(lhsName);
              dangling.erase(lhsName);
            }
          }
        } else if (!lhsName.empty()) {
          expired.erase(lhsName);
          dangling.erase(lhsName);
        }
        atStmtStart = false;
        continue;
      }
      if (isPunct(i, "&")) {
        size_t arg = nextSig(i); // `f(&p)` similarly escapes p
        if (arg < closeIdx && toks_[arg].kind == TokKind::Identifier) {
          auto it = live.find(toks_[arg].text);
          if (it != live.end())
            it->second.escaped = true;
        }
        atStmtStart = false;
        continue;
      }
      if (isPunct(i, "*")) {
        size_t arg = nextSig(i);
        if (arg < closeIdx && toks_[arg].kind == TokKind::Identifier &&
            expired.count(toks_[arg].text))
          reportDanglingUse(t, toks_[arg].text);
        atStmtStart = false;
        continue;
      }
      if (t.kind != TokKind::Identifier) {
        atStmtStart = false;
        continue;
      }

      if (isIdent(i, "malloc") || isIdent(i, "fopen")) {
        size_t lp = nextSig(i);
        if (isPunct(lp, "(")) {
          size_t eq = findEqBefore(i);
          if (eq != kNPos && eq < closeIdx) {
            size_t lhs = findAssignmentLhs(eq);
            if (lhs != kNPos && lhs > openIdx && !isDerefLhs(lhs)) {
              const std::string name = toks_[lhs].text;
              if (!params.count(name) && !globals.count(name)) {
                RawAlloc a;
                a.kind = isIdent(i, "fopen") ? AllocKind::Fopen
                                             : AllocKind::Malloc;
                a.possiblyNull = a.kind == AllocKind::Fopen;
                a.line = t.line;
                a.column = t.column;
                live[name] = a;
              }
            }
          }
        }
      } else if (isIdent(i, "free") || isIdent(i, "fclose")) {
        size_t lp = nextSig(i);
        size_t arg = isPunct(lp, "(") ? nextSig(lp) : kNPos;
        if (arg < closeIdx && toks_[arg].kind == TokKind::Identifier) {
          const std::string name = toks_[arg].text;
          bool isFclose = isIdent(i, "fclose");
          auto it = live.find(name);
          // A second fclose on a mutually-exclusive path (e.g. an early
          // return inside an if) is valid C; the linear scan cannot prove a
          // double close, so only use-after-close is diagnosed.
          if (isFclose && closedFiles.count(name)) {
            // Straight-line double close is provable. If control flow occurs
            // between the closes, the paths may be mutually exclusive.
            size_t first = closeTokenIdx.count(name) ? closeTokenIdx[name] : 0;
            bool straightLine = true;
            for (size_t k = first + 1; k < i && straightLine; ++k)
              if (isIdent(k, "if") || isIdent(k, "else") ||
                  isIdent(k, "return") || isIdent(k, "goto") ||
                  isIdent(k, "while") || isIdent(k, "for"))
                straightLine = false;
            if (straightLine)
              plainError("double close of FILE* '" + name + "'", t,
                         "release the stream exactly once");
          } else if (it != live.end()) {
            if (isFclose) {
              // `fclose` on a plainly possibly-null fopen result is UB.  A
              // null test anywhere before this point suppresses the report.
              if (it->second.kind == AllocKind::Fopen &&
                  it->second.possiblyNull && !nullChecked.count(name))
                plainError("fclose on possibly null FILE* '" + name +
                               "' from fopen",
                           t, "test the fopen result before closing it");
              closedFiles.insert(name);
              closeTokenIdx[name] = i;
              auto ait = fileAliases.find(name);
              if (ait != fileAliases.end())
                for (const auto &a : ait->second)
                  closedFiles.insert(a);
            }
            live.erase(it); // freed on this path
          }
        }
      } else if (isIdent(i, "setbuf") || isIdent(i, "setvbuf")) {
        size_t lp = nextSig(i);
        if (isPunct(lp, "(")) {
          size_t comma = kNPos;
          int depth = 0;
          for (size_t k = lp; k < closeIdx; ++k) {
            if (isPunct(k, "("))
              ++depth;
            else if (isPunct(k, ")")) {
              if (--depth == 0)
                break;
            } else if (isPunct(k, ",") && depth == 1) {
              comma = k;
              break;
            }
          }
          if (comma != kNPos) {
            size_t bufArg = nextSig(comma);
            if (bufArg < closeIdx &&
                toks_[bufArg].kind == TokKind::Identifier) {
              LocalVar *lv = lookupLocal(toks_[bufArg].text);
              if (lv && lv->isArray && !lv->isStatic)
                plainError(std::string(t.text) +
                               " buffer '" + toks_[bufArg].text +
                               "' is a stack array that will dangle",
                           t, "use a buffer with static or heap lifetime");
            }
          }
        }
      } else if (isIdent(i, "return")) {
        size_t v = nextSig(i);
        std::string returned;
        if (v < closeIdx && toks_[v].kind == TokKind::Identifier) {
          size_t after = nextSig(v);
          if (after < closeIdx && isPunct(after, ";"))
            returned = toks_[v].text;
        }
        for (auto &kv : live) {
          if (kv.second.reported)
            continue;
          if (!returned.empty() && kv.first == returned) {
            kv.second.escaped = true; // ownership leaves the function
            continue;
          }
          if (kv.second.escaped || nullGuardedReturn(i, kv.first))
            continue;
          kv.second.reported = true;
          std::string msg;
          if (kv.second.kind == AllocKind::Fopen)
            msg = "FILE* '" + kv.first +
                  "' leaks on this return path (no matching fclose)";
          else
            msg = "allocation '" + kv.first +
                  "' leaks on this return path (no matching free)";
          strictDiag(msg, t,
                     "free/close the resource on every return path, or use a "
                     "C+ managed allocation");
        }
      } else if (isIdent(i, "goto")) {
        // A forward jump to a label past a free() of a live allocation skips
        // cleanup. STRICT must reject that path (a plain goto is still fine).
        if (strict_) {
          size_t target = nextSig(i);
          if (target < closeIdx && toks_[target].kind == TokKind::Identifier) {
            size_t label = kNPos;
            for (size_t k = i + 1; k < closeIdx; ++k)
              if (toks_[k].kind == TokKind::Identifier &&
                  toks_[k].text == toks_[target].text &&
                  isPunct(nextSig(k), ":")) { label = k; break; }
            if (label != kNPos)
              for (auto &kv : live) {
                if (kv.second.escaped || gotoReported.count(kv.first))
                  continue;
                for (size_t k = i + 1; k < label; ++k)
                  if ((isIdent(k, "free") || isIdent(k, "fclose"))) {
                    size_t lp = nextSig(k);
                    size_t arg = isPunct(lp, "(") ? nextSig(lp) : kNPos;
                    if (arg < label && toks_[arg].text == kv.first) {
                      gotoReported.insert(kv.first);
                      plainError("'goto' bypasses cleanup of live allocation '" +
                                     kv.first + "'",
                                 t, "free the resource before the jump or use "
                                    "structured control flow");
                      break;
                    }
                  }
              }
          }
        }
      } else if (isIdent(i, "longjmp")) {
        // The generic longjmp warning is already produced by the bounds
        // checker; STRICT mode escalates it when a live allocation exists.
        if (strict_) {
          for (auto &kv : live) {
            if (kv.second.escaped || longjmpReported.count(kv.first))
              continue;
            // The setjmp resume path may free the allocation after longjmp;
            // a forward scan for free(p) in the remaining body avoids a
            // false positive on that idiom.
            bool freedLater = false;
            for (size_t k = i + 1; hasSetjmp && k < closeIdx && !freedLater; ++k)
              if (isIdent(k, "free") || isIdent(k, "fclose")) {
                size_t lp = nextSig(k);
                size_t arg = isPunct(lp, "(") ? nextSig(lp) : kNPos;
                if (arg < closeIdx && toks_[arg].text == kv.first)
                  freedLater = true;
              }
            if (freedLater)
              continue;
            longjmpReported.insert(kv.first);
            errorAt("longjmp bypasses cleanup of live allocation '" +
                        kv.first + "'",
                    t, "the allocation is never reached by a free/fclose");
          }
        }
      } else if (kFileUseFuncs.count(t.text)) {
        // A FILE* that came from fopen and was already closed must not be
        // used again (this also catches `fclose(f); fprintf(f, ...);`).
        size_t lp = nextSig(i);
        size_t rp = isPunct(lp, "(") ? matchingParen(lp) : kNPos;
        if (rp == kNPos || rp > closeIdx)
          rp = closeIdx;
        // The FILE* is not always argument 0 (fputs(str, f), fread(..., f)).
        for (size_t k = lp + 1; k < rp; ++k) {
          if (toks_[k].kind != TokKind::Identifier)
            continue;
          const std::string &candidate = toks_[k].text;
          bool closed = closedFiles.count(candidate) != 0;
          auto ait = fileAliases.find(candidate);
          if (!closed && ait != fileAliases.end())
            for (const auto &a : ait->second)
              if (closedFiles.count(a)) { closed = true; break; }
          if (closed) {
            plainError("use of closed FILE* '" + candidate + "'", t,
                       "the stream was already closed");
            break;
          }
        }
      } else if (isIdent(i, "if")) {
        noteFopenNullChecks(i);
      } else if (toks_[i].kind == TokKind::Identifier &&
                 isPunct(nextSig(i), "(") && live.count(t.text) == 0 &&
                 !kFileUseFuncs.count(t.text)) {
        // Passing a live pointer to any other function may transfer
        // ownership (e.g. a helper that frees it). Conservatively treat it
        // as escaped so the leak analyzer does not report a false positive.
        size_t lp = nextSig(i);
        size_t rp = matchingParen(lp);
        if (rp != kNPos)
          for (size_t k = nextSig(lp); k < rp; ++k)
            if (toks_[k].kind == TokKind::Identifier) {
              auto it = live.find(toks_[k].text);
              if (it != live.end())
                it->second.escaped = true;
            }
      }

      if (expired.count(t.text)) {
        size_t after = nextSig(i);
        if (isPunct(after, "[")) // `p[0]` after the target left scope
          reportDanglingUse(t, t.text);
      }

      atStmtStart = false;
    }
  }

  // --- edit application -----------------------------------------------------

  void applyEdits() {
    std::stable_sort(edits_.begin(), edits_.end(),
                     [](const Edit &a, const Edit &b) {
                       if (a.offset != b.offset)
                         return a.offset < b.offset;
                       return a.seq < b.seq;
                     });

    output_.reserve(source_.size() + 256);
    size_t cursor = 0;
    for (const auto &e : edits_) {
      if (e.offset < cursor)
        continue; // overlapping edits cannot occur, but stay safe
      output_.append(source_, cursor, e.offset - cursor);
      output_ += e.text;
      cursor = e.offset + e.length;
    }
    output_.append(source_, cursor, source_.size() - cursor);
  }

  // --- state ----------------------------------------------------------------

  const std::string &source_;
  std::vector<Token> toks_;
  DiagnosticEngine &diags_;
  std::string filename_;
  Mode mode_;
  bool strict_ = false;

  std::set<std::string> resourceTypes_;
  std::map<std::string, OwnedVar> owned_;
  std::map<std::string, std::string> borrows_;
  std::set<std::string> moved_;
  std::set<std::string> movedOwners_;
  std::set<std::string> explicitlyDropped_;
  size_t skipMoveArg_ = kNPos;

  std::vector<Scope> scopes_;
  std::vector<LoopInfo> loops_;
  std::vector<Edit> edits_;

  std::string output_;
  bool inFunction_ = false;
  int functionDepth_ = 0;
  int parenDepth_ = 0;
  bool atStmtStart_ = true;
  unsigned obligations_ = 0;
  unsigned deferCounter_ = 0;
  unsigned removedMarkers_ = 0;
  unsigned seq_ = 0;
};

} // namespace

LoweredSource lowerRaii(const std::string &source,
                        std::vector<std::string> resourceTypes,
                        DiagnosticEngine &diags, const std::string &filename,
                        Mode mode, bool strict) {
  Lowering lowering(source, std::move(resourceTypes), diags, filename, mode,
                    strict);
  return lowering.run();
}

} // namespace cplus

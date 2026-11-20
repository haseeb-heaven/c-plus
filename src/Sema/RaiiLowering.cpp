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
           DiagnosticEngine &diags, const std::string &filename, Mode mode)
      : source_(source), toks_(lexCPlus(source)), diags_(diags),
        filename_(filename), mode_(mode) {
    for (auto &t : resourceTypes)
      resourceTypes_.insert(t);
  }

  LoweredSource run() {
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

    bool wasFunction = s.functionBody;
    scopes_.pop_back();
    if (functionDepth_ > 0)
      --functionDepth_;
    if (wasFunction)
      inFunction_ = false;
    atStmtStart_ = true;
  }

  void handleIdentifier(size_t i, const Token &t) {
    if (t.text == "return") {
      std::string exclude;
      checkReturnValue(i, &exclude);
      std::string drops = dropsFor(0, scopes_.size(), exclude);
      if (!drops.empty())
        insertStatementCleanup(i, drops);
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

    if (atStmtStart_ && inFunction_ && parenDepth_ == 0)
      tryResourceDeclaration(i);
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
        if (v.name == exclude)
          continue;
        if (v.managed)
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

    // Returning a resource moves ownership to the caller.  Zero the source so
    // its automatic destructor is a no-op.
    *excluded = it->first;
    edits_.push_back(Edit{toks_[v].offset, toks_[v].text.size(),
                          "cplus_move(" + toks_[v].text + ")", seq_++});
  }

  void checkAssignment(size_t i) {
    size_t lhs = prevSig(i);
    if (lhs == kNPos || toks_[lhs].kind != TokKind::Identifier)
      return;
    auto it = owned_.find(toks_[lhs].text);
    if (it == owned_.end())
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

  std::set<std::string> resourceTypes_;
  std::map<std::string, OwnedVar> owned_;

  std::vector<Scope> scopes_;
  std::vector<LoopInfo> loops_;
  std::vector<Edit> edits_;

  std::string output_;
  bool inFunction_ = false;
  int functionDepth_ = 0;
  int parenDepth_ = 0;
  bool atStmtStart_ = true;
  unsigned obligations_ = 0;
  unsigned removedMarkers_ = 0;
  unsigned seq_ = 0;
};

} // namespace

LoweredSource lowerRaii(const std::string &source,
                        std::vector<std::string> resourceTypes,
                        DiagnosticEngine &diags, const std::string &filename,
                        Mode mode) {
  Lowering lowering(source, std::move(resourceTypes), diags, filename, mode);
  return lowering.run();
}

} // namespace cplus

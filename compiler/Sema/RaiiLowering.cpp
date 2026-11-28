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

/// One cleanup obligation.  Identity is the (scope, position) pair it occupies
/// in `Lowering::scopes_`, never its source name: a shadowing declaration in an
/// inner scope is a *different* obligation and must be destroyed on its own.
/// Every piece of mutable per-binding state below used to live in a
/// function-wide, name-keyed set, so an inner `R x` aliased the outer `x`'s
/// moved/dropped flags.  Keeping it here makes the state die with its binding.
struct OwnedVar {
  std::string name;
  std::string type;   // resource type (for drop generation)
  std::string drop;   // destructor symbol
  bool managed = false;
  bool isDefer = false;
  std::string deferText;
  /// The handle was transferred away, by `cplus_move` or by `return owner;`.
  /// A borrow of this binding is dead from here on.  (A `return` is not a
  /// use-after-move: the token it names is the transfer itself.)
  bool moved = false;
  /// An explicit `cplus_move` named this binding, so later direct uses are
  /// use-after-move.  Tracked separately from `moved` for exactly that reason.
  bool movedByMove = false;
  /// The moved-more-than-once warning has already been issued.
  bool moveWarned = false;
  /// A destructor call naming this binding was seen.  Only used for
  /// duplicate-drop detection; the automatic drop stays (the explicit call is
  /// guarded by `cplus_zero`, so it is a no-op instead of a double free).
  bool explicitlyDropped = false;
  /// The drop lives inside an unconditional `defer`, whose text IS the
  /// scope-exit cleanup, so the generated drop must be suppressed.
  bool deferDropped = false;
  /// Offset of the declaration's first token.  A shadowing declaration needs a
  /// saved address emitted just before it, while its name still refers to the
  /// enclosing binding.
  size_t declOffset = kNPos;
  /// Emitted-C name of the saved address for this binding when the C compiler
  /// can no longer resolve it by name (a return crossing a shadow boundary).
  std::string escapeAlias;
  unsigned line = 0;
  unsigned column = 0;
};

/// A binding's address inside `Lowering::scopes_`: scope index plus position in
/// that scope's `vars`.  Both are append-only, so a DeclId stays valid until
/// its scope closes.  Used to say "destroy everything except THIS binding",
/// which a bare name cannot express once the name is shadowed.
struct DeclId {
  size_t scope = kNPos;
  size_t var = kNPos;
  bool valid() const { return scope != kNPos; }
};

struct Scope {
  size_t closeOffset = 0;
  unsigned closeLine = 0;
  std::vector<OwnedVar> vars;
  /// `T *p = &owner;` aliases declared in this scope, mapped to the owner name
  /// they borrow.  Scoped, so a shadowed alias cannot outlive its block and a
  /// borrow in one function is never visible in a sibling function.
  std::map<std::string, std::string> borrows;
  bool functionBody = false;
  /// A `try { ... }` or `catch (...) { ... }` body.  Owners declared inside
  /// one of these are skipped by a `throw` (lowered to a longjmp); owners
  /// declared before it survive and are cleaned up after the handler.
  bool exceptionBlock = false;
};

struct LoopInfo {
  /// Index in `scopes` of the loop body block, or -1 when the body is not a
  /// compound statement.
  int bodyPos = -1;
  size_t stackSizeAtKeyword = 0;
  /// A `switch` is tracked in the same stack as loops, but `continue` must
  /// target the innermost real loop rather than the switch frame.
  bool isSwitch = false;
  /// Issue #228: a `do` frame stays alive after its body closes so the
  /// trailing `while` can be matched to *this* frame.  A single function-wide
  /// "awaiting while" flag let a nested `while`/`do` steal that match and
  /// leave `break` targeting the wrong loop.
  bool isDo = false;
  bool bodyClosed = false;
};

struct Edit {
  size_t offset = 0;
  size_t length = 0;
  std::string text;
  unsigned seq = 0;
};

/// One `defer` statement.  The token range matters because a conditional
/// defer body (`defer if (c) drop(&r);`) must not suppress the automatic drop:
/// on the false branch the explicit drop never runs.
struct DeferInfo {
  size_t startTok = 0;
  size_t endTok = 0; // statement semicolon
  size_t startOffset = 0;
  size_t endOffset = 0;
  bool conditional = false;
  std::string ownerName;
};

/// Issue #185: one lexical `region r { ... }` frame.  The token is an ordinary
/// stack struct; region pointers are thin `T *` values.  Nothing in this frame
/// is part of an emitted struct or function signature, so the C ABI is intact.
struct RegionFrame {
  std::string name;
  size_t scope = kNPos;
  /// Token index of the block's `{`.  The release obligation is attached when
  /// that brace opens its scope, not when the `region` keyword is scanned.
  size_t openTok = kNPos;
  /// Brace depth of the region body.  A binding declared at a smaller depth is
  /// an enclosing local: it outlives the region and storing a region pointer
  /// into it is an escape even though neither a global nor a parameter is
  /// involved (issue #185).
  size_t scopeDepth = kNPos;
  unsigned line = 0;
  unsigned column = 0;
  std::set<std::string> pointers;
};

/// Self-contained region prelude.  It uses only the checked heap entry points
/// already declared by "cplus/cplus.h"; the token itself has no heap node and
/// teardown is a single cplus_release of the backing block.
const char *const kRegionPrelude =
    "#line 1 \"<cplus-region-prelude>\"\n"
    "#ifndef CPLUS_REGION_PRELUDE_DEFINED\n"
    "#define CPLUS_REGION_PRELUDE_DEFINED\n"
    "#include <stddef.h>\n"
    "#include <stdint.h>\n"
    "extern void *cplus_alloc(size_t size);\n"
    "extern void cplus_release(void *ptr);\n"
    "#define CPLUS_REGION_DEFAULT_BYTES 4096u\n"
    "typedef struct cplus_region_token {\n"
    "  unsigned char *base;\n"
    "  size_t used;\n"
    "  size_t capacity;\n"
    "  unsigned long long magic;\n"
    "} cplus_region_token;\n"
    "#define CPLUS_REGION_MAGIC 0x43504c555245474fULL\n"
    "__attribute__((unused)) static inline void cplus_region_init("
    "cplus_region_token *r, size_t bytes) {\n"
    "  if (!r) return;\n"
    "  r->base = 0; r->used = 0; r->capacity = 0; r->magic = 0;\n"
    "  if (bytes == 0) bytes = CPLUS_REGION_DEFAULT_BYTES;\n"
    "  r->base = (unsigned char *)cplus_alloc(bytes);\n"
    "  if (r->base) { r->capacity = bytes; r->magic = CPLUS_REGION_MAGIC; }\n"
    "}\n"
    "__attribute__((unused)) static inline void *cplus_region_take("
    "cplus_region_token *r, size_t bytes) {\n"
    "  size_t pad;\n"
    "  unsigned char *p;\n"
    "  if (!r || r->magic != CPLUS_REGION_MAGIC || !r->base || bytes == 0)\n"
    "    return (void *)0;\n"
    "  pad = (16u - (r->used & 15u)) & 15u;\n"
    "  if (pad > r->capacity - r->used || bytes > r->capacity - r->used - pad)\n"
    "    return (void *)0;\n"
    "  p = r->base + r->used + pad;\n"
    "  r->used += pad + bytes;\n"
    "  return (void *)p;\n"
    "}\n"
    "__attribute__((unused)) static inline void cplus_region_release("
    "cplus_region_token *r) {\n"
    "  if (!r || r->magic != CPLUS_REGION_MAGIC) return;\n"
    "  r->magic = 0;\n"
    "  if (r->base) cplus_release(r->base);\n"
    "  r->base = 0; r->used = 0; r->capacity = 0;\n"
    "}\n"
    "#endif\n";

bool startsWith(const std::string &s, const char *prefix) {
  size_t n = std::char_traits<char>::length(prefix);
  return s.size() >= n && s.compare(0, n, prefix) == 0;
}

/// Issue #179: Cake qualifiers are compile-time-only. They are normally
/// already erased by CPlusRewriter.cpp, but lowerRaii() is also called
/// directly (headers, tooling, tests), so treat them as ignorable trivia here
/// as well. Defer bodies are re-emitted from the same token stream, so
/// filtering them in joinTokens* keeps the LIFO cleanup text qualifier-free.
bool isCakeQualifier(const Token &t) {
  if (t.kind != TokKind::Identifier)
    return false;
  return t.text == "_Opt" || t.text == "_Owner" || t.text == "_View";
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

/// Return the logical source location that the C preprocessor sees at byte
/// offset `at` in `text`. `quotedFile` comes back already escaped for use in a
/// #line directive; `line` is the line of the physical line containing `at`.
/// Earlier #line directives are honoured: a rewritten translation unit starts
/// with `#line 1 "orig.cp"`, so the byte after it is orig.cp:1, not line 2.
/// This is the location the region prelude temporarily displaces, and the one
/// its trailing #line directive must restore.
void logicalLocationAt(const std::string &text, size_t at,
                       const std::string &fallbackFile,
                       std::string &quotedFile, unsigned &line) {
  quotedFile = quoteFile(fallbackFile);
  line = 1;
  size_t pos = 0;
  for (;;) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos)
      eol = text.size();
    // Stop at the line containing `at`; its logical location is current.
    if (eol >= at)
      break;

    std::string nextQuoted;
    unsigned nextLine = 0;
    size_t k = pos;
    while (k < eol && (text[k] == ' ' || text[k] == '\t'))
      ++k;
    if (k < eol && text[k] == '#') {
      ++k;
      while (k < eol && (text[k] == ' ' || text[k] == '\t'))
        ++k;
      // Accept both `#line N "f"` and the GCC spelling `# N "f"`.
      if (k + 5 <= eol && text.compare(k, 5, "line ") == 0)
        k += 5;
      size_t digits = k;
      while (digits < eol && text[digits] >= '0' && text[digits] <= '9')
        ++digits;
      if (digits > k) {
        nextLine = 0;
        for (size_t j = k; j < digits; ++j)
          nextLine = nextLine * 10 + static_cast<unsigned>(text[j] - '0');
        size_t q = text.find('"', digits);
        if (q != std::string::npos && q < eol) {
          size_t qe = text.find('"', q + 1);
          if (qe != std::string::npos && qe <= eol)
            nextQuoted = text.substr(q, qe - q + 1); // keep the escaping
        }
      }
    }
    // A #line directive sets the *following* line's location; any other line
    // advances the logical line number within the current file.
    if (nextLine != 0) {
      if (!nextQuoted.empty())
        quotedFile = nextQuoted;
      line = nextLine;
    } else {
      ++line;
    }
    if (eol == text.size())
      break;
    pos = eol + 1;
  }
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
    // Global declarations and non-local-jump/termination helpers are needed by
    // both the escape checks and the owner-aware jump checks below.  Computing
    // them once keeps the token-level scan single-pass and cheap.
    globalNames_ = collectGlobals();
    // Issue #185: plain locals are not ownership obligations, so scopes_ does
    // not know about them.  Record their declaration depths once so an
    // assignment to an enclosing local can be recognised as a region escape.
    collectLocalScopes();
    collectControlFlowHelpers();
    collectTerminationMacros();
    collectNoreturnDeclarations();
    scan();
    stripCakeQualifiers();
    applyEdits();

    LoweredSource result;
    result.text = std::move(output_);
    // By-value transfer cleanup emits cplus_zero in the lowered C. The
    // rewriter's declaration is keyed off the ORIGINAL source, which never
    // mentions it, so declare it here to keep the STRICT safety AST parseable.
    if (result.text.find("cplus_zero(") != std::string::npos &&
        result.text.find("extern void cplus_zero(") == std::string::npos)
      result.text = "#line 1 \"<cplus-raii-prelude>\"\n"
                    "#include <stddef.h>\n"
                    "extern void cplus_zero(void *, size_t);\n"
                    "#line 1 \"" + quoteFile(filename_) + "\"\n" + result.text;
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
      if (isTrivia(k) || isCakeQualifier(toks_[k]))
        continue;
      if (!out.empty())
        out += " ";
      out += toks_[k].text;
    }
    return out;
  }

  /// `joinTokens` with a single insertion after token `at`.  Used to fold a
  /// zeroing guard into a defer's re-emitted text (a normal source edit would
  /// land inside the span that `defer` deletes from the output).
  std::string joinTokensWithInsert(size_t from, size_t to, size_t at,
                                   const std::string &ins) const {
    std::string out;
    for (size_t k = from; k < to; ++k) {
      if (isTrivia(k) || isCakeQualifier(toks_[k]))
        continue;
      if (!out.empty())
        out += " ";
      out += toks_[k].text;
      if (k == at)
        out += ins;
    }
    return out;
  }

  /// True when `name` denotes a call that never returns (a process terminator
  /// or a non-local jump), either directly or via a helper defined in this
  /// translation unit.  A helper declared in another TU is invisible here --
  /// see the limitation documented on collectControlFlowHelpers().
  bool isTerminationName(const std::string &name) const {
    return name == "exit" || name == "_Exit" || name == "abort" ||
           name == "quick_exit" || name == "_exit" || name == "longjmp" ||
           terminationHelpers_.count(name) != 0 ||
           longjmpHelpers_.count(name) != 0;
  }

  /// Find the variable assigned by the `=` at `eq`.  Handles both the plain
  /// `fp = exit` case and the function-pointer declarator `void (*fp)(int) =
  /// exit`; kNPos when the target is not a simple identifier.
  size_t findAssignmentTarget(size_t eq) const {
    size_t p = prevSig(eq);
    if (p == kNPos)
      return kNPos;
    if (toks_[p].kind == TokKind::Identifier)
      return p;
    if (!isPunct(p, ")"))
      return kNPos;
    // Function-pointer declarator: `(*name)(...) = ...`.  The `)` immediately
    // before `=` closes the parameter list; step back over it to the `(*name)`
    // group and pull the identifier out of that group.
    size_t paramsOpen = matchingParenBackward(p);
    if (paramsOpen == kNPos)
      return kNPos;
    size_t groupClose = prevSig(paramsOpen);
    if (groupClose == kNPos || !isPunct(groupClose, ")"))
      return kNPos;
    size_t groupOpen = matchingParenBackward(groupClose);
    if (groupOpen == kNPos)
      return kNPos;
    for (size_t k = groupOpen + 1; k < groupClose; ++k)
      if (isPunct(k, "*")) {
        size_t nm = nextSig(k);
        if (nm < groupClose && toks_[nm].kind == TokKind::Identifier)
          return nm;
      }
    return kNPos;
  }

  /// Find the owner taken by address anywhere inside a destructor call.  Plain
  /// `drop(&r)`, redundant parens `drop((&r))`, casts `drop((R*)&r)` and
  /// `drop(*&r)` must all be recognised, otherwise the automatic drop still
  /// runs after the explicit one and double-frees the handle.
  size_t findDroppedOwner(size_t lp, size_t rp) const {
    size_t found = kNPos;
    for (size_t k = lp + 1; k < rp; ++k) {
      if (!isPunct(k, "&"))
        continue;
      size_t nm = nextSig(k);
      if (nm < rp && toks_[nm].kind == TokKind::Identifier &&
          hasLiveOwner(toks_[nm].text))
        found = nm; // last match wins for multi-argument calls
    }
    return found;
  }

  /// Emit the idempotence guard that makes the scope-exit drop a no-op after an
  /// explicit drop.  At statement level we wrap the call in a compound block
  /// and add a real second statement; this avoids introducing a comma at the
  /// top level of a macro argument (`WRAP(drop(&r))` would otherwise be parsed
  /// as two macro arguments).  Inside any parenthesis (macro argument, `for`
  /// header, expression) we instead parenthesise the comma expression so the
  /// comma stays at nesting depth 1 and the surrounding grammar is untouched.
  void emitExplicitDropGuard(size_t callTok, size_t rp,
                             const std::string &name) {
    const std::string zero =
        ", (void)cplus_zero(&" + name + ", sizeof(" + name + "))";
    if (parenDepth_ > 0) {
      // Wrap the *whole call*: wrapping only the owner token would let the
      // call's own `)` close the wrapper for cast/paren forms (`drop((R*)&r)`).
      edits_.push_back(Edit{toks_[callTok].offset, 0, "(", seq_++});
      edits_.push_back(
          Edit{toks_[rp].offset + toks_[rp].text.size(), 0, zero + ")", seq_++});
      return;
    }
    size_t semi = statementEnd(rp + 1);
    bool callEndsStatement = semi != kNPos;
    if (callEndsStatement)
      for (size_t k = rp + 1; k < semi; ++k)
        if (!isTrivia(k)) {
          callEndsStatement = false;
          break;
        }
    if (callEndsStatement) {
      // Consume the statement's own semicolon: no stray `;` before `else`.
      std::string call = joinTokens(callTok, rp + 1);
      edits_.push_back(Edit{
          toks_[callTok].offset,
          toks_[semi].offset + toks_[semi].text.size() - toks_[callTok].offset,
          "{ " + call + "; (void)cplus_zero(&" + name + ", sizeof(" + name +
              ")); }",
          seq_++});
      return;
    }
    // Larger expression at statement level: keep the parenthesised form.
    edits_.push_back(Edit{toks_[callTok].offset, 0, "(", seq_++});
    edits_.push_back(
        Edit{toks_[rp].offset + toks_[rp].text.size(), 0, zero + ")", seq_++});
  }

  /// True when the token range contains the declaration of a C+ owner.  A
  /// forward `goto` that skips such a declaration reaches the label with an
  /// uninitialised handle, so the scope-exit destructor would run on garbage.
  bool jumpSkipsOwnerInit(size_t from, size_t to) const {
    for (size_t k = from; k < to; ++k) {
      if (toks_[k].kind == TokKind::Comment) {
        if (startsWith(toks_[k].text, kOwnPrefix) ||
            startsWith(toks_[k].text, kMemPrefix))
          return true;
        continue;
      }
      if (toks_[k].kind != TokKind::Identifier)
        continue;
      if (toks_[k].text == "defer")
        return true;
      if (!resourceTypes_.count(toks_[k].text))
        continue;
      size_t nm = nextSig(k);
      if (nm < to && toks_[nm].kind == TokKind::Identifier) {
        size_t after = nextSig(nm);
        if (after >= to || !isPunct(after, "(")) // not `R f(...)` declaration
          return true;
      }
    }
    return false;
  }

  /// Resolve the label of a `goto` and report whether the skipped region
  /// declares an owner.  The scan stops at a brace boundary so an unrelated
  /// later block is never blamed.
  bool gotoSkipsOwner(size_t i) const {
    size_t lbl = nextSig(i);
    if (lbl >= toks_.size() || toks_[lbl].kind != TokKind::Identifier)
      return false;
    // A forward goto may leave several nested blocks; only the end of the
    // enclosing function stops the search. Brace balance avoids matching a
    // label in a different function.
    int depth = 0;
    for (size_t k = lbl; k < toks_.size(); ++k) {
      if (isPunct(k, "{"))
        ++depth;
      else if (isPunct(k, "}")) {
        if (depth == 0)
          return false;
        --depth;
      }
      if (toks_[k].kind == TokKind::Identifier &&
          toks_[k].text == toks_[lbl].text && isPunct(nextSig(k), ":"))
        return jumpSkipsOwnerInit(lbl + 1, k);
    }
    return false;
  }

  /// Resolve the token index of the label named by the `goto` at `i`.
  /// Unlike gotoSkipsOwner(), this scan must keep going past the closing brace
  /// of the goto's own block: a normal `goto done;` leaves that block and the
  /// label sits after it.  The enclosing function's closing brace (the first
  /// scope) bounds the search so a label in a sibling function cannot match.
  size_t gotoLabelToken(size_t i) const {
    size_t lbl = nextSig(i);
    if (!inFunction_ || scopes_.empty() || lbl >= toks_.size() ||
        toks_[lbl].kind != TokKind::Identifier)
      return kNPos;
    const size_t functionEnd = scopes_.front().closeOffset;
    for (size_t k = lbl; k < toks_.size(); ++k) {
      if (toks_[k].offset > functionEnd)
        return kNPos;
      if (toks_[k].kind == TokKind::Identifier &&
          toks_[k].text == toks_[lbl].text && isPunct(nextSig(k), ":"))
        return k;
    }
    return kNPos;
  }

  /// Issue #183: drops for owners whose scope the `goto` leaves.  A scope is
  /// exited when the label lies after its closing brace; every scope deeper
  /// than the innermost one still containing the label is therefore unwound.
  /// When no scope is crossed (a local jump within the same block) the result
  /// is empty and the existing diagnostics remain the only response.
  std::string gotoExitDrops(size_t i) {
    if (scopes_.empty())
      return std::string();
    const size_t label = gotoLabelToken(i);
    if (label == kNPos)
      return std::string();
    const size_t labelOffset = toks_[label].offset;
    size_t from = scopes_.size();
    for (size_t p = scopes_.size(); p > 0; --p) {
      // A scope still open at the goto has no closeOffset yet (kNPos); treat
      // it as crossed when the label lies outside the current statement.
      if (scopes_[p - 1].closeOffset == kNPos) {
        from = p - 1;
        continue;
      }
      if (labelOffset > scopes_[p - 1].closeOffset)
        from = p - 1;
      else
        break;
    }
    if (from >= scopes_.size())
      return std::string();
    return dropsFor(from, scopes_.size(), DeclId{});
  }

  /// Find the compound statement controlled by the `if`/`switch`/`while`
  /// whose condition contains the `setjmp` call at `i`.  kNPos when the
  /// guarded statement is not a compound block (the live-owner half of
  /// `setjmpSkipsOwner()` still covers that spelling).
  size_t setjmpProtectedBlock(size_t i) const {
    size_t kw = kNPos;
    for (size_t k = prevSig(i); k != kNPos; k = prevSig(k)) {
      if (isIdent(k, "if") || isIdent(k, "switch") || isIdent(k, "while")) {
        kw = k;
        break;
      }
      if (isPunct(k, "{") || isPunct(k, "}") || isPunct(k, ";"))
        break;
    }
    if (kw == kNPos) {
      // `int rc = setjmp(env); if (rc == 0) { ... }` -- the setjmp result is
      // stored first, so the protected block is the following if-statement.
      size_t semi = statementEnd(i);
      for (size_t k = semi, steps = 0;
           k != kNPos && k < toks_.size() && steps < 24;
           k = nextSig(k), ++steps) {
        if (isIdent(k, "if")) {
          size_t lp2 = nextSig(k);
          if (!isPunct(lp2, "("))
            break;
          size_t rp2 = matchingParen(lp2);
          if (rp2 == kNPos)
            break;
          size_t body2 = nextSig(rp2);
          if (isPunct(body2, "{") && matchingBrace(body2) != kNPos)
            return body2;
          break;
        }
        if (isPunct(k, "}") || isPunct(k, ";"))
          continue;
      }
      return kNPos;
    }
    size_t lp = nextSig(kw);
    if (!isPunct(lp, "("))
      return kNPos;
    size_t rp = matchingParen(lp);
    if (rp == kNPos || rp <= i)
      return kNPos;
    size_t body = nextSig(rp);
    return isPunct(body, "{") && matchingBrace(body) != kNPos ? body : kNPos;
  }

  /// A `setjmp` environment saves the current stack frame: a later `longjmp`
  /// resumes there *without* running the scope-exit drop of any C+ owner
  /// declared in the protected block, or already live when the environment
  /// was saved.  STRICT rejects that case from the setjmp side as well as the
  /// longjmp side (a jumping helper may live in another TU/function, where no
  /// owner is live at the jump token itself).  Plain-C setjmp/longjmp with no
  /// owners stays accepted.  The rewrite's ownership markers and resource
  /// types are still visible in this token stream, so the block scan reuses
  /// the same owner-detection predicate as the forward-`goto` check.
  bool setjmpSkipsOwner(size_t i) const {
    if (hasLiveOwners())
      return true;
    size_t body = setjmpProtectedBlock(i);
    if (body == kNPos)
      return false;
    return jumpSkipsOwnerInit(body + 1, matchingBrace(body));
  }

  void handleDefer(size_t i) {
    if (scopes_.empty()) {
      errorAt("defer cannot appear outside a function scope", toks_[i]);
      return;
    }
    size_t prev = prevSig(i);
    if (prev != kNPos &&
        (isPunct(prev, ")") ||
         (toks_[prev].kind == TokKind::Identifier &&
          (toks_[prev].text == "else" || toks_[prev].text == "do")))) {
      errorAt("defer cannot be used as the unbraced body of a control "
              "statement; wrap it in '{ ... }'",
              toks_[i]);
      return;
    }

    size_t bodyStart = nextSig(i);
    bool isBlock = bodyStart < toks_.size() && isPunct(bodyStart, "{");
    size_t bodyEnd = kNPos;
    size_t deleteEndOffset = 0;
    std::string deferText;

    if (isBlock) {
      size_t close = matchingBrace(bodyStart);
      if (close == kNPos) {
        errorAt("unterminated defer block", toks_[i]);
        return;
      }
      bodyEnd = close + 1;
      deleteEndOffset = toks_[close].offset + toks_[close].text.size();
      deferText = joinTokens(bodyStart, bodyEnd);
    } else {
      size_t semi = statementEnd(i + 1);
      if (semi == kNPos || semi == i + 1) {
        errorAt("defer requires a statement", toks_[i]);
        return;
      }
      bodyEnd = semi;
      deleteEndOffset = toks_[semi].offset;
      deferText = joinTokens(i + 1, semi);
    }

    for (size_t k = bodyStart; k < bodyEnd; ++k) {
      if (toks_[k].kind != TokKind::Identifier)
        continue;
      const std::string &kw = toks_[k].text;
      if (kw == "return" || kw == "break" || kw == "continue" || kw == "goto") {
        errorAt("'" + kw + "' cannot jump out of a defer cleanup", toks_[k]);
        if (isBlock)
          skipTo_ = bodyEnd - 1;
        return;
      }
      // A throw/longjmp/terminator in the body exits before the rest of the
      // defer (and any pending owner cleanups) can run.  Throwing helpers are
      // collected before scan(), so they are visible here too.
      if (mode_ != Mode::LegacyC &&
          (kw == "throw" || throwHelpers_.count(kw) != 0 ||
           isTerminationName(kw) || terminationMacros_.count(kw) != 0))
        errorAt("'" + kw + "' cannot jump out of a defer cleanup", toks_[k]);
    }

    OwnedVar v;
    v.name = "__cplus_defer_" + std::to_string(deferCounter_++);
    v.isDefer = true;
    v.deferText = deferText;
    v.line = toks_[i].line;
    v.column = toks_[i].column;
    registerOwned(v);

    // Detect a conditional cleanup body.  `defer if (c) drop(&r);` only drops
    // on one branch; suppressing the automatic drop would leak on the other.
    // Record the token span so the explicit-drop guard can be folded into the
    // re-emitted defer text instead of an edit inside the deleted region.
    bool conditional = false;
    for (size_t k = bodyStart; k < bodyEnd; ++k) {
      if (toks_[k].kind == TokKind::Identifier) {
        const std::string &s = toks_[k].text;
        if (s == "if" || s == "while" || s == "for" || s == "switch")
          conditional = true;
      } else if (isPunct(k, "?") || isPunct(k, "&&") || isPunct(k, "||")) {
        conditional = true;
      }
    }
    defers_.push_back(DeferInfo{bodyStart, bodyEnd, toks_[i].offset,
                                deleteEndOffset, conditional, v.name});

    edits_.push_back(Edit{toks_[i].offset,
                          deleteEndOffset - toks_[i].offset,
                          std::string(), seq_++});
    if (isBlock) {
      // Do not let scan() push/pop a Scope for the defer block's braces,
      // while still tracking explicit *_drop(&owner) calls inside the block.
      for (size_t k = bodyStart + 1; k < bodyEnd - 1; ++k) {
        if (toks_[k].kind == TokKind::Identifier &&
            toks_[k].text.size() > 5 &&
            toks_[k].text.compare(toks_[k].text.size() - 5, 5, "_drop") == 0)
          handleExplicitDrop(k, toks_[k]);
      }
      skipTo_ = bodyEnd - 1;
    }
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
        if (skipTo_ != kNPos) {
          i = skipTo_;
          skipTo_ = kNPos;
        }
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
    v.declOffset = toks_[i].offset;
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

  /// Record one cleanup obligation in the innermost scope.  A declaration can
  /// be discovered twice (resource-type scan and ownership marker), so the
  /// same *name in the same scope* must not create two obligations.  The same
  /// name in an enclosing scope is a distinct binding and is kept: that is the
  /// shadowing case this scan used to skip entirely.
  void registerOwned(const OwnedVar &v) {
    for (auto &existing : scopes_.back().vars) {
      if (existing.name != v.name)
        continue;
      // Merge rather than overwrite: the marker and the declaration each carry
      // part of the metadata, and the live per-binding flags (moved, dropped,
      // defer text) must survive a second discovery of the same declaration.
      if (existing.drop.empty())
        existing.drop = v.drop;
      if (existing.type.empty())
        existing.type = v.type;
      existing.managed = existing.managed || v.managed;
      existing.isDefer = existing.isDefer || v.isDefer;
      if (existing.deferText.empty())
        existing.deferText = v.deferText;
      if (!existing.line) {
        existing.line = v.line;
        existing.column = v.column;
      }
      return;
    }
    scopes_.back().vars.push_back(v);
    const DeclId id{scopes_.size() - 1, scopes_.back().vars.size() - 1};
    liveByName_[v.name].push_back(id);
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
      // A loop whose body is a single statement has no compound block for
      // closeScope() to retire its frame (bodyPos stays -1).  The body's `;`
      // is the end of the loop; header semicolons are excluded by parenDepth_.
      // Leaving the frame alive made a later break/continue read a stale
      // target and drop nothing.
      if (parenDepth_ == 0 && !loops_.empty() && loops_.back().bodyPos < 0)
        loops_.pop_back();
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
    std::vector<OwnedVar> byValueRootVars;
    if (!inFunction_) {
      size_t p = prevSig(i);
      if (p != kNPos && isPunct(p, ")")) {
        isFunction = true;
        inFunction_ = true;
        size_t openParen = matchingParenBackward(p);
        size_t fnameIdx = openParen == kNPos ? kNPos : prevSig(openParen);
        currentFunctionName_ =
            fnameIdx != kNPos && toks_[fnameIdx].kind == TokKind::Identifier
                ? toks_[fnameIdx].text
                : std::string();
        // Issue #229: a by-value resource parameter is owned by the callee on
        // *every* exit path, including an early `return`.  analyzePlainC()
        // records the parameter bindings before scan(), so install them in the
        // function's root scope here; dropsFor() then sees them like any other
        // local.  The old closing-brace-only edit leaked on early returns.
        auto bvit = byValueParamsByOpen_.find(i);
        if (bvit != byValueParamsByOpen_.end())
          byValueRootVars = bvit->second;
        currentFunctionReturnsVoid_ = false;
        for (size_t k = fnameIdx; k != kNPos && k > 0;) {
          size_t prev = prevSig(k);
          if (prev == kNPos || isPunct(prev, ";") || isPunct(prev, "{") ||
              isPunct(prev, "}"))
            break;
          if (isIdent(prev, "void")) {
            currentFunctionReturnsVoid_ = true;
            break;
          }
          if (isPunct(prev, "*")) // `void *f()` is not a void return
            break;
          k = prev;
        }
        // Parameters are out-param escape targets: storing a live owner into
        // one hands its lifetime to the caller.
        currentParams_.clear();
        size_t open = matchingParenBackward(p);
        if (open != kNPos) {
          for (size_t k = open + 1; k < p; ++k)
            if (toks_[k].kind == TokKind::Identifier)
              currentParams_.insert(toks_[k].text);
        }
      }
    }

    Scope s;
    s.closeOffset = t.offset;
    s.closeLine = t.line;
    s.functionBody = isFunction;
    s.exceptionBlock = isFunction ? false : isExceptionBlockOpen(i);
    scopes_.push_back(std::move(s));
    for (const OwnedVar &v : byValueRootVars)
      registerOwned(v);

    // Issue #185: a lexical region's release belongs to the block this brace
    // opens.  handleRegion() cannot register it because the `region` keyword is
    // scanned while the *enclosing* scope is current; doing it there emitted
    // `cplus_region_release(&r)` at function exit, after `r` went out of scope.
    // Registering on entry to the block scope keeps the release inside r's
    // lifetime: scope exit emits it just before the closing brace, and an early
    // return from the block still runs it via dropsFor().
    if (!regions_.empty() && regions_.back().openTok == i &&
        regions_.back().scope == kNPos) {
      RegionFrame &frame = regions_.back();
      frame.scope = scopes_.size() - 1;
      OwnedVar v;
      v.name = "__cplus_region_" + std::to_string(regionCounter_++);
      v.isDefer = true;
      v.deferText = "cplus_region_release(&" + frame.name + ");";
      v.line = frame.line;
      v.column = frame.column;
      registerOwned(v);
    }

    // Issue #228: braces inside a loop *header* (`for (struct S s = {1}; ...)`)
    // are not the loop body.  Binding bodyPos to them retired the loop frame
    // before the real body, so break/continue dropped nothing.
    if (parenDepth_ == 0 && loops_.size() > 0 && loops_.back().bodyPos < 0)
      loops_.back().bodyPos = static_cast<int>(scopes_.size()) - 1;

    ++functionDepth_;
    atStmtStart_ = true;
  }

  void closeScope(size_t i, const Token &t) {
    if (scopes_.empty())
      return;
    // A region's pointers die with the scope that owns its token.
    if (!regions_.empty() && regions_.back().scope == scopes_.size() - 1)
      regions_.pop_back();
    Scope &s = scopes_.back();
    std::string drops = dropsFor(scopes_.size() - 1, scopes_.size(), DeclId{});
    if (!drops.empty())
      insertBefore(t, drops);

    int closedIndex = static_cast<int>(scopes_.size()) - 1;
    if (!loops_.empty() && loops_.back().bodyPos == closedIndex) {
      if (loops_.back().isDo)
        // Keep the do frame until its trailing `while` is seen.  Marking the
        // body closed makes that `while` match this frame and no inner loop.
        loops_.back().bodyClosed = true;
      else
        loops_.pop_back();
    }
    // A loop whose body is a compound statement owned by a nested construct
    // (e.g. `while (c) switch (x) { ... }`) never got a bodyPos of its own.
    // Retire that frame here too so it cannot shadow a real enclosing loop.
    // Header braces live at parenDepth_ > 0 and must never retire the frame.
    if (parenDepth_ == 0 && !loops_.empty() && loops_.back().bodyPos < 0 &&
        loops_.back().stackSizeAtKeyword == static_cast<size_t>(closedIndex))
      loops_.pop_back();

    // Retire the live-binding entries of every obligation in this scope so an
    // enclosing binding of the same name becomes visible again.
    for (const auto &v : s.vars) {
      auto it = liveByName_.find(v.name);
      if (it == liveByName_.end() || it->second.empty())
        continue;
      // The stack is in declaration order, so this scope's entries are on top.
      it->second.pop_back();
      if (it->second.empty())
        liveByName_.erase(it);
    }
    bool wasFunction = s.functionBody;
    scopes_.pop_back();
    if (functionDepth_ > 0)
      --functionDepth_;
    if (wasFunction) {
      inFunction_ = false;
    }
    atStmtStart_ = true;
  }

  // --- issue #185: lexical regions -----------------------------------------

  /// The region frame that owns `name`, if any.  Pointers recorded by
  /// cplus_region_alloc are only valid until that frame's scope closes.
  const RegionFrame *regionOwningPointer(const std::string &name) const {
    for (auto it = regions_.rbegin(); it != regions_.rend(); ++it)
      if (it->pointers.count(name))
        return &*it;
    return nullptr;
  }

  /// Find a region pointer anywhere in an expression, not just as its first
  /// token: a cast (`(int *)p`), parentheses or arithmetic must hide neither
  /// the pointer nor its owning frame.
  const RegionFrame *regionPointerInExpr(size_t from, size_t to,
                                         std::string *name) const {
    for (size_t k = from; k < to; ++k) {
      if (toks_[k].kind != TokKind::Identifier)
        continue;
      const RegionFrame *frame = regionOwningPointer(toks_[k].text);
      if (frame) {
        if (name)
          *name = toks_[k].text;
        return frame;
      }
    }
    return nullptr;
  }

  /// True when the storage written by the assignment at `eq` outlives the
  /// region.  Globals and parameters were already recognised; this adds
  /// enclosing locals and struct fields through their base object (issue
  /// #185).  A binding declared at the region's own depth (or deeper) dies
  /// with the token and is therefore not an escape.
  bool lhsOutlivesRegion(size_t eq, const RegionFrame &frame) const {
    size_t start = eq;
    while (start > 0 && !isPunct(start - 1, ";") &&
           !isPunct(start - 1, "{") && !isPunct(start - 1, "}"))
      --start;
    for (size_t k = start; k < eq; ++k) {
      if (toks_[k].kind != TokKind::Identifier)
        continue;
      const std::string &n = toks_[k].text;
      if (globalNames_.count(n) || currentParams_.count(n))
        return true;
      auto it = localsByScope_.find(n);
      if (it == localsByScope_.end())
        continue;
      for (size_t d : it->second)
        if (d < frame.scopeDepth)
          return true;
    }
    return false;
  }

  void handleRegion(size_t i) {
    size_t nameIdx = nextSig(i);
    if (nameIdx >= toks_.size() ||
        toks_[nameIdx].kind != TokKind::Identifier) {
      errorAt("region requires a token name: 'region r { ... }'", toks_[i]);
      return;
    }
    size_t after = nextSig(nameIdx);
    std::string capacity = std::to_string(4096u);
    size_t open = after;
    size_t capFrom = kNPos, capTo = kNPos;
    size_t capEnd = after;
    if (isPunct(after, "(")) {
      size_t rp = matchingParen(after);
      if (rp == kNPos || rp <= after + 1) {
        errorAt("region capacity must be a non-empty expression", toks_[i]);
        return;
      }
      capacity = joinTokens(after + 1, rp);
      capFrom = after;
      capTo = rp;
      capEnd = rp;
      open = nextSig(rp);
    }
    if (!isPunct(open, "{")) {
      errorAt("region requires a compound block: 'region r { ... }'",
              toks_[i]);
      return;
    }

    const std::string name = toks_[nameIdx].text;
    // Erase `region <name> (<capacity>)`; the `{` and the body stay verbatim.
    size_t eraseEnd = capTo != kNPos ? capTo : nameIdx;
    edits_.push_back(Edit{toks_[i].offset,
                          toks_[eraseEnd].offset +
                              toks_[eraseEnd].text.size() -
                              toks_[i].offset,
                          std::string(), seq_++});
    edits_.push_back(Edit{toks_[open].offset + toks_[open].text.size(), 0,
                          " cplus_region_token " + name +
                              "; cplus_region_init(&" + name + ", " +
                              capacity + ");",
                          seq_++});

    // The release becomes a defer obligation of the *block scope*, registered
    // by openScope() when `open` is scanned.  Registering it here would attach
    // it to the enclosing scope (usually the function body), so the release
    // would be emitted at function exit where `name` is no longer in scope.
    // Registering it first in the block keeps LIFO order: owners declared in
    // the body are dropped before the backing region is released.
    RegionFrame frame;
    frame.name = name;
    frame.openTok = open;
    // Depth of the body scope: bindings declared at this depth (or deeper)
    // die with the region; anything declared at a smaller depth outlives it.
    size_t depth = 0;
    for (size_t k = 0; k < open; ++k) {
      if (isPunct(k, "{"))
        ++depth;
      else if (isPunct(k, "}") && depth > 0)
        --depth;
    }
    frame.scopeDepth = depth + 1;
    frame.line = toks_[i].line;
    frame.column = toks_[i].column;
    regions_.push_back(frame);
    sawRegion_ = true;
    (void)capEnd;
  }

  /// `cplus_region_alloc(r, bytes)` lowers to a bump take from the stack token
  /// `r`.  The result stays a bare `T *`; no fat pointer crosses an ABI edge.
  void handleRegionAlloc(size_t i) {
    size_t lp = nextSig(i);
    if (!isPunct(lp, "(")) {
      errorAt("cplus_region_alloc requires a region token argument", toks_[i]);
      return;
    }
    size_t arg = nextSig(lp);
    size_t comma = arg;
    int depth = 0;
    while (comma < toks_.size() && !isPunct(comma, ")")) {
      if (isPunct(comma, "("))
        ++depth;
      else if (isPunct(comma, ")"))
        --depth;
      if (depth == 0 && isPunct(comma, ","))
        break;
      ++comma;
    }
    if (arg >= toks_.size() || toks_[arg].kind != TokKind::Identifier ||
        !isPunct(comma, ",")) {
      errorAt("cplus_region_alloc(region, bytes) expects two arguments",
              toks_[i]);
      return;
    }
    const std::string regionName = toks_[arg].text;
    RegionFrame *frame = nullptr;
    for (auto it = regions_.rbegin(); it != regions_.rend(); ++it)
      if (it->name == regionName) {
        frame = &*it;
        break;
      }
    if (!frame) {
      errorAt("region '" + regionName + "' is not in scope", toks_[arg],
              "declare the region with 'region " + regionName + " { ... }'");
      return;
    }

    edits_.push_back(Edit{toks_[i].offset, toks_[i].text.size(),
                          "cplus_region_take", seq_++});
    edits_.push_back(
        Edit{toks_[arg].offset, 0, "&", seq_++});

    // Record the result binding so return/assignment escapes are diagnosed.
    size_t eq = i;
    while (eq > 0 && !isPunct(eq, ";") && !isPunct(eq, "{")) {
      --eq;
      if (isPunct(eq, "="))
        break;
    }
    if (isPunct(eq, "=")) {
      size_t lhs = prevSig(eq);
      if (lhs != kNPos && toks_[lhs].kind == TokKind::Identifier)
        frame->pointers.insert(toks_[lhs].text);
    }
  }

  /// A pointer borrowed from a region may not leave that region's lexical
  /// scope.  This is enforced statically (`CPLUS-LIFETIME`) before the token
  /// release is emitted, because the backing block is reclaimed at close.
  void checkRegionEscape(size_t i, const Token &t) {
    if (regions_.empty())
      return;
    size_t end = statementEnd(i);
    if (end == kNPos)
      end = toks_.size();
    for (size_t k = i + 1; k < end; ++k) {
      if (toks_[k].kind != TokKind::Identifier)
        continue;
      const RegionFrame *frame = regionOwningPointer(toks_[k].text);
      if (!frame)
        continue;
      errorAt("[CPLUS-LIFETIME] pointer '" + toks_[k].text +
                  "' escapes region '" + frame->name + "'",
              t, "copy the data before leaving the region, or widen the "
                 "region scope");
      return;
    }
  }

  void handleExplicitDrop(size_t i, const Token &t) {
    size_t lp = nextSig(i);
    if (!isPunct(lp, "("))
      return;
    size_t rp = matchingParen(lp);
    if (rp == kNPos || rp >= toks_.size())
      return;
    size_t ownerTok = findDroppedOwner(lp, rp);
    DeferInfo *defer = nullptr;
    for (auto &d : defers_)
      if (toks_[i].offset >= d.startOffset && toks_[i].offset < d.endOffset) {
        defer = &d;
        break;
      }
    if (ownerTok != kNPos) {
      const std::string &name = toks_[ownerTok].text;
      // The obligation is the innermost live binding of that name; the
      // dropped flag is per binding, so dropping an inner shadow does
      // not mark the outer owner as already released.
      OwnedVar *owner = ownerAt(findLiveOwner(name));
      if (owner && owner->explicitlyDropped && strict_ &&
          mode_ != Mode::LegacyC)
        errorAt("resource '" + name + "' is explicitly dropped more than once",
                t,
                "release each owner exactly once; a second drop may "
                "free already-freed memory");
      if (owner)
        owner->explicitlyDropped = true;
      if (defer && !defer->conditional) {
        // Unconditional defer: its text *is* the scope-exit cleanup, so
        // suppress the automatic drop.  No source edit is needed (the
        // defer span is deleted from the output).
        if (owner)
          owner->deferDropped = true;
      } else if (defer) {
        // Conditional defer: the explicit drop runs on one branch only.
        // Keep the automatic drop and fold a zeroing guard into the
        // re-emitted defer text so the true branch cannot double-free.
        const std::string guard =
            " , (void)cplus_zero(&" + name + ", sizeof(" + name + "))";
        std::string text = joinTokensWithInsert(defer->startTok, defer->endTok,
                                                rp, guard);
        if (OwnedVar *guarded = ownerAt(findLiveOwner(defer->ownerName)))
          guarded->deferText = text;
      } else {
        // `cplus_zero` is a no-op on an already empty handle, so the
        // automatic drop inserted at scope exit cannot double-free.
        emitExplicitDropGuard(i, rp, name);
      }
    } else {
      size_t amp = nextSig(lp);
      if (strict_ && mode_ != Mode::LegacyC && amp < toks_.size() &&
          toks_[amp].kind == TokKind::Identifier &&
          !borrowOwner(toks_[amp].text).empty())
        errorAt("explicit drop through alias '" + toks_[amp].text +
                    "' is not supported",
                t, "call the destructor with the owner's address directly");
    }
  }

  void handleIdentifier(size_t i, const Token &t) {
    // Cake qualifiers carry no cleanup obligation; they are erased by
    // stripCakeQualifiers() and must not be mistaken for a use of a binding.
    if (isCakeQualifier(t))
      return;
    if (t.text == "defer") {
      handleDefer(i);
      return;
    }
    if (t.text == "region") {
      handleRegion(i);
      return;
    }
    if (t.text == "cplus_region_alloc") {
      handleRegionAlloc(i);
      return;
    }
    // Track explicit destructors.  Historically `explicitlyDropped_` suppressed
    // the automatic drop for the whole scope, but that set is path-insensitive:
    // a drop inside `if (cond)` leaked on the fall-through path.  We now keep
    // the automatic drop on every path and make the explicit drop idempotent by
    // zeroing the handle right after it (the same guard `cplus_move` uses).
    // `OwnedVar::explicitlyDropped` is retained only to detect a second
    // explicit drop.
    if (t.text.size() > 5 &&
        t.text.compare(t.text.size() - 5, 5, "_drop") == 0) {
      handleExplicitDrop(i, t);
    }
    if (t.text == "cplus_move") {
      size_t lp = nextSig(i);
      if (isPunct(lp, "(")) {
        size_t arg = nextSig(lp);
        if (arg < toks_.size() && toks_[arg].kind == TokKind::Identifier &&
            hasLiveOwner(toks_[arg].text)) {
          OwnedVar *owner = ownerAt(findLiveOwner(toks_[arg].text));
          if (owner && owner->moveWarned)
            warningAt("resource '" + toks_[arg].text +
                          "' is moved more than once",
                      toks_[arg]);
          if (owner) {
            owner->moveWarned = true;
            owner->movedByMove = true;
            owner->moved = true;
          }
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
    const OwnedVar *used = ownerAt(findLiveOwner(t.text));
    if (used && used->movedByMove && mode_ != Mode::LegacyC) {
      if (strict_)
        errorAt("use-after-move: use of moved-from resource '" + t.text + "'",
                t, "the handle was zeroed by cplus_move");
      else
        warningAt("use of moved-from resource '" + t.text + "'", t,
                  "the handle was zeroed by cplus_move");
    }
    const std::string borrowedOwner = borrowOwner(t.text);
    if (!borrowedOwner.empty()) {
      const OwnedVar *owner = ownerAt(findLiveOwner(borrowedOwner));
      if (owner && owner->moved)
        errorAt("borrow '" + t.text + "' of '" + borrowedOwner +
                    "' is used after that resource was moved",
                t, "re-borrow the new owner instead");
    }
    if (t.text == "return") {
      checkRegionEscape(i, t);
      DeclId exclude;
      checkReturnValue(i, &exclude);
      std::string drops = dropsFor(0, scopes_.size(), exclude);
      if (!drops.empty())
        insertReturnCleanup(i, drops);
      atStmtStart_ = false;
      return;
    }
    if (t.text == "break" || t.text == "continue") {
      // `continue` targets a loop; a switch frame on top of the stack must be
      // skipped or the enclosing loop's owners would not be destroyed.
      std::string drops = loopExitDrops(t.text == "continue");
      if (!drops.empty())
        insertStatementCleanup(i, drops);
      atStmtStart_ = false;
      return;
    }
    if (t.text == "goto") {
      // Issue #183: a jump that leaves a scope with live owners now runs their
      // drops at the goto site, before the branch.  `insertStatementCleanup`
      // wraps the jump in a block so an `if (c) goto L;` cannot capture the
      // injected call.  Diagnostics are deliberately unchanged: STRICT still
      // fails closed on the construct, but the emitted C no longer skips the
      // destructor even when the diagnostic is only a warning.
      std::string exitDrops = gotoExitDrops(i);
      if (exitDrops.empty() && hasLiveOwners())
        exitDrops = dropsFor(0, scopes_.size(), DeclId{});
      if (!exitDrops.empty())
        insertStatementCleanup(i, exitDrops);
      // STRICT fails closed when a managed/resource owner is live at the jump,
      // and also when the jump skips an owner declaration: the label is then
      // reached with an uninitialised handle that the scope-exit drop reads.
      // A plain goto with no cleanup obligations is untouched.
      if (strict_ && mode_ != Mode::LegacyC && hasLiveOwners())
        errorAt("'goto' bypasses C+ cleanup; destructors may not run", t,
                "use structured control flow or an explicit cleanup call");
      else if (strict_ && mode_ != Mode::LegacyC && gotoSkipsOwner(i))
        errorAt("'goto' jumps over the declaration of a C+ owner; cleanup may "
                "run on an uninitialised handle",
                t, "declare owners before the jump, or use structured control "
                   "flow");
      else
        warningAt("'goto' bypasses C+ cleanup; destructors may not run", t,
                  "use structured control flow or an explicit cleanup call");
      atStmtStart_ = false;
      return;
    }
    if (t.text == "longjmp" ||
        (longjmpHelpers_.count(t.text) && isPunct(nextSig(i), "("))) {
      // Match the goto rule: STRICT rejects a non-local jump while a C+ owner
      // is live.  Plain-C longjmp is untouched -- analyzePlainC keeps its own
      // live-allocation rule, so no new diagnostic is emitted without owners.
      // A helper that contains the jump (e.g. `escape()`) is collected by
      // collectControlFlowHelpers(), so it is rejected at its call site when
      // an owner is live there, even though no owner is live inside the helper.
      if (strict_ && mode_ != Mode::LegacyC && hasLiveOwners())
        errorAt("longjmp bypasses C+ cleanup; destructors may not run", t,
                "use structured control flow or an explicit cleanup call");
      atStmtStart_ = false;
      return;
    }
    if (t.text == "setjmp") {
      // The setjmp half of the non-local-jump rule: a protected block that
      // declares (or protects an already live) C+ owner would have its
      // scope-exit drop skipped by any longjmp to this environment.  No owner
      // means ordinary C compatibility, and LegacyC/DEFAULT stay permissive.
      if (strict_ && mode_ != Mode::LegacyC && setjmpSkipsOwner(i))
        errorAt("'setjmp' protects a C+ owner whose destructor would be "
                "skipped by a longjmp",
                t, "declare owners outside the protected region, or use "
                   "structured control flow");
      atStmtStart_ = false;
      return;
    }
    if (t.text == "throw") {
      // Exception lowering rewrites `throw` into cplus_exception_raise(), which
      // longjmps to the enclosing try frame.  That non-local jump skips the
      // scope-exit destructor of every owner declared inside that try block.
      // Diagnose here, before lowerExceptions() hides the spelling.  An owner
      // declared *before* the try survives to be cleaned up after the handler
      // and is deliberately not flagged (throwSkipsOwner()).
      if (mode_ != Mode::LegacyC && throwSkipsOwner())
        errorAt("'throw' bypasses C+ cleanup; destructors may not run", t,
                "release live owners before throwing, or declare the owner "
                "outside the try region");
      atStmtStart_ = false;
      return;
    }
    // A helper that contains `throw` is a non-local jump in every caller that
    // has an owner live inside its own try region, just like longjmpHelpers_.
    if ((throwHelpers_.count(t.text) || throwAliases_.count(t.text)) &&
        isPunct(nextSig(i), "(")) {
      if (mode_ != Mode::LegacyC && throwSkipsOwner())
        errorAt("call through '" + t.text +
                    "' may throw and bypass C+ cleanup",
                t, "release live owners before calling a throwing helper");
      atStmtStart_ = false;
      return;
    }
    // Issue #229: passing an owned local by value transfers the handle to the
    // callee.  Zero the caller's handle *at this call site* so its own
    // scope-exit drop becomes a no-op even when the call sits on a branch that
    // is not taken.  The old forward scan from the declaration suppressed the
    // drop across unrelated functions and conditional branches, leaking the
    // owner on every path that did not make the call.
    auto bvit = byValueParamIndices_.find(t.text);
    if (bvit != byValueParamIndices_.end() && isPunct(nextSig(i), "(")) {
      size_t lp = nextSig(i), rp = matchingParen(lp);
      if (rp != kNPos && rp > lp) {
        size_t start = lp + 1;
        unsigned argIndex = 0;
        int argDepth = 0;
        std::set<std::string> transferred;
        for (size_t k = lp + 1; k <= rp; ++k) {
          bool end = k == rp;
          if (!end && isPunct(k, "(")) ++argDepth;
          if (!end && isPunct(k, ")")) --argDepth;
          if (!end && !(isPunct(k, ",") && argDepth == 0))
            continue;
          if (bvit->second.count(argIndex)) {
            // `start` already names the first token after `(` or `,`.
            // nextSig() advances before skipping trivia, so calling it on a
            // significant `start` would step over a single-token argument
            // (`f(x)`) and test the closing `)` instead.  That silently
            // dropped every transfer guard for the common `f(x)` spelling.
            size_t arg = isTrivia(start) ? nextSig(start) : start;
            if (arg < rp && toks_[arg].kind == TokKind::Identifier &&
                hasLiveOwner(toks_[arg].text))
              transferred.insert(toks_[arg].text);
          }
          ++argIndex;
          start = k + 1;
        }
        // A trailing `(void)cplus_zero(...)` comma operand has type void, so
        // wrapping a call that is used as a value (`int r = f(o);`) would
        // emit invalid C. Only add the guard for a bare call statement.
        bool bareCallStatement =
            isPunct(nextSig(rp), ";") || isPunct(nextSig(rp), "}");
        if (!transferred.empty() && bareCallStatement) {
          std::string tail;
          for (const std::string &name : transferred)
            tail += ", (void)cplus_zero(&" + name + ", sizeof(" + name + "))";
          tail += ")";
          edits_.push_back(Edit{toks_[i].offset, 0, "(", seq_++});
          edits_.push_back(
              Edit{toks_[rp].offset + toks_[rp].text.size(), 0, tail, seq_++});
        }
      }
    }
    if ((t.text == "exit" || t.text == "_Exit" || t.text == "abort" ||
         t.text == "quick_exit" || t.text == "_exit" ||
         (terminationHelpers_.count(t.text) && isPunct(nextSig(i), "("))) &&
        strict_ && mode_ != Mode::LegacyC && hasLiveOwners()) {
      // Terminating the process never runs scope-exit destructors.  Emit a
      // diagnostic (no new ABI/runtime call) while an owner is still live.
      errorAt("'" + t.text + "' bypasses C+ cleanup; destructors may not run",
              t, "release live owners before terminating, or use structured "
                 "control flow");
      atStmtStart_ = false;
      return;
    }
    // Indirect termination: a function pointer last assigned a terminator /
    // longjmp helper, or a macro whose body is a terminator.  The direct-name
    // checks above cannot see these spellings.  The alias set is updated in
    // checkAssignment() as the token scan advances, so a later reassignment to
    // a normal function removes the alias and avoids a false positive.
    {
      auto mit = terminationMacros_.find(t.text);
      bool indirectCall = terminationAliases_.count(t.text) != 0 ||
                          (mit != terminationMacros_.end() &&
                           (!mit->second || isPunct(nextSig(i), "(")));
      if (indirectCall && strict_ && mode_ != Mode::LegacyC &&
          hasLiveOwners()) {
        errorAt("call through '" + t.text +
                    "' may bypass C+ cleanup; destructors may not run",
                t, "release live owners before a non-returning call, or use "
                   "structured control flow");
        atStmtStart_ = false;
        return;
      }
    }
    if (t.text == "for" || t.text == "while" || t.text == "do" ||
        t.text == "switch") {
      // Issue #228: only the *innermost, body-closed* `do` frame owns the
      // trailing `while`.  A nested `while` or `do` now gets its own frame, so
      // break/continue always target the loop syntactically around them.
      bool closesDo = t.text == "while" && !loops_.empty() &&
                      loops_.back().isDo && loops_.back().bodyClosed;
      if (closesDo) {
        loops_.pop_back();
      } else {
        LoopInfo l;
        l.stackSizeAtKeyword = scopes_.size();
        l.isSwitch = t.text == "switch";
        l.isDo = t.text == "do";
        loops_.push_back(l);
      }
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
  ///
  /// Issue #230: only the `return` keyword is replaced.  The expression is
  /// left in the source verbatim so edits discovered later while scanning its
  /// tokens (explicit-drop zero guards, cplus_region_alloc lowering, ...) are
  /// not overwritten by a whole-span replacement.  `__auto_type` avoids
  /// spelling the type twice and decays arrays, so `return buf;` is valid C.
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
    const size_t keywordOffset = toks_[keywordIdx].offset;
    const size_t keywordLength = toks_[keywordIdx].text.size();
    std::string before, middle;
    if (currentFunctionReturnsVoid_) {
      // `return void_expr;` is legal in a void function; `__auto_type` cannot
      // hold void, so evaluate and discard the expression instead.
      before = "{ (void)(";
      middle = "); " + drops + "return";
    } else {
      before = "{ __auto_type __cplus_ret = (";
      middle = "); " + drops + "return __cplus_ret";
    }
    edits_.push_back(
        Edit{keywordOffset, keywordLength, before, seq_++});
    edits_.push_back(Edit{toks_[semi].offset, 0, middle, seq_++});
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

  /// The innermost live binding named `name`, or an invalid DeclId.  The
  /// per-name stack is pushed by registerOwned() and popped by closeScope(), so
  /// a shadowing inner declaration hides the outer one exactly while its scope
  /// is open and the outer binding is visible again once it closes.
  DeclId findLiveOwner(const std::string &name) const {
    auto it = liveByName_.find(name);
    if (it == liveByName_.end() || it->second.empty())
      return DeclId{};
    return it->second.back();
  }

  /// True when a binding with the same name is live in a deeper scope, so its
  /// name no longer denotes this binding at the current point.
  bool isShadowed(DeclId id) const {
    const OwnedVar *v = ownerAt(id);
    if (!v)
      return false;
    auto it = liveByName_.find(v->name);
    if (it == liveByName_.end() || it->second.empty())
      return false;
    const DeclId top = it->second.back();
    return !(top.scope == id.scope && top.var == id.var);
  }

  /// Saved reference to a binding whose name is shadowed at the drop point.
  /// A `return` inside a shadowing block must destroy the enclosing owner, but
  /// the emitted C name resolves to the inner object, so the address is saved
  /// at the point where the name still meant the outer binding.  Returns an
  /// empty string when that is impossible (deferred cleanup text is copied
  /// verbatim and cannot be rewritten to use a saved address).
  std::string ensureEscapeAlias(DeclId id) {
    OwnedVar *v = ownerAt(id);
    if (!v)
      return std::string();
    if (!v->escapeAlias.empty())
      return v->escapeAlias;
    if (v->isDefer)
      return std::string(); // deferText is verbatim; no rewritable reference
    if (!v->managed && v->type.empty())
      return std::string(); // no type name available to declare the address
    // Emit the saved address immediately before the declaration that shadows
    // this binding: at that offset `&name` still denotes this object.
    auto it = liveByName_.find(v->name);
    if (it == liveByName_.end())
      return std::string();
    const DeclId shadow = it->second.back();
    const OwnedVar *inner = ownerAt(shadow);
    if (!inner || inner->declOffset == kNPos)
      return std::string();
    std::string alias =
        "__cplus_shadow_" + std::to_string(++shadowCounter_) + "_" + v->name;
    std::string decl = v->managed
                           ? "void *" + alias + " = (void *)" + v->name + "; "
                           : v->type + " *" + alias + " = &" + v->name + "; ";
    edits_.push_back(Edit{inner->declOffset, 0, decl, seq_++});
    v->escapeAlias = alias;
    return alias;
  }

  bool hasLiveOwner(const std::string &name) const {
    return findLiveOwner(name).valid();
  }

  const OwnedVar *ownerAt(DeclId id) const {
    if (!id.valid() || id.scope >= scopes_.size())
      return nullptr;
    const auto &vars = scopes_[id.scope].vars;
    if (id.var >= vars.size())
      return nullptr;
    return &vars[id.var];
  }

  OwnedVar *ownerAt(DeclId id) {
    return const_cast<OwnedVar *>(
        static_cast<const Lowering *>(this)->ownerAt(id));
  }

  /// The owner a `T *p = &owner;` alias in scope `scopeIdx` borrows, or empty.
  std::string borrowOwner(const std::string &alias) const {
    for (size_t p = scopes_.size(); p > 0; --p) {
      const auto &b = scopes_[p - 1].borrows;
      auto it = b.find(alias);
      if (it != b.end())
        return it->second;
    }
    return std::string();
  }

  /// `hasLiveOwners` answers "is any destructor still pending?".  The scope
  /// stack is the only record of ownership now; the old function-wide `owned_`
  /// map kept entries for bindings whose scope had already closed.
  bool hasLiveOwners() const {
    for (const auto &s : scopes_)
      if (!s.vars.empty())
        return true;
    return false;
  }

  /// Does the `{` at `openBrace` open a try/catch body?  Exception lowering
  /// consumes these braces and turns them into a setjmp/longjmp frame, so
  /// they are the boundary a `throw` unwinds out of.
  bool isExceptionBlockOpen(size_t openBrace) const {
    size_t p = prevSig(openBrace);
    for (size_t steps = 0; p != kNPos && steps < 8; ++steps) {
      if (isIdent(p, "try") || isIdent(p, "catch"))
        return true;
      if (isPunct(p, ";") || isPunct(p, "{") || isPunct(p, "}"))
        return false;
      p = prevSig(p);
    }
    return false;
  }

  /// True when a `throw` (or a call to a throwing helper) at this point would
  /// skip a destructor that would otherwise run.  Owners declared inside the
  /// innermost enclosing try/catch block are lost to the longjmp; an owner
  /// declared before that block survives and is still cleaned up normally
  /// after the handler, so it must not trigger a diagnostic.  With no
  /// enclosing try the throw is unhandled and skips every live owner.
  bool throwSkipsOwner() const {
    size_t boundary = 0;
    for (size_t p = scopes_.size(); p > 0; --p) {
      if (scopes_[p - 1].exceptionBlock) {
        boundary = p - 1;
        break;
      }
    }
    for (size_t p = boundary; p < scopes_.size(); ++p)
      for (const auto &v : scopes_[p].vars)
        if (!v.explicitlyDropped && !v.movedByMove)
          return true;
    return false;
  }

  /// Cleanup calls for scopes [from, to), innermost first, and within one
  /// scope in reverse declaration order.  `exclude` names the single binding
  /// whose ownership has been transferred and must not be destroyed here; it is
  /// an identity, not a name, so the outer binding of a shadowed name is still
  /// destroyed by its own scope exit.
  ///
  /// Explicit drops no longer suppress the automatic drop: a drop on only one
  /// branch must not leak on the other.  Instead the explicit drop is guarded
  /// with `cplus_zero`, so the automatic destructor sees an empty handle and
  /// becomes a no-op.  A by-value transfer is likewise made idempotent by a
  /// zeroing guard emitted at the call site (issue #229), not by suppressing
  /// this drop from a forward scan.  The only remaining suppression is for a
  /// drop embedded in `defer`, whose text is itself the scope-exit cleanup.
  std::string dropsFor(size_t from, size_t to, DeclId exclude) {
    std::string out;
    for (size_t p = to; p > from; --p) {
      const auto &vars = scopes_[p - 1].vars;
      for (size_t k = vars.size(); k > 0; --k) {
        const DeclId id{p - 1, k - 1};
        const OwnedVar &v = vars[k - 1];
        if ((exclude.valid() && exclude.scope == p - 1 &&
             exclude.var == k - 1) ||
            v.deferDropped || v.name == "NULL")
          continue;
        // At a scope exit the name may already denote an inner shadow, so the
        // drop has to go through the saved address instead.  This only matters
        // for a `return` that leaves several scopes at once; closeScope() and
        // break/continue drops never cross a shadow boundary.
        const bool shadowed = isShadowed(id);
        const std::string alias = shadowed ? ensureEscapeAlias(id) : std::string();
        if (shadowed && alias.empty()) {
          // Cannot name the object and cannot save a usable reference: emitting
          // a drop by name would destroy the INNER object.  Leak rather than
          // corrupt, and say so.
          warningAt("cleanup for shadowed owner '" + v.name +
                        "' cannot be emitted at this exit",
                    toks_[0]);
          continue;
        }
        if (v.isDefer)
          out += v.deferText +
                 (!v.deferText.empty() && v.deferText.back() == '}' ? " "
                                                                    : "; ");
        else if (v.managed)
          out += shadowed ? "cplus_release(" + alias + "); "
                          : "cplus_release(" + v.name + "); " + v.name +
                                " = NULL; ";
        else
          out += v.drop + "(" + (shadowed ? alias : "&" + v.name) + "); ";
      }
    }
    return out;
  }

  std::string loopExitDrops(bool continueStmt = false) {
    if (loops_.empty())
      return std::string();
    size_t p = loops_.size() - 1;
    if (continueStmt) {
      // `continue` never targets a switch; walk past switch frames so owners
      // of the enclosing loop body still get destroyed.
      while (p > 0 && loops_[p].isSwitch)
        --p;
      if (loops_[p].isSwitch)
        return std::string(); // no enclosing loop
    }
    const LoopInfo &l = loops_[p];
    size_t from = l.bodyPos >= 0 ? static_cast<size_t>(l.bodyPos)
                                 : l.stackSizeAtKeyword;
    if (from > scopes_.size())
      from = scopes_.size();
    return dropsFor(from, scopes_.size(), DeclId{});
  }

  void checkReturnValue(size_t i, DeclId *excluded) {
    size_t v = nextSig(i);
    if (v >= toks_.size() || toks_[v].kind != TokKind::Identifier)
      return;
    // Only a direct `return x;` transfers the object.  An expression such as
    // `return x[0] == 1;` merely reads through it and is safe.
    size_t after = nextSig(v);
    if (after < toks_.size() && !isPunct(after, ";"))
      return;
    // Borrow-escape checks moved to the AST return-expression analysis in
    // AstSafetyCheck (issue #26).  The token-level form only recognized a bare
    // `return b;` and was bypassed by casts, parentheses, conditionals and
    // helper calls; keeping it here would also duplicate the semantic
    // diagnostic for the direct form.
    DeclId id = findLiveOwner(toks_[v].text);
    OwnedVar *owner = ownerAt(id);
    if (!owner)
      return;

    if (owner->managed) {
      Diag d;
      d.level = DiagLevel::Error;
      d.message = "managed pointer '" + owner->name +
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
    *excluded = id;
    owner->moved = true;
  }

  void checkAssignment(size_t i) {
    // Track function-pointer aliases of process terminators / longjmp helpers.
    // A call through such an alias never returns, so any cleanup injected after
    // it is dead code.  Reassignment to a non-terminator clears the alias, so
    // this stays conservative without flagging ordinary function pointers.
    {
      size_t target = findAssignmentTarget(i);
      if (target != kNPos && toks_[target].kind == TokKind::Identifier) {
        size_t rhs = nextSig(i);
        if (isPunct(rhs, "&"))
          rhs = nextSig(rhs);
        bool terminator = rhs < toks_.size() &&
                          toks_[rhs].kind == TokKind::Identifier &&
                          isTerminationName(toks_[rhs].text);
        bool thrower = rhs < toks_.size() &&
                       toks_[rhs].kind == TokKind::Identifier &&
                       throwHelpers_.count(toks_[rhs].text) != 0;
        if (terminator)
          terminationAliases_.insert(toks_[target].text);
        else
          terminationAliases_.erase(toks_[target].text);
        if (thrower)
          throwAliases_.insert(toks_[target].text);
        else
          throwAliases_.erase(toks_[target].text);
      }
    }

    size_t lhs = prevSig(i);
    if (lhs == kNPos || toks_[lhs].kind != TokKind::Identifier)
      return;
    {
      // A region pointer may be cast, parenthesised or combined with other
      // operands; scan the whole right-hand side.  The store escapes when its
      // destination outlives the region: a global, an out-parameter, an
      // enclosing local or a field of such an object (issue #185).
      size_t rhsTok = nextSig(i);
      size_t rhsEnd = statementEnd(i);
      if (rhsEnd == kNPos)
        rhsEnd = toks_.size();
      std::string pointerName;
      const RegionFrame *frame =
          regionPointerInExpr(rhsTok, rhsEnd, &pointerName);
      if (frame && (globalNames_.count(toks_[lhs].text) ||
                    currentParams_.count(toks_[lhs].text) ||
                    lhsOutlivesRegion(i, *frame)))
        errorAt("[CPLUS-LIFETIME] pointer '" + pointerName +
                    "' escapes region '" + frame->name + "'",
                toks_[lhs], "copy the data before leaving the region");
    }
    DeclId lhsId = findLiveOwner(toks_[lhs].text);
    OwnedVar *lhsOwner = ownerAt(lhsId);
    if (!lhsOwner) {
      // Storing a live owner into a global or an out-parameter lets its
      // lifetime escape the scope that will release it.  `return owner;` is
      // checked separately (a return transfers ownership); an assignment does
      // not, so STRICT rejects it rather than emitting a later use-after-free.
      size_t rhs = nextSig(i);
      if (rhs < toks_.size() && toks_[rhs].kind == TokKind::Identifier &&
          hasLiveOwner(toks_[rhs].text) &&
          (globalNames_.count(toks_[lhs].text) ||
           currentParams_.count(toks_[lhs].text)) &&
          strict_ && mode_ != Mode::LegacyC)
        errorAt("resource '" + toks_[rhs].text +
                    "' escapes through global/out-parameter '" +
                    toks_[lhs].text + "'",
                toks_[lhs],
                "copy the data instead of storing the owner, or transfer it "
                "explicitly");
      return;
    }

    // `*p = ...` and `p->field = ...` modify the pointee, not the owner.
    size_t before = prevSig(lhs);
    if (before != kNPos &&
        (isPunct(before, "*") || isPunct(before, ".") ||
         isPunct(before, "->")))
      return;

    size_t rhs = nextSig(i);
    if (rhs < toks_.size() && toks_[rhs].kind == TokKind::Identifier) {
      DeclId rhsId = findLiveOwner(toks_[rhs].text);
      if (rhsId.valid()) {
        const OwnedVar *rhsOwner = ownerAt(rhsId);
        errorAt("resource '" + lhsOwner->name + "' cannot be copied from '" +
                    rhsOwner->name + "'",
                toks_[lhs],
                "transfer ownership with cplus_move(" + rhsOwner->name +
                    ")");
        return;
      }
    }
    if (lhsOwner->managed) {
      errorAt("managed pointer '" + lhsOwner->name + "' cannot be reassigned",
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

    // Borrowed pointers are not owned by this scope.  An array of owners is
    // not tracked either (no per-element destructor is generated), so fail
    // closed in STRICT instead of silently leaving every element undropped.
    // DEFAULT only warns: a program that drops each element by hand is still
    // accepted there, since the linear scan cannot prove the array is leaked.
    if (isPunct(after, "[")) {
      strictDiag("array of resource type '" + toks_[typeIdx].text +
                     "' has no automatic element cleanup",
                 toks_[nameIdx],
                 "drop each element explicitly, or use a managed allocation");
      return;
    }
    if (isPunct(after, "*"))
      return;

    size_t semi = statementEnd(nameIdx);
    if (semi == kNPos)
      return;

    const std::string name = toks_[nameIdx].text;
    // No "already owned" early return here: a declaration may not be skipped
    // because an *enclosing* scope owns the same name.  registerOwned() still
    // deduplicates the same declaration being discovered twice (type scan and
    // ownership marker) inside one scope, so removing this guard cannot create
    // a second obligation for one binding.

    // Multi-declarator statements are rejected: each owning variable must
    // have its own declaration so cleanup order is unambiguous.
    int paren = 0, bracket = 0, brace = 0;
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
      // A comma inside an aggregate initializer (`Outer x = {{1}, 10};`) is
      // a field separator, not a second declarator.
      else if (isPunct(k, "{"))
        ++brace;
      else if (isPunct(k, "}"))
        --brace;
      else if (isPunct(k, "=") && paren == 0 && bracket == 0 && brace == 0) {
        // Record the first initializer but KEEP scanning: a comma after it
        // is still a second declarator. Breaking here let `R a={1}, b={2};`
        // through with neither destructor emitted.
        if (eq == kNPos)
          eq = k;
      } else if (isPunct(k, ",") && paren == 0 && bracket == 0 && brace == 0) {
        errorAt("declare one owning resource per statement", toks_[k]);
        return;
      }
    }

    // A resource local initialised from a call that receives a live owner by
    // value duplicates the handle (e.g. `R b = dupr(a);`): the callee's
    // parameter and the new local both own it, so two destructors run.  STRICT
    // rejects this; `cplus_move` is the only sanctioned transfer expression.
    if (eq != kNPos) {
      size_t init = nextSig(eq);
      if (init < semi && toks_[init].kind == TokKind::Identifier &&
          toks_[init].text != "cplus_move") {
        size_t lp = nextSig(init);
        if (isPunct(lp, "(")) {
          size_t rp = matchingParen(lp);
          if (rp != kNPos && rp <= semi) {
            for (size_t k = lp + 1; k < rp; ++k) {
              if (toks_[k].kind != TokKind::Identifier ||
                  !hasLiveOwner(toks_[k].text) || toks_[k].text == name)
                continue;
              size_t afterArg = nextSig(k);
              if (isPunct(afterArg, ".") || isPunct(afterArg, "->") ||
                  isPunct(afterArg, "[") || isPunct(prevSig(k), "&"))
                continue; // field read or borrow, not a by-value copy
              if (strict_ && mode_ != Mode::LegacyC)
                errorAt("resource '" + toks_[k].text +
                            "' cannot be copied into a call returning an owner",
                        toks_[k],
                        "transfer ownership with cplus_move(" +
                            toks_[k].text + ") instead");
              break;
            }
          }
        }
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
    v.declOffset = toks_[typeIdx].offset;
    v.line = toks_[nameIdx].line;
    v.column = toks_[nameIdx].column;
    registerOwned(v);
  }

  /// Qualifiers allowed around the `*`/base type of a pointer borrow.  The
  /// set is deliberately small: arbitrary identifiers here would let an
  /// expression such as `a * b;` be read as a declaration.
  static bool isBorrowQualifier(const std::string &s) {
    return s == "const" || s == "volatile" || s == "restrict" ||
           s == "__restrict" || s == "__restrict__";
  }

  /// Type specifiers that may precede the base type name (`struct T *p`).
  static bool isBorrowTypeSpecifier(const std::string &s) {
    return isBorrowQualifier(s) || s == "struct" || s == "union" ||
           s == "enum";
  }

  /// Record `T *p = &owner;` and `T *p = managed_owner;` as borrows of a
  /// local resource or managed allocation.  This is a deliberately small
  /// borrow checker: it catches dangling escapes and use-after-move, not
  /// aliasing or general lifetime relationships.
  bool tryBorrowDeclaration(size_t i) {
    const size_t semi = statementEnd(i);
    if (semi == kNPos)
      return false;

    // Prefix: any number of type specifiers/qualifiers, exactly one base type
    // identifier, then a single `*`.  This accepts `const T *`, `struct T *`,
    // `T const *` and their combinations without confusing an expression
    // statement for a declaration.
    size_t cur = i;
    bool sawType = false;
    while (cur < semi) {
      if (isPunct(cur, "*"))
        break;
      if (toks_[cur].kind != TokKind::Identifier)
        return false;
      if (isBorrowTypeSpecifier(toks_[cur].text)) {
        cur = nextSig(cur);
        continue;
      }
      if (sawType)
        return false;
      sawType = true;
      cur = nextSig(cur);
    }
    if (!sawType || !isPunct(cur, "*"))
      return false;

    // `T * const p` is still a direct pointer to the owner.  A second `*` is
    // not: a pointer-to-pointer alias is outside this small checker.
    cur = nextSig(cur);
    while (cur < semi && toks_[cur].kind == TokKind::Identifier &&
           isBorrowQualifier(toks_[cur].text))
      cur = nextSig(cur);
    if (cur >= semi || toks_[cur].kind != TokKind::Identifier)
      return false;
    const size_t name = cur;

    const size_t eq = nextSig(name);
    if (!isPunct(eq, "=") || eq >= semi)
      return false;
    const size_t init = nextSig(eq);
    const bool addressOf = isPunct(init, "&");
    const size_t ownerIdx = addressOf ? nextSig(init) : init;
    if (ownerIdx >= semi || toks_[ownerIdx].kind != TokKind::Identifier)
      return false;
    if (!hasLiveOwner(toks_[ownerIdx].text))
      return false;
    scopes_.back().borrows[toks_[name].text] = toks_[ownerIdx].text;
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
    bool inDirective = false;
    for (size_t i = 0; i < toks_.size(); ++i) {
      // The rewriter prepends a `#line` directive to every translation unit.
      // Without skipping preprocessor lines, `atStart` is consumed by that
      // directive and the first real declaration (typically a global such as
      // `static int *g;`) is never recorded.  Region-escape checks rely on
      // globalNames_, so a region pointer stored into such a global escaped
      // detection entirely.
      if (inDirective) {
        if (toks_[i].kind == TokKind::Whitespace &&
            toks_[i].text.find('\n') != std::string::npos)
          inDirective = false;
        continue;
      }
      if (isPunct(i, "#")) {
        inDirective = true;
        continue;
      }
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

  /// Issue #185: record every plain local declaration together with the brace
  /// depth of the scope that owns it.  `globalNames_` covers file-scope stores
  /// and `currentParams_` covers out-parameters, but neither covers the two
  /// remaining ways a region pointer can outlive its token: an enclosing
  /// local (`int *saved; region r { saved = p; }`) and a struct field
  /// (`b.p = p;`).  Both need the declaration depth of the storage location.
  void collectLocalScopes() {
    size_t depth = 0;
    bool inDirective = false;
    bool atStmtStart = true;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (inDirective) {
        if (toks_[i].kind == TokKind::Whitespace &&
            toks_[i].text.find('\n') != std::string::npos)
          inDirective = false;
        continue;
      }
      if (isPunct(i, "#")) {
        inDirective = true;
        continue;
      }
      if (isTrivia(i))
        continue;
      if (isPunct(i, "{")) {
        ++depth;
        atStmtStart = true;
        continue;
      }
      if (isPunct(i, "}")) {
        if (depth > 0)
          --depth;
        atStmtStart = true;
        continue;
      }
      if (isPunct(i, ";") || isPunct(i, ":")) {
        atStmtStart = true;
        continue;
      }
      if (atStmtStart && depth > 0) {
        size_t end = statementEnd(i);
        if (end != kNPos) {
          std::map<std::string, LocalVar> found;
          recordLocalDecl(i, end, found, nullptr, static_cast<int>(depth));
          for (const auto &kv : found)
            localsByScope_[kv.first].push_back(depth);
        }
      }
      atStmtStart = false;
    }
  }

  /// Collect the names of functions that contain `longjmp` or a process
  /// terminator (`exit`/`_Exit`/`abort`).  These helpers bypass the destructors
  /// of live owners in their *callers*, which a purely local scan cannot see.
  /// The scan is deliberately token-level and conservative: a helper is only
  /// recorded when its definition is visible in this translation unit.
  /// LIMITATION: a terminator helper defined in another TU is invisible here.
  /// A call to such a helper is reported by the driver's per-TU (MULTITU)
  /// disclosure, which is a warning; STRICT cannot prove it is `noreturn`
  /// without whole-program information.  We deliberately do not treat every
  /// unresolved call as terminating -- that would reject ordinary C+ programs
  /// that call ordinary library functions while an owner is live.
  void collectControlFlowHelpers() {
    if (mode_ == Mode::LegacyC)
      return;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isPunct(i, "{"))
        continue;
      size_t before = prevSig(i);
      if (before == kNPos || !isPunct(before, ")"))
        continue; // not a function body
      size_t end = matchingBrace(i);
      if (end == kNPos)
        continue;
      size_t open = matchingParenBackward(before);
      std::string name;
      if (open != kNPos && open > 0) {
        size_t nm = prevSig(open);
        if (nm != kNPos && toks_[nm].kind == TokKind::Identifier)
          name = toks_[nm].text;
      }
      bool jumps = false, terminates = false, throws = false;
      for (size_t k = i + 1; k < end; ++k) {
        if (toks_[k].kind != TokKind::Identifier)
          continue;
        if (toks_[k].text == "longjmp")
          jumps = true;
        // `throw` lowers to a longjmp as well; a helper containing one skips
        // the scope-exit destructors of owners live at its call sites.
        if (toks_[k].text == "throw")
          throws = true;
        if (toks_[k].text == "exit" || toks_[k].text == "_Exit" ||
            toks_[k].text == "abort" || toks_[k].text == "quick_exit" ||
            toks_[k].text == "_exit")
          terminates = true;
      }
      if (!name.empty()) {
        if (jumps)
          longjmpHelpers_.insert(name);
        if (throws)
          throwHelpers_.insert(name);
        if (terminates)
          terminationHelpers_.insert(name);
      }
      i = end;
    }
  }

  /// A `_Noreturn` / `__attribute__((noreturn))` prototype can be declared in
  /// a header and defined in another translation unit.  Its body is invisible
  /// to collectControlFlowHelpers(), so record the declared name here and let
  /// the existing termination check reject calls made with a live owner.
  void collectNoreturnDeclarations() {
    if (mode_ == Mode::LegacyC)
      return;
    // Marker-driven: only declarations that actually carry `noreturn` cost
    // anything, so large translation units stay linear in practice.
    for (size_t m = 0; m < toks_.size(); ++m) {
      if (toks_[m].kind != TokKind::Identifier)
        continue;
      bool marker = isIdent(m, "_Noreturn");
      if (isIdent(m, "noreturn")) {
        size_t op = prevSig(m), oop = op == kNPos ? kNPos : prevSig(op);
        marker = isPunct(op, "(") && isPunct(oop, "(") &&
                 isPunct(nextSig(m), ")");
      }
      if (!marker)
        continue;
      size_t lo = m < 32 ? 0 : m - 32;
      size_t hi = std::min(toks_.size(), m + 32);
      for (size_t k = lo; k < hi; ++k) {
        if (toks_[k].kind != TokKind::Identifier || !isPunct(nextSig(k), "("))
          continue;
        // The name must be in the same declaration as the marker.
        size_t a = std::min(k, m), b = std::max(k, m);
        bool sameDecl = true;
        for (size_t q = a; q < b; ++q)
          if (isPunct(q, ";") || isPunct(q, "{") || isPunct(q, "}")) {
            sameDecl = false;
            break;
          }
        if (sameDecl) {
          terminationHelpers_.insert(toks_[k].text);
          break;
        }
      }
    }
  }

  /// Preprocessor directives are skipped by scan(), so a macro such as
  /// `#define DIE() exit(1)` would otherwise hide the terminator from the
  /// owner-aware jump checks and leave an unreachable drop after the call.
  /// Record macro names whose replacement list contains a terminator; the bool
  /// marks a function-like macro (a bare object-like macro also counts as a
  /// call site).  Only macros with a visible terminator are recorded, so
  /// ordinary wrappers are untouched.
  void collectTerminationMacros() {
    if (mode_ == Mode::LegacyC)
      return;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isPunct(i, "#"))
        continue;
      size_t def = nextSig(i);
      if (!isIdent(def, "define"))
        continue;
      size_t name = nextSig(def);
      if (name >= toks_.size() || toks_[name].kind != TokKind::Identifier)
        continue;
      bool functionLike = isPunct(nextSig(name), "(");
      bool terminates = false;
      size_t newline = kNPos;
      for (size_t k = name + 1; k < toks_.size(); ++k) {
        if (toks_[k].kind == TokKind::Whitespace &&
            toks_[k].text.find('\n') != std::string::npos) {
          newline = k;
          break;
        }
        if (toks_[k].kind == TokKind::Identifier &&
            isTerminationName(toks_[k].text))
          terminates = true;
      }
      if (terminates)
        terminationMacros_[toks_[name].text] = functionLike;
      if (newline != kNPos)
        i = newline;
    }
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
      // `cplus_no_cleanup` is not an implemented attribute (see LangFeatures.h)
      // and is intentionally unrecognised by the backend.  Silently honouring
      // it would hide both raw-allocation and owner cleanup failures, so STRICT
      // discloses the opt-out instead.  LegacyC is left untouched.
      bool noCleanup = false;
      size_t noCleanupTok = kNPos;
      for (size_t k = before, steps = 0;
           k != kNPos && k > 0 && steps < 32; ++steps) {
        size_t p = prevSig(k);
        if (p == kNPos || isPunct(p, ";") || isPunct(p, "}"))
          break;
        if (isIdent(p, "cplus_no_cleanup")) {
          noCleanup = true;
          noCleanupTok = p;
          break;
        }
        k = p;
      }
      size_t end = matchingBrace(i);
      if (end == kNPos)
        continue;
      if (noCleanup && strict_)
        errorAt("'cplus_no_cleanup' is not supported; cleanup analysis cannot "
                "be opted out",
                toks_[noCleanupTok == kNPos ? i : noCleanupTok]);
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
      size_t fnNameIdx = prevSig(openParen);
      const std::string funcName =
          fnNameIdx != kNPos && toks_[fnNameIdx].kind == TokKind::Identifier
              ? toks_[fnNameIdx].text
              : std::string();
      for (size_t k = openParen + 1; k < closeParen; ++k)
        if (toks_[k].kind == TokKind::Identifier)
          params.insert(toks_[k].text);
      // A by-value resource/class parameter is owned by the callee: its
      // destructor must run at function exit, just like a local owner.
      size_t start = openParen + 1;
      unsigned paramIndex = 0;
      int depth = 0;
      for (size_t k = openParen + 1; k <= closeParen; ++k) {
        bool end = k == closeParen;
        if (!end && isPunct(k, "(")) ++depth;
        if (!end && isPunct(k, ")")) --depth;
        if (!end && !(isPunct(k, ",") && depth == 0))
          continue;
        if (k > start) {
          size_t nameIdx = kNPos, typeIdx = kNPos;
          bool pointer = false;
          for (size_t j = start; j < k; ++j) {
            if (isPunct(j, "*"))
              pointer = true;
            if (toks_[j].kind == TokKind::Identifier) {
              nameIdx = j;
              if (typeIdx == kNPos &&
                  std::find(resourceTypes_.begin(), resourceTypes_.end(),
                            toks_[j].text) != resourceTypes_.end())
                typeIdx = j;
            }
          }
          if (!pointer && nameIdx != kNPos && typeIdx != kNPos &&
              nameIdx != typeIdx) {
            OwnedVar v;
            v.name = toks_[nameIdx].text;
            v.type = toks_[typeIdx].text;
            v.drop = v.type + "_drop";
            v.declOffset = typeIdx;
            v.line = toks_[typeIdx].line;
            v.column = toks_[typeIdx].column;
            // Issue #229: the binding is installed in the function's root
            // scope by openScope(), so its destructor runs on early returns
            // as well as on fallthrough.  No closing-brace-only edit here.
            byValueParamsByOpen_[openIdx].push_back(v);
            if (!funcName.empty())
              byValueParamIndices_[funcName].insert(paramIndex);
          }
        }
        if (!end || k > start)
          ++paramIndex;
        start = k + 1;
      }
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

  /// Remove any Cake qualifier token that survived into this pass. Erasing it
  /// (rather than translating it to a helper call) leaves a bare `T *` in the
  /// emitted C, so struct layout, offsets and calling conventions are exactly
  /// those of plain C. Cleanup for `_Owner` bindings is still emitted by the
  /// existing reverse-declaration-order dropsFor() walk, and defer bodies are
  /// re-emitted in LIFO order from joinTokens(), which filters these tokens.
  void stripCakeQualifiers() {
    for (size_t i = 0; i < toks_.size(); ++i)
      if (isCakeQualifier(toks_[i]))
        edits_.push_back(Edit{toks_[i].offset, toks_[i].text.size(),
                              std::string(), seq_++});
  }

  // --- edit application -----------------------------------------------------

  void applyEdits() {
    std::stable_sort(edits_.begin(), edits_.end(),
                     [](const Edit &a, const Edit &b) {
                       if (a.offset != b.offset)
                         return a.offset < b.offset;
                       return a.seq < b.seq;
                     });

    for (const auto &e : edits_)
      if (e.text.find("R_drop") != std::string::npos ||
          (e.text.find("{ ") != std::string::npos && e.text.size() < 60))
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
    if (sawRegion_ &&
        output_.find("CPLUS_REGION_PRELUDE_DEFINED") == std::string::npos) {
      // The driver prepends its own preamble after lowering, so an edit at
      // offset 0 can be lost. Insert the self-contained prelude into the
      // final text before the first include so declarations precede their
      // uses; it is guarded and order-independent.
      size_t at = output_.find("#include");
      if (at == std::string::npos)
        at = 0;
      // kRegionPrelude switches both __FILE__ and __LINE__ to the synthetic
      // "<cplus-region-prelude>" name. Without a trailing directive every
      // later diagnostic, __LINE__ expansion and Cake qualifier lookup would
      // be attributed to that prelude. Restore the location the preprocessor
      // had at the insertion point (honouring any #line already in the text).
      std::string restoreFile;
      unsigned restoreLine = 0;
      logicalLocationAt(output_, at, filename_, restoreFile, restoreLine);
      std::string prelude(kRegionPrelude);
      prelude += "#line " + std::to_string(restoreLine) + " " + restoreFile +
                 "\n";
      output_.insert(at, prelude);
    }
  }

  // --- state ----------------------------------------------------------------

  const std::string &source_;
  std::vector<Token> toks_;
  DiagnosticEngine &diags_;
  std::string filename_;
  Mode mode_;
  bool strict_ = false;

  std::set<std::string> resourceTypes_;
  // Ownership state is fully derived from `scopes_`: each Scope holds its own
  // bindings (with per-binding moved/dropped flags) and its own borrows.  There
  // is deliberately no function-wide name-keyed owner *map*: that duplication
  // was the cause of issue #28 (a shadowing inner binding was invisible).
  // This index is only a name -> live binding stack used to resolve a name to
  // the innermost declaration; it is pushed and popped with the scopes, so it
  // carries no duplicated metadata.
  std::map<std::string, std::vector<DeclId>> liveByName_;
  unsigned shadowCounter_ = 0;
  std::set<std::string> globalNames_;
  /// Issue #185: plain local name -> brace depths at which it is declared.
  /// Used to decide whether an assignment target outlives a region frame.
  std::map<std::string, std::vector<size_t>> localsByScope_;
  std::set<std::string> currentParams_;
  std::set<std::string> longjmpHelpers_;
  /// Functions whose definition contains `throw`; calling one is a non-local
  /// jump that skips the caller's try-region owner destructors.
  std::set<std::string> throwHelpers_;
  std::set<std::string> terminationHelpers_;
  /// Function-pointer variables whose last visible assignment was a
  /// terminator/longjmp (or a helper that contains one).
  std::set<std::string> terminationAliases_;
  std::set<std::string> throwAliases_;
  // Functions taking resource owners by value: call sites transfer those
  // arguments to the callee (the callee owns every exit path).
  std::map<std::string, std::set<unsigned>> byValueParamIndices_;
  // Parameter bindings keyed by the function body's opening brace token, so
  // openScope() can install them in the root scope before scan() reaches any
  // return statement.
  std::map<size_t, std::vector<OwnedVar>> byValueParamsByOpen_;
  std::string currentFunctionName_;
  bool currentFunctionReturnsVoid_ = false;
  /// Macro name -> function-like?  Macros hiding a terminator in their body.
  std::map<std::string, bool> terminationMacros_;
  std::vector<DeferInfo> defers_;
  /// Issue #185: innermost-first stack of active lexical regions.
  std::vector<RegionFrame> regions_;
  bool sawRegion_ = false; // survives regions_ being cleared during scan
  unsigned regionCounter_ = 0;
  size_t skipMoveArg_ = kNPos;
  size_t skipTo_ = kNPos;

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

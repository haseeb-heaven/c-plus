//===- StaticBoundsCheck.cpp - C+ static bounds check -------------*- C++ -*-===//
//
// This pass is deliberately small and cheap: it reuses the C+ lexer tokens
// and the extents collected by the syntax rewriter.  It never tries to be a
// C type checker; that is the host C compiler's job.
//
//===----------------------------------------------------------------------===//

#include "Sema/StaticBoundsCheck.h"

#include "Sema/CPlusLexer.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <set>
#include <string>
#include <vector>

namespace cplus {

namespace {

constexpr size_t kNPos = static_cast<size_t>(-1);

struct ArrayShape {
  size_t pos;
  std::string name;
  std::vector<size_t> dims;
  // Token index of the closing brace of the block that declared this array.
  // A fact from an inner block must not be used after that block closes.
  // kNPos means file scope or a shape inherited from the rewriter.
  size_t scopeEnd = kNPos;
  // Byte width of one element, read from the declaration's type word.  Needed
  // for byte-cast aliases (char*/void*), where a subscript counts bytes while
  // dims count elements.  0 means unknown: byte-unit checks then stay silent.
  size_t elemBytes = 0;
};

/// A pointer whose value is a provable constant offset (in elements) from a
/// fixed array.  This is what lets the checker see `p[-1]` in
/// `int *p = a; p[-1];` or a one-past dereference in
/// `int *p = a + 4; (void)*p;` without doing any type analysis.  Facts are
/// invalidated by any second assignment or ++/--/+=/-=, so a stale offset is
/// never used to reject valid code.
struct TrackedArrayPointer {
  std::string array;         // name of the underlying fixed array
  std::vector<size_t> dims;  // shape of that array
  long offset = 0;           // constant element offset from the array base
  size_t pos = 0;            // token position of the tracked assignment
  bool rowPointer = false;   // `int (*r)[W] = a`: subscripts select rows
};

class Checker {
  // Declared before the helpers that use it.
  struct FuncSig {
    std::vector<std::string> params;
    std::vector<bool> outParam; // `T **out`: an allocation written via *out
    // Indices of the parameters declared `restrict` / `__restrict` /
    // `__restrict__`.  Only these parameters may alias with each other.
    std::vector<bool> restrictParam;
    // Token position of each parameter's name.  A parameter shadows an outer
    // name-keyed extent for the whole body; a declaration at/after this token
    // (e.g. `void f(int a[8])`) still describes the parameter itself.
    std::map<std::string, size_t> paramDeclPos;
    size_t bodyOpen = 0;
    size_t bodyClose = 0;
  };

public:
  Checker(const std::string &source,
          const std::map<std::string, size_t> &arrays,
          DiagnosticEngine &diags, const std::string &filename)
      : toks_(lexCPlus(source)), source_(source), arrays_(arrays), diags_(diags),
        filename_(filename) {
  }

  void run() {
    for (const auto &entry : arrays_) {
      ArrayShape seed;
      seed.pos = 0;
      seed.name = entry.first;
      seed.dims.push_back(entry.second);
      localArrays_.push_back(seed);
    }
    collectLocalArrays();
    collectFunctions();
    // A volatile object can change between reads: its initializer is NOT a
    // compile-time fact.  Collect first so every constant-folding site can
    // refuse to treat it as one (runtime_fail/bounds.c relies on the guard).
    collectVolatileNames();
    // Wave 4 (red finding): bounds on index parameters come from call sites,
    // so they must be collected before any extent/index fact is used.
    collectParamConstBounds();
    for (const auto &entry : funcs_)
      functionOpens_.insert(entry.second.bodyOpen);
    // Wave 2: struct-field extent chains are resolved before the token loop
    // so every subscript can consult the propagated field facts and the
    // extents learned at recursive call sites.
    collectStructShapes();
    collectStructTypedVars();
    collectFieldInitializers();
    propagateFieldFactsToParams();
    collectParamArrayExtents();
    // Red-team closure: bounds that escape single-function/loop analysis.
    // These run before the token loop so every subscript site can consult the
    // constant, recursion and out-param facts collected here.
    collectLocalConstants();
    collectConstantVlaExtents(); // foldable VLA counts become static extents
    collectPhiConstants(); // folded `c ? a : b` index values
    collectCallGraph();
    collectRecursiveCallArgs();
    collectRecursiveGuardBounds(); // weak early-return guard => reachable max arg
    collectOutParamAllocations();
    collectCallConstants();
    collectAllocations();
    // A realloc result often reaches its final variable through `q = realloc(..);
    // p = q;` — carry (or, for an unknown new size, invalidate) the extent so
    // no stale extent is used for p's later subscripts.
    propagateReallocAliases();
    collectReturnExtents();
    propagateReturnExtents();
    // Collect lightweight facts used by the bug-pattern checks below.  Each
    // collector is deliberately conservative so it cannot reject valid C.
    collectPointerNames();
    collectByteCastAliases();
    collectRawAllocationPointers();
    collectSizeConstants();
    collectNegativeNames();
    collectArrayPointers(); // constant-offset aliases of fixed arrays
    collectPointerToPointerParams(); // `int **pp` bound to &array-alias
    collectTypePunnedPointers();
    checkMemcpyOverlap();
    // Targeted compile-time rejections for user-provided bug patterns.
    checkSizeofPointer();        // bug 35
    checkAllocationOverflow();   // bug 41
    checkNegativeToUnsigned();   // bug 42
    checkUnalignedCasts();       // bug 46
    checkDataRaces();
    checkStaticBufferCalls();
    checkGetenvInvalidation();
    checkCtypeChar();
    checkUncheckedIo();
    checkConstCastMutation();
    checkUnsequencedModifications(); // UB hidden by ++ instrumentation in lowered C
    checkMacroStatements();
    checkForkThreads();
    checkRestrictAliasing();
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (isIdent(i, "for"))
        checkLoop(i);
      else {
        checkTypePun(i);
        checkTypePunnedDeref(i); // bug 45
        checkPointerArithmetic(i);
        if (isIdent(i, "while"))
          checkWhileLoop(i); // induction bound past a known extent
        checkTrackedArrayPointer(i); // constant index / one-past / multi-dim
        // Wave 4 (red finding): subscript/formed-deref through a pointer whose
        // extent is proven but whose index is a call-site-bounded parameter.
        checkParamIndexSubscript(i);
        checkPointerToPointerSubscript(i); // (*pp)[k] through &array-alias
        checkRecursiveArraySubscript(i); // unbounded parameter in a cycle
        checkRecursivePointerParamSubscript(i); // derived index via pointer param
        checkOutParamHeapSubscript(i);   // extent learned from make(&p)
        checkStructFieldSubscript(i);    // field extent/index from call site
        checkPhiIndexSubscript(i);       // ternary/phi index into known extent
        checkTernaryIndexSubscript(i);   // arr[c ? -1 : i] with an OOB branch
        checkFoldedIndexSubscript(i);    // helper return / pointer-difference index
        checkDirectOnePastDeref(i);  // *(array + N) at the array extent
        checkUnrelatedPointerCompare(i);
        checkEmptyInfiniteLoop(i);
        checkNanCompare(i);
        checkVlaSizeof(i);
        checkConstantVlaSubscript(i);
        checkZeroAlloc(i);
        checkFflushStdin(i);
        checkVla(i); // no-op: Safe mode lowers VLAs instead of rejecting them
        // Ignore tokens that are part of a preprocessor directive such as
        // `#include <alloca.h>` — merely including the header is not a call.
        bool inDirective = false;
        for (size_t k = i; k > 0 && toks_[k].line == toks_[i].line; --k)
          if (isPunct(k - 1, "#")) { inDirective = true; break; }
        if (isIdent(i, "alloca") && !inDirective)
          errorAt(toks_[i], "alloca has no bounds checking; use cplus_alloc");
        if ((isIdent(i, "setjmp") || isIdent(i, "longjmp")) && !inDirective)
          warningAt(toks_[i], std::string("'") + toks_[i].text +
                                  "' bypasses C+ deterministic cleanup");
        if (isIdent(i, "unreachable"))
          errorAt(toks_[i], "C23 unreachable() is not supported in safe C+");
        if (isIdent(i, "_BitInt"))
          errorAt(toks_[i], "_BitInt has no safe C+ lowering; use int64_t or "
                            "cplus_checked arithmetic");
        if (isIdent(i, "nullptr"))
          errorAt(toks_[i], "C23 nullptr is not supported; use NULL");
        if (isIdent(i, "unsequenced") || isIdent(i, "reproducible"))
          errorAt(toks_[i], "optimizer attributes that can lie about side "
                            "effects are not supported in safe C+");
        if (isIdent(i, "realloc"))
          checkRawRealloc(i);
      }
    }
  }

private:
  bool trivia(size_t i) const {
    return toks_[i].kind == TokKind::Whitespace ||
           toks_[i].kind == TokKind::Comment;
  }

  size_t nextSig(size_t i) const {
    ++i;
    while (i < toks_.size() && trivia(i))
      ++i;
    return i;
  }

  bool isPunct(size_t i, const char *p) const {
    return i < toks_.size() && toks_[i].kind == TokKind::Punct &&
           toks_[i].text == p;
  }

  bool isIdent(size_t i, const char *s) const {
    return i < toks_.size() && toks_[i].kind == TokKind::Identifier &&
           toks_[i].text == s;
  }

  size_t match(size_t open, const char *l, const char *r) const {
    int depth = 0;
    for (size_t i = open; i < toks_.size(); ++i) {
      if (isPunct(i, l))
        ++depth;
      else if (isPunct(i, r)) {
        if (--depth == 0)
          return i;
      }
    }
    return kNPos;
  }

  static bool number(const Token &t, long *out) {
    if (t.kind != TokKind::Number)
      return false;
    char *end = nullptr;
    long v = strtol(t.text.c_str(), &end, 0);
    if (end == t.text.c_str())
      return false;
    *out = v;
    return true;
  }

  /// If a function returns a local whose extent is known, remember that
  /// extent for the function's call sites.
  void collectReturnExtents() {
    for (const auto &entry : funcs_) {
      const FuncSig &sig = entry.second;
      if (sig.bodyClose == kNPos)
        continue;
      size_t extent = 0;
      bool conflict = false;
      for (size_t k = sig.bodyOpen; k <= sig.bodyClose; ++k) {
        if (!isIdent(k, "return"))
          continue;
        size_t v = nextSig(k);
        if (v > sig.bodyClose || toks_[v].kind != TokKind::Identifier)
          continue;
        size_t found = 0;
        if (!findExtent(toks_[v].text, k, &found))
          continue;
        if (extent && extent != found)
          conflict = true;
        extent = found;
      }
      if (!conflict && extent)
        returnExtents_[entry.first] = extent;
    }
  }

  /// `int *p = make(5);` inherits the extent inferred for make's return.
  void propagateReturnExtents() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier)
        continue;
      auto it = returnExtents_.find(toks_[i].text);
      if (it == returnExtents_.end())
        continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "("))
        continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos || isPunct(nextSig(rp), "{"))
        continue;
      size_t start = i;
      while (start > 0) {
        --start;
        if (isPunct(start, ";") || isPunct(start, "{") ||
            isPunct(start, "}")) {
          ++start;
          break;
        }
      }
      size_t eq = kNPos;
      int paren = 0, bracket = 0;
      for (size_t k = start; k < i; ++k) {
        if (isPunct(k, "(")) ++paren;
        else if (isPunct(k, ")")) --paren;
        else if (isPunct(k, "[")) ++bracket;
        else if (isPunct(k, "]")) --bracket;
        else if (isPunct(k, "=") && paren == 0 && bracket == 0) {
          eq = k;
          break;
        }
      }
      if (eq == kNPos)
        continue;
      size_t name = kNPos;
      for (size_t k = eq; k > start; --k)
        if (toks_[k - 1].kind == TokKind::Identifier) {
          name = k - 1;
          break;
        }
      if (name != kNPos) {
        ArrayShape inherited;
        inherited.pos = i;
        inherited.name = toks_[name].text;
        inherited.dims.push_back(it->second);
        localArrays_.push_back(inherited);
      }
    }
  }

  static std::string categoryOf(const std::string &message) {
    if (message.find("realloc") != std::string::npos)
      return "CPLUS-REALLOC";
    if (message.find("allocation") != std::string::npos ||
        message.find("malloc") != std::string::npos)
      return "CPLUS-OWNER";
    if (message.find("bounds") != std::string::npos ||
        message.find("array index") != std::string::npos ||
        message.find("out-of-bounds") != std::string::npos ||
        message.find("pointer arithmetic") != std::string::npos ||
        message.find("VLA") != std::string::npos)
      return "CPLUS-BOUNDS";
    if (message.find("borrow") != std::string::npos)
      return "CPLUS-BORROW";
    if (message.find("goto") != std::string::npos ||
        message.find("longjmp") != std::string::npos)
      return "CPLUS-RAII";
    return "CPLUS-FFI";
  }

  static std::string tagged(const std::string &message) {
    if (message.rfind("[CPLUS-", 0) == 0)
      return message;
    return "[" + categoryOf(message) + "] " + message;
  }

  void errorAt(const Token &at, const std::string &message) {
    Diag d;
    d.level = DiagLevel::Error;
    d.message = tagged(message);
    d.file = filename_;
    d.line = at.line;
    d.column = at.column;
    d.help = "use C+ managed allocations and cplus_* helpers";
    diags_.report(std::move(d));
  }

  /// Report a *provable* out-of-bounds access with the same dual category tag
  /// the AST safety pass emits for safe-bounds errors.  This pass runs before
  /// lowering and is the only place that can see facts such as a call-site
  /// array extent behind a pointer parameter or an `int **` alias.  Reusing
  /// the dual tag keeps those diagnostics consistent with the AST pass and
  /// with the interprocedural suite's exact-substring expectations.  `tagged`
  /// passes a message that already starts with "[CPLUS-" through verbatim.
  void errorAtSafeBounds(const Token &at, const std::string &message) {
    errorAt(at, "[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] " + message);
  }

  void warningAt(const Token &at, const std::string &message) {
    Diag d;
    d.level = DiagLevel::Warning;
    d.message = tagged(message);
    d.file = filename_;
    d.line = at.line;
    d.column = at.column;
    d.help = "RAII is not guaranteed across non-local jumps; use structured "
             "control flow in safe C+";
    diags_.report(std::move(d));
  }

  void report(const Token &at, long index, const std::string &name,
              size_t extent) {
    Diag d;
    d.level = DiagLevel::Error;
    d.message = tagged("out-of-bounds access: index " + std::to_string(index) +
                " reaches past array '" + name + "' of length " +
                std::to_string(extent));
    d.file = filename_;
    d.line = at.line;
    d.column = at.column;
    d.help = "use a bound of " + name + ".len or a smaller index";
    diags_.report(std::move(d));
  }

  void reportConstantIndex(const Token &at, long index,
                           const std::string &name, size_t extent) {
    Diag d;
    d.level = DiagLevel::Error;
    d.message = tagged("constant array index " + std::to_string(index) +
                       " is outside the bounds of '" + name + "'");
    d.file = filename_;
    d.line = at.line;
    d.column = at.column;
    d.help = "array contains " + std::to_string(extent) +
             " elements; valid indices are 0.." +
             std::to_string(extent - 1);
    diags_.report(std::move(d));
  }

  /// Report a negative constant index that reaches a fixed array through a
  /// tracked pointer alias.
  void reportNegativeAliasIndex(const Token &at, long index,
                                const std::string &array,
                                const std::string &ptr) {
    Diag d;
    d.level = DiagLevel::Error;
    d.message = tagged("negative array index " + std::to_string(index) +
                       " on '" + array + "' via pointer '" + ptr + "'");
    d.file = filename_;
    d.line = at.line;
    d.column = at.column;
    d.help = "array '" + array + "' has valid indices starting at 0";
    diags_.report(std::move(d));
  }

  /// Report a constant index past the extent of an array reached through a
  /// tracked pointer alias (the one-past-pointer rule).
  void reportAliasOutOfBounds(const Token &at, long index,
                              const std::string &array, size_t extent,
                              const std::string &ptr) {
    Diag d;
    d.level = DiagLevel::Error;
    d.message = tagged("out-of-bounds access: index " + std::to_string(index) +
                       " reaches past array '" + array + "' via pointer '" +
                       ptr + "'");
    d.file = filename_;
    d.line = at.line;
    d.column = at.column;
    d.help = "array contains " + std::to_string(extent) +
             " elements; valid indices are 0.." +
             std::to_string(extent - 1);
    diags_.report(std::move(d));
  }

  void collectLocalArrays() {
    static const char *const kTypes[] = {
        "int", "char", "short", "long", "float", "double", "void",
        "unsigned", "signed", "struct", "union", "enum", "size_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t"};
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier || !isPunct(nextSig(i), "["))
        continue;
      size_t prev = i == 0 ? kNPos : i - 1;
      while (prev != kNPos && trivia(prev)) {
        if (prev == 0) { prev = kNPos; break; }
        --prev;
      }
      if (prev == kNPos || toks_[prev].kind != TokKind::Identifier)
        continue;
      bool type = false;
      for (const char *t : kTypes)
        if (toks_[prev].text == t) { type = true; break; }
      if (!type && !isPunct(prev, ","))
        continue;
      std::vector<size_t> dims;
      size_t cursor = nextSig(i);
      while (isPunct(cursor, "[")) {
        size_t inner = nextSig(cursor);
        long value = 0;
        if (inner >= toks_.size() || !number(toks_[inner], &value) ||
            value <= 0)
          break;
        dims.push_back(static_cast<size_t>(value));
        size_t close = match(cursor, "[", "]");
        if (close == kNPos)
          break;
        cursor = nextSig(close);
      }
      // A comma means a second declarator (`int a[4], b[8]`); walk back to
      // the shared type word so both shapes get the same element width.
      size_t typeTok = prev;
      if (isPunct(prev, ",")) {
        for (size_t k = prev; k > 0; --k) {
          size_t j = k - 1;
          if (isPunct(j, ";") || isPunct(j, "{") || isPunct(j, "}"))
            break;
          if (toks_[j].kind != TokKind::Identifier)
            continue;
          bool isType = false;
          for (const char *t : kTypes)
            if (toks_[j].text == t) { isType = true; break; }
          if (isType) { typeTok = j; break; }
        }
      }
      if (!dims.empty())
        localArrays_.push_back({i, toks_[i].text, dims,
                                enclosingScopeEnd(i),
                                typeSize(toks_[typeTok].text)});
    }
  }

  /// Closing brace of the innermost block enclosing `pos`, found by matching
  /// braces backwards. This is a scope fence for name-keyed facts: without it
  /// an inner `int a[4]` silently replaces an outer `int a[8]` after the
  /// inner block closes.
  size_t enclosingScopeEnd(size_t pos) const {
    unsigned depth = 0;
    for (size_t k = pos; k > 0; --k) {
      size_t j = k - 1;
      if (isPunct(j, "}"))
        ++depth;
      else if (isPunct(j, "{")) {
        if (depth == 0)
          return match(j, "{", "}");
        --depth;
      }
    }
    return kNPos;
  }

  bool findShape(const std::string &name, size_t pos,
                 const std::vector<size_t> **shape) const {
    // Name-keyed extent facts are collected over the whole token stream, but
    // a function parameter with the same name shadows an outer/file-scope
    // object for the entire function body.  Otherwise
    // `static int a[4]; void f(int *a){ a[9]; }` is checked against the
    // global array.  Only shapes declared at or after the parameter token
    // can describe the parameter itself (`void f(int a[8])`).
    size_t shadowDecl = kNPos;
    for (const auto &entry : funcs_) {
      const FuncSig &sig = entry.second;
      if (pos < sig.bodyOpen || pos > sig.bodyClose)
        continue;
      auto pit = sig.paramDeclPos.find(name);
      if (pit != sig.paramDeclPos.end() && pit->second <= pos &&
          (shadowDecl == kNPos || pit->second > shadowDecl))
        shadowDecl = pit->second;
    }
    for (size_t k = localArrays_.size(); k > 0; --k) {
      const ArrayShape &e = localArrays_[k - 1];
      // A declaration from a block that has already closed is dead: do not
      // let its smaller extent reject an access to the outer object.
      if (e.scopeEnd != kNPos && pos > e.scopeEnd)
        continue;
      if (shadowDecl != kNPos && e.pos < shadowDecl)
        continue; // shadowed by a parameter of the same name
      if (e.name == name && e.pos <= pos) {
        // A realloc with an unprovable new size replaces this extent; a
        // stale extent must never drive an OOB diagnostic.
        auto inv = reallocInvalidAt_.find(name);
        if (inv != reallocInvalidAt_.end())
          for (size_t at : inv->second)
            if (at > e.pos && at <= pos)
              return false;
        *shape = &e.dims;
        return true;
      }
    }
    return false;
  }

  bool findExtent(const std::string &name, size_t pos, size_t *extent) const {
    const std::vector<size_t> *dims = nullptr;
    if (!findShape(name, pos, &dims))
      return false;

    *extent = dims->front();
    return true;
  }

  /// Byte width of one element of the array behind a byte-cast alias.  The
  /// width comes from the declaration's type word; 0 means unknown, in which
  /// case a byte-unit check must stay silent instead of guessing a bound.
  size_t arrayElemBytes(const std::string &name, size_t pos) const {
    for (size_t k = localArrays_.size(); k > 0; --k) {
      const ArrayShape &e = localArrays_[k - 1];
      if (e.name != name || e.pos > pos || e.elemBytes == 0)
        continue;
      if (e.scopeEnd != kNPos && pos > e.scopeEnd)
        continue;
      return e.elemBytes;
    }
    return 0;
  }

  struct Interval {
    bool known = false;
    long lo = 0;
    long hi = 0;
  };

  static Interval constant(long v) { return {true, v, v}; }

  size_t findTopOp(size_t from, size_t to, const std::string &ops) const {
    int paren = 0, bracket = 0;
    for (size_t k = from; k < to; ++k) {
      if (isPunct(k, "(")) ++paren;
      else if (isPunct(k, ")")) --paren;
      else if (isPunct(k, "[")) ++bracket;
      else if (isPunct(k, "]")) --bracket;
      else if (paren == 0 && bracket == 0 &&
               toks_[k].kind == TokKind::Punct &&
               ops.find(toks_[k].text[0]) != std::string::npos)
        return k;
    }
    return kNPos;
  }

  /// Conservative interval evaluation for simple index expressions.  Anything
  /// not understood returns unknown, which suppresses the diagnostic rather
  /// than risking a false positive.
  Interval evalIndex(size_t from, size_t to, const std::string &var,
                     long vlo, long vhi) const {
    while (from < to && trivia(from))
      ++from;
    while (to > from && trivia(to - 1))
      --to;
    if (from >= to)
      return {};
    if (from + 1 == to) {
      long value = 0;
      if (number(toks_[from], &value))
        return constant(value);
      if (toks_[from].kind == TokKind::Identifier && toks_[from].text == var)
        return {true, vlo, vhi};
      return {};
    }
    if (isPunct(from, "(")) {
      size_t close = match(from, "(", ")");
      if (close == to - 1)
        return evalIndex(from + 1, close, var, vlo, vhi);
    }
    size_t op = findTopOp(from, to, "+-");
    if (op != kNPos) {
      Interval a = evalIndex(from, op, var, vlo, vhi);
      Interval b = evalIndex(op + 1, to, var, vlo, vhi);
      if (!a.known || !b.known) return {};
      if (isPunct(op, "+")) return {true, a.lo + b.lo, a.hi + b.hi};
      return {true, a.lo - b.hi, a.hi - b.lo};
    }
    op = findTopOp(from, to, "*/%");
    if (op != kNPos) {
      Interval a = evalIndex(from, op, var, vlo, vhi);
      Interval b = evalIndex(op + 1, to, var, vlo, vhi);
      if (!a.known || !b.known || b.lo != b.hi) return {};
      long d = b.lo;
      if (d == 0) return {};
      if (isPunct(op, "*")) {
        long c[] = {a.lo * d, a.hi * d};
        return {true, std::min(c[0], c[1]), std::max(c[0], c[1])};
      }
      if (isPunct(op, "/")) {
        if (d > 0) return {true, a.lo / d, a.hi / d};
        return {true, a.hi / d, a.lo / d};
      }
      if (d > 0 && a.lo >= 0) return {true, 0, d - 1};
      return {};
    }
    return {};
  }

  /// Check a subscript chain such as g[i/4][i%4] against the array's shape.
  void checkSubscriptChain(size_t nameIdx, long vlo, long vhi,
                           const std::string &var) {
    const std::vector<size_t> *dims = nullptr;
    if (!findShape(toks_[nameIdx].text, nameIdx, &dims))
      return;
    // Skip array declarations inside the loop body (e.g. `int r[4];`).
    size_t prev = nameIdx == 0 ? kNPos : nameIdx - 1;
    while (prev != kNPos && trivia(prev)) {
      if (prev == 0) { prev = kNPos; break; }
      --prev;
    }
    size_t firstOpen = nextSig(nameIdx);
    size_t firstClose = isPunct(firstOpen, "[") ? match(firstOpen, "[", "]")
                                                 : kNPos;
    if (firstClose != kNPos) {
      size_t after = nextSig(firstClose);
      bool declTail = isPunct(after, ";") || isPunct(after, ",") ||
                      isPunct(after, "=");
      bool declHead = isPunct(prev, ",") ||
                      (prev != kNPos && toks_[prev].kind == TokKind::Identifier);
      if (declTail && declHead)
        return;
    }
    size_t cursor = firstOpen;
    for (size_t d = 0; d < dims->size(); ++d) {
      if (!isPunct(cursor, "["))
        return;
      size_t close = match(cursor, "[", "]");
      if (close == kNPos)
        return;
      Interval v = evalIndex(cursor + 1, close, var, vlo, vhi);
      if (v.known && (v.lo < 0 || v.hi >= static_cast<long>((*dims)[d])))
        reportConstantIndex(toks_[cursor], v.hi, toks_[nameIdx].text,
                            (*dims)[d]);
      cursor = nextSig(close);
    }
  }

  //===--------------------------------------------------------------------===//
  // Red-team closure: position-keyed constant facts and alias resolution.
  //
  // Rationale: the checker used to accept only literal subscripts, so any
  // laundering step (cast, field store, typedef, ternary, ++ chain, recursion)
  // hid a provably out-of-bounds index.  Every fact below is keyed by source
  // position and invalidated on the next write, so a stale fact can never
  // reject valid C.  Nothing here changes DEFAULT behaviour for unbounded
  // indices: we only report a value we can actually prove.
  //===--------------------------------------------------------------------===//

  static constexpr long kInvalidConst = LONG_MIN;

  size_t stmtEnd(size_t from) const {
    int paren = 0, bracket = 0, brace = 0;
    for (size_t k = from; k < toks_.size(); ++k) {
      if (isPunct(k, "(")) ++paren;
      else if (isPunct(k, ")")) { if (paren > 0) --paren; }
      else if (isPunct(k, "[")) ++bracket;
      else if (isPunct(k, "]")) { if (bracket > 0) --bracket; }
      else if (isPunct(k, "{")) ++brace;
      else if (isPunct(k, "}")) {
        if (brace == 0) return k;
        --brace;
      } else if ((isPunct(k, ";") || isPunct(k, ",")) && paren == 0 &&
                 bracket == 0 && brace == 0) {
        return k;
      }
    }
    return toks_.size();
  }

  void trim(size_t *from, size_t *to) const {
    while (*from < *to && trivia(*from)) ++*from;
    while (*to > *from && trivia(*to - 1)) --*to;
  }

  bool localConstAt(const std::string &name, size_t pos, long *out) const {
    if (isVolatileName(name))
      return false; // a volatile value is a runtime value, not a constant
    auto it = localConsts_.find(name);
    if (it == localConsts_.end())
      return false;
    long value = kInvalidConst;
    for (const auto &ev : it->second) {
      if (ev.first > pos)
        break;
      value = ev.second;
    }
    if (value == kInvalidConst)
      return false;
    *out = value;
    return true;
  }

  /// Evaluate a compile-time integer expression used as an index.  Handles
  /// literals, signs, local constant facts, `cond ? a : b` and literal-return
  /// helpers.  Unknown expressions simply return false (no diagnostic).
  bool indexExprValue(size_t from, size_t to, long *out) const {
    trim(&from, &to);
    if (from >= to)
      return false;
    // The comma operator's value is its last operand.  Only top-level commas
    // are operators here: commas inside `(...)` separate call arguments and
    // are consumed by splitArgs before this helper sees them.  This folds
    // `(0, 9, 1)` to 1 instead of guessing from an earlier operand.
    {
      size_t lastComma = kNPos;
      int paren = 0, bracket = 0;
      for (size_t k = from; k < to; ++k) {
        if (isPunct(k, "(")) ++paren;
        else if (isPunct(k, ")")) --paren;
        else if (isPunct(k, "[")) ++bracket;
        else if (isPunct(k, "]")) --bracket;
        else if (isPunct(k, ",") && paren == 0 && bracket == 0)
          lastComma = k;
      }
      if (lastComma != kNPos)
        return indexExprValue(lastComma + 1, to, out);
    }
    // Wave 2: fold a top-level `a + b` / `a - b` before the unary case.  This
    // is what makes chained pointer formation (`q = a + 3 + 4`) and derived
    // recursive indices (`p[i + 9]`) provable.  Doing it first keeps
    // `-1 + 9` correct (the unary rule alone would read it as `-(1 + 9)`).
    // Only fully constant operands are consumed; anything else falls through
    // to the existing conservative handlers, so no valid C is rejected.
    {
      size_t op = kNPos;
      int paren = 0, bracket = 0;
      for (size_t k = from; k < to; ++k) {
        if (isPunct(k, "(")) ++paren;
        else if (isPunct(k, ")")) --paren;
        else if (isPunct(k, "[")) ++bracket;
        else if (isPunct(k, "]")) --bracket;
        else if (k > from && paren == 0 && bracket == 0 &&
                 (isPunct(k, "+") || isPunct(k, "-"))) { op = k; break; }
      }
      if (op != kNPos) {
        long a = 0, b = 0;
        if (indexExprValue(from, op, &a) && indexExprValue(op + 1, to, &b)) {
          *out = isPunct(op, "+") ? a + b : a - b;
          return true;
        }
      }
    }
    if (isPunct(from, "-") || isPunct(from, "+")) {
      long v = 0;
      if (!indexExprValue(from + 1, to, &v))
        return false;
      *out = isPunct(from, "-") ? -v : v;
      return true;
    }
    size_t q = kNPos;
    int paren = 0, bracket = 0;
    for (size_t k = from; k < to; ++k) {
      if (isPunct(k, "(")) ++paren;
      else if (isPunct(k, ")")) --paren;
      else if (isPunct(k, "[")) ++bracket;
      else if (isPunct(k, "]")) --bracket;
      else if (isPunct(k, "?") && paren == 0 && bracket == 0) { q = k; break; }
    }
    if (q != kNPos) {
      size_t colon = kNPos;
      paren = 0; bracket = 0;
      for (size_t k = q + 1; k < to; ++k) {
        if (isPunct(k, "(")) ++paren;
        else if (isPunct(k, ")")) --paren;
        else if (isPunct(k, "[")) ++bracket;
        else if (isPunct(k, "]")) --bracket;
        else if (isPunct(k, ":") && paren == 0 && bracket == 0) { colon = k; break; }
      }
      if (colon == kNPos)
        return false;
      long cond = 0;
      if (indexExprValue(from, q, &cond))
        return indexExprValue(cond ? q + 1 : colon + 1,
                              cond ? colon : to, out);
      long a = 0, b = 0;
      bool okA = indexExprValue(q + 1, colon, &a);
      bool okB = indexExprValue(colon + 1, to, &b);
      if (!okA || !okB)
        return false;
      // Both branches are literals but the condition is not: take the larger
      // branch so an unavoidable out-of-bounds path is still diagnosed.
      *out = std::max(a, b);
      return true;
    }
    if (isPunct(from, "(")) {
      size_t close = matchIn(from, to, "(", ")");
      if (close != kNPos) {
        size_t last = close;
        while (last > from + 1 && trivia(last - 1)) --last;
        if (last == to)
          return indexExprValue(from + 1, close, out);
      }
    }
    if (from + 1 == to) {
      long v = 0;
      if (number(toks_[from], &v)) { *out = v; return true; }
      if (toks_[from].kind == TokKind::Identifier) {
        if (localConstAt(toks_[from].text, from, &v)) { *out = v; return true; }
        auto r = returnConstants_.find(toks_[from].text);
        if (r != returnConstants_.end()) { *out = r->second; return true; }
      }
    }
    if (toks_[from].kind == TokKind::Identifier) {
      size_t lp = nextSig(from);
      if (lp < to && isPunct(lp, "(")) {
        size_t rp = matchIn(lp, to, "(", ")");
        if (rp != kNPos) {
          bool tail = true;
          for (size_t k = rp + 1; k < to; ++k)
            if (!trivia(k)) { tail = false; break; }
          auto r = returnConstants_.find(toks_[from].text);
          if (tail && r != returnConstants_.end()) { *out = r->second; return true; }
        }
      }
    }
    return false;
  }

  /// Wave 4 (red finding): a VLA extent that is a parameter with a single,
  /// unique call-site value folds to a static extent too (`int a[n]` in
  /// `f(int n, int i)`, called only as `f(4,9)`).  A symbol with several
  /// distinct call-site values is deliberately NOT folded: guessing one
  /// extent would risk rejecting valid calls, so the runtime guard owns it.
  bool foldExtentExpr(size_t from, size_t to, long *value) const {
    if (indexExprValue(from, to, value))
      return true;
    size_t f = from, t = to;
    trim(&f, &t);
    if (f + 1 != t || toks_[f].kind != TokKind::Identifier)
      return false;
    if (isVolatileName(toks_[f].text))
      return false;
    const std::string *fn = enclosingFunction(f);
    if (!fn)
      return false;
    auto fit = paramConstUnique_.find(*fn);
    if (fit == paramConstUnique_.end())
      return false;
    const auto &params = funcs_.at(*fn).params;
    for (size_t p = 0; p < params.size(); ++p)
      if (params[p] == toks_[f].text) {
        auto it = fit->second.find(p);
        if (it == fit->second.end())
          return false;
        *value = it->second;
        return true;
      }
    return false;
  }

  /// A VLA extent that folds to a compile-time constant is a static extent.
  /// Register it as an array shape so a provably out-of-bounds constant
  /// subscript (`int n = 4; int a[n]; a[9];`) is rejected by the same constant
  /// index check used for fixed arrays, independently of the runtime guard the
  /// rewriter emits.  This runs after collectLocalConstants() so `n` is known.
  /// Non-constant extents are skipped: the runtime guard handles those.
  void collectConstantVlaExtents() {
    static const char *const kTypes[] = {
        "int", "char", "short", "long", "float", "double", "void",
        "unsigned", "signed", "struct", "union", "enum", "size_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t"};
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier)
        continue;
      size_t open = nextSig(i);
      if (!isPunct(open, "["))
        continue;
      size_t prev = prevSig(i);
      if (prev == kNPos)
        continue;
      bool declaration = false;
      for (const char *t : kTypes)
        if (toks_[prev].text == t) { declaration = true; break; }
      if (!declaration && isPunct(prev, ",")) {
        // A comma can separate declarators (`int a[4], b[N]`) but is also
        // the separator in a call argument list (`printf("...", b[3])`).
        // Walk back to a type keyword only through declarator tokens; if a
        // '(' or ';' is reached first this is an expression, not a VLA.
        // Wave 4 (false-positive hardening): a ')' must also stop the walk.
        // Without it, `f(sizeof(int), b[3])` walks back from the comma past
        // the closing ')' of sizeof(int) and mistakes `int` for a declared
        // type, registering `b` with the bogus extent 3.  A later valid
        // `x = b[5]` is then rejected even though b really has 8 elements
        // (the same class of call-argument bug as the original b[3] case).
        size_t k = prev;
        while (k != kNPos) {
          if (isPunct(k, "(") || isPunct(k, ")") || isPunct(k, ";") ||
              isPunct(k, "{") || isPunct(k, "}"))
            break;
          bool typeTok = false;
          for (const char *t : kTypes)
            if (toks_[k].text == t) { typeTok = true; break; }
          if (typeTok) { declaration = true; break; }
          k = prevSig(k);
        }
      }
      if (!declaration)
        continue;
      // Wave 4 (VLA hardening): register every dimension that folds to a
      // compile-time constant, not just a single-dimensional VLA.  A
      // multi-dimensional VLA whose extents fold (`int n=4; int a[n][n];`)
      // has just as provable an out-of-bounds constant subscript as a fixed
      // array; skipping it left `a[0][9]` silently accepted.  We only fold
      // extents that are entirely constant, and we require every bracket to
      // fold, so an unknown dimension never produces a guessed extent.
      std::vector<size_t> dims;
      size_t cursor = open;
      while (isPunct(cursor, "[")) {
        size_t close = match(cursor, "[", "]");
        if (close == kNPos || close == nextSig(cursor))
          break;
        long value = 0;
        if (!foldExtentExpr(cursor + 1, close, &value) || value <= 0)
          break;
        dims.push_back(static_cast<size_t>(value));
        cursor = nextSig(close);
      }
      if (dims.empty() || isPunct(cursor, "[")) // an extent did not fold
        continue;
      localArrays_.push_back({i, toks_[i].text, dims, enclosingScopeEnd(i)});
      constantVlaNames_.insert(toks_[i].text);
      constantVlaDeclPos_.insert(i); // precise declaration-site suppression
    }
  }

  /// Constant subscript into a VLA whose extent folded to a compile-time
  /// value.  The runtime guard emitted by the rewriter still handles the
  /// general case; this gives `check --strict` a diagnostic for the provable
  /// case without waiting for execution.
  void checkConstantVlaSubscript(size_t i) {
    if (toks_[i].kind != TokKind::Identifier ||
        !constantVlaNames_.count(toks_[i].text))
      return;
    size_t lb = nextSig(i);
    if (!isPunct(lb, "["))
      return;
    // Wave 4: suppress only the exact declaration token (recorded when the
    // extent was folded), not any name preceded by an identifier/comma.  The
    // old heuristic also skipped real accesses such as `return a[9]`, and it
    // read the declaration's count bracket as if it were an index.  `sizeof`
    // operands are unevaluated, so a constant there is not an access.
    if (constantVlaDeclPos_.count(i) || inUnevaluatedContext(i))
      return;
    const std::vector<size_t> *dims = nullptr;
    if (!findShape(toks_[i].text, i, &dims) || dims->empty())
      return;
    // Walk the whole subscript chain so a constant past the extent in a
    // non-leading dimension (`int a[n][n]; a[0][9];`) is diagnosed too.
    size_t cursor = lb;
    for (size_t d = 0; d < dims->size(); ++d) {
      if (!isPunct(cursor, "["))
        break;
      size_t rb = match(cursor, "[", "]");
      if (rb == kNPos)
        break;
      long index = 0;
      if (indexExprValue(cursor + 1, rb, &index)) {
        long extent = static_cast<long>((*dims)[d]);
        if (index < 0 &&
            hasNonNegativeGuard(toks_[nextSig(cursor)].text, i))
          continue;
        if (index < 0)
          errorAt(toks_[cursor], "negative array index " +
                                     std::to_string(index) + " on '" +
                                     toks_[i].text + "'");
        else if (index >= extent)
          reportConstantIndex(toks_[cursor], index, toks_[i].text,
                              static_cast<size_t>(extent));
      }
      cursor = nextSig(rb);
    }
  }

  /// Record every write to a name with the value (or "invalid").  A fact is
  /// only visible at source positions after its write and before the next.
  void collectLocalConstants() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier)
        continue;
      const std::string name = toks_[i].text;
      if (isVolatileName(name))
        continue; // never record a volatile initializer as a constant fact
      size_t n = nextSig(i);
      if (isPunct(n, "=")) {
        long v = 0;
        size_t end = stmtEnd(nextSig(n));
        auto &ev = localConsts_[name];
        if (indexExprValue(nextSig(n), end, &v))
          ev.push_back({i, v});
        else
          ev.push_back({i, kInvalidConst});
        continue;
      }
      if (isPunct(n, "++") || isPunct(n, "--") || isPunct(n, "+=") ||
          isPunct(n, "-=") || isPunct(n, "*=") || isPunct(n, "/=") ||
          isPunct(n, "%=") || isPunct(prevSig(i), "&")) {
        localConsts_[name].push_back({i, kInvalidConst});
        continue;
      }
      if ((isPunct(i, "++") || isPunct(i, "--")) &&
          n < toks_.size() && toks_[n].kind == TokKind::Identifier)
        localConsts_[toks_[n].text].push_back({i, kInvalidConst});
    }
    // A function with one `return <literal>;` is a constant expression at
    // its call sites (`a[nine()]` is otherwise invisible to token analysis).
    for (const auto &entry : funcs_) {
      const FuncSig &sig = entry.second;
      long v = 0;
      int count = 0;
      bool single = true;
      for (size_t k = sig.bodyOpen; k <= sig.bodyClose; ++k) {
        if (!isIdent(k, "return"))
          continue;
        ++count;
        if (!indexExprValue(nextSig(k), stmtEnd(nextSig(k)), &v))
          single = false;
      }
      if (count == 1 && single)
        returnConstants_[entry.first] = v;
    }
  }

  /// Integer locals whose value is chosen by a conditional expression.  The
  /// ternary folds to the maximum of its constant branches (a phi value),
  /// which the host AST checker cannot reproduce.  A subscript using such a
  /// value is provably out of bounds when that maximum exceeds the base's
  /// extent.  Facts are position-keyed and invalidated exactly like the
  /// ordinary local constants, so a later write cannot leave a stale index.
  std::map<std::string, std::vector<std::pair<size_t, long>>> phiConsts_;

  bool phiConstAt(const std::string &name, size_t pos, long *out) const {
    if (isVolatileName(name))
      return false;
    auto it = phiConsts_.find(name);
    if (it == phiConsts_.end()) return false;
    long value = kInvalidConst;
    for (const auto &ev : it->second) {
      if (ev.first > pos) break;
      value = ev.second;
    }
    if (value == kInvalidConst) return false;
    *out = value;
    return true;
  }

  void collectPhiConstants() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier) continue;
      size_t eq = nextSig(i);
      if (!isPunct(eq, "=")) continue;
      size_t end = stmtEnd(nextSig(eq));
      size_t q = kNPos;
      int paren = 0, bracket = 0;
      for (size_t k = nextSig(eq); k < end; ++k) {
        if (isPunct(k, "(")) ++paren;
        else if (isPunct(k, ")")) --paren;
        else if (isPunct(k, "[")) ++bracket;
        else if (isPunct(k, "]")) --bracket;
        else if (isPunct(k, "?") && paren == 0 && bracket == 0) { q = k; break; }
      }
      if (q == kNPos) continue;
      long v = 0;
      phiConsts_[toks_[i].text].push_back(
          {i, indexExprValue(nextSig(eq), end, &v) ? v : kInvalidConst});
    }
  }

  /// `arr[cond ? -1 : i]`: an inline conditional index where any constant
  /// branch is outside the object's extent.  Even when the other branch is
  /// guarded by a loop condition, the OOB branch is reachable at compile time
  /// and must be rejected rather than trapped at runtime by the bounds guard.
  void checkTernaryIndexSubscript(size_t i) {
    if (toks_[i].kind != TokKind::Identifier) return;
    size_t lb = nextSig(i);
    if (!isPunct(lb, "[")) return;
    size_t rb = match(lb, "[", "]");
    if (rb == kNPos) return;
    size_t from = nextSig(lb), to = rb;
    size_t q = kNPos, colon = kNPos;
    int paren = 0, bracket = 0;
    for (size_t k = from; k < to; ++k) {
      if (isPunct(k, "(")) ++paren;
      else if (isPunct(k, ")")) --paren;
      else if (isPunct(k, "[")) ++bracket;
      else if (isPunct(k, "]")) --bracket;
      else if (paren == 0 && bracket == 0) {
        if (q == kNPos && isPunct(k, "?")) q = k;
        else if (q != kNPos && colon == kNPos && isPunct(k, ":")) colon = k;
      }
    }
    if (q == kNPos || colon == kNPos) return;
    const std::vector<size_t> *dims = nullptr;
    if (!findShape(toks_[i].text, i, &dims) || dims->empty()) return;
    long extent = (long)dims->front();
    // A compile-time constant condition selects exactly one arm; the other
    // arm is never evaluated and must not reject valid C such as
    // `a[0 ? 9 : 1]`.  indexExprValue already folds the selected arm.  Only
    // literal conditions qualify: a folded local is not a constant expression
    // and the both-arm rule below must still catch e.g. `f ? -1 : 0`.
    long cond = 0;
    bool literalCond = indexExprValue(from, q, &cond);
    for (size_t k = from; literalCond && k < q; ++k)
      if (toks_[k].kind == TokKind::Identifier)
        literalCond = false;
    if (literalCond) {
      long value = 0;
      if (indexExprValue(from, to, &value) &&
          (value < 0 || value >= extent))
        reportConstantIndex(toks_[q], value, toks_[i].text,
                            static_cast<size_t>(extent));
      return;
    }
    std::vector<std::pair<size_t, size_t>> branches = {
        {q + 1, colon}, {colon + 1, to}};
    for (const auto &br : branches) {
      long value = 0;
      bool found = indexExprValue(br.first, br.second, &value);
      if (!found) {
        // `-1 - i`: a negative constant followed only by subtraction of a
        // nonnegative loop induction is provably negative as well.
        size_t k = br.first;
        if (isPunct(k, "-")) {
          long neg = 0;
          ++k;
          if (k < br.second && number(toks_[k], &neg)) {
            ++k;
            bool onlyMinusVar = neg > 0;
            while (k < br.second) {
              if (!isPunct(k, "-")) { onlyMinusVar = false; break; }
              ++k;
              if (k >= br.second || toks_[k].kind != TokKind::Identifier) {
                onlyMinusVar = false; break;
              }
              ++k;
            }
            if (onlyMinusVar) {
              value = -neg;
              found = true;
            }
          }
        }
      }
      if (!found) continue;
      if (value < 0 || value >= extent) {
        reportConstantIndex(toks_[q], value, toks_[i].text,
                            static_cast<size_t>(extent));
        return;
      }
    }
  }

  /// `int i = c ? 1 : 6; p[i] = 1;` — index chosen by a phi.  Only a single
  /// identifier index with a folded phi value and a known base extent is
  /// reported; a tight guard on that index suppresses the report.
  void checkPhiIndexSubscript(size_t i) {
    if (toks_[i].kind != TokKind::Identifier) return;
    size_t lb = nextSig(i);
    if (!isPunct(lb, "[")) return;
    size_t rb = match(lb, "[", "]");
    if (rb == kNPos) return;
    size_t idx = nextSig(lb);
    if (idx + 1 != rb || toks_[idx].kind != TokKind::Identifier) return;
    long value = 0;
    if (!phiConstAt(toks_[idx].text, idx, &value)) return;
    const std::string *fn = enclosingFunction(i);
    if (!fn) return;
    long extent = 0;
    const std::vector<size_t> *dims = nullptr;
    if (findShape(toks_[i].text, i, &dims) && !dims->empty()) {
      extent = dims->front();
    } else {
      auto eit = paramExtents_.find(*fn);
      if (eit == paramExtents_.end()) return;
      const auto &params = funcs_.at(*fn).params;
      size_t pos = kNPos;
      for (size_t p = 0; p < params.size(); ++p)
        if (params[p] == toks_[i].text) { pos = p; break; }
      if (pos == kNPos) return;
      auto pit = eit->second.find(pos);
      if (pit == eit->second.end() || pit->second == 0) return;
      extent = pit->second;
    }
    if (hasUpperGuard(funcs_.at(*fn), toks_[idx].text, i, extent)) return;
    if (value < 0 || value >= extent)
      errorAtSafeBounds(toks_[lb], "array index " + std::to_string(value) +
          " chosen by conditional expression is out of bounds of '" +
          toks_[i].text + "' of length " + std::to_string(extent));
  }

  /// Strip `(Type *)` casts and fully enclosing parentheses from an
  /// initialiser so `void *v = a; int *p = (int *)v;` stays one alias chain.
  size_t stripCast(size_t from, size_t end) const {
    size_t r = from;
    for (int guard = 0; guard < 8; ++guard) {
      while (r < end && trivia(r)) ++r;
      if (!isPunct(r, "("))
        break;
      size_t close = matchIn(r, end, "(", ")");
      if (close == kNPos)
        break;
      size_t last = close;
      while (last > r + 1 && trivia(last - 1)) --last;
      bool typed = false, valid = true;
      for (size_t k = r + 1; k < last; ++k) {
        if (trivia(k))
          continue;
        if (toks_[k].kind == TokKind::Identifier) {
          if (isTypeWord(toks_[k].text) || toks_[k].text == "void")
            typed = true;
        } else if (isPunct(k, "*")) {
          typed = true;
        } else {
          valid = false;
          break;
        }
      }
      if (valid && typed) {
        r = nextSig(close);
        continue;
      }
      size_t after = nextSig(close);
      bool onlyTrivia = true;
      for (size_t k = after; k < end; ++k)
        if (!trivia(k)) { onlyTrivia = false; break; }
      if (onlyTrivia) { r = nextSig(r); continue; }
      break;
    }
    return r;
  }

  /// Innermost function whose body contains `pos`.
  const std::string *enclosingFunction(size_t pos) const {
    const std::string *best = nullptr;
    size_t bestOpen = 0;
    for (const auto &entry : funcs_) {
      const FuncSig &sig = entry.second;
      if (pos >= sig.bodyOpen && pos <= sig.bodyClose &&
          (!best || sig.bodyOpen > bestOpen)) {
        best = &entry.first;
        bestOpen = sig.bodyOpen;
      }
    }
    return best;
  }

  // --- recursion / mutual recursion ----------------------------------------

  std::map<std::string, std::set<std::string>> callGraph_;

  void collectCallGraph() {
    for (const auto &entry : funcs_) {
      const FuncSig &sig = entry.second;
      auto &out = callGraph_[entry.first];
      for (size_t k = sig.bodyOpen; k <= sig.bodyClose; ++k) {
        if (toks_[k].kind != TokKind::Identifier ||
            !funcs_.count(toks_[k].text))
          continue;
        size_t lp = nextSig(k);
        if (!isPunct(lp, "(")) continue;
        if (isPunct(prevSig(k), ".") || isPunct(prevSig(k), "->")) continue;
        out.insert(toks_[k].text);
      }
    }
  }

  bool reaches(const std::string &from, const std::string &to) const {
    std::set<std::string> seen;
    std::vector<std::string> work;
    auto it = callGraph_.find(from);
    if (it != callGraph_.end())
      for (const auto &n : it->second) work.push_back(n);
    while (!work.empty()) {
      std::string cur = work.back();
      work.pop_back();
      if (cur == to) return true;
      if (!seen.insert(cur).second) continue;
      auto jt = callGraph_.find(cur);
      if (jt == callGraph_.end()) continue;
      for (const auto &n : jt->second) work.push_back(n);
    }
    return false;
  }

  bool isRecursive(const std::string &fn) const { return reaches(fn, fn); }

  bool sameCycle(const std::string &a, const std::string &b) const {
    return a == b || (reaches(a, b) && reaches(b, a));
  }

  /// Largest constant argument supplied at a call to a recursive function.
  /// Recursive self-calls pass expressions (`i - 1`) and are skipped; the
  /// initial call from outside the cycle supplies the provable upper bound.
  std::map<std::string, std::vector<long>> recursiveMaxArg_;

  void collectRecursiveCallArgs() {
    for (const auto &entry : funcs_) {
      if (!isRecursive(entry.first))
        continue;
      size_t np = entry.second.params.size();
      std::vector<long> slots(np, kInvalidConst);
      for (size_t i = 0; i < toks_.size(); ++i) {
        if (toks_[i].kind != TokKind::Identifier ||
            toks_[i].text != entry.first)
          continue;
        size_t lp = nextSig(i);
        if (!isPunct(lp, "(")) continue;
        size_t rp = match(lp, "(", ")");
        if (rp == kNPos || isPunct(nextSig(rp), "{")) continue;
        auto args = splitArgs(lp + 1, rp);
        if (args.size() != np) continue;
        for (size_t a = 0; a < np; ++a) {
          long v = 0;
          if (indexExprValue(args[a].first, args[a].second, &v) && v > slots[a])
            slots[a] = v;
        }
      }
      recursiveMaxArg_[entry.first] = slots;
    }
  }

  /// An early-return guard such as `if (i > 5) return;` bounds the values the
  /// index parameter can take on the path that reaches the subscript, even
  /// though the recursive call itself passes an expression (`i + 1`) that is
  /// not a compile-time constant.  When that bound reaches the array extent
  /// it is itself proof that the cycle walks out of bounds, so record it as
  /// an effective maximum argument for the cycle check.  Tight guards are
  /// already suppressed by hasUpperGuard and are harmless here.
  std::map<std::string, std::vector<long>> recursiveGuardMax_;

  void collectRecursiveGuardBounds() {
    for (const auto &entry : funcs_) {
      const FuncSig &sig = entry.second;
      if (!isRecursive(entry.first) || sig.bodyClose == kNPos) continue;
      size_t np = sig.params.size();
      std::vector<long> slots(np, kInvalidConst);
      for (size_t k = sig.bodyOpen; k <= sig.bodyClose; ++k) {
        if (!isIdent(k, "if")) continue;
        size_t lp = nextSig(k);
        if (!isPunct(lp, "(")) continue;
        size_t rp = matchIn(lp, sig.bodyClose, "(", ")");
        if (rp == kNPos) continue;
        size_t var = kNPos, op = kNPos;
        for (size_t q = lp + 1; q < rp; ++q) {
          if (toks_[q].kind == TokKind::Identifier) {
            var = q;
            op = nextSig(q);
            break;
          }
        }
        if (var == kNPos || op >= rp) continue;
        long n = 0;
        size_t nv = nextSig(op);
        if (nv >= rp || !number(toks_[nv], &n)) continue;
        // Only an early-return guard bounds the path that continues to the
        // subscript (a guard around the access itself is a different shape,
        // handled by hasUpperGuard).
        size_t body = nextSig(rp);
        size_t end = isPunct(body, "{") ? matchIn(body, sig.bodyClose, "{", "}")
                                        : stmtEnd(body);
        if (end == kNPos) continue;
        bool early = false;
        for (size_t q = body; q <= end; ++q)
          if (isIdent(q, "return")) { early = true; break; }
        if (!early) continue;
        for (size_t p = 0; p < np; ++p) {
          if (sig.params[p] != toks_[var].text) continue;
          // `i > N` allows i == N; `i >= N` allows at most N - 1.
          long reach = isPunct(op, ">") ? n : n - 1;
          if (reach > slots[p]) slots[p] = reach;
          break;
        }
      }
      recursiveGuardMax_[entry.first] = slots;
    }
  }

  /// An explicit upper guard (`if (i >= E) return;` or a surrounding
  /// `if (i < E)`) makes a parameter index provably bounded, so fail-closed
  /// recursion rules must stay silent there.
  bool hasUpperGuard(const FuncSig &sig, const std::string &var, size_t at,
                     long extent) const {
    for (size_t k = sig.bodyOpen; k < at; ++k) {
      if (!isIdent(k, "if")) continue;
      size_t lp = nextSig(k);
      if (!isPunct(lp, "(")) continue;
      size_t rp = matchIn(lp, at, "(", ")");
      if (rp == kNPos) continue;
      size_t v = kNPos;
      for (size_t q = lp + 1; q < rp; ++q)
        if (toks_[q].kind == TokKind::Identifier && toks_[q].text == var) {
          v = q;
          break;
        }
      if (v == kNPos) continue;
      size_t op = nextSig(v);
      if (op >= rp) continue;
      long n = 0;
      size_t nv = nextSig(op);
      if (nv >= rp || !number(toks_[nv], &n)) continue;
      size_t body = nextSig(rp);
      size_t end = isPunct(body, "{") ? matchIn(body, at, "{", "}")
                                      : stmtEnd(body);
      bool early = false;
      // Only a guard that keeps EVERY continuing index below the extent is a
      // proof of safety.  `if (i >= N) return;` proceeds for i < N, so it
      // suffices when N <= extent; `if (i > N) return;` proceeds for i <= N,
      // so it needs N <= extent - 1.  The old `n >= extent` form wrongly
      // accepted weak guards such as `i > 5` for a 4-element array and
      // silently suppressed a real recursion overflow, so the comparison is
      // reversed here.  Tight guards used by the existing safe corpus still
      // suppress exactly as before.
      if ((isPunct(op, ">=") && n <= extent) ||
          (isPunct(op, ">") && n <= extent - 1)) {
        if (end != kNPos && end < at)
          for (size_t q = body; q <= end; ++q)
            if (isIdent(q, "return")) { early = true; break; }
      }
      if (early) return true;
      if (((isPunct(op, "<") && n <= extent) ||
           (isPunct(op, "<=") && n <= extent - 1)) &&
          end != kNPos && at > body && at < end)
        return true;
    }
    return false;
  }

  /// A parameter index into a fixed array inside a recursive cycle cannot be
  /// bounded by local analysis.  When the call site supplies a constant that
  /// already reaches the extent, the overflow is provable: reject it.  For a
  /// genuinely unknown argument only warn (DEFAULT remains permissive).
  void checkRecursiveArraySubscript(size_t i) {
    if (toks_[i].kind != TokKind::Identifier) return;
    size_t lb = nextSig(i);
    if (!isPunct(lb, "[")) return;
    const std::vector<size_t> *dims = nullptr;
    if (!findShape(toks_[i].text, i, &dims) || dims->empty()) return;
    const std::string *fn = enclosingFunction(i);
    if (!fn || !isRecursive(*fn)) return;
    const FuncSig &sig = funcs_.at(*fn);
    size_t rb = match(lb, "[", "]");
    if (rb == kNPos) return;
    size_t idxTok = kNPos;
    int parts = 0;
    for (size_t k = lb + 1; k < rb; ++k) {
      if (trivia(k)) continue;
      ++parts;
      if (toks_[k].kind == TokKind::Identifier) idxTok = k;
    }
    if (parts != 1 || idxTok == kNPos) return;
    size_t pos = kNPos;
    for (size_t p = 0; p < sig.params.size(); ++p)
      if (sig.params[p] == toks_[idxTok].text) { pos = p; break; }
    if (pos == kNPos) return;
    long extent = static_cast<long>((*dims)[0]);
    if (hasUpperGuard(sig, toks_[idxTok].text, i, extent)) return;
    long best = kInvalidConst;
    for (const auto &entry : funcs_) {
      if (!sameCycle(*fn, entry.first)) continue;
      auto it = recursiveMaxArg_.find(entry.first);
      if (it == recursiveMaxArg_.end() || pos >= it->second.size()) continue;
      if (it->second[pos] > best) best = it->second[pos];
    }
    if (best == kInvalidConst) {
      warningAt(toks_[lb], "array index '" + toks_[idxTok].text +
                "' into fixed array '" + toks_[i].text +
                "' cannot be bounded across recursive calls; STRICT rejects "
                "unverified recursive indices");
      return;
    }
    if (best >= extent)
      errorAt(toks_[lb], "array index " + std::to_string(best) +
              " from recursive call is out of bounds of array '" +
              toks_[i].text + "' of length " + std::to_string(extent));
  }

  // --- allocation extents written through out-parameters -------------------

  std::map<std::string, std::pair<size_t, size_t>> outParamExtent_;

  bool outParamAllocCount(size_t from, size_t to, size_t *count) const {
    bool hasSizeof = false, hasStar = false;
    for (size_t k = from; k < to; ++k) {
      if (isIdent(k, "sizeof")) hasSizeof = true;
      else if (isPunct(k, "*")) hasStar = true;
    }
    if (!hasSizeof) return allocationCount(from, to, count);
    if (!hasStar) { *count = 1; return true; } // sizeof(T): one element
    return allocationCount(from, to, count);
  }

  void collectOutParamAllocations() {
    std::map<std::string, std::map<size_t, size_t>> writes;
    for (const auto &entry : funcs_) {
      const FuncSig &sig = entry.second;
      for (size_t p = 0; p < sig.params.size(); ++p) {
        if (!sig.outParam[p]) continue;
        const std::string &name = sig.params[p];
        for (size_t k = sig.bodyOpen; k <= sig.bodyClose; ++k) {
          if (!isPunct(k, "*")) continue;
          size_t id = nextSig(k);
          if (id >= toks_.size() || toks_[id].kind != TokKind::Identifier ||
              toks_[id].text != name || !isPunct(nextSig(id), "="))
            continue;
          size_t alloc = nextSig(nextSig(id));
          if (!(isIdent(alloc, "malloc") || isIdent(alloc, "calloc") ||
                isIdent(alloc, "cplus_alloc") || isIdent(alloc, "cplus_zalloc")))
            continue;
          size_t lp = nextSig(alloc);
          if (!isPunct(lp, "(")) continue;
          size_t rp = matchIn(lp, toks_.size(), "(", ")");
          if (rp == kNPos) continue;
          size_t count = 0;
          if (isIdent(alloc, "calloc")) {
            // calloc(nmemb, size): nmemb, not the sizeof argument, is the
            // element count; using 1 here would be an unsound over-approach.
            auto a = splitArgs(lp + 1, rp);
            long n = 0;
            if (a.size() >= 2 &&
                indexExprValue(a[0].first, a[0].second, &n) && n > 0)
              count = static_cast<size_t>(n);
          } else {
            outParamAllocCount(lp + 1, rp, &count);
          }
          if (count)
            writes[entry.first][p] = count;
        }
      }
    }
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier) continue;
      auto fit = writes.find(toks_[i].text);
      if (fit == writes.end()) continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "(")) continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos || isPunct(nextSig(rp), "{")) continue;
      auto args = splitArgs(lp + 1, rp);
      for (const auto &kv : fit->second) {
        if (kv.first >= args.size()) continue;
        size_t a = args[kv.first].first, b = args[kv.first].second;
        if (!isPunct(a, "&")) continue;
        size_t name = nextSig(a);
        if (name >= b || toks_[name].kind != TokKind::Identifier) continue;
        auto &slot = outParamExtent_[toks_[name].text];
        if (slot.second && slot.second != kv.second)
          slot = {i, 0}; // conflicting extents: forget the fact
        else
          slot = {i, kv.second};
      }
    }
    // A later write or address-taken use can replace the allocation; a stale
    // extent must never reject a valid subscript.
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier) continue;
      auto it = outParamExtent_.find(toks_[i].text);
      if (it == outParamExtent_.end() || i <= it->second.first) continue;
      size_t n = nextSig(i);
      bool takenAtCall = isPunct(prevSig(i), "&") &&
                         (isPunct(prevSig(prevSig(i)), "(") ||
                          isPunct(prevSig(prevSig(i)), ","));
      if (isPunct(n, "=") || isPunct(n, "++") || isPunct(n, "--") ||
          isPunct(n, "+=") || isPunct(n, "-=") ||
          (isPunct(prevSig(i), "&") && !takenAtCall))
        it->second.second = 0;
    }
  }

  void checkOutParamHeapSubscript(size_t i) {
    auto it = outParamExtent_.find(toks_[i].text);
    if (it == outParamExtent_.end() || i < it->second.first ||
        it->second.second == 0)
      return;
    size_t lb = nextSig(i);
    if (!isPunct(lb, "[")) return;
    size_t rb = match(lb, "[", "]");
    if (rb == kNPos) return;
    long idx = 0;
    if (!constantIndexAt(lb + 1, rb, &idx) || idx < 0) return;
    if (static_cast<size_t>(idx) >= it->second.second)
      errorAt(toks_[lb], "array index " + std::to_string(idx) +
              " is out of bounds of heap extent '" + toks_[i].text +
              "' of " + std::to_string(it->second.second) + " elements");
  }

  //===--------------------------------------------------------------------===//
  // Wave 2 — residual spatial hardening (struct fields / derived indices).
  //
  // Rationale: the wave-1 pass tracked bare array names and literal indices.
  // The residual red findings all launder the extent through a second name:
  // a struct field written at the call site, a pointer parameter of a
  // recursive helper, or a helper's return value used as an index.  STRICT
  // check emits no runtime guards, so a subscript that is *provable* through
  // these chains must be rejected here rather than accepted and guarded
  // later.  Every rule stays silent unless the whole chain (extent + index)
  // folds to a compile-time constant; genuinely unknown indices are left to
  // the AST owner's fail-closed rule, so DEFAULT never blanket-rejects C.
  //===--------------------------------------------------------------------===//

  struct FieldShape {
    std::string name;
    size_t extent = 0;    // non-zero for a fixed-size array field
    bool pointer = false; // `T *p` (extent may arrive from its initialiser)
    std::string structTag; // non-empty for an inline `struct T field;`
  };

  struct FieldFact {
    bool valid = false;
    bool isExtent = false; // true: value is an array extent; false: an integer
    long value = 0;
    std::string array;     // array name backing a pointer field
    size_t pos = 0;        // source position from which the fact is visible
  };

  std::map<std::string, std::vector<FieldShape>> structFields_;
  std::map<std::string, std::string> typedefStruct_; // multi-level typedef chain
  std::map<std::string, std::string> varStruct_;     // object/param -> struct tag
  std::map<std::string, FieldFact> fieldFacts_;      // "object.field" -> fact
  std::map<std::string, std::map<size_t, size_t>> paramExtents_; // fn -> param -> extent

  void parseFieldShape(size_t from, size_t to, std::vector<FieldShape> *fields) {
    if (from >= to)
      return;
    bool pointer = false;
    size_t name = kNPos;
    size_t extent = 0;
    std::string structTag;
    for (size_t k = from; k < to; ++k) {
      if (isPunct(k, "*")) pointer = true;
      if (isPunct(k, "[")) {
        long v = 0;
        size_t n = nextSig(k);
        if (n < to && number(toks_[n], &v) && v > 0)
          extent = static_cast<size_t>(v);
      }
      if (isIdent(k, "struct") || isIdent(k, "union")) {
        size_t t = nextSig(k); // raw k+1 may be whitespace
        if (t < to && toks_[t].kind == TokKind::Identifier)
          structTag = toks_[t].text;
      }
      // The last identifier that is not a type word is the declarator name.
      if (toks_[k].kind == TokKind::Identifier && !isTypeWord(toks_[k].text)) {
        if (k == from && (isIdent(from, "struct") || isIdent(from, "union")))
          continue; // the tag of an inline `struct inner in;`
        if (!structTag.empty() && toks_[k].text == structTag &&
            k + 1 < to)
          continue; // the tag, not the declarator
        name = k;
      }
    }
    if (name != kNPos) {
      FieldShape f;
      f.name = toks_[name].text;
      f.pointer = pointer;
      f.extent = extent;
      f.structTag = structTag;
      fields->push_back(f);
    }
  }

  /// Read struct/union member shapes (including fixed arrays and FAMs) and
  /// multi-level `typedef struct B TB; typedef TB TC;` aliases.  A flexible
  /// array member has extent 0 here and can never produce a *provable* OOB,
  /// so it is deliberately ignored (no false positive on valid FAM code).
  void collectStructShapes() {
    for (size_t i = 0; i + 2 < toks_.size(); ++i) {
      if (!isIdent(i, "struct") && !isIdent(i, "union")) continue;
      size_t tag = nextSig(i);
      if (tag >= toks_.size() || toks_[tag].kind != TokKind::Identifier) continue;
      size_t brace = nextSig(tag);
      if (!isPunct(brace, "{")) continue;
      size_t close = match(brace, "{", "}");
      if (close == kNPos) continue;
      std::vector<FieldShape> fields;
      size_t start = nextSig(brace);
      for (size_t k = brace + 1; k <= close; ++k) {
        if (!isPunct(k, ";")) continue;
        parseFieldShape(start, k, &fields);
        start = nextSig(k);
      }
      if (!fields.empty())
        structFields_[toks_[tag].text] = fields;
    }
    for (size_t i = 0; i + 2 < toks_.size(); ++i) {
      if (!isIdent(i, "typedef")) continue;
      size_t t = nextSig(i);
      if (isIdent(t, "struct") || isIdent(t, "union")) t = nextSig(t);
      if (t >= toks_.size() || toks_[t].kind != TokKind::Identifier) continue;
      size_t alias = nextSig(t);
      while (alias < toks_.size() && isPunct(alias, "*")) alias = nextSig(alias);
      if (alias < toks_.size() && toks_[alias].kind == TokKind::Identifier &&
          !isPunct(nextSig(alias), "("))
        typedefStruct_[toks_[alias].text] = toks_[t].text;
    }
  }

  std::string resolveStructTag(std::string name) const {
    for (int guard = 0; guard < 8; ++guard) {
      auto it = typedefStruct_.find(name);
      if (it == typedefStruct_.end()) break;
      name = it->second;
    }
    return structFields_.count(name) ? name : std::string();
  }

  /// Associate object and parameter names with their struct tag.  Covers
  /// `struct B b;`, `struct B *s;` and typedef spellings on locals/params.
  void collectStructTypedVars() {
    for (size_t i = 0; i + 1 < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier) continue;
      std::string tag;
      size_t afterType = i;
      if (isIdent(i, "struct") || isIdent(i, "union")) {
        size_t t = nextSig(i);
        if (t < toks_.size() && toks_[t].kind == TokKind::Identifier) {
          tag = toks_[t].text;
          afterType = t;
        }
      } else {
        tag = resolveStructTag(toks_[i].text);
      }
      if (tag.empty() || !structFields_.count(tag)) continue;
      size_t n = nextSig(afterType);
      while (n < toks_.size() && isPunct(n, "*")) n = nextSig(n);
      if (n < toks_.size() && toks_[n].kind == TokKind::Identifier &&
          !isTypeWord(toks_[n].text))
        varStruct_[toks_[n].text] = tag;
    }
  }

  void setFieldFact(const std::string &obj, const std::string &field,
                    bool isExtent, long value, const std::string &array,
                    size_t pos) {
    FieldFact f;
    f.valid = true;
    f.isExtent = isExtent;
    f.value = value;
    f.array = array;
    f.pos = pos;
    fieldFacts_[obj + "." + field] = f;
  }

  /// Record one field's value: a pointer field initialised from a fixed array
  /// yields that array's extent; an integer field yields a folded constant.
  void recordFieldValue(const std::string &obj, const std::string &fieldName,
                        const std::vector<FieldShape> &fields, size_t from,
                        size_t to, size_t pos) {
    const FieldShape *shape = nullptr;
    for (const auto &f : fields)
      if (f.name == fieldName) { shape = &f; break; }
    if (!shape) return;
    size_t r = stripCast(from, to);
    if (shape->pointer) {
      if (r < to && toks_[r].kind == TokKind::Identifier) {
        size_t extent = 0;
        if (findExtent(toks_[r].text, r, &extent) && extent)
          setFieldFact(obj, fieldName, true, static_cast<long>(extent),
                       toks_[r].text, pos);
      }
    } else {
      long v = 0;
      if (indexExprValue(r, to, &v))
        setFieldFact(obj, fieldName, false, v, std::string(), pos);
    }
  }

  /// Call-site field initialisers: both brace form `{a, 4}` and assignment
  /// form `b.p = a; b.n = 4;` are understood, by field position/name.
  void collectFieldInitializers() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier) continue;
      const std::string obj = toks_[i].text;
      auto sit = varStruct_.find(obj);
      if (sit == varStruct_.end()) continue;
      auto fit = structFields_.find(sit->second);
      if (fit == structFields_.end()) continue;
      size_t dot = nextSig(i);
      if (isPunct(dot, ".") || isPunct(dot, "->")) {
        size_t field = nextSig(dot);
        size_t eq = field == kNPos ? kNPos : nextSig(field);
        if (field >= toks_.size() || toks_[field].kind != TokKind::Identifier ||
            !isPunct(eq, "=")) continue;
        size_t rhs = nextSig(eq);
        recordFieldValue(obj, toks_[field].text, fit->second, rhs,
                         stmtEnd(rhs), eq);
        continue;
      }
      size_t eq = nextSig(i);
      if (!isPunct(eq, "=")) continue;
      size_t brace = nextSig(eq);
      if (!isPunct(brace, "{")) continue;
      size_t close = match(brace, "{", "}");
      if (close == kNPos) continue;
      auto args = splitArgs(brace + 1, close);
      for (size_t a = 0; a < args.size() && a < fit->second.size(); ++a)
        recordFieldValue(obj, fit->second[a].name, fit->second, args[a].first,
                         args[a].second, eq);
    }
  }

  /// Copy the caller's field facts onto a struct (or struct-pointer)
  /// parameter.  Facts are function-scoped (pos 0) because the call site may
  /// appear after the callee in source order; a later write to the parameter
  /// is still handled by the existing invalidation passes.
  void propagateFieldFactsToParams() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier) continue;
      auto fit = funcs_.find(toks_[i].text);
      if (fit == funcs_.end()) continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "(")) continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos || isPunct(nextSig(rp), "{")) continue;
      auto args = splitArgs(lp + 1, rp);
      const auto &params = fit->second.params;
      for (size_t p = 0; p < params.size() && p < args.size(); ++p) {
        if (!varStruct_.count(params[p])) continue;
        size_t a = args[p].first;
        if (isPunct(a, "&")) a = nextSig(a); // `wr(&s)` -> pointer param
        if (a >= args[p].second || toks_[a].kind != TokKind::Identifier) continue;
        const std::string src = toks_[a].text;
        std::vector<std::pair<std::string, FieldFact>> copies;
        for (const auto &kv : fieldFacts_) {
          size_t dot = kv.first.find('.');
          if (dot != std::string::npos && kv.first.compare(0, dot, src) == 0)
            copies.push_back({params[p] + kv.first.substr(dot), kv.second});
        }
        for (auto &kv : copies) {
          kv.second.pos = 0;
          fieldFacts_[kv.first] = kv.second;
        }
      }
    }
  }

  /// `b.p[b.n]` / `s->p[s->n]`: use the field extent (from the initialiser or
  /// the declared array shape) and the folded field index.  Only a constant
  /// index outside a known extent is reported, so valid dynamic accesses are
  /// untouched.
  void checkStructFieldSubscript(size_t i) {
    if (toks_[i].kind != TokKind::Identifier) return;
    auto sit = varStruct_.find(toks_[i].text);
    if (sit == varStruct_.end()) return;
    std::string tag = sit->second;
    std::string objPath = toks_[i].text;
    size_t cursor = nextSig(i);
    // Walk a member chain (`s.p[i]`, `o.in[1].v[3]`).  Each segment's extent
    // comes from a call-site field fact when available, otherwise from the
    // declared array shape; an unprovable index stops the walk silently.
    for (int depth = 0; depth < 6; ++depth) {
      if (!isPunct(cursor, ".") && !isPunct(cursor, "->")) return;
      size_t field = nextSig(cursor);
      if (field >= toks_.size() || toks_[field].kind != TokKind::Identifier) return;
      auto fit = structFields_.find(tag);
      if (fit == structFields_.end()) return;
      const FieldShape *shape = nullptr;
      for (const auto &f : fit->second)
        if (f.name == toks_[field].text) { shape = &f; break; }
      if (!shape) return;
      size_t after = nextSig(field);
      if (isPunct(after, "[")) {
        size_t rb = match(after, "[", "]");
        if (rb == kNPos) return;
        long extent = 0;
        auto ff = fieldFacts_.find(objPath + "." + toks_[field].text);
        if (ff != fieldFacts_.end() && ff->second.valid &&
            ff->second.isExtent && ff->second.pos <= i)
          extent = ff->second.value; // pointer field initialised from an array
        else
          extent = static_cast<long>(shape->extent); // declared array field
        if (extent > 0) {
          long index = 0;
          size_t idx = nextSig(after);
          if (idx < rb && toks_[idx].kind == TokKind::Identifier &&
              idx + 2 < rb && (isPunct(idx + 1, ".") ||
                               isPunct(idx + 1, "->")) &&
              toks_[idx + 2].kind == TokKind::Identifier) {
            auto iit = fieldFacts_.find(objPath + "." + toks_[idx + 2].text);
            if (iit != fieldFacts_.end() && iit->second.valid &&
                !iit->second.isExtent && iit->second.pos <= i)
              index = iit->second.value;
            else
              return; // index field not provable: stay silent
          } else if (!constantIndexAt(idx, rb, &index)) {
            // Wave 4 (red finding): `void w(struct S *s, int i){ s->a[i]=777; }`
            // called as `w(&s, 9)` — the field extent is known and the index
            // is bounded by the call site, so the OOB is provable.
            if (!paramIndexUpperBound(idx, rb, &index))
              return;
            const std::string *fn = enclosingFunction(i);
            if (fn) {
              bool guarded = false;
              for (size_t k = idx; k < rb; ++k)
                if (toks_[k].kind == TokKind::Identifier &&
                    hasFoldedUpperGuard(funcs_.at(*fn), toks_[k].text, i,
                                        extent))
                  guarded = true;
              if (guarded)
                return;
            }
          }
          if (index < 0 || index >= extent) {
            errorAtSafeBounds(toks_[after], "out-of-bounds access: index " +
                                      std::to_string(index) +
                                      " reaches past array '" + objPath + "." +
                                      toks_[field].text + "' of length " +
                                      std::to_string(extent));
            return;
          }
        }
        after = nextSig(rb);
      }
      if ((isPunct(after, ".") || isPunct(after, "->")) &&
          !shape->structTag.empty()) {
        std::string nextTag = resolveStructTag(shape->structTag);
        if (nextTag.empty()) nextTag = shape->structTag;
        if (!structFields_.count(nextTag)) return;
        tag = nextTag;
        objPath += "." + toks_[field].text;
        cursor = after;
        continue;
      }
      return;
    }
  }

  /// Learn a pointer parameter's extent from call sites that pass a fixed
  /// array.  A small fixed point carries extents across (mutually) recursive
  /// calls, where the callee receives a caller parameter rather than a named
  /// array.  Conflicting extents are forgotten, never guessed.
  void collectParamArrayExtents() {
    for (int iter = 0; iter < 3; ++iter) {
      bool changed = false;
      for (size_t i = 0; i < toks_.size(); ++i) {
        if (toks_[i].kind != TokKind::Identifier) continue;
        auto fit = funcs_.find(toks_[i].text);
        if (fit == funcs_.end()) continue;
        size_t lp = nextSig(i);
        if (!isPunct(lp, "(")) continue;
        size_t rp = match(lp, "(", ")");
        if (rp == kNPos || isPunct(nextSig(rp), "{")) continue;
        auto args = splitArgs(lp + 1, rp);
        const auto &params = fit->second.params;
        for (size_t p = 0; p < params.size() && p < args.size(); ++p) {
          size_t a = args[p].first;
          if (isPunct(a, "&")) a = nextSig(a);
          if (a >= args[p].second || toks_[a].kind != TokKind::Identifier) continue;
          size_t extent = 0;
          if (!findExtent(toks_[a].text, a, &extent)) {
            const std::string *caller = enclosingFunction(i);
            if (!caller) continue;
            const auto &cparams = funcs_.at(*caller).params;
            for (size_t cp = 0; cp < cparams.size(); ++cp) {
              if (cparams[cp] != toks_[a].text) continue;
              auto cit = paramExtents_[*caller].find(cp);
              if (cit != paramExtents_[*caller].end()) extent = cit->second;
            }
          }
          if (!extent) continue;
          auto &slot = paramExtents_[toks_[i].text][p];
          if (slot && slot != extent) slot = 0;
          else if (!slot) { slot = extent; changed = true; }
        }
      }
      if (!changed) break;
    }
  }

  /// `rec(p, i)` with `p[i + 9]` and a constant extent arriving at a cycle
  /// entry: fold the derived index from the largest constant supplied at a
  /// cycle call site.  An explicit upper guard suppresses the report (the
  /// access is then provably bounded); no constant means no proof and hence
  /// no diagnostic here — the AST owner applies STRICT fail-closed there.
  void checkRecursivePointerParamSubscript(size_t i) {
    if (toks_[i].kind != TokKind::Identifier) return;
    size_t lb = nextSig(i);
    if (!isPunct(lb, "[")) return;
    size_t rb = match(lb, "[", "]");
    if (rb == kNPos) return;
    const std::string *fn = enclosingFunction(i);
    if (!fn || !isRecursive(*fn)) return;
    const FuncSig &sig = funcs_.at(*fn);
    size_t pos = kNPos;
    for (size_t p = 0; p < sig.params.size(); ++p)
      if (sig.params[p] == toks_[i].text) { pos = p; break; }
    if (pos == kNPos) return;
    auto eit = paramExtents_.find(*fn);
    if (eit == paramExtents_.end()) return;
    auto pit = eit->second.find(pos);
    if (pit == eit->second.end() || pit->second == 0) return;
    long extent = static_cast<long>(pit->second);

    size_t idx = nextSig(lb);
    long delta = 0;
    if (idx + 1 == rb && toks_[idx].kind == TokKind::Identifier) {
      // bare parameter index
    } else if (idx + 3 == rb && toks_[idx].kind == TokKind::Identifier &&
               (isPunct(idx + 1, "+") || isPunct(idx + 1, "-")) &&
               number(toks_[idx + 2], &delta)) {
      if (isPunct(idx + 1, "-")) delta = -delta;
    } else {
      return;
    }
    // The derived index is often a *different* parameter from the pointer
    // base (`p[i + 9]`), so resolve whichever parameter the index names.
    if (toks_[idx].kind != TokKind::Identifier) return;
    const std::string &param = toks_[idx].text;
    if (hasUpperGuard(sig, param, i, extent)) return;
    size_t argPos = kNPos;
    for (size_t p = 0; p < sig.params.size(); ++p)
      if (sig.params[p] == param) { argPos = p; break; }
    if (argPos == kNPos) return;
    long best = kInvalidConst;
    for (const auto &entry : funcs_) {
      if (!sameCycle(*fn, entry.first)) continue;
      auto rit = recursiveMaxArg_.find(entry.first);
      if (rit != recursiveMaxArg_.end() && argPos < rit->second.size() &&
          rit->second[argPos] > best)
        best = rit->second[argPos];
      // A weak early-return guard proves the index grows to its bound even
      // though the recursive call passes `i + 1` (not a constant).
      auto git = recursiveGuardMax_.find(entry.first);
      if (git != recursiveGuardMax_.end() && argPos < git->second.size() &&
          git->second[argPos] > best)
        best = git->second[argPos];
    }
    if (best == kInvalidConst) return;
    long index = best + delta;
    if (index < 0 || index >= extent)
      errorAtSafeBounds(toks_[lb], "array index " + std::to_string(index) +
                             " from recursive call is out of bounds of '" +
                             toks_[i].text + "' of extent " +
                             std::to_string(extent));
  }

  /// Values the host AST checker cannot fold: a literal-returning helper
  /// (`int i = off(); a[i]`) or a difference of two constant-offset aliases
  /// (`int d = q - p; a[d]`).  Both are provable with tokens + wave-1 facts.
  bool novelIndexValue(size_t from, size_t to, long *out) const {
    size_t r = from, e = to;
    trim(&r, &e);
    if (r >= e) return false;
    if (r + 1 < e && toks_[r].kind == TokKind::Identifier &&
        isPunct(nextSig(r), "(")) {
      auto it = returnConstants_.find(toks_[r].text);
      if (it != returnConstants_.end()) { *out = it->second; return true; }
    }
    if (e == r + 3 && toks_[r].kind == TokKind::Identifier &&
        isPunct(r + 1, "-") && toks_[r + 2].kind == TokKind::Identifier) {
      auto a = arrayPtrs_.find(toks_[r].text);
      auto b = arrayPtrs_.find(toks_[r + 2].text);
      if (a != arrayPtrs_.end() && b != arrayPtrs_.end() &&
          a->second.array == b->second.array) {
        *out = a->second.offset - b->second.offset;
        return true;
      }
    }
    if (e == r + 1 && toks_[r].kind == TokKind::Identifier) {
      if (isVolatileName(toks_[r].text))
        return false; // a volatile index cannot be folded from its init
      for (size_t k = r; k > 0; --k) {
        if (!isPunct(k, "=")) continue;
        size_t lhs = prevSig(k);
        if (lhs == kNPos || toks_[lhs].kind != TokKind::Identifier ||
            toks_[lhs].text != toks_[r].text) continue;
        size_t rhs = nextSig(k);
        if (novelIndexValue(rhs, stmtEnd(rhs), out)) return true;
        break; // most recent assignment is not novel: stop looking
      }
    }
    return false;
  }

  /// True when the nearest enclosing `if` condition proves `name >= 0`
  /// before the subscript at `pos`.  A value folded from its initializer can
  /// be negative at declaration yet guarded before every use
  /// (`prev = j - 1; if (prev >= 0 && tmp[prev] == x)`); reporting -1 there
  /// rejected valid plain C (c_apps/game_2048).
  bool hasNonNegativeGuard(const std::string &name, size_t pos) const {
    size_t ifPos = kNPos;
    int braces = 0;
    for (size_t k = pos; k > 0; --k) {
      size_t p = k - 1;
      if (isPunct(p, ";"))
        break;
      // The guarded use may be inside the `if` body; allow a small number of
      // brace crossings to reach the condition, but never scan past a
      // statement boundary or into an unrelated outer scope.
      if (isPunct(p, "{") || isPunct(p, "}")) {
        if (++braces > 3)
          break;
      }
      if (isIdent(p, "if")) { ifPos = p; break; }
    }
    if (ifPos == kNPos)
      return false;
    size_t lp = nextSig(ifPos);
    if (lp == kNPos || !isPunct(lp, "("))
      return false;
    size_t rp = match(lp, "(", ")");
    if (rp == kNPos)
      return false;
    // The guarded subscript can itself be part of the `if` condition, in
    // which case rp is after `pos`; that is the common `p && a[i]` form.
    for (size_t k = nextSig(lp); k < rp; ++k) {
      if (toks_[k].kind != TokKind::Identifier || toks_[k].text != name)
        continue;
      size_t op = nextSig(k);
      if (op != kNPos && op < rp && isPunct(op, ">=")) {
        size_t z = nextSig(op);
        if (z != kNPos && z < rp && toks_[z].kind == TokKind::Number &&
            toks_[z].text == "0")
          return true;
      }
    }
    // `0 <= name` form.
    for (size_t k = nextSig(lp); k < rp; ++k) {
      if (toks_[k].kind != TokKind::Number || toks_[k].text != "0")
        continue;
      size_t op = nextSig(k);
      if (op == kNPos || op >= rp || !isPunct(op, "<="))
        continue;
      size_t n = nextSig(op);
      if (n != kNPos && n < rp && toks_[n].kind == TokKind::Identifier &&
          toks_[n].text == name)
        return true;
    }
    return false;
  }

  void checkFoldedIndexSubscript(size_t i) {
    if (toks_[i].kind != TokKind::Identifier) return;
    size_t lb = nextSig(i);
    if (!isPunct(lb, "[")) return;
    size_t rb = match(lb, "[", "]");
    if (rb == kNPos) return;
    const std::vector<size_t> *dims = nullptr;
    if (!findShape(toks_[i].text, i, &dims) || dims->empty()) return;
    long index = 0;
    if (!novelIndexValue(lb + 1, rb, &index)) return;
    if (index < 0 && hasNonNegativeGuard(toks_[nextSig(lb)].text, i)) return;
    long extent = static_cast<long>(dims->front());
    if (index < 0)
      errorAt(toks_[lb], "negative array index " + std::to_string(index) +
                             " on '" + toks_[i].text + "'");
    else if (index >= extent)
      reportConstantIndex(toks_[lb], index, toks_[i].text,
                          static_cast<size_t>(extent));
  }

  static bool isAssignOpToken(const Token &t) {
    if (t.kind != TokKind::Punct) return false;
    static const char *const kOps[] = {"=",  "+=",  "-=",  "*=",  "/=",
                                       "%=", "<<=", ">>=", "&=",  "|=", "^="};
    for (const char *op : kOps)
      if (t.text == op) return true;
    return false;
  }

  /// Multiple unsequenced modifications of one scalar in a single full
  /// expression are undefined behaviour (`i++ + ++i`, `i = i++`, ...).  The
  /// rewriter now lowers `++` to an instrumented call plus `+`, which hides
  /// the UB from clang's `-Werror=unsequenced` on the generated C, so the
  /// check must run on the ORIGINAL tokens here.  It is a proven error in
  /// both DEFAULT and STRICT, not a STRICT-only rule.
  ///
  /// Sequence points that split the scan: `;`, `{`, `}`, the comma operator,
  /// `&&`, `||` and `?:`.  A comma inside a call's argument list is NOT a
  /// sequence point (`f(i++, i++)` is UB), whereas a comma inside grouping
  /// parentheses is (`i = (i++, 5)` is fine).  Only pairs where at least one
  /// event is an increment/decrement are reported, so ambiguous forms such as
  /// `i = i = 2` are left to the host compiler rather than risking a false
  /// positive.
  void checkUnsequencedModifications() {
    struct Mod {
      std::string var;
      size_t pos;
      bool inc;
    };
    size_t start = 0;
    for (size_t end = 0; end <= toks_.size(); ++end) {
      bool boundary = end == toks_.size() || isPunct(end, ";") ||
                      isPunct(end, "{") || isPunct(end, "}");
      if (!boundary) continue;
      std::vector<Mod> mods;
      std::vector<size_t> barriers;
      std::vector<bool> callParens; // stack: is this '(' a function call?
      int bracket = 0;
      for (size_t k = start; k < end; ++k) {
        if (isPunct(k, "(")) {
          size_t p = prevSig(k);
          bool call = p != kNPos &&
                      (toks_[p].kind == TokKind::Identifier ||
                       isPunct(p, ")") || isPunct(p, "]"));
          callParens.push_back(call);
        } else if (isPunct(k, ")") && !callParens.empty()) {
          callParens.pop_back();
        } else if (isPunct(k, "[")) {
          ++bracket;
        } else if (isPunct(k, "]")) {
          if (bracket > 0) --bracket;
        } else if (isPunct(k, ",") || isPunct(k, "&&") || isPunct(k, "||") ||
                   isPunct(k, "?") || isPunct(k, ":")) {
          bool plain = bracket == 0;
          for (bool call : callParens)
            if (call) { plain = false; break; }
          if (plain) barriers.push_back(k);
        }

        if (toks_[k].kind == TokKind::Identifier &&
            !isTypeWord(toks_[k].text)) {
          size_t p = prevSig(k);
          bool member = isPunct(p, ".") || isPunct(p, "->");
          if (!member) {
            size_t n = nextSig(k);
            if (n < end && (isPunct(n, "++") || isPunct(n, "--")))
              mods.push_back({toks_[k].text, k, true});
            else if (n < end && isAssignOpToken(toks_[n])) {
              // The write happens after the RHS is evaluated, so anchor it at
              // the end of the full expression.  `i = (i++, 5)` then has the
              // comma operator between the increment and the write and stays
              // accepted, while `i = i++` has no barrier and is still caught.
              size_t afterRhs = stmtEnd(nextSig(n));
              mods.push_back({toks_[k].text,
                              afterRhs < end ? afterRhs : end, false});
            }
          }
        }
        if ((isPunct(k, "++") || isPunct(k, "--")) && k < end) {
          size_t id = nextSig(k);
          if (id < end && toks_[id].kind == TokKind::Identifier &&
              !isTypeWord(toks_[id].text) &&
              !isPunct(prevSig(id), ".") && !isPunct(prevSig(id), "->"))
            mods.push_back({toks_[id].text, k, true});
        }
      }
      for (size_t a = 0; a < mods.size(); ++a)
        for (size_t b = a + 1; b < mods.size(); ++b) {
          if (mods[a].var != mods[b].var ||
              (!mods[a].inc && !mods[b].inc))
            continue;
          size_t lo = mods[a].pos, hi = mods[b].pos;
          if (lo > hi) std::swap(lo, hi);
          bool separated = false;
          for (size_t q : barriers)
            if (q > lo && q < hi) { separated = true; break; }
          if (!separated) {
            errorAt(toks_[mods[a].pos],
                    "multiple unsequenced modifications to '" + mods[a].var +
                        "' in one expression");
            a = mods.size(); // one diagnostic per full expression
            break;
          }
        }
      start = end + 1;
    }
  }

  /// `* (Type *) &x` type punning is undefined under strict aliasing (except
  /// for character types, which C explicitly allows).
  void checkTypePun(size_t i) {
    if (!isPunct(i, "*"))
      return;
    size_t lp = nextSig(i);
    if (!isPunct(lp, "("))
      return;
    size_t typeIdx = nextSig(lp);
    if (typeIdx >= toks_.size() ||
        toks_[typeIdx].kind != TokKind::Identifier)
      return;
    const std::string &type = toks_[typeIdx].text;
    if (type == "char" || type == "signed" || type == "unsigned" ||
        type == "void")
      return;
    size_t star = nextSig(typeIdx);
    size_t rp = nextSig(star);
    size_t amp = nextSig(rp);
    if (isPunct(star, "*") && isPunct(rp, ")") && isPunct(amp, "&"))
      errorAt(toks_[i],
              "type-punned dereference violates strict aliasing; use memcpy");
  }

  std::string allocatorName(size_t i) const {
    if (i >= toks_.size())
      return std::string();
    if (isPunct(i, "(")) {
      size_t rp = match(i, "(", ")");
      if (rp == kNPos)
        return std::string();
      i = nextSig(rp);
    }
    if (i < toks_.size() && toks_[i].kind == TokKind::Identifier)
      return toks_[i].text;
    return std::string();
  }

  bool isAllocator(const std::string &name) const {
    return name == "cplus_alloc" || name == "cplus_zalloc" ||
           name == "cplus_realloc" || name == "cplus_new" ||
           name == "cplus_new_array" || name == "malloc" ||
           name == "calloc";
  }

  void checkForkThreads() {
    bool hasThreads = false, hasFork = false;
    size_t forkAt = kNPos;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (isIdent(i, "pthread_create")) hasThreads = true;
      if (isIdent(i, "fork")) { hasFork = true; if (forkAt == kNPos) forkAt = i; }
    }
    if (hasThreads && hasFork && forkAt != kNPos)
      errorAt(toks_[forkAt], "fork() in a multi-threaded process can deadlock; "
                             "use posix_spawn or fork before creating threads");
  }

  /// Two `restrict`-qualified parameters of the same call must not refer to
  /// overlapping objects.  Only the parameters the function itself declares
  /// as `restrict` matter: an unrelated function elsewhere in the translation
  /// unit must not turn every call into an error.
  void checkRestrictAliasing() {
    for (const auto &entry : funcs_) {
      const FuncSig &sig = entry.second;
      bool anyRestrict = false;
      for (bool restricted : sig.restrictParam)
        if (restricted)
          anyRestrict = true;
      if (!anyRestrict)
        continue;
      for (size_t i = 0; i < toks_.size(); ++i) {
        if (toks_[i].kind != TokKind::Identifier ||
            toks_[i].text != entry.first)
          continue;
        size_t lp = nextSig(i);
        if (!isPunct(lp, "("))
          continue;
        size_t rp = match(lp, "(", ")");
        if (rp == kNPos || isPunct(nextSig(rp), "{"))
          continue; // the definition itself, not a call
        auto args = splitArgs(lp + 1, rp);
        size_t count = args.size() < sig.restrictParam.size()
                           ? args.size()
                           : sig.restrictParam.size();
        for (size_t a = 0; a < count; ++a) {
          for (size_t b = a + 1; b < count; ++b) {
            // At least one of the two must be restrict-qualified; a pair of
            // unrestricted parameters may legitimately alias.
            if (!sig.restrictParam[a] && !sig.restrictParam[b])
              continue;
            std::string lhs = baseName(args[a].first, args[a].second);
            std::string rhs = baseName(args[b].first, args[b].second);
            if (!lhs.empty() && lhs == rhs)
              errorAt(toks_[i], "call passes overlapping pointers to restrict "
                                "parameters");
          }
        }
      }
    }
  }

  void checkFflushStdin(size_t i) {
    if (!isIdent(i, "fflush")) return;
    size_t lp = nextSig(i);
    size_t arg = nextSig(lp);
    if (isIdent(arg, "stdin"))
      errorAt(toks_[i], "fflush(stdin) is undefined; discard input explicitly");
  }

  /// C99 variable-length arrays used to be rejected outright here.  They are
  /// now *lowered* by the Safe-mode rewriter (instrumentDynamicArrayBounds in
  /// CPlusRewriter.cpp) to a `cplus_alloc_array`/`cplus_release` pair with the
  /// same identifier, so the stack allocation is bounded and deterministically
  /// released.  This pass runs only in Safe mode (see lowerUnit), i.e. exactly
  /// where that lowering is active; LEGACY mode never calls it and therefore
  /// keeps emitting the original plain-C VLA verbatim.  Keep the recognition
  /// former recognition logic (constant-expression exclusions) lived here; it
  /// is intentionally gone because there is nothing left to diagnose.
  /// Rejecting here would undo the lowering and break ordinary C99 code for no
  /// safety benefit.  VLA-*derived* misuse is still caught by the
  /// other checks -- in particular checkVlaSizeof() below rejects a side
  /// effect evaluated by `sizeof` on a VLA type.
  void checkVla(size_t) {
    return;
  }

  /// Only reject `p = realloc(p, ...)` (and bare calls).  Wrappers such as
  /// `return realloc(p, n);` and alias assignments may be perfectly safe.
  void checkRawRealloc(size_t i) {
    size_t lp = nextSig(i);
    if (!isPunct(lp, "("))
      return;
    size_t rp = match(lp, "(", ")");
    if (rp == kNPos)
      return;
    auto args = splitArgs(lp + 1, rp);
    if (args.empty())
      return;
    std::string argText = exprText(args[0].first, args[0].second);
    std::string argBase = baseName(args[0].first, args[0].second);
    size_t eq = kNPos;
    for (size_t k = i; k > 0; --k) {
      if (isPunct(k, "=")) { eq = k; break; }
      if (isPunct(k, ";") || isPunct(k, "{") || isPunct(k, "}"))
        break;
    }
    if (eq != kNPos) {
      // The assigned lvalue is the whole expression before `=`, so that
      // `s->buf = realloc(s->buf, n)` and `arr[i] = realloc(arr[i], n)` are
      // compared as whole lvalues instead of a single trailing identifier.
      size_t lhs = lvalueStart(eq);
      std::string lhsText = exprText(lhs, eq);
      std::string lhsBase = baseName(lhs, eq);
      bool same = lhsText == argText;
      // `p = realloc(p + 1, n)` overwrites the only pointer to the original
      // block too: the plain variable is the base of the reallocated object.
      if (!same && !lhsText.empty() && lhsText == lhsBase && lhsBase == argBase)
        same = true;
      if (same) {
        errorAt(toks_[i], "raw realloc can lose the original block on failure; "
                          "use cplus_realloc");
        return;
      }
    }
    size_t prev = i == 0 ? kNPos : i - 1;
    while (prev != kNPos && trivia(prev)) {
      if (prev == 0) { prev = kNPos; break; }
      --prev;
    }
    if (prev == kNPos || isPunct(prev, ";") || isPunct(prev, "{") ||
        isPunct(prev, "}"))
      errorAt(toks_[i], "raw realloc return value is ignored");
  }

  void checkNanCompare(size_t i) {
    if (!isIdent(i, "NAN"))
      return;
    size_t before = i == 0 ? kNPos : i - 1;
    while (before != kNPos && trivia(before)) {
      if (before == 0) { before = kNPos; break; }
      --before;
    }
    size_t after = nextSig(i);
    bool equality = (before != kNPos && (isPunct(before, "==") ||
                                          isPunct(before, "!="))) ||
                    isPunct(after, "==") || isPunct(after, "!=");
    if (equality)
      errorAt(toks_[i], "NaN is never equal to itself; use isnan()");
  }

  void checkVlaSizeof(size_t i) {
    if (!isIdent(i, "sizeof"))
      return;
    size_t lp = nextSig(i);
    size_t rp = kNPos;
    if (isPunct(lp, "(")) {
      rp = match(lp, "(", ")");
      if (rp == kNPos)
        return;
      bool bracket = false;
      for (size_t k = lp + 1; k < rp; ++k) {
        if (isPunct(k, "[")) bracket = true;
        if (bracket && (isPunct(k, "++") || isPunct(k, "--")))
          errorAt(toks_[i], "sizeof on a VLA evaluates the expression at run time");
      }
    } else if (toks_[lp].kind != TokKind::Identifier) {
      return; // `sizeof(type)` without parentheses is not a VLA name here
    }
    // `int a[n++]; size_t s = sizeof a;` has the same run-time side effect,
    // but the increment lives in the declaration, not inside sizeof. Handle
    // both `sizeof a` and `sizeof(a)`.
    size_t id = isPunct(lp, "(") ? nextSig(lp) : lp;
    if (isPunct(lp, "(") && (id >= rp || toks_[id].kind != TokKind::Identifier ||
                             nextSig(id) != rp))
      return;
    const std::string &name = toks_[id].text;
    for (size_t k = id; k > 0; --k) {
      size_t decl = k - 1;
      if (toks_[decl].kind != TokKind::Identifier || toks_[decl].text != name)
        continue;
      size_t ob = nextSig(decl);
      if (!isPunct(ob, "["))
        continue;
      size_t cb = match(ob, "[", "]");
      if (cb == kNPos)
        continue;
      for (size_t j = ob + 1; j < cb; ++j)
        if (isPunct(j, "++") || isPunct(j, "--")) {
          errorAt(toks_[i],
                  "sizeof on a VLA evaluates the expression at run time");
          return;
        }
      break;
    }
  }

  void checkZeroAlloc(size_t i) {
    if (!(isIdent(i, "malloc") || isIdent(i, "calloc") ||
          isIdent(i, "realloc")))
      return;
    size_t lp = nextSig(i);
    if (!isPunct(lp, "("))
      return;
    size_t rp = match(lp, "(", ")");
    if (rp == kNPos)
      return;
    auto args = splitArgs(lp + 1, rp);
    // realloc(ptr, size): only the second argument is a size; realloc(NULL,n)
    // is a valid allocation, and realloc(p,0) is a separate documented case.
    if (isIdent(i, "realloc")) {
      // realloc(NULL, n) is a valid allocation; realloc(p, 0) is the
      // implementation-defined zero-size case and stays rejected.
      if (args.size() > 1) {
        long value = 0;
        if (constantFactor(args[1].first, args[1].second, &value) && value == 0)
          errorAt(toks_[i], "zero-size allocation is implementation-defined; "
                            "use cplus_alloc");
      }
    } else {
      for (const auto &arg : args) {
        long value = 0;
        if (constantFactor(arg.first, arg.second, &value) && value == 0)
          errorAt(toks_[i], "zero-size allocation is implementation-defined; "
                            "use cplus_alloc");
      }
    }
  }

  /// Casting away const and then writing through the result is undefined for
  /// an originally const object.  A plain cast (Lua-style ABI casts) is fine.
  void checkConstCastMutation() {
    std::set<std::string> globalConstObjects;
    int depth = 0;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (isPunct(i, "{")) { ++depth; continue; }
      if (isPunct(i, "}")) { --depth; continue; }
      if (depth != 0 || !isIdent(i, "const"))
        continue;
      size_t end = i;
      while (end < toks_.size() && !isPunct(end, ";") &&
             !isPunct(end, "{") && !isPunct(end, ","))
        ++end;
      for (size_t k = i + 1; k < end; ++k)
        if (isPunct(k, "=")) { end = k; break; }
      bool pointer = false;
      std::string name;
      for (size_t k = i + 1; k < end; ++k) {
        if (isPunct(k, "*"))
          pointer = true;
        if (toks_[k].kind == TokKind::Identifier &&
            !isTypeWord(toks_[k].text) && toks_[k].text != "const" &&
            toks_[k].text != "volatile")
          name = toks_[k].text;
      }
      if (!pointer && !name.empty())
        globalConstObjects.insert(name);
    }

    std::set<std::string> constObjects;
    std::set<std::string> constPointers;
    std::set<std::string> castPtrs;
    auto declarationEnd = [&](size_t from) {
      size_t end = from;
      int parens = 0, brackets = 0;
      while (end < toks_.size()) {
        if (isPunct(end, "(")) ++parens;
        else if (isPunct(end, ")")) {
          if (parens == 0) return end;
          --parens;
        } else if (isPunct(end, "[")) ++brackets;
        else if (isPunct(end, "]")) --brackets;
        else if (parens == 0 && brackets == 0 &&
                 (isPunct(end, ";") || isPunct(end, ",") ||
                  isPunct(end, "{")))
          return end;
        ++end;
      }
      return end;
    };

    for (size_t i = 0; i < toks_.size(); ++i) {
      if (functionOpens_.count(i)) {
        constObjects.clear();
        constPointers.clear();
        castPtrs.clear();
        for (const auto &entry : funcs_) {
          if (entry.second.bodyOpen != i)
            continue;
          for (const auto &param : entry.second.paramDeclPos) {
            size_t begin = param.second;
            while (begin > 0) {
              --begin;
              if (isPunct(begin, "(") || isPunct(begin, ","))
                break;
            }
            bool isConst = false, pointer = false;
            for (size_t k = begin + 1; k < param.second; ++k) {
              if (isIdent(k, "const")) isConst = true;
              if (isPunct(k, "*")) pointer = true;
            }
            if (isConst)
              (pointer ? constPointers : constObjects).insert(param.first);
          }
        }
      }
      if (!isIdent(i, "const"))
        continue;
      size_t end = declarationEnd(i);
      for (size_t k = i + 1; k < end; ++k)
        if (isPunct(k, "=")) { end = k; break; }
      bool pointer = false;
      std::string name;
      for (size_t k = i + 1; k < end; ++k) {
        if (isPunct(k, "*"))
          pointer = true;
        if (toks_[k].kind == TokKind::Identifier &&
            !isTypeWord(toks_[k].text) && toks_[k].text != "const" &&
            toks_[k].text != "volatile")
          name = toks_[k].text;
      }
      if (!name.empty())
        (pointer ? constPointers : constObjects).insert(name);
      i = end == 0 ? i : end - 1;
    }

    auto isConstOperand = [&](size_t from, size_t to) {
      to = stmtEnd(from);
      from = stripCast(from, to);
      trim(&from, &to);
      if (isPunct(from, "&")) {
        size_t id = nextSig(from);
        if (id < to && toks_[id].kind == TokKind::Identifier &&
            (globalConstObjects.count(toks_[id].text) ||
             constObjects.count(toks_[id].text)))
          return true;
      }
      if (from < to && toks_[from].kind == TokKind::Identifier &&
          constPointers.count(toks_[from].text))
        return true;
      return false;
    };

    for (size_t i = 0; i < toks_.size(); ++i) {
      if (functionOpens_.count(i)) {
        castPtrs.clear();
        continue;
      }
      if (!isPunct(i, "("))
        continue;
      size_t close = kNPos;
      if (!castClose(i, toks_.size(), &close))
        continue;
      size_t operand = nextSig(close);
      if (operand >= toks_.size() || !isConstOperand(operand, stmtEnd(operand)))
        continue;
      size_t after = stmtEnd(operand);
      while (after > operand && trivia(after - 1))
        --after;
      if (isPunct(prevSig(i), "*") && after < toks_.size() &&
          (isPunct(after, "=") || isPunct(after, "+=") ||
           isPunct(after, "-=") || isPunct(after, "++") ||
           isPunct(after, "--")))
        errorAt(toks_[i], "casting away const and writing to the original "
                          "object is undefined");
    }

    for (size_t i = 0; i < toks_.size(); ++i) {
      if (functionOpens_.count(i)) {
        castPtrs.clear();
        continue;
      }
      if (isPunct(i, "=")) {
        size_t lhs = prevSig(i);
        size_t rhs = nextSig(i);
        if (lhs == kNPos || rhs >= toks_.size() ||
            toks_[lhs].kind != TokKind::Identifier)
          continue;
        bool derivesFromConst = false;
        if (toks_[rhs].kind == TokKind::Identifier && castPtrs.count(toks_[rhs].text))
          derivesFromConst = true;
        if (isPunct(rhs, "(")) {
          size_t close = kNPos;
          if (castClose(rhs, toks_.size(), &close)) {
            bool pointer = false;
            for (size_t k = rhs + 1; k < close; ++k)
              if (isPunct(k, "*")) pointer = true;
            size_t operand = nextSig(close);
            derivesFromConst = pointer && operand < toks_.size() &&
                isConstOperand(operand, stmtEnd(operand));
          }
        }
        if (derivesFromConst)
          castPtrs.insert(toks_[lhs].text);
        else
          castPtrs.erase(toks_[lhs].text);
        continue;
      }
      if (toks_[i].kind != TokKind::Identifier ||
          !castPtrs.count(toks_[i].text))
        continue;
      size_t prev = prevSig(i);
      size_t next = nextSig(i);
      bool derefWrite = isPunct(prev, "*") &&
          (isPunct(next, "=") || isPunct(next, "+=") ||
           isPunct(next, "-=") || isPunct(next, "++") ||
           isPunct(next, "--"));
      bool indexWrite = false;
      if (isPunct(next, "[")) {
        size_t rb = match(next, "[", "]");
        if (rb != kNPos) {
          size_t after = nextSig(rb);
          indexWrite = isPunct(after, "=") || isPunct(after, "+=") ||
                       isPunct(after, "-=") || isPunct(after, "++") ||
                       isPunct(after, "--");
        }
      }
      if (derefWrite || indexWrite)
        errorAt(toks_[i], "casting away const and writing to the original "
                          "object is undefined");
    }
  }

  void checkNegativeComparison() {
    std::set<std::string> negativeNames;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (functionOpens_.count(i))
        negativeNames.clear();
      if (toks_[i].kind != TokKind::Identifier)
        continue;
      if (isPunct(nextSig(i), "=")) {
        size_t value = nextSig(nextSig(i));
        if (isPunct(value, "-")) {
          long ignored = 0;
          if (number(toks_[nextSig(value)], &ignored))
            negativeNames.insert(toks_[i].text);
        }
      }
      if (!negativeNames.count(toks_[i].text))
        continue;
      size_t op = nextSig(i);
      if (isPunct(op, "<") || isPunct(op, "<=") || isPunct(op, ">") ||
          isPunct(op, ">=")) {
        size_t rhs = nextSig(op);
        if (rhs < toks_.size() && toks_[rhs].kind == TokKind::Identifier)
          errorAt(toks_[i], "comparison of a negative value with an unsigned "
                            "operand is always false or true");
      }
    }
  }

  void checkMacroStatements() {
    std::istringstream in(source_);
    std::string line;
    unsigned lineNo = 0;
    while (std::getline(in, line)) {
      ++lineNo;
      // Strip comments and string literals: a ';' inside either is not a
      // macro statement.
      std::string code;
      bool inComment = false;
      for (size_t k = 0; k < line.size(); ++k) {
        if (inComment) {
          if (line[k] == '*' && k + 1 < line.size() && line[k + 1] == '/') {
            inComment = false;
            ++k;
          }
          continue;
        }
        if (line[k] == '/' && k + 1 < line.size() && line[k + 1] == '*') {
          inComment = true;
          ++k;
          continue;
        }
        code += line[k];
      }
      // A ';' inside a string/char literal is not a statement.  Skipping
      // such macro lines avoids false positives (e.g. Lua's DLMSG).
      if (code.find('"') != std::string::npos ||
          code.find('\'') != std::string::npos)
        continue;
      // A single `if (...) a; else b;` is one statement despite the ';'.
      if (code.find("if ") != std::string::npos ||
          code.find("if(") != std::string::npos ||
          code.find("else") != std::string::npos)
        continue;
      size_t def = code.find("#define");
      if (def == std::string::npos)
        continue;
      // A multi-statement macro is only unsafe when a ';' appears at the
      // replacement list's top level.  Statements inside { ... }, do { } while
      // or an if/else body are already grouped.
      int paren = 0, brace = 0;
      bool unsafe = false;
      for (size_t k = def + 7; k < code.size(); ++k) {
        char c = code[k];
        if (c == '(') ++paren;
        else if (c == ')') --paren;
        else if (c == '{') ++brace;
        else if (c == '}') --brace;
        else if (c == ';' && paren == 0 && brace == 0) unsafe = true;
      }
      if (!unsafe)
        continue;
      if (code.find("do {") != std::string::npos &&
          code.find("while (0)") != std::string::npos)
        continue;
      Diag d;
      d.level = DiagLevel::Error;
      d.message = "multi-statement macro must be wrapped in do { ... } while (0)";
      d.file = filename_;
      d.line = lineNo;
      d.column = 1;
      d.help = "wrap the macro body to keep if/else binding correct";
      diags_.report(std::move(d));
    }
  }

  //--- Constant index / one-past / multi-dimensional array rules ------------//

  /// Evaluate a subscript expression only when it is a provable compile-time
  /// integer.  Accepts a signed literal (`-1`, `+2`, `0x10`) or a plain name
  /// already known to hold a negative value from the conservative bug-42
  /// facts.  Anything else returns false so no diagnostic is produced.
  bool constantIndexAt(size_t from, size_t to, long *out) const {
    trim(&from, &to);
    if (from >= to)
      return false;
    long value = 0;
    if (indexExprValue(from, to, &value)) {
      *out = value;
      return true;
    }
    // A name proved negative elsewhere is enough to establish the sign; use
    // -1 as a conservative representative (the rule only needs "< 0").
    if (from + 1 == to && toks_[from].kind == TokKind::Identifier) {
      if (isVolatileName(toks_[from].text))
        return false; // volatile sign is not a compile-time fact
      const std::set<std::string> *neg = negativeFactsAt(from);
      if (neg && neg->count(toks_[from].text) &&
          !guardedNegative(from, toks_[from].text)) {
        *out = -1;
        return true;
      }
    }
    return false;
  }

  /// Name declared by an `int (*r)[W]` LHS of an assignment.  The token just
  /// before `=` is `]`, so the simple identifier case does not find it.
  size_t rowPointerLhs(size_t eq) const {
    size_t closeBracket = prevSig(eq);
    if (closeBracket == kNPos || !isPunct(closeBracket, "]"))
      return kNPos;
    int depth = 0;
    size_t lb = closeBracket;
    for (;;) {
      if (isPunct(lb, "]")) ++depth;
      else if (isPunct(lb, "[") && --depth == 0) break;
      if (lb == 0) return kNPos;
      --lb;
    }
    size_t rp = prevSig(lb);
    if (rp == kNPos || !isPunct(rp, ")"))
      return kNPos;
    depth = 0;
    size_t lp = rp;
    for (;;) {
      if (isPunct(lp, ")")) ++depth;
      else if (isPunct(lp, "(") && --depth == 0) break;
      if (lp == 0) return kNPos;
      --lp;
    }
    // A declarator is preceded by a type (`int (*r)[W]`); a subscript such as
    // `(*pp)[9] = ...` is not, and must never be mistaken for a new alias
    // assignment that would invalidate pp.
    size_t beforeType = prevSig(lp);
    if (beforeType == kNPos || toks_[beforeType].kind != TokKind::Identifier ||
        !isTypeWord(toks_[beforeType].text))
      return kNPos;
    size_t star = nextSig(lp);
    if (!isPunct(star, "*"))
      return kNPos;
    size_t name = nextSig(star);
    if (name < rp && toks_[name].kind == TokKind::Identifier)
      return name;
    return kNPos;
  }

  /// True when the identifier at `lhs` is the *object* of a write-through
  /// (`*p = v`, `s.p = v`) rather than the variable being assigned.  A
  /// declaration `T *p = ...` is distinguished by its type-word/comma/`(`/
  /// `*` predecessor, so it is still recognised as a real write to `p`.
  bool isWriteThroughObject(size_t lhs) const {
    size_t prev = prevSig(lhs);
    if (isPunct(prev, ".") || isPunct(prev, "->")) return true;
    if (isPunct(prev, "*")) {
      size_t before = prevSig(prev);
      if (before == kNPos) return true;
      if ((toks_[before].kind == TokKind::Identifier &&
           isTypeWord(toks_[before].text)) ||
          isPunct(before, ",") || isPunct(before, "(") || isPunct(before, "*"))
        return false;
      return true;
    }
    return false;
  }

  /// Parse an initialiser `a`, `a + N`, `a - N`, `&a[N]`, or another tracked
  /// pointer (optionally offset) into a constant-offset alias of `a`.
  bool parseArrayPointerInit(size_t rhs, TrackedArrayPointer *out) const {
    if (rhs >= toks_.size())
      return false;
    size_t end = stmtEnd(rhs);
    rhs = stripCast(rhs, end);
    if (rhs >= toks_.size())
      return false;
    if (isPunct(rhs, "&")) {
      size_t id = nextSig(rhs);
      if (id >= toks_.size() || toks_[id].kind != TokKind::Identifier)
        return false;
      // `int **pp = &p`: keep p's proven offset through the indirection.
      auto alias = arrayPtrs_.find(toks_[id].text);
      if (alias != arrayPtrs_.end()) {
        *out = alias->second;
        out->pos = 0;
        return true;
      }
      size_t lb = nextSig(id);
      if (!isPunct(lb, "["))
        return false;
      const std::vector<size_t> *dims = nullptr;
      if (!findShape(toks_[id].text, rhs, &dims) || dims->empty())
        return false;
      size_t rb = match(lb, "[", "]");
      long idx = 0;
      if (rb == kNPos || !constantIndexAt(lb + 1, rb, &idx))
        return false;
      out->array = toks_[id].text;
      out->dims = *dims;
      out->offset = idx;
      return true;
    }
    // `int *p = s.p` / `int *p = s.a`: follow a struct/union field to the
    // array (or pointer alias) previously stored in that field.
    if (rhs + 2 < toks_.size() && toks_[rhs].kind == TokKind::Identifier &&
        (isPunct(rhs + 1, ".") || isPunct(rhs + 1, "->")) &&
        toks_[rhs + 2].kind == TokKind::Identifier) {
      const std::string &field = toks_[rhs + 2].text;
      auto fit = arrayPtrs_.find(field);
      if (fit != arrayPtrs_.end()) {
        *out = fit->second;
        out->pos = 0;
        return true;
      }
      const std::vector<size_t> *dims = nullptr;
      if (findShape(field, rhs, &dims) && !dims->empty()) {
        out->array = field;
        out->dims = *dims;
        out->offset = 0;
        return true;
      }
      // Wave 4 (red finding): `int *p = s.a;` where `a` is a fixed array
      // field.  Without this the alias is invisible and a call-site-bounded
      // `p[i]` (the canary overwrites) stays uninstrumented and unrejected.
      auto vit = varStruct_.find(toks_[rhs].text);
      if (vit != varStruct_.end()) {
        auto sit = structFields_.find(vit->second);
        if (sit != structFields_.end())
          for (const auto &f : sit->second)
            if (f.name == field && f.extent > 0) {
              out->array = toks_[rhs].text + "." + field;
              out->dims.push_back(f.extent);
              out->offset = 0;
              return true;
            }
      }
    }
    if (toks_[rhs].kind != TokKind::Identifier)
      return false;
    const std::string &name = toks_[rhs].text;
    size_t after = nextSig(rhs);
    if (isPunct(after, "+") || isPunct(after, "-")) {
      long amount = 0;
      if (!constantIndexAt(after + 1, end, &amount))
        return false;
      out->offset = isPunct(after, "+") ? amount : -amount;
    }
    // A direct array name, or an already tracked pointer alias.
    const std::vector<size_t> *dims = nullptr;
    long inherited = 0;
    if (findShape(name, rhs, &dims)) {
      inherited = 0;
    } else {
      auto it = arrayPtrs_.find(name);
      if (it == arrayPtrs_.end())
        return false;
      dims = &it->second.dims;
      inherited = it->second.offset;
      out->array = it->second.array;
      out->rowPointer = it->second.rowPointer;
    }
    if (dims->empty())
      return false;
    out->dims = *dims;
    if (out->array.empty())
      out->array = name;
    out->offset += inherited;
    return true;
  }

  /// Rule: only a single, fully constant assignment makes a pointer a
  /// constant-offset alias of a fixed array.  A second write (or an increment)
  /// invalidates the fact, so the offset can never be stale at a use site.
  void collectArrayPointers() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isPunct(i, "="))
        continue;
      size_t lhs = prevSig(i);
      bool rowPointer = false;
      if (lhs == kNPos || toks_[lhs].kind != TokKind::Identifier) {
        lhs = rowPointerLhs(i);
        rowPointer = lhs != kNPos;
      }
      if (lhs == kNPos || isWriteThroughObject(lhs))
        continue;
      const std::string var = toks_[lhs].text;
      TrackedArrayPointer tp;
      if (!parseArrayPointerInit(nextSig(i), &tp)) {
        arrayPtrInvalid_.insert(var);
        arrayPtrs_.erase(var);
        continue;
      }
      if (arrayPtrInvalid_.count(var) || arrayPtrs_.count(var)) {
        // Wave 2 (chained formation): `p = p + 6` moves the same pointer, it
        // does not reassign a new provenance.  Fold the constant offset into
        // the existing alias instead of invalidating it, so a one-past
        // dereference after a chain remains provable.  Any other second write
        // still invalidates the fact exactly as before.
        auto existing = arrayPtrs_.find(var);
        if (existing != arrayPtrs_.end() && !arrayPtrInvalid_.count(var)) {
          size_t rhs = nextSig(i);
          if (rhs + 2 < toks_.size() && toks_[rhs].kind == TokKind::Identifier &&
              toks_[rhs].text == var &&
              (isPunct(rhs + 1, "+") || isPunct(rhs + 1, "-"))) {
            long amount = 0;
            if (indexExprValue(rhs + 2, stmtEnd(rhs + 2), &amount)) {
              existing->second.offset += isPunct(rhs + 1, "+") ? amount : -amount;
              existing->second.pos = i; // recorded at the `=` token
              continue;
            }
          }
        }
        arrayPtrInvalid_.insert(var);
        arrayPtrs_.erase(var);
        continue;
      }
      tp.pos = i;
      tp.rowPointer = rowPointer;
      arrayPtrs_[var] = tp;
    }
    // Fold a constant increment chain (`p++; p++; ...`) instead of dropping
    // the alias: a one-past dereference after four increments is provable.
    // Any write we cannot evaluate invalidates the fact as before.
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier)
        continue;
      const std::string &var = toks_[i].text;
      auto initial = arrayPtrs_.find(var);
      if (initial == arrayPtrs_.end())
        continue;
      size_t n = nextSig(i);
      // The fact was recorded at the `=` token, not at the LHS identifier.
      if (n == initial->second.pos)
        continue;
      // A write *through* the pointer (`*p = v`, `s.p = v`) is not a write to
      // the pointer variable itself; ignoring that distinction used to erase
      // a valid alias before the very dereference we need to check.
      if (isWriteThroughObject(i))
        continue;
      long delta = 0;
      bool known = false;
      if (isPunct(n, "++")) { delta = 1; known = true; }
      else if (isPunct(n, "--")) { delta = -1; known = true; }
      else if (isPunct(n, "+=") || isPunct(n, "-=")) {
        long v = 0;
        size_t from = nextSig(n);
        if (constantIndexAt(from, stmtEnd(from), &v)) {
          delta = isPunct(n, "+=") ? v : -v;
          known = true;
        }
      } else if (isPunct(n, "=")) {
        // Every `=` was already classified by the first loop above (tracked,
        // self-relative fold, or invalidated).  Re-handling it here used to
        // erase a freshly folded alias at its own declaration's `=`.
        continue;
      } else {
        continue;
      }
      auto it = arrayPtrs_.find(var);
      if (it == arrayPtrs_.end())
        continue;
      if (known)
        it->second.offset += delta;
      else {
        arrayPtrInvalid_.insert(var);
        arrayPtrs_.erase(it);
      }
    }
  }

  /// True when `from` sits inside `sizeof(...)`, whose operand is unevaluated.
  bool inUnevaluatedContext(size_t from) const {
    int paren = 0;
    for (size_t k = from; k > 0;) {
      --k;
      if (isPunct(k, ")")) {
        ++paren;
      } else if (isPunct(k, "(")) {
        if (paren == 0)
          return isIdent(prevSig(k), "sizeof");
        --paren;
      } else if (isPunct(k, ";") || isPunct(k, "{") || isPunct(k, "}")) {
        return false;
      }
    }
    return false;
  }

  /// `int **pp` parameters bound to `&p` at a call site, where `p` is a
  /// tracked constant-offset alias of a fixed array.  Dereferencing `pp`
  /// yields that array, so `(*pp)[k]` can be bounds-checked with the array's
  /// extent.  Only call sites that pass the address of a *provable* array
  /// alias are recorded, so an unknown inner pointer stays silent.
  std::map<std::string, TrackedArrayPointer> ptr2Params_;

  void collectPointerToPointerParams() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier) continue;
      auto fit = funcs_.find(toks_[i].text);
      if (fit == funcs_.end()) continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "(")) continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos || isPunct(nextSig(rp), "{")) continue;
      auto args = splitArgs(lp + 1, rp);
      const auto &params = fit->second.params;
      for (size_t p = 0; p < params.size() && p < args.size(); ++p) {
        size_t a = args[p].first;
        if (!isPunct(a, "&")) continue; // only `wr(&p)` aliases are tracked
        size_t name = nextSig(a);
        if (name >= args[p].second || toks_[name].kind != TokKind::Identifier)
          continue;
        auto it = arrayPtrs_.find(toks_[name].text);
        if (it == arrayPtrs_.end() || it->second.dims.empty()) continue;
        TrackedArrayPointer tp = it->second;
        tp.pos = 0; // a parameter fact is visible throughout the callee
        auto prior = ptr2Params_.find(params[p]);
        if (prior != ptr2Params_.end() &&
            (prior->second.array != tp.array || prior->second.dims != tp.dims))
          ptr2Params_.erase(prior); // conflicting extents: forget the fact
        else
          ptr2Params_[params[p]] = tp;
      }
    }
  }

  void checkPointerToPointerSubscript(size_t i) {
    auto it = ptr2Params_.find(toks_[i].text);
    if (it == ptr2Params_.end()) return;
    // `(*pp)[k]`: the identifier is the operand of `*`, and the subscript
    // follows the closing parenthesis of the dereference.
    if (!isPunct(prevSig(i), "*") || !isPunct(nextSig(i), ")")) return;
    size_t lb = nextSig(nextSig(i));
    if (!isPunct(lb, "[")) return;
    size_t rb = match(lb, "[", "]");
    if (rb == kNPos) return;
    long idx = 0;
    if (!constantIndexAt(lb + 1, rb, &idx)) return;
    long absolute = it->second.offset + idx;
    long long total = 1;
    for (size_t d : it->second.dims) total *= static_cast<long long>(d);
    if (absolute < 0 || absolute >= total)
      errorAtSafeBounds(toks_[lb], "array index " + std::to_string(idx) +
          " reaches past array '" + it->second.array +
          "' via pointer-to-pointer parameter '" + toks_[i].text + "'");
  }

  //===--------------------------------------------------------------------===//
  // Wave 4 — call-site-bounded pointer indices (red-team spatial finding).
  //
  // Rationale: the red suite drives OOB through a pointer alias with a
  // non-constant index (`void w(int i){ int a[4]; int *p=a; p[i]=777; } w(4);`,
  // `*(p+i)`, VLA aliases, realloc shrink, ptr-to-ptr, row pointers).  The
  // index is not a literal, so the constant-only checks stay silent, yet a
  // single call site proves the exact value.  These rules fire ONLY when a)
  // the base extent is provable (stack/heap/VLA/allocation/array alias) and
  // b) the index is provably bounded by call-site constants.  A genuinely
  // runtime-unknown index (getchar/argv/volatile) yields no bound here, so
  // no valid C is blanked; the Rewriter owner wires the runtime guard for
  // that case.  Guard-suppressed accesses stay silent.
  //===--------------------------------------------------------------------===//

  /// Wave 4 regression fix: a volatile-qualified object's initializer is a
  /// runtime value, never a constant fact.  Record every declarator named in
  /// a `volatile` declaration so no folding site (local constants, phi,
  /// novel indices, call-site bounds) can turn it into a compile-time bound.
  std::set<std::string> volatileNames_;

  void collectVolatileNames() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isIdent(i, "volatile"))
        continue;
      size_t k = nextSig(i);
      while (k < toks_.size() && !isPunct(k, ";") && !isPunct(k, "{") &&
             !isPunct(k, "}")) {
        if (toks_[k].kind == TokKind::Identifier &&
            !isTypeWord(toks_[k].text) && !isIdent(k, "volatile")) {
          volatileNames_.insert(toks_[k].text);
          int paren = 0, bracket = 0;
          while (k < toks_.size()) {
            if (isPunct(k, "(")) ++paren;
            else if (isPunct(k, ")")) --paren;
            else if (isPunct(k, "[")) ++bracket;
            else if (isPunct(k, "]")) --bracket;
            else if (paren == 0 && bracket == 0 &&
                     (isPunct(k, ",") || isPunct(k, ";")))
              break;
            ++k;
          }
          ++k; // step past ',' to the next declarator (or ';' out of loop)
          continue;
        }
        ++k;
      }
    }
  }

  bool isVolatileName(const std::string &name) const {
    return volatileNames_.count(name) != 0;
  }

  void collectParamConstBounds() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier)
        continue;
      auto fit = funcs_.find(toks_[i].text);
      if (fit == funcs_.end())
        continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "("))
        continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos || isPunct(nextSig(rp), "{"))
        continue; // a definition, not a call
      auto args = splitArgs(lp + 1, rp);
      const auto &params = fit->second.params;
      for (size_t p = 0; p < params.size() && p < args.size(); ++p) {
        // A conditional argument is folded to its MAX by indexExprValue;
        // using that as a proven bound could reject the always-taken safe
        // branch, so leave the guard/runtime path to handle it.
        bool conditional = false;
        for (size_t k = args[p].first; k < args[p].second; ++k) {
          if (isPunct(k, "?")) conditional = true;
          if (toks_[k].kind == TokKind::Identifier &&
              isVolatileName(toks_[k].text))
            conditional = true; // volatile arg is not a constant bound
        }
        if (conditional)
          continue;
        long v = 0;
        if (!indexExprValue(args[p].first, args[p].second, &v))
          continue;
        auto &seen = paramConstSeen_[toks_[i].text];
        auto &mx = paramConstMax_[toks_[i].text][p];
        if (!seen.count(p)) {
          mx = v;
          paramConstUnique_[toks_[i].text][p] = v;
          seen.insert(p);
        } else {
          if (v > mx)
            mx = v;
          auto uit = paramConstUnique_[toks_[i].text].find(p);
          if (uit != paramConstUnique_[toks_[i].text].end() && uit->second != v)
            paramConstUnique_[toks_[i].text].erase(uit);
        }
      }
    }
  }

  /// Upper bound of an index expression whose value comes from a call-site
  /// constant parameter (bare parameter or parameter +/- constant delta).
  /// Constants and genuinely dynamic expressions return false: the former
  /// are handled by the existing literal checks (avoiding duplicate
  /// diagnostics), the latter by the Rewriter's runtime guard.
  bool paramIndexUpperBound(size_t from, size_t to, long *high) const {
    size_t f = from, t = to;
    trim(&f, &t);
    if (f >= t)
      return false;
    const std::string *fn = enclosingFunction(f);
    if (!fn)
      return false;
    auto fit = paramConstMax_.find(*fn);
    if (fit == paramConstMax_.end())
      return false;
    const FuncSig &sig = funcs_.at(*fn);
    size_t paramTok = kNPos, paramPos = kNPos;
    for (size_t k = f; k < t; ++k) {
      if (toks_[k].kind != TokKind::Identifier)
        continue;
      for (size_t p = 0; p < sig.params.size(); ++p)
        if (sig.params[p] == toks_[k].text) {
          paramTok = k;
          paramPos = p;
          break;
        }
      if (paramTok != kNPos)
        break;
    }
    if (paramTok == kNPos)
      return false;
    auto pit = fit->second.find(paramPos);
    if (pit == fit->second.end())
      return false;
    long base = pit->second;
    if (f + 1 == t) {
      *high = base;
      return true;
    }
    if (paramTok == f && f + 2 < t &&
        (isPunct(f + 1, "+") || isPunct(f + 1, "-"))) {
      long d = 0;
      if (!indexExprValue(f + 2, t, &d))
        return false;
      *high = isPunct(f + 1, "+") ? base + d : base - d;
      return true;
    }
    if (paramTok + 1 < t && isPunct(paramTok, "+")) {
      long d = 0;
      if (!indexExprValue(paramTok + 2, t, &d))
        return false;
      *high = base + d;
      return true;
    }
    if (paramTok > f && isPunct(paramTok - 1, "+")) {
      long d = 0;
      if (!indexExprValue(f, paramTok - 1, &d))
        return false;
      *high = base + d;
      return true;
    }
    return false;
  }

  /// Provable element count and constant offset behind a subscript base at
  /// `dim`: tracked alias, pointer-to-pointer parameter, fixed/heap/VLA
  /// shape, or a pointer parameter whose extent a call site supplied.
  bool baseExtentForCheck(size_t pos, const std::string &name, size_t dim,
                          long *count, long *offset) const {
    *offset = 0;
    auto fromAlias = [&](const TrackedArrayPointer &tp) {
      if (tp.dims.empty())
        return false;
      if (tp.rowPointer) {
        if (dim >= tp.dims.size())
          return false;
        *count = static_cast<long>(tp.dims[dim]);
        *offset = (dim == 0) ? tp.offset : 0;
      } else {
        long long total = 1;
        for (size_t d : tp.dims)
          total *= static_cast<long long>(d);
        *count = static_cast<long>(total);
        *offset = tp.offset;
      }
      return *count > 0;
    };
    auto it = arrayPtrs_.find(name);
    if (it != arrayPtrs_.end() && pos >= it->second.pos &&
        fromAlias(it->second))
      return true;
    auto pt = ptr2Params_.find(name);
    if (pt != ptr2Params_.end() && fromAlias(pt->second))
      return true;
    const std::vector<size_t> *dims = nullptr;
    if (findShape(name, pos, &dims) && !dims->empty()) {
      *count = static_cast<long>((*dims)[std::min(dim, dims->size() - 1)]);
      return *count > 0;
    }
    const std::string *fn = enclosingFunction(pos);
    if (!fn)
      return false;
    auto eit = paramExtents_.find(*fn);
    if (eit == paramExtents_.end())
      return false;
    const auto &params = funcs_.at(*fn).params;
    for (size_t p = 0; p < params.size(); ++p)
      if (params[p] == name) {
        auto e = eit->second.find(p);
        if (e == eit->second.end() || e->second == 0)
          return false;
        *count = static_cast<long>(e->second);
        return true;
      }
    return false;
  }

  /// An explicit `if (idx >= X) return;`-style guard folded through local or
  /// call-site constants makes the continuing index provably bounded.
  bool hasFoldedUpperGuard(const FuncSig &sig, const std::string &var,
                           size_t at, long extent) const {
    if (hasUpperGuard(sig, var, at, extent))
      return true;
    for (size_t k = sig.bodyOpen; k < at; ++k) {
      if (!isIdent(k, "if"))
        continue;
      size_t lp = nextSig(k);
      if (!isPunct(lp, "("))
        continue;
      size_t rp = matchIn(lp, at, "(", ")");
      if (rp == kNPos)
        continue;
      size_t v = kNPos;
      for (size_t q = lp + 1; q < rp; ++q)
        if (toks_[q].kind == TokKind::Identifier && toks_[q].text == var) {
          v = q;
          break;
        }
      if (v == kNPos)
        continue;
      size_t op = nextSig(v);
      size_t nv = nextSig(op);
      if (nv >= rp || !(isPunct(op, ">=") || isPunct(op, ">")))
        continue;
      long n = 0;
      if (!indexExprValue(nv, rp, &n))
        continue;
      size_t body = nextSig(rp);
      size_t end = isPunct(body, "{") ? matchIn(body, at, "{", "}")
                                      : stmtEnd(body);
      if (end == kNPos || end >= at)
        continue;
      for (size_t q = body; q <= end; ++q)
        if (isIdent(q, "return")) {
          long reach = isPunct(op, ">") ? n : n - 1;
          if (reach < extent)
            return true;
        }
    }
    return false;
  }

  /// A byte-like pointee (char/void/8-bit) reinterprets an element count as
  /// a byte count, so the token-level extent is in the wrong unit.  Exclude
  /// such bases from the call-site-index rule to avoid false positives on
  /// valid code (`int a[4]; char *q=(char*)a; q[9];`).  The AST byte-accurate
  /// check still covers literal indices there.
  std::set<std::string> byteCastAliases_;

  void collectByteCastAliases() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isPunct(i, "="))
        continue;
      size_t lhs = prevSig(i);
      if (lhs == kNPos || toks_[lhs].kind != TokKind::Identifier)
        continue;
      size_t r = nextSig(i);
      if (!isPunct(r, "(")) {
        // `char *p = malloc(4*sizeof(int))`: no cast, but the extent folded
        // from the allocation is in ints while p counts bytes.
        size_t star = prevSig(lhs);
        size_t typeTok =
            (star != kNPos && isPunct(star, "*")) ? prevSig(star) : kNPos;
        if (typeTok != kNPos && toks_[typeTok].kind == TokKind::Identifier) {
          const std::string &t = toks_[typeTok].text;
          if (t == "char" || t == "void" || t == "int8_t" || t == "uint8_t")
            byteCastAliases_.insert(toks_[lhs].text);
        }
        continue;
      }
      size_t close = match(r, "(", ")");
      if (close == kNPos)
        continue;
      bool byte = false, star = false;
      for (size_t k = r + 1; k < close; ++k) {
        if (isPunct(k, "*")) star = true;
        if (toks_[k].kind == TokKind::Identifier) {
          const std::string &t = toks_[k].text;
          if (t == "char" || t == "void" || t == "int8_t" || t == "uint8_t")
            byte = true;
        }
      }
      if (star && byte)
        byteCastAliases_.insert(toks_[lhs].text);
    }
  }

  void checkParamIndexSubscript(size_t i) {
    if (inUnevaluatedContext(i))
      return;
    // Formed dereference `*(base + idx)` (or `idx + base`).
    if (isPunct(i, "*")) {
      size_t lp = nextSig(i);
      if (!isPunct(lp, "("))
        return;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos)
        return;
      size_t base = nextSig(lp);
      if (base >= rp || toks_[base].kind != TokKind::Identifier)
        return;
      if (byteCastAliases_.count(toks_[base].text))
        return; // element unit is bytes, not the folded element count
      size_t op = nextSig(base);
      if (!isPunct(op, "+"))
        return; // backward motion cannot be bounded above by a max index
      long high = 0;
      if (!paramIndexUpperBound(op + 1, rp, &high))
        return;
      long extent = 0, offset = 0;
      if (!baseExtentForCheck(base, toks_[base].text, 0, &extent, &offset))
        return;
      // Compare the ABSOLUTE index (offset included): a negative subscript on
      // an interior pointer (`q = a+4; q[-1]`) is a valid earlier element.
      if (high + offset < 0 || high + offset >= extent)
        errorAtSafeBounds(toks_[i], "pointer index " + std::to_string(high) +
            " from call site is out of bounds of '" + toks_[base].text +
            "' of extent " + std::to_string(extent));
      return;
    }
    if (toks_[i].kind != TokKind::Identifier)
      return;
    const std::string baseName = toks_[i].text;
    if (byteCastAliases_.count(baseName))
      return; // byte-granular alias: folded extent is in the wrong unit
    size_t lb = nextSig(i);
    if (isPunct(lb, ")") && isPunct(prevSig(i), "*"))
      lb = nextSig(lb); // `(*q)[k]`: q is tracked in ptr2Params_
    if (!isPunct(lb, "["))
      return;
    // The bracket of a declaration holds the extent, not an index.  In
    // particular `int a[n]` with n call-site-bounded must not be read as
    // `a[n]` OOB.  Reuse the declaration-shape test from checkSubscriptChain.
    if (constantVlaDeclPos_.count(i))
      return;
    size_t close = match(lb, "[", "]");
    if (close != kNPos) {
      size_t after = nextSig(close);
      bool declTail = isPunct(after, ";") || isPunct(after, ",") ||
                      isPunct(after, "=") || isPunct(after, "{");
      size_t prev = prevSig(i);
      bool declHead = isPunct(prev, ",") ||
                      (prev != kNPos && toks_[prev].kind == TokKind::Identifier &&
                       (isTypeWord(toks_[prev].text) ||
                        isIdent(prev, "struct") || isIdent(prev, "union")));
      if (declTail && declHead)
        return;
    }
    long extent = 0, offset = 0;
    if (!baseExtentForCheck(i, baseName, 0, &extent, &offset))
      return;
    const std::string *fn = enclosingFunction(i);
    size_t cursor = lb;
    for (size_t dim = 0; cursor < toks_.size() && isPunct(cursor, "[");
         ++dim) {
      size_t rb = match(cursor, "[", "]");
      if (rb == kNPos)
        break;
      long high = 0;
      if (paramIndexUpperBound(cursor + 1, rb, &high)) {
        long dimExtent = extent, dimOffset = offset;
        if (dim > 0 &&
            !baseExtentForCheck(i, baseName, dim, &dimExtent, &dimOffset))
          break;
        bool guarded = false;
        if (fn)
          for (size_t k = cursor + 1; k < rb; ++k)
            if (toks_[k].kind == TokKind::Identifier) {
              if (hasFoldedUpperGuard(funcs_.at(*fn), toks_[k].text, i,
                                      dimExtent)) {
                guarded = true;
                break;
              }
            }
        if (!guarded &&
            (high + dimOffset < 0 || high + dimOffset >= dimExtent)) {
          errorAtSafeBounds(toks_[cursor],
              "pointer index " + std::to_string(high) +
              " from call site is out of bounds of '" + baseName +
              "' of extent " + std::to_string(dimExtent));
          return;
        }
      }
      cursor = nextSig(rb);
    }
  }

  /// Rules (negative index, one-past pointer, multi-dimensional bounds):
  /// check a dereference or a subscript chain through a tracked alias.  Every
  /// index and every extent used here is a compile-time constant, so valid
  /// code (unknown indices, one-past pointer formation, `p[-1]` back from a
  /// one-past pointer, reassigned pointers) is never rejected.
  void checkTrackedArrayPointer(size_t i) {
    auto it = arrayPtrs_.find(toks_[i].text);
    if (it == arrayPtrs_.end() || i < it->second.pos)
      return;
    if (inUnevaluatedContext(i))
      return;
    const TrackedArrayPointer &tp = it->second;
    if (tp.dims.empty())
      return;

    // A byte-granular alias (char*/void* cast or declaration) counts BYTES,
    // while the tracked dims count elements of the underlying array.  Reuse
    // the same byteCastAliases_ fact as checkParamIndexSubscript and
    // checkPointerToPointerSubscript and convert the extent once, so
    // `int a[4]; char *b=(char*)a; b[4]` stays in bounds while `b[36]` is
    // still rejected.  An unknown element width suppresses the diagnostic
    // rather than guessing; the AST byte-accurate check still covers those
    // sites.  Row pointers keep their row-count unit.
    long long total = 1;
    for (size_t dim : tp.dims)
      total *= static_cast<long long>(dim);
    bool byteUnit = false;
    if (!tp.rowPointer && byteCastAliases_.count(toks_[i].text)) {
      size_t elemBytes = arrayElemBytes(tp.array, i);
      if (elemBytes == 0)
        return; // no provable byte bound
      byteUnit = true;
      total *= static_cast<long long>(elemBytes);
    }

    size_t lb = nextSig(i);
    // `(*pp)[k]`: the tracked pointer is the dereferenced operand, so the
    // subscript follows the closing parenthesis rather than the identifier.
    if (isPunct(lb, ")") && isPunct(prevSig(i), "*"))
      lb = nextSig(lb);
    if (isPunct(lb, "[")) {
      if (tp.rowPointer) {
        // Pointer-to-row: the k-th subscript is checked against dims[k].
        size_t cursor = lb;
        for (size_t d = 0; d < tp.dims.size(); ++d) {
          if (!isPunct(cursor, "[")) break;
          size_t rb = match(cursor, "[", "]");
          if (rb == kNPos) break;
          long idx = 0;
          if (constantIndexAt(cursor + 1, rb, &idx)) {
            // `int (*r)[W] = &a[K]` selects row K; the first subscript must
            // be added to K before comparing with the row count.
            long absolute = idx + (d == 0 ? tp.offset : 0);
            if (absolute < 0)
              reportNegativeAliasIndex(toks_[cursor], absolute, tp.array,
                                       toks_[i].text);
            else if (static_cast<size_t>(absolute) >= tp.dims[d])
              reportAliasOutOfBounds(toks_[cursor], absolute, tp.array,
                                     tp.dims[d], toks_[i].text);
          }
          cursor = nextSig(rb);
        }
      } else {
        // Element pointer: its absolute index is offset + subscript, checked
        // against the total number of elements (or bytes for a byte alias).
        size_t rb = match(lb, "[", "]");
        if (rb == kNPos)
          return;
        long idx = 0;
        if (!constantIndexAt(lb + 1, rb, &idx))
          return;
        // `&p[0]` only forms a pointer; do not treat it as an access.
        if (isPunct(prevSig(i), "&"))
          return;
        long absolute = tp.offset + idx;
        if (absolute < 0)
          reportNegativeAliasIndex(toks_[lb], absolute, tp.array,
                                   toks_[i].text);
        else if (absolute >= total)
          reportAliasOutOfBounds(toks_[lb], absolute, tp.array,
                                 static_cast<size_t>(total), toks_[i].text);
      }
      return;
    }

    // Unary dereference `*p`: report only when the tracked offset itself is
    // outside the array (one-past or before the start).  Skip `T *p` decls and
    // `&*p`, which only forms the original pointer.
    if (!isPunct(prevSig(i), "*"))
      return;
    if (isPunct(prevSig(prevSig(i)), "&"))
      return;
    size_t before = prevSig(prevSig(i));
    if (before != kNPos &&
        ((toks_[before].kind == TokKind::Identifier &&
          isTypeWord(toks_[before].text)) ||
         isPunct(before, ",") || isPunct(before, "*")))
      return;
    long absolute = tp.rowPointer ? 0 : tp.offset;
    long long extent = byteUnit ? total : static_cast<long long>(tp.dims[0]);
    if (absolute < 0)
      reportNegativeAliasIndex(toks_[i], absolute, tp.array, toks_[i].text);
    else if (static_cast<long long>(absolute) >= extent)
      reportAliasOutOfBounds(toks_[i], absolute, tp.array,
                             static_cast<size_t>(extent), toks_[i].text);
  }

  /// One-past rule for the direct spelling `*(array + N)`: forming `array + N`
  /// is fine, but dereferencing it when N equals the constant extent is not.
  void checkDirectOnePastDeref(size_t i) {
    if (!isPunct(i, "*") || inUnevaluatedContext(i))
      return;
    if (isPunct(prevSig(i), "&")) // `&*(a + N)` only forms a pointer
      return;
    size_t lp = nextSig(i);
    if (!isPunct(lp, "("))
      return;
    size_t rp = match(lp, "(", ")");
    if (rp == kNPos)
      return;
    size_t name = nextSig(lp);
    if (name >= rp || toks_[name].kind != TokKind::Identifier)
      return;
    size_t op = nextSig(name);
    if (!isPunct(op, "+"))
      return;
    long amount = 0;
    if (!constantIndexAt(nextSig(op), rp, &amount))
      return;
    size_t extent = 0;
    if (!findExtent(toks_[name].text, i, &extent) || extent == 0)
      return;
    if (amount == static_cast<long>(extent))
      reportAliasOutOfBounds(toks_[i], amount, toks_[name].text, extent,
                             toks_[name].text + std::string(" + ") +
                                 std::to_string(amount));
  }

  void checkPointerArithmetic(size_t i) {
    if (toks_[i].kind != TokKind::Identifier)
      return;
    size_t arrayExtent = 0;
    if (!findExtent(toks_[i].text, i, &arrayExtent))
      return;
    size_t op = nextSig(i);
    if (!isPunct(op, "+") && !isPunct(op, "-"))
      return;
    size_t numTok = nextSig(op);
    if (numTok >= toks_.size())
      return;
    long amount = 0;
    if (!number(toks_[numTok], &amount))
      return;
    if (isPunct(op, "+") && amount > static_cast<long>(arrayExtent))
      errorAt(toks_[op], "pointer arithmetic more than one past the end of '" +
                             toks_[i].text + "'");
    if (isPunct(op, "-") && amount < 0)
      errorAt(toks_[op], "pointer arithmetic before the start of '" +
                             toks_[i].text + "'");
  }

  void checkUnrelatedPointerCompare(size_t i) {
    if (!isPunct(i, "&"))
      return;
    size_t a = nextSig(i);
    size_t op = nextSig(a);
    if (a >= toks_.size() || toks_[a].kind != TokKind::Identifier ||
        !(isPunct(op, "<") || isPunct(op, "<=") || isPunct(op, ">") ||
          isPunct(op, ">=")))
      return;
    size_t amp = nextSig(op);
    if (!isPunct(amp, "&"))
      return;
    size_t b = nextSig(amp);
    if (b >= toks_.size() || toks_[b].kind != TokKind::Identifier)
      return;
    errorAt(toks_[op], "relational comparison of pointers to different "
                       "objects is undefined");
  }

  void checkEmptyInfiniteLoop(size_t i) {
    if (!isIdent(i, "while"))
      return;
    size_t lp = nextSig(i);
    if (!isPunct(lp, "("))
      return;
    size_t one = nextSig(lp);
    long value = 0;
    if (!number(toks_[one], &value) || value != 1)
      return;
    size_t rp = nextSig(one);
    if (!isPunct(rp, ")"))
      return;
    size_t body = nextSig(rp);
    if (!isPunct(body, "{"))
      return;
    size_t close = match(body, "{", "}");
    if (close != kNPos && nextSig(body) == close)
      errorAt(toks_[i], "empty infinite loop has undefined behavior; add a "
                        "volatile or atomic side effect");
  }

  void checkStaticBufferCalls() {
    static const char *const kStateful[] = {"strtok", "asctime", "gmtime",
                                            "localtime", "inet_ntoa", "strerror"};
    for (const auto &entry : funcs_) {
      for (const char *fn : kStateful) {
        unsigned count = 0;
        size_t first = kNPos, last = kNPos;
        std::map<std::string, size_t> savedAt;
        for (size_t k = entry.second.bodyOpen; k <= entry.second.bodyClose; ++k) {
          if (!isIdent(k, fn))
            continue;
          if (first == kNPos)
            first = k;
          last = k;
          ++count;
          // `v = strtok(...)` records a saved result that a later call
          // invalidates.
          size_t eq = k;
          while (eq > entry.second.bodyOpen && trivia(eq - 1))
            --eq;
          if (eq > entry.second.bodyOpen && isPunct(eq - 1, "=")) {
            size_t lhs = eq - 1;
            while (lhs > entry.second.bodyOpen && trivia(lhs - 1))
              --lhs;
            --lhs;
            if (lhs >= entry.second.bodyOpen &&
                toks_[lhs].kind == TokKind::Identifier)
              savedAt[toks_[lhs].text] = k;
          }
        }
        if (count > 1 && first != kNPos) {
          bool used_after = false;
          for (size_t k = last + 1; k <= entry.second.bodyClose; ++k)
            if (toks_[k].kind == TokKind::Identifier) {
              auto sit = savedAt.find(toks_[k].text);
              // Only a result saved by an EARLIER call is invalidated. Using
              // the variable assigned by the last call is safe.
              if (sit != savedAt.end() && sit->second < last)
                used_after = true;
            }
          const std::string msg = std::string("'") + fn +
              "' keeps static state; a second call invalidates earlier results";
          if (used_after)
            errorAt(toks_[first], msg);
          else
            warningAt(toks_[first], msg);
        }
      }
    }
  }

  void checkCtypeChar() {
    static const char *const kCtype[] = {
        "isalpha", "isdigit", "isalnum", "isspace", "islower", "isupper",
        "tolower", "toupper", "ispunct", "isxdigit"};
    std::set<std::string> plainChars;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (functionOpens_.count(i))
        plainChars.clear();
      if (isIdent(i, "char")) {
        size_t prev = i == 0 ? kNPos : i - 1;
        while (prev != kNPos && trivia(prev)) {
          if (prev == 0) { prev = kNPos; break; }
          --prev;
        }
        if (prev != kNPos &&
            (isIdent(prev, "unsigned") || isIdent(prev, "signed")))
          continue;
        size_t name = nextSig(i);
        if (name < toks_.size() && toks_[name].kind == TokKind::Identifier)
          plainChars.insert(toks_[name].text);
        continue;
      }
      if (toks_[i].kind != TokKind::Identifier)
        continue;
      bool ctype = false;
      for (const char *fn : kCtype)
        if (toks_[i].text == fn) {
          ctype = true;
          break;
        }
      if (!ctype)
        continue;
      size_t arg = nextSig(nextSig(i));
      if (arg < toks_.size() && plainChars.count(toks_[arg].text))
        errorAt(toks_[arg], "cast a plain char to unsigned char before <ctype.h>");
    }
  }

  // getenv() returns a pointer into the environ table; any later setenv()
  // may reallocate it, leaving earlier results dangling.
  void checkGetenvInvalidation() {
    for (const auto &entry : funcs_) {
      std::map<std::string, size_t> saved;
      for (size_t k = entry.second.bodyOpen; k <= entry.second.bodyClose; ++k) {
        if (!isIdent(k, "getenv"))
          continue;
        size_t eq = k;
        while (eq > entry.second.bodyOpen && trivia(eq - 1)) --eq;
        if (eq > entry.second.bodyOpen && isPunct(eq - 1, "=")) {
          size_t lhs = eq - 1;
          while (lhs > entry.second.bodyOpen && trivia(lhs - 1)) --lhs;
          if (lhs > entry.second.bodyOpen)
            saved[toks_[lhs - 1].text] = k;
        }
      }
      if (saved.empty())
        continue;
      for (size_t k = entry.second.bodyOpen; k <= entry.second.bodyClose; ++k) {
        if (!isIdent(k, "setenv"))
          continue;
        for (size_t u = k + 1; u <= entry.second.bodyClose; ++u) {
          if (toks_[u].kind != TokKind::Identifier)
            continue;
          auto it = saved.find(toks_[u].text);
          if (it != saved.end() && it->second < k) {
            errorAt(toks_[it->second],
                    "getenv result '" + it->first +
                        "' may dangle after setenv reallocates environ");
            break;
          }
        }
      }
    }
  }

  void checkUncheckedIo() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!(isIdent(i, "fread") || isIdent(i, "fwrite")))
        continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "("))
        continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos || !isPunct(nextSig(rp), ";"))
        continue;
      // A bare statement starts after ';', '{' or '}'.  An initializer,
      // argument to another call, condition, or assignment already uses the
      // return value and must not be reported.
      size_t prev = i == 0 ? kNPos : i - 1;
      while (prev != kNPos && trivia(prev)) {
        if (prev == 0) { prev = kNPos; break; }
        --prev;
      }
      if (prev == kNPos || isPunct(prev, ";") || isPunct(prev, "{") ||
          isPunct(prev, "}"))
        errorAt(toks_[i], "check the return value of " + toks_[i].text);
    }
  }

  std::string baseName(size_t from, size_t to) const {
    for (size_t k = from; k < to; ++k)
      if (toks_[k].kind == TokKind::Identifier)
        return toks_[k].text; // first identifier: E.field -> E, str + 2 -> str
    return std::string();
  }

  /// Spelling of a token range with whitespace and comments removed, so that
  /// `s -> buf` and `arr [ i ]` normalise to `s->buf` and `arr[i]`.
  std::string exprText(size_t from, size_t to) const {
    std::string out;
    for (size_t k = from; k < to && k < toks_.size(); ++k) {
      if (trivia(k))
        continue;
      out += toks_[k].text;
    }
    return out;
  }

  /// Start token of the lvalue expression that ends just before `end`.
  /// Walks back over identifiers, `->`, `.`, `*` and bracketed subscripts.
  size_t lvalueStart(size_t end) const {
    int depth = 0;
    size_t start = end;
    while (start > 0) {
      size_t k = start - 1;
      if (trivia(k)) { --start; continue; }
      if (toks_[k].kind == TokKind::Identifier) {
        // A statement keyword ends the expression: `return p = realloc(..)`
        // assigns to `p`, not to `return p`.
        if (isControlKeyword(toks_[k].text))
          break;
        --start;
        continue;
      }
      if (toks_[k].kind == TokKind::Number) { --start; continue; }
      if (isPunct(k, "]")) { ++depth; --start; continue; }
      if (isPunct(k, "[")) {
        if (depth == 0)
          break;
        --depth;
        --start;
        continue;
      }
      if (depth == 0 &&
          (isPunct(k, "->") || isPunct(k, ".") || isPunct(k, "*"))) {
        --start;
        continue;
      }
      break;
    }
    return start;
  }

  /// Keywords that never form part of an lvalue expression.
  static bool isControlKeyword(const std::string &text) {
    static const char *const kWords[] = {
        "return", "if",    "else",  "while",  "for",    "do",     "switch",
        "case",   "default", "break", "continue", "goto", "sizeof"};
    for (const char *word : kWords)
      if (text == word)
        return true;
    return false;
  }

  /// memcpy with overlapping source/destination is undefined; memmove is the
  /// correct API.
  void checkMemcpyOverlap() {
    struct Pointer {
      std::string base;
      long long offset = 0; // bytes from the base, not pointer elements
      size_t elemBytes = 0;
    };
    std::map<std::string, Pointer> aliases;

    // Pointer arithmetic is scaled by the operand's pointee type.  Unknown
    // types stay silent rather than guessing a byte width.
    auto pointerTypeBytes = [&](size_t from, size_t to) {
      std::string type;
      size_t stars = 0;
      for (size_t k = from; k < to; ++k) {
        if (trivia(k))
          continue;
        if (isPunct(k, "*")) {
          ++stars;
        } else if (toks_[k].kind == TokKind::Identifier &&
                   isTypeWord(toks_[k].text)) {
          if (!type.empty())
            type += " ";
          type += toks_[k].text;
        } else {
          return size_t{0};
        }
      }
      return stars == 1 ? typeSize(type) : size_t{0};
    };

    auto declaredPointerBytes = [&](size_t lhs) {
      size_t begin = lhs;
      while (begin > 0) {
        --begin;
        if (trivia(begin))
          continue;
        if (isPunct(begin, ";") || isPunct(begin, "{") ||
            isPunct(begin, "}") || isPunct(begin, "(") ||
            isPunct(begin, ","))
          break;
      }
      return pointerTypeBytes(begin + 1, lhs);
    };

    auto addOffset = [](long long *offset, long amount, size_t width,
                        bool add) {
      if (width == 0)
        return false;
      const __int128 delta = static_cast<__int128>(amount) * width;
      const __int128 value = static_cast<__int128>(*offset) +
                             (add ? delta : -delta);
      if (value < LLONG_MIN || value > LLONG_MAX)
        return false;
      *offset = static_cast<long long>(value);
      return true;
    };

    // `char s[] = "text";` carries no explicit extent, so findShape() cannot
    // see it, but the literal still fixes the element width.  Resolving the
    // array here lets pointer arithmetic on it keep a known byte scale.
    auto stringArrayElemBytes = [&](const std::string &name, size_t pos,
                                     size_t *elemBytes) -> bool {
      for (size_t i = 0; i < toks_.size(); ++i) {
        if (toks_[i].kind != TokKind::Identifier || toks_[i].text != name)
          continue;
        if (i > pos)
          continue;
        size_t scopeEnd = enclosingScopeEnd(i);
        if (scopeEnd != kNPos && pos > scopeEnd)
          continue;
        size_t lb = nextSig(i);
        if (lb >= toks_.size() || !isPunct(lb, "["))
          continue;
        size_t rb = matchIn(lb, toks_.size(), "[", "]");
        if (rb == kNPos)
          continue;
        size_t inner = nextSig(lb);
        if (inner > rb)
          continue;
        if (inner != rb)
          continue; // an explicit extent is handled by findShape()
        size_t eq = nextSig(rb);
        if (!isPunct(eq, "="))
          continue;
        size_t lit = nextSig(eq);
        if (lit >= toks_.size() || toks_[lit].kind != TokKind::StringLit)
          continue;
        size_t typeTok = prevSig(i);
        if (typeTok == kNPos || toks_[typeTok].kind != TokKind::Identifier)
          continue;
        size_t width = typeSize(toks_[typeTok].text);
        if (width != 1)
          continue; // only a byte-wide element type holds a string literal
        *elemBytes = width;
        return true;
      }
      return false;
    };

    // Resolve only expressions whose base and constant byte offset are known:
    // an array, &array[k], a prior local alias, or one of those +/- a constant.
    auto parsePointer = [&](size_t from, size_t to, const auto &self,
                           Pointer *out) -> bool {
      trim(&from, &to);
      if (from >= to)
        return false;
      size_t castBytes = 0;
      if (isPunct(from, "(")) {
        size_t close = kNPos;
        if (castClose(from, to, &close))
          castBytes = pointerTypeBytes(from + 1, close);
      }
      from = stripCast(from, to);
      if (from >= to)
        return false;

      if (isPunct(from, "&")) {
        size_t id = nextSig(from);
        size_t lb = nextSig(id);
        if (id >= to || lb >= to || !isPunct(lb, "["))
          return false;
        size_t rb = matchIn(lb, to, "[", "]");
        long index = 0;
        if (rb == kNPos || nextSig(rb) < to ||
            !constantIndexAt(lb + 1, rb, &index))
          return false;
        const std::vector<size_t> *dims = nullptr;
        if (findShape(toks_[id].text, id, &dims) && !dims->empty()) {
          out->base = toks_[id].text;
          out->elemBytes = arrayElemBytes(out->base, from);
        } else {
          size_t width = 0;
          if (!stringArrayElemBytes(toks_[id].text, id, &width))
            return false;
          out->base = toks_[id].text;
          out->elemBytes = width;
        }
        return addOffset(&out->offset, index, out->elemBytes, true);
      }

      if (toks_[from].kind != TokKind::Identifier)
        return false;
      const std::string &name = toks_[from].text;
      size_t after = nextSig(from);
      Pointer base;
      const std::vector<size_t> *dims = nullptr;
      if (findShape(name, from, &dims) && !dims->empty()) {
        base.base = name;
        base.elemBytes = arrayElemBytes(name, from);
      } else {
        size_t width = 0;
        if (stringArrayElemBytes(name, from, &width)) {
          base.base = name;
          base.elemBytes = width;
        } else {
          auto alias = aliases.find(name);
          if (alias == aliases.end())
            return false;
          base = alias->second;
        }
      }
      if (base.elemBytes == 0)
        return false;

      *out = base;
      if (after >= to)
        return true;
      if (!isPunct(after, "+") && !isPunct(after, "-"))
        return false;
      long amount = 0;
      size_t amountEnd = to;
      for (size_t k = after + 1; k < to; ++k)
        if (isPunct(k, ")") || isPunct(k, ";") || isPunct(k, ",")) {
          amountEnd = k;
          break;
        }
      if (!constantIndexAt(after + 1, amountEnd, &amount))
        return false;
      const size_t width = castBytes ? castBytes : base.elemBytes;
      if (!addOffset(&out->offset, amount, width, isPunct(after, "+")))
        return false;
      out->elemBytes = width;
      return true;
    };

    for (size_t i = 0; i < toks_.size(); ++i) {
      if (isPunct(i, "=")) {
        size_t lhs = prevSig(i);
        if (lhs == kNPos || toks_[lhs].kind != TokKind::Identifier ||
            isWriteThroughObject(lhs))
          continue;
        const std::string &name = toks_[lhs].text;
        Pointer value;
        size_t rhs = nextSig(i);
        if (parsePointer(rhs, stmtEnd(rhs), parsePointer, &value)) {
          const size_t declared = declaredPointerBytes(lhs);
          if (declared != 0)
            value.elemBytes = declared;
          else {
            auto prior = aliases.find(name);
            if (prior == aliases.end() || prior->second.elemBytes == 0) {
              aliases.erase(name);
              continue;
            }
            value.elemBytes = prior->second.elemBytes;
          }
          aliases[name] = std::move(value);
        } else {
          aliases.erase(name);
        }
        continue;
      }

      if (!isIdent(i, "memcpy"))
        continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "("))
        continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos)
        continue;
      auto args = splitArgs(lp + 1, rp);
      if (args.size() < 3)
        continue;
      long bytes = 0;
      const bool sizeKnown =
          indexExprValue(args[2].first, args[2].second, &bytes);
      // `sizeof` of a complete type is never zero, so a copy sized by sizeof
      // always moves at least one byte even when the exact count is unknown.
      size_t sizeFrom = args[2].first, sizeTo = args[2].second;
      trim(&sizeFrom, &sizeTo);
      const bool sizePositive =
          (sizeKnown && bytes > 0) || isIdent(sizeFrom, "sizeof");
      if (!sizePositive)
        continue; // an unknown or empty copy cannot overlap

      Pointer dst, src;
      bool overlap = false;
      if (sizeKnown && bytes > 0 &&
          parsePointer(args[0].first, args[0].second, parsePointer, &dst) &&
          parsePointer(args[1].first, args[1].second, parsePointer, &src) &&
          dst.base == src.base) {
        const __int128 dstEnd = static_cast<__int128>(dst.offset) + bytes;
        const __int128 srcEnd = static_cast<__int128>(src.offset) + bytes;
        overlap = dst.offset < srcEnd && src.offset < dstEnd;
      }
      // Identical expressions still overlap even when their base is unknown.
      if (!overlap && !join(args[0].first, args[0].second).empty() &&
          join(args[0].first, args[0].second) ==
              join(args[1].first, args[1].second))
        overlap = true;
      if (overlap)
        errorAt(toks_[i], "memcpy source and destination overlap; use memmove");
    }
  }

  std::string prevSafe(size_t i) const {
    if (i == 0) return std::string();
    size_t p = i - 1;
    while (p > 0 && trivia(p)) --p;
    return toks_[p].text;
  }

  void collectGlobals() {
    int depth = 0;
    static const char *const kTypes[] = {
        "int", "char", "short", "long", "float", "double", "void",
        "unsigned", "signed", "struct", "union", "enum", "static",
        "volatile", "const", "size_t", "_Atomic"};
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (isPunct(i, "{")) { ++depth; continue; }
      if (isPunct(i, "}")) { --depth; continue; }
      if (depth != 0 || toks_[i].kind != TokKind::Identifier)
        continue;
      size_t next = nextSig(i);
      if (next >= toks_.size() || isPunct(next, "("))
        continue;
      size_t prev = i == 0 ? kNPos : i - 1;
      while (prev != kNPos && trivia(prev)) {
        if (prev == 0) { prev = kNPos; break; }
        --prev;
      }
      bool afterType = false;
      if (prev != kNPos && toks_[prev].kind == TokKind::Identifier)
        for (const char *type : kTypes)
          if (toks_[prev].text == type) {
            afterType = true;
            break;
          }
      if (afterType ||
          (prev != kNPos && (isPunct(prev, ";") || isPunct(prev, "}"))))
        globals_.insert(toks_[i].text);
        bool isVolatile = false;
        size_t scan = i;
        while (scan > 0) {
          --scan;
          if (isPunct(scan, ";") || isPunct(scan, "}"))
            break;
          if (isIdent(scan, "volatile"))
            isVolatile = true;
        }
        if (isVolatile)
          volatileGlobals_.insert(toks_[i].text);
    }
  }

  /// A conservative check: a thread entry function that writes a global
  /// without taking any C+ lock is a data race.
  void checkDataRaces() {
    collectGlobals();
    std::map<std::string, int> workerCounts;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isIdent(i, "pthread_create"))
        continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "("))
        continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos)
        continue;
      auto args = splitArgs(lp + 1, rp);
      if (args.size() < 3)
        continue;
      std::string name = baseName(args[2].first, args[2].second);
      if (!name.empty())
        ++workerCounts[name];
    }
    for (const auto &entry : funcs_) {
      // A single thread does not create a race; require the same entry to be
      // passed to at least two pthread_create calls.
      if (workerCounts[entry.first] < 2)
        continue;
      const FuncSig &sig = entry.second;
      bool locked = false;
      for (size_t k = sig.bodyOpen; k <= sig.bodyClose; ++k) {
        if (toks_[k].kind != TokKind::Identifier)
          continue;
        const std::string &word = toks_[k].text;
        // Direct runtime calls or a lock-taking helper (Lock_acquire,
        // Guard_lock, ...) count as synchronisation for this conservative
        // check.
        if (word.find("lock") != std::string::npos ||
            word.find("Lock") != std::string::npos ||
            word.find("guard") != std::string::npos ||
            word.find("Guard") != std::string::npos)
          locked = true;
      }
      if (locked)
        continue;
      for (size_t k = sig.bodyOpen; k <= sig.bodyClose; ++k) {
        if (toks_[k].kind != TokKind::Identifier ||
            !globals_.count(toks_[k].text))
          continue;
        size_t next = nextSig(k);
        bool write = isPunct(next, "=") || isPunct(next, "++") ||
                     isPunct(next, "--") || isPunct(next, "+=");
        if (write && !volatileGlobals_.count(toks_[k].text))
          errorAt(toks_[k], "unsynchronized write to shared global '" +
                                toks_[k].text + "' in thread function '" +
                                entry.first + "'");
      }
    }
  }

  void checkPointerLeaks() {
    std::map<std::string, bool> live;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (functionOpens_.count(i))
        live.clear();
      if (isPunct(i, "=")) {
        size_t lhs = i == 0 ? kNPos : i - 1;
        while (lhs != kNPos && trivia(lhs)) {
          if (lhs == 0) { lhs = kNPos; break; }
          --lhs;
        }
        if (lhs == kNPos || toks_[lhs].kind != TokKind::Identifier)
          continue;
        size_t before = lhs == 0 ? kNPos : lhs - 1;
        while (before != kNPos && trivia(before)) {
          if (before == 0) { before = kNPos; break; }
          --before;
        }
        bool declarationStar = false;
        if (isPunct(before, "*") && before != kNPos) {
          size_t typeTok = before == 0 ? kNPos : before - 1;
          while (typeTok != kNPos && trivia(typeTok)) {
            if (typeTok == 0) { typeTok = kNPos; break; }
            --typeTok;
          }
          if (typeTok != kNPos && toks_[typeTok].kind == TokKind::Identifier) {
            static const char *const kTypes[] = {
                "int", "char", "short", "long", "float", "double", "void",
                "unsigned", "signed", "struct", "union", "enum", "const",
                "static", "volatile", "size_t"};
            for (const char *type : kTypes)
              if (toks_[typeTok].text == type) {
                declarationStar = true;
                break;
              }
          }
        }
        if (before != kNPos &&
            ((isPunct(before, "*") && !declarationStar) ||
             isPunct(before, ".") || isPunct(before, "->")))
          continue; // assignment through the pointer, not to the pointer
        const std::string name = toks_[lhs].text;
        size_t rhs = nextSig(i);
        std::string alloc = allocatorName(rhs);
        size_t reallocArg = nextSig(nextSig(rhs));
        bool reallocSelf = isIdent(rhs, "cplus_realloc") &&
                           (alloc == "cplus_realloc") &&
                           reallocArg < toks_.size() &&
                           toks_[reallocArg].kind == TokKind::Identifier &&
                           toks_[reallocArg].text == name;
        if (live[name] && !reallocSelf)
          errorAt(toks_[lhs], "reassigning pointer '" + name +
                                  "' would leak its live allocation");
        if (isAllocator(alloc)) {
          live[name] = true;
          continue;
        }
        if (live[name] && !reallocSelf)
          live[name] = false;
      }
      if (isIdent(i, "cplus_release") || isIdent(i, "free")) {
        size_t lp = nextSig(i);
        size_t rp = match(lp, "(", ")");
        if (rp != kNPos) {
          std::string released;
          for (size_t k = lp + 1; k < rp; ++k)
            if (toks_[k].kind == TokKind::Identifier)
              released = toks_[k].text; // handles self->field
          if (!released.empty())
            live[released] = false;
        }
      }
    }
  }

  std::vector<std::pair<size_t, size_t>> splitArgs(size_t from,
                                                    size_t to) const {
    std::vector<std::pair<size_t, size_t>> out;
    int paren = 0, bracket = 0, brace = 0;
    size_t start = from;
    for (size_t k = from; k < to; ++k) {
      if (isPunct(k, "(")) ++paren;
      else if (isPunct(k, ")")) --paren;
      else if (isPunct(k, "[")) ++bracket;
      else if (isPunct(k, "]")) --bracket;
      else if (isPunct(k, "{")) ++brace;
      else if (isPunct(k, "}")) --brace;
      else if (isPunct(k, ",") && paren == 0 && bracket == 0 && brace == 0) {
        out.push_back({start, k});
        start = k + 1;
      }
    }
    out.push_back({start, to});
    return out;
  }

  /// Record function parameter names so literal call arguments can be
  /// propagated into allocation sizes inside those functions.
  void collectFunctions() {
    static const char *const kControl[] = {
        "if", "for", "while", "switch", "return", "sizeof", "do", "case"};
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier)
        continue;
      bool control = false;
      for (const char *word : kControl)
        if (toks_[i].text == word) {
          control = true;
          break;
        }
      if (control)
        continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "("))
        continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos || !isPunct(nextSig(rp), "{"))
        continue;
      auto parts = splitArgs(lp + 1, rp);
      if (parts.size() == 1 &&
          join(parts[0].first, parts[0].second).empty())
        continue;
      std::vector<std::string> params;
      std::vector<bool> outFlags;
      std::vector<bool> restrictFlags;
      std::map<std::string, size_t> declPos;
      for (const auto &part : parts) {
        size_t last = kNPos;
        int stars = 0;
        bool restricted = false;
        for (size_t k = part.first; k < part.second; ++k) {
          if (toks_[k].kind == TokKind::Identifier) {
            last = k;
            if (toks_[k].text == "restrict" || toks_[k].text == "__restrict" ||
                toks_[k].text == "__restrict__")
              restricted = true;
          } else if (isPunct(k, "*"))
            ++stars;
        }
        params.push_back(last == kNPos ? std::string() : toks_[last].text);
        outFlags.push_back(stars >= 2);
        restrictFlags.push_back(restricted);
        if (last != kNPos)
          declPos[toks_[last].text] = last;
      }
      FuncSig sig;
      sig.params = params;
      sig.outParam = outFlags;
      sig.restrictParam = restrictFlags;
      sig.paramDeclPos = declPos;
      sig.bodyOpen = nextSig(rp);
      sig.bodyClose = match(sig.bodyOpen, "{", "}");
      funcs_[toks_[i].text] = sig;
    }
  }

  std::string join(size_t from, size_t to) const {
    std::string out;
    for (size_t k = from; k < to; ++k)
      if (!trivia(k)) {
        if (!out.empty())
          out += " ";
        out += toks_[k].text;
      }
    return out;
  }

  void collectCallConstants() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier)
        continue;
      auto it = funcs_.find(toks_[i].text);
      if (it == funcs_.end())
        continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "("))
        continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos || isPunct(nextSig(rp), "{"))
        continue; // function definition, not a call
      auto args = splitArgs(lp + 1, rp);
      if (args.size() != it->second.params.size())
        continue;
      for (size_t k = 0; k < args.size(); ++k) {
        if (it->second.params[k].empty())
          continue;
        size_t only = kNPos;
        bool single = true;
        for (size_t n = args[k].first; n < args[k].second; ++n) {
          if (trivia(n))
            continue;
          if (only != kNPos) {
            single = false;
            break;
          }
          only = n;
        }
        long value = 0;
        if (!single || only == kNPos || !number(toks_[only], &value)) {
          conflict_.insert(it->second.params[k]);
          continue;
        }
        auto c = constants_.find(it->second.params[k]);
        if (c != constants_.end() && c->second != value)
          conflict_.insert(it->second.params[k]);
        else
          constants_[it->second.params[k]] = value;
      }
    }
    for (const auto &name : conflict_)
      constants_.erase(name);
  }

  bool constantFactor(size_t from, size_t to, long *out) const {
    size_t only = kNPos;
    bool single = true;
    for (size_t k = from; k < to; ++k) {
      if (trivia(k))
        continue;
      if (only != kNPos) {
        single = false;
        break;
      }
      only = k;
    }
    if (!single || only == kNPos)
      return false;
    if (number(toks_[only], out))
      return true;
    if (toks_[only].kind == TokKind::Identifier) {
      auto it = constants_.find(toks_[only].text);
      if (it != constants_.end()) {
        *out = it->second;
        return true;
      }
    }
    return false;
  }

  /// Recognise `sizeof(T) * N` (or `N * sizeof(T)`) and return N when it is
  /// a known constant, i.e. the element count of the allocation.
  bool allocationCount(size_t from, size_t to, size_t *count) const {
    size_t sizeofIdx = kNPos, star = kNPos;
    for (size_t k = from; k < to; ++k) {
      if (isIdent(k, "sizeof")) sizeofIdx = k;
      else if (isPunct(k, "*")) star = k;
    }
    if (sizeofIdx == kNPos || star == kNPos)
      return false;
    long value = 0;
    if (star > sizeofIdx) {
      if (!constantFactor(nextSig(star), to, &value))
        return false;
    } else {
      size_t end = star;
      while (end > from && trivia(end - 1))
        --end;
      if (!constantFactor(from, end, &value))
        return false;
    }
    if (value <= 0)
      return false;
    *count = static_cast<size_t>(value);
    return true;
  }

  void collectAllocations() {
    static const char *const kAllocators[] = {
        "cplus_alloc", "cplus_zalloc", "cplus_realloc", "cplus_new_array",
        "cplus_new", "realloc"};
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier)
        continue;
      bool isAlloc = false;
      for (const char *name : kAllocators)
        if (toks_[i].text == name) {
          isAlloc = true;
          break;
        }
      if (!isAlloc)
        continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "("))
        continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos)
        continue;

      size_t start = i;
      while (start > 0) {
        --start;
        if (isPunct(start, ";") || isPunct(start, "{") ||
            isPunct(start, "}")) {
          ++start;
          break;
        }
      }
      size_t eq = kNPos;
      int paren = 0, bracket = 0;
      for (size_t k = start; k < i; ++k) {
        if (isPunct(k, "(")) ++paren;
        else if (isPunct(k, ")")) --paren;
        else if (isPunct(k, "[")) ++bracket;
        else if (isPunct(k, "]")) --bracket;
        else if (isPunct(k, "=") && paren == 0 && bracket == 0) {
          eq = k;
          break;
        }
      }
      if (eq == kNPos)
        continue;
      size_t name = kNPos;
      for (size_t k = eq; k > start; --k)
        if (toks_[k - 1].kind == TokKind::Identifier) {
          name = k - 1;
          break;
        }
      if (name == kNPos)
        continue;

      size_t count = 0;
      if (toks_[i].text == "cplus_new") {
        count = 1;
      } else if (toks_[i].text == "cplus_new_array") {
        auto args = splitArgs(lp + 1, rp);
        long rawCount = 0;
        if (args.size() < 2 ||
            !constantFactor(args[1].first, args[1].second, &rawCount))
          continue;
        if (rawCount <= 0)
          continue;
        count = static_cast<size_t>(rawCount);
      } else {
        auto args = splitArgs(lp + 1, rp);
        size_t from = lp + 1, to = rp;
        // Wave 4 (red finding): realloc gives the result a NEW extent; the
        // caller's old extent is stale.  cplus_realloc and raw realloc are
        // handled identically (size is arg #1).  A bare byte count is
        // converted using the destination's element size.
        bool isRealloc = (toks_[i].text == "cplus_realloc" ||
                          toks_[i].text == "realloc");
        if (isRealloc) {
          if (args.size() < 2) {
            reallocResultNames_.insert(toks_[name].text);
            reallocInvalidAt_[toks_[name].text].push_back(i);
            continue;
          }
          from = args[1].first;
          to = args[1].second;
        }
        if (!allocationCount(from, to, &count) && isRealloc) {
          long bytes = 0;
          size_t starTok = prevSig(name);
          size_t typeTok = (starTok != kNPos && isPunct(starTok, "*"))
                               ? prevSig(starTok) : kNPos;
          std::string ty;
          if (typeTok != kNPos && toks_[typeTok].kind == TokKind::Identifier)
            ty = toks_[typeTok].text;
          size_t es = typeSize(ty);
          if (es && constantFactor(from, to, &bytes) && bytes > 0 &&
              bytes % static_cast<long>(es) == 0)
            count = static_cast<size_t>(bytes / static_cast<long>(es));
        }
        if (isRealloc) {
          // Register the result name even when the new size is unknown: the
          // alias-transfer pass must invalidate the old extent at `p = q`.
          reallocResultNames_.insert(toks_[name].text);
          if (count == 0)
            reallocInvalidAt_[toks_[name].text].push_back(i);
        }
        if (count == 0)
          continue;
      }
      if (count > 0) {
        ArrayShape allocated;
        allocated.pos = i;
        allocated.name = toks_[name].text;
        allocated.dims.push_back(count);
        localArrays_.push_back(allocated);
      }
    }
  }

  /// Wave 4 regression fix: carry a realloc result's extent through the common
  /// `int *q = cplus_realloc(p, N); p = q;` (and `p = q + 0`) idiom.  If the new
  /// size could not be folded, invalidate p's old extent instead so no stale
  /// bound is used.  Restricted to realloc result names, so ordinary pointer
  /// assignments cannot introduce new extents or false positives.
  void propagateReallocAliases() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isPunct(i, "="))
        continue;
      size_t lhs = prevSig(i);
      if (lhs == kNPos || toks_[lhs].kind != TokKind::Identifier)
        continue;
      size_t rhs = stripCast(nextSig(i), toks_.size());
      if (rhs >= toks_.size() || toks_[rhs].kind != TokKind::Identifier ||
          !reallocResultNames_.count(toks_[rhs].text))
        continue;
      size_t end = stmtEnd(rhs);
      size_t p = nextSig(rhs);
      if (p < end && (isPunct(p, "+") || isPunct(p, "-"))) {
        long d = 0;
        if (!constantIndexAt(nextSig(p), end, &d) || d != 0)
          continue; // a real offset change is a different object
      } else if (p != end && p != kNPos) {
        continue; // not a bare alias
      }
      const std::vector<size_t> *dims = nullptr;
      if (findShape(toks_[rhs].text, i, &dims) && !dims->empty())
        localArrays_.push_back({i, toks_[lhs].text, *dims});
      else
        reallocInvalidAt_[toks_[lhs].text].push_back(i);
    }
  }

  /// While-loop analogue of checkLoop.  An induction variable initialised to
  /// a constant, tested as `i < N` and stepped by `i++` reaches every value in
  /// [start, N-1]; a subscript `p[i]` on a base whose extent is known at the
  /// call site is therefore provably out of bounds when N-1 >= extent.  The
  /// host AST checker sees only the lowered loop and cannot fold this chain.
  /// Anything not understood (symbolic bound, no step, unrelated index)
  /// bails out silently, so valid loops are never rejected.
  void checkWhileLoop(size_t whileIdx) {
    size_t open = nextSig(whileIdx);
    if (!isPunct(open, "(")) return;
    size_t close = match(open, "(", ")");
    if (close == kNPos) return;

    std::string var;
    long bound = 0;
    bool lessEqual = false;
    for (size_t k = open + 1; k + 1 < close; ++k) {
      if (toks_[k].kind != TokKind::Identifier) continue;
      size_t op = nextSig(k);
      if (op >= close || (!isPunct(op, "<") && !isPunct(op, "<="))) continue;
      size_t nv = nextSig(op);
      if (nv >= close || !number(toks_[nv], &bound)) continue;
      var = toks_[k].text;
      lessEqual = isPunct(op, "<=");
      break;
    }
    if (var.empty() || bound <= 0) return;

    // Most recent constant assignment to the induction variable before the
    // loop (scanning backwards finds the last one, not a stale earlier one).
    long start = 0;
    bool haveStart = false;
    for (size_t k = whileIdx; k > 0; --k) {
      if (toks_[k].kind != TokKind::Identifier || toks_[k].text != var) continue;
      size_t eq = nextSig(k);
      if (eq >= whileIdx || !isPunct(eq, "=")) continue;
      size_t nv = nextSig(eq);
      if (nv < whileIdx && number(toks_[nv], &start)) { haveStart = true; break; }
    }
    if (!haveStart || start < 0) return;

    size_t body = nextSig(close);
    size_t bodyEnd;
    if (isPunct(body, "{")) {
      bodyEnd = match(body, "{", "}");
      if (bodyEnd == kNPos) return;
    } else {
      bodyEnd = stmtEnd(body);
    }
    if (bodyEnd == kNPos || bodyEnd <= body) return;
    long step = 0;
    if (!parseStep(body, bodyEnd, var, &step) || step <= 0) return;
    long maxIndex = lessEqual ? bound : bound - 1;
    if (maxIndex < start) return;

    for (size_t k = body; k < bodyEnd; ++k) {
      if (toks_[k].kind != TokKind::Identifier) continue;
      size_t lb = nextSig(k);
      if (!isPunct(lb, "[")) continue;
      size_t rb = match(lb, "[", "]");
      if (rb == kNPos || rb >= bodyEnd) continue;
      Interval iv = evalIndex(lb + 1, rb, var, start, maxIndex);
      if (!iv.known || iv.hi < 0) continue;
      long extent = 0;
      const std::vector<size_t> *dims = nullptr;
      if (findShape(toks_[k].text, k, &dims) && !dims->empty()) {
        extent = dims->front();
      } else {
        const std::string *fn = enclosingFunction(k);
        if (!fn) continue;
        auto eit = paramExtents_.find(*fn);
        if (eit == paramExtents_.end()) continue;
        const auto &params = funcs_.at(*fn).params;
        size_t pos = kNPos;
        for (size_t p = 0; p < params.size(); ++p)
          if (params[p] == toks_[k].text) { pos = p; break; }
        if (pos == kNPos) continue;
        auto pit = eit->second.find(pos);
        if (pit == eit->second.end() || pit->second == 0) continue;
        extent = pit->second;
      }
      if (iv.hi >= extent) {
        errorAtSafeBounds(toks_[lb], "array index " + std::to_string(iv.hi) +
            " from while-loop induction is out of bounds of '" +
            toks_[k].text + "' of length " + std::to_string(extent));
        break;
      }
    }
  }

  /// A subscript with a literal index that is known too large.
  /// Parse `for (init; cond; step)` and check subscripts of the loop index.
  void checkLoop(size_t forIdx) {
    size_t open = nextSig(forIdx);
    if (!isPunct(open, "("))
      return;
    size_t close = match(open, "(", ")");
    if (close == kNPos)
      return;

    size_t s0 = nextSig(open);
    size_t semi0 = findSemi(s0, close);
    size_t semi1 = semi0 == kNPos ? kNPos : findSemi(nextSig(semi0), close);
    if (semi0 == kNPos || semi1 == kNPos)
      return;

    std::string var;
    long start = 0;
    if (!parseInit(s0, semi0, &var, &start))
      return;

    long bound = 0;
    bool lessEqual = false;
    if (!parseCondition(nextSig(semi0), semi1, var, &bound, &lessEqual))
      return;

    long step = 0;
    if (!parseStep(nextSig(semi1), close, var, &step) || step <= 0)
      return;

    long maxIndex = lessEqual ? bound : bound - 1;
    if (maxIndex < start || maxIndex < 0)
      return;

    size_t body = nextSig(close);
    size_t bodyEnd = kNPos;
    if (isPunct(body, "{")) {
      bodyEnd = match(body, "{", "}");
      if (bodyEnd == kNPos)
        return;
    } else {
      size_t k = body;
      while (k < toks_.size() && !isPunct(k, ";"))
        ++k;
      bodyEnd = k;
    }
    if (bodyEnd == kNPos)
      return;

    for (size_t k = body; k < bodyEnd; ++k) {
      if (toks_[k].kind != TokKind::Identifier)
        continue;
      checkSubscriptChain(k, start, maxIndex, var);
    }
  }

  size_t findSemi(size_t from, size_t limit) const {
    int paren = 0, bracket = 0, brace = 0;
    for (size_t k = from; k < limit; ++k) {
      if (isPunct(k, "(")) ++paren;
      else if (isPunct(k, ")")) --paren;
      else if (isPunct(k, "[")) ++bracket;
      else if (isPunct(k, "]")) --bracket;
      else if (isPunct(k, "{")) ++brace;
      else if (isPunct(k, "}")) --brace;
      else if (isPunct(k, ";") && paren == 0 && bracket == 0 && brace == 0)
        return k;
    }
    return kNPos;
  }

  bool parseInit(size_t from, size_t to, std::string *var, long *start) {
    for (size_t k = from; k < to; ++k) {
      if (toks_[k].kind != TokKind::Identifier)
        continue;
      size_t eq = nextSig(k);
      if (eq >= to || !isPunct(eq, "="))
        continue;
      *var = toks_[k].text;
      for (size_t n = nextSig(eq); n < to; ++n)
        if (number(toks_[n], start))
          return true;
      return false;
    }
    return false;
  }

  bool parseCondition(size_t from, size_t to, const std::string &var,
                      long *bound, bool *lessEqual) {
    for (size_t k = from; k < to; ++k) {
      if (!(toks_[k].kind == TokKind::Identifier && toks_[k].text == var))
        continue;
      size_t op = nextSig(k);
      if (op >= to || (!isPunct(op, "<") && !isPunct(op, "<=")))
        continue;
      size_t n = nextSig(op);
      if (n >= to || !number(toks_[n], bound))
        continue;
      *lessEqual = isPunct(op, "<=");
      return true;
    }
    return false;
  }

  bool parseStep(size_t from, size_t to, const std::string &var, long *step) {
    for (size_t k = from; k < to; ++k) {
      if (!(toks_[k].kind == TokKind::Identifier && toks_[k].text == var))
        continue;
      size_t n = nextSig(k);
      if (isPunct(n, "++")) {
        *step = 1;
        return true;
      }
      if (isPunct(n, "+=") && n + 1 < to)
        return number(toks_[n + 1], step);
      if (isPunct(n, "=") && n + 3 < to &&
          toks_[n + 1].text == var && isPunct(n + 2, "+"))
        return number(toks_[n + 3], step);
    }
    // ++i
    for (size_t k = from; k + 1 < to; ++k)
      if (isPunct(k, "++") && toks_[k + 1].text == var) {
        *step = 1;
        return true;
      }
    return false;
  }

  //===--------------------------------------------------------------------===//
  // Additional conservative checks for the user-provided bug corpus.
  // These reuse the token stream only; every check bails out (no diagnostic)
  // on anything it cannot understand, so valid C is never rejected.
  //===--------------------------------------------------------------------===//

  static bool isTypeWord(const std::string &s) {
    static const char *const kWords[] = {
        "void", "char", "short", "int", "long", "float", "double", "signed",
        "unsigned", "struct", "union", "enum", "size_t", "ssize_t",
        "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t",
        "int64_t", "uint64_t", "intptr_t", "uintptr_t", "const", "volatile",
        "_Bool", "FILE"};
    for (const char *w : kWords)
      if (s == w)
        return true;
    return false;
  }

  size_t prevSig(size_t i) const {
    if (i == 0)
      return kNPos;
    size_t p = i - 1;
    while (p > 0 && trivia(p))
      --p;
    return p;
  }

  void skipWs(size_t &p, size_t end) const {
    while (p < end && trivia(p))
      ++p;
  }

  /// Version of match() bounded by `end` rather than the whole token stream.
  size_t matchIn(size_t open, size_t end, const char *l, const char *r) const {
    int depth = 0;
    for (size_t i = open; i < end; ++i) {
      if (isPunct(i, l))
        ++depth;
      else if (isPunct(i, r) && --depth == 0)
        return i;
    }
    return kNPos;
  }

  bool isKnownArray(const std::string &name, size_t pos) const {
    const std::vector<size_t> *dims = nullptr;
    return findShape(name, pos, &dims);
  }

  /// Collect names declared as pointers (`T *p`, including parameters).  Only
  /// used to recognise the `sizeof(pointer)` buffer-size bug (bug 35).
  void collectPointerNames() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isPunct(i, "*"))
        continue;
      size_t prev = prevSig(i);
      if (prev == kNPos)
        continue;
      bool typeContext =
          (toks_[prev].kind == TokKind::Identifier &&
           isTypeWord(toks_[prev].text)) ||
          isPunct(prev, ",") || isPunct(prev, "(") || isPunct(prev, "*");
      if (!typeContext)
        continue;
      size_t name = nextSig(i);
      if (name < toks_.size() && toks_[name].kind == TokKind::Identifier &&
          !isTypeWord(toks_[name].text))
        pointerNames_.insert(toks_[name].text);
    }
  }

  /// The argument ranges of memset/memcpy/memmove (size argument is arg #2).
  bool memoryCallArgs(size_t i,
                      std::vector<std::pair<size_t, size_t>> *args) const {
    if (!(isIdent(i, "memset") || isIdent(i, "memcpy") ||
          isIdent(i, "memmove")))
      return false;
    size_t lp = nextSig(i);
    if (!isPunct(lp, "("))
      return false;
    size_t rp = match(lp, "(", ")");
    if (rp == kNPos)
      return false;
    *args = splitArgs(lp + 1, rp);
    return true;
  }

  /// Bug 35: `memset(buf, 0, sizeof(buf))` where `buf` is a pointer sizes only
  /// the pointer, not the buffer.  Arrays are exempt: there sizeof is correct.
  void checkSizeofPointer() {
    std::vector<std::pair<size_t, size_t>> args;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!memoryCallArgs(i, &args) || args.size() < 3)
        continue;
      size_t from = args[2].first, to = args[2].second;
      for (size_t k = from; k < to; ++k) {
        if (!isIdent(k, "sizeof"))
          continue;
        size_t op = nextSig(k);
        std::string name;
        if (isPunct(op, "(")) {
          size_t close = matchIn(op, to, "(", ")");
          if (close == kNPos || close >= to)
            continue;
          size_t id = nextSig(op);
          if (id >= close || toks_[id].kind != TokKind::Identifier ||
              nextSig(id) != close)
            continue; // only a bare `sizeof(name)`, not sizeof(*p)/sizeof(T)
          name = toks_[id].text;
        } else if (op < to && toks_[op].kind == TokKind::Identifier) {
          name = toks_[op].text;
        }
        if (name.empty() || !pointerNames_.count(name))
          continue;
        if (isKnownArray(name, k))
          continue;
        errorAt(toks_[k], "sizeof(" + name +
                              ") is the pointer size, not the buffer size; "
                              "pass the element count instead");
      }
    }
  }

  //--- Unsigned constant folding (bug 41) ----------------------------------//

  static bool parseUInt(const Token &t, uint64_t *out) {
    if (t.kind != TokKind::Number)
      return false;
    const std::string &s = t.text;
    char *end = nullptr;
    unsigned long long v = strtoull(s.c_str(), &end, 0);
    if (end == s.c_str())
      return false;
    *out = static_cast<uint64_t>(v);
    return true;
  }

  static bool isUnsignedTypeName(const std::string &s) {
    return s == "size_t" || s == "unsigned" || s == "uint64_t" ||
           s == "uint32_t" || s == "uint16_t" || s == "uint8_t" ||
           s == "uintptr_t";
  }

  /// Byte size of a spelled-out integer type; 0 means "unknown" (suppresses
  /// the diagnostic).  Host sizes match the x86-64 targets C+ builds for.
  static size_t typeSize(const std::string &type) {
    if (type.find("char") != std::string::npos)
      return 1;
    if (type.find("short") != std::string::npos)
      return 2;
    if (type.find("long long") != std::string::npos ||
        type.find("double") != std::string::npos ||
        type.find("size_t") != std::string::npos ||
        type.find("uint64") != std::string::npos ||
        type.find("int64") != std::string::npos ||
        type.find("long") != std::string::npos)
      return 8;
    if (type.find("int") != std::string::npos ||
        type.find("float") != std::string::npos ||
        type.find("uint32") != std::string::npos ||
        type.find("unsigned") != std::string::npos)
      return 4;
    return 0;
  }

  bool lookupUnsignedConst(const std::string &name, size_t pos,
                           uint64_t *out) const {
    auto it = unsignedConsts_.find(name);
    if (it == unsignedConsts_.end())
      return false;
    for (size_t k = it->second.size(); k > 0; --k)
      if (it->second[k - 1].first <= pos) {
        *out = it->second[k - 1].second;
        return true;
      }
    return false;
  }

  /// A `( TYPE * )` cast opening at `open`?  Returns the index of `)`.
  bool castClose(size_t open, size_t end, size_t *close) const {
    size_t k = open + 1;
    skipWs(k, end);
    bool anyType = false;
    while (k < end) {
      if (toks_[k].kind == TokKind::Identifier && isTypeWord(toks_[k].text)) {
        anyType = true;
        ++k;
        skipWs(k, end);
        continue;
      }
      if (isPunct(k, "*")) {
        ++k;
        skipWs(k, end);
        continue;
      }
      if (isPunct(k, ")")) {
        if (anyType) {
          *close = k;
          return true;
        }
        return false;
      }
      return false;
    }
    return false;
  }

  uint64_t evalUUnary(size_t &p, size_t end, bool &ok, bool &overflow) const {
    skipWs(p, end);
    if (p >= end) {
      ok = false;
      return 0;
    }
    if (isPunct(p, "-")) {
      ++p;
      return static_cast<uint64_t>(0) - evalUUnary(p, end, ok, overflow);
    }
    if (isPunct(p, "+")) {
      ++p;
      return evalUUnary(p, end, ok, overflow);
    }
    if (isPunct(p, "(")) {
      size_t close = kNPos;
      if (castClose(p, end, &close)) { // cast is transparent for unsigned math
        p = close + 1;
        return evalUUnary(p, end, ok, overflow);
      }
      size_t gclose = matchIn(p, end, "(", ")"); // plain grouping
      if (gclose == kNPos) {
        ok = false;
        return 0;
      }
      ++p;
      uint64_t v = evalUAdd(p, gclose, ok, overflow);
      skipWs(p, end);
      if (p < end && isPunct(p, ")"))
        ++p;
      else
        ok = false;
      return v;
    }
    if (isIdent(p, "sizeof")) { // sizeof(TYPE)
      ++p;
      skipWs(p, end);
      if (p < end && isPunct(p, "(")) {
        size_t close = matchIn(p, end, "(", ")");
        if (close == kNPos) {
          ok = false;
          return 0;
        }
        std::string type;
        for (size_t k = p + 1; k < close; ++k)
          if (toks_[k].kind == TokKind::Identifier) {
            if (!type.empty())
              type += " ";
            type += toks_[k].text;
          }
        size_t sz = typeSize(type);
        if (sz == 0) {
          ok = false;
          return 0;
        }
        p = close + 1;
        return static_cast<uint64_t>(sz);
      }
      ok = false;
      return 0;
    }
    uint64_t num = 0;
    if (parseUInt(toks_[p], &num)) {
      ++p;
      return num;
    }
    if (toks_[p].kind == TokKind::Identifier) {
      uint64_t value = 0;
      if (lookupUnsignedConst(toks_[p].text, p, &value)) {
        ++p;
        return value;
      }
    }
    ok = false;
    return 0;
  }

  uint64_t evalUMul(size_t &p, size_t end, bool &ok, bool &overflow) const {
    uint64_t a = evalUUnary(p, end, ok, overflow);
    while (ok) {
      skipWs(p, end);
      if (p >= end)
        break;
      bool div = isPunct(p, "/"), mod = isPunct(p, "%");
      if (!isPunct(p, "*") && !div && !mod)
        break;
      ++p;
      uint64_t b = evalUUnary(p, end, ok, overflow);
      if (!ok)
        break;
      if (div || mod) {
        if (b == 0) {
          ok = false;
          break;
        }
        a = div ? a / b : a % b;
      } else {
        if (b != 0 && a > (~static_cast<uint64_t>(0)) / b)
          overflow = true;
        a *= b;
      }
    }
    return a;
  }

  uint64_t evalUAdd(size_t &p, size_t end, bool &ok, bool &overflow) const {
    uint64_t a = evalUMul(p, end, ok, overflow);
    while (ok) {
      skipWs(p, end);
      if (p >= end)
        break;
      if (isPunct(p, "+")) {
        ++p;
        uint64_t b = evalUMul(p, end, ok, overflow);
        if (!ok)
          break;
        if (a > (~static_cast<uint64_t>(0)) - b)
          overflow = true;
        a += b;
      } else if (isPunct(p, "-")) {
        ++p;
        uint64_t b = evalUMul(p, end, ok, overflow);
        if (!ok)
          break;
        a -= b; // unsigned wraparound is well defined
      } else {
        break;
      }
    }
    return a;
  }

  bool evalUnsignedExpr(size_t from, size_t to, uint64_t *out,
                        bool *overflow) const {
    size_t p = from;
    bool ok = true;
    *overflow = false;
    uint64_t v = evalUAdd(p, to, ok, *overflow);
    skipWs(p, to);
    if (!ok || p != to)
      return false;
    *out = v;
    return true;
  }

  /// Record compile-time values of size_t/unsigned locals so allocation sizes
  /// can be constant-folded.  Values are keyed by source position so shadowed
  /// or reassigned names cannot leak across scopes (bug 41).
  void collectSizeConstants() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier ||
          !isUnsignedTypeName(toks_[i].text))
        continue;
      size_t name = nextSig(i);
      if (name >= toks_.size() || toks_[name].kind != TokKind::Identifier)
        continue;
      size_t eq = nextSig(name);
      if (!isPunct(eq, "="))
        continue;
      size_t semi = findSemi(nextSig(eq), toks_.size());
      if (semi == kNPos)
        continue;
      uint64_t value = 0;
      bool overflow = false;
      if (!evalUnsignedExpr(nextSig(eq), semi, &value, &overflow))
        continue;
      unsignedConsts_[toks_[name].text].push_back({name, value});
    }
  }

  /// Bug 41: reject allocation sizes whose constant arithmetic wraps size_t,
  /// e.g. `malloc(n * sizeof(int))` with n == SIZE_MAX / 2 + 2.
  void checkAllocationOverflow() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier)
        continue;
      const std::string &fn = toks_[i].text;
      if (!(isAllocator(fn) || fn == "realloc"))
        continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "("))
        continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos)
        continue;
      for (const auto &arg : splitArgs(lp + 1, rp)) {
        bool hasSizeof = false;
        for (size_t k = arg.first; k < arg.second; ++k)
          if (isIdent(k, "sizeof")) {
            hasSizeof = true;
            break;
          }
        if (!hasSizeof)
          continue;
        uint64_t value = 0;
        bool overflow = false;
        if (!evalUnsignedExpr(arg.first, arg.second, &value, &overflow))
          continue;
        if (overflow)
          errorAt(toks_[i], "allocation size wraps size_t; check the "
                            "multiplication before allocating");
      }
    }
  }

  //--- Negative-to-unsigned conversions (bug 42) ---------------------------//

  /// Bug 42 facts: per-function set of names known to hold a negative value,
  /// gathered from local `name = -NUMBER` initialisers and from negative
  /// literal arguments propagated into callee parameters.  A name with any
  /// other (or unknown) assignment is dropped to avoid false positives.
  /// The generic checkNegativeComparison() is deliberately not used: it would
  /// falsely reject valid signed comparisons such as `if (x < y)`.
  void collectNegativeNames() {
    // Local assignments, scoped to each function body.
    for (const auto &entry : funcs_) {
      const FuncSig &sig = entry.second;
      if (sig.bodyClose == kNPos)
        continue;
      std::set<std::string> conflict;
      for (size_t k = sig.bodyOpen; k <= sig.bodyClose; ++k) {
        if (toks_[k].kind != TokKind::Identifier)
          continue;
        size_t eq = nextSig(k);
        if (eq > sig.bodyClose || !isPunct(eq, "="))
          continue;
        const std::string &name = toks_[k].text;
        size_t val = nextSig(eq);
        if (val > sig.bodyClose)
          continue;
        bool negative = isPunct(val, "-");
        if (negative)
          val = nextSig(val);
        long ignored = 0;
        if (val <= sig.bodyClose && number(toks_[val], &ignored)) {
          if (negative)
            negFacts_[entry.first].insert(name);
          else
            conflict.insert(name);
        } else {
          conflict.insert(name);
        }
      }
      for (const auto &n : conflict)
        negFacts_[entry.first].erase(n);
    }
    // Negative literal arguments propagated into the callee's parameters.
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier)
        continue;
      auto it = funcs_.find(toks_[i].text);
      if (it == funcs_.end())
        continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "("))
        continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos || isPunct(nextSig(rp), "{"))
        continue; // a definition, not a call
      auto args = splitArgs(lp + 1, rp);
      if (args.size() != it->second.params.size())
        continue;
      for (size_t k = 0; k < args.size(); ++k) {
        const std::string &param = it->second.params[k];
        if (param.empty())
          continue;
        size_t a = args[k].first, b = args[k].second;
        skipWs(a, b);
        if (!isPunct(a, "-"))
          continue;
        long ignored = 0;
        size_t num = nextSig(a);
        if (num < b && number(toks_[num], &ignored))
          negFacts_[it->first].insert(param);
      }
    }
  }

  /// Negative-value facts in scope at `pos`, or null when none are known.
  /// True if an earlier `if (x < 0) return;` guard makes the negative fact
  /// irrelevant past this point (the code cannot continue with x < 0).
  bool guardedNegative(size_t pos, const std::string &name) const {
    for (const auto &entry : funcs_) {
      const FuncSig &sig = entry.second;
      if (!(sig.bodyOpen <= pos && pos <= sig.bodyClose))
        continue;
      for (size_t k = sig.bodyOpen; k + 5 < pos; ++k) {
        if (!isIdent(k, "if"))
          continue;
        size_t lp = nextSig(k);
        if (!isPunct(lp, "("))
          continue;
        size_t a = nextSig(lp);
        if (a >= toks_.size() || toks_[a].kind != TokKind::Identifier ||
            toks_[a].text != name)
          continue;
        size_t op = nextSig(a), zero = nextSig(op), rp = nextSig(zero);
        if (op < toks_.size() && isPunct(op, "<") && zero < toks_.size() &&
            toks_[zero].text == "0" && rp < toks_.size() && isPunct(rp, ")") &&
            isIdent(nextSig(rp), "return"))
          return true;
      }
    }
    return false;
  }

  const std::set<std::string> *negativeFactsAt(size_t pos) const {
    for (const auto &entry : funcs_) {
      const FuncSig &sig = entry.second;
      if (sig.bodyOpen <= pos && pos <= sig.bodyClose) {
        auto it = negFacts_.find(entry.first);
        return it == negFacts_.end() ? nullptr : &it->second;
      }
    }
    return nullptr;
  }

  bool castIsUnsignedType(size_t from, size_t to) const {
    for (size_t k = from; k < to; ++k) {
      if (toks_[k].kind != TokKind::Identifier)
        continue;
      const std::string &s = toks_[k].text;
      if (s == "size_t" || s == "unsigned" || s.rfind("uint", 0) == 0)
        return true;
    }
    return false;
  }

  /// Bug 42: a value known to be negative, cast to size_t (or passed bare) as
  /// a memory-operation length, becomes a huge unsigned size.
  void checkNegativeToUnsigned() {
    std::vector<std::pair<size_t, size_t>> args;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!memoryCallArgs(i, &args) || args.size() < 3)
        continue;
      const std::set<std::string> *neg = negativeFactsAt(i);
      if (!neg || neg->empty())
        continue;
      size_t from = args[2].first, to = args[2].second;
      bool reported = false;
      for (size_t k = from; k < to && !reported; ++k) {
        if (!isPunct(k, "("))
          continue;
        size_t close = matchIn(k, to, "(", ")");
        if (close == kNPos || close >= to ||
            !castIsUnsignedType(k + 1, close))
          continue;
        size_t id = nextSig(close);
        if (id >= to || toks_[id].kind != TokKind::Identifier)
          continue;
        if (neg->count(toks_[id].text) &&
            !guardedNegative(i, toks_[id].text)) {
          errorAt(toks_[k], "negative length '" + toks_[id].text +
                                "' converted to size_t becomes a huge size");
          reported = true;
        }
      }
      if (reported)
        continue;
      // Bare known-negative identifier used directly as the size argument.
      size_t only = kNPos;
      bool single = true;
      for (size_t k = from; k < to; ++k) {
        if (trivia(k))
          continue;
        if (only != kNPos) {
          single = false;
          break;
        }
        only = k;
      }
      if (single && only != kNPos && toks_[only].kind == TokKind::Identifier &&
          neg->count(toks_[only].text) &&
          !guardedNegative(i, toks_[only].text))
        errorAt(toks_[only], "negative length '" + toks_[only].text +
                                 "' passed as an unsigned size");
    }
  }

  //--- Strict-aliasing type punning (bug 45) --------------------------------//

  /// Record pointers initialised from `(T*)&other` where T is not a character
  /// type; writing through them violates strict aliasing.  The existing
  /// checkTypePun() only handles the inline `* (T*) &x` spelling.
  void collectTypePunnedPointers() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isPunct(i, "="))
        continue;
      size_t lhs = prevSig(i);
      if (lhs == kNPos || toks_[lhs].kind != TokKind::Identifier)
        continue;
      size_t rhs = nextSig(i);
      if (!isPunct(rhs, "("))
        continue;
      size_t close = match(rhs, "(", ")");
      if (close == kNPos)
        continue;
      bool hasStar = false, charLike = false;
      for (size_t k = rhs + 1; k < close; ++k) {
        if (isPunct(k, "*"))
          hasStar = true;
        if (toks_[k].kind == TokKind::Identifier &&
            (toks_[k].text == "char" || toks_[k].text == "void"))
          charLike = true; // character types may alias anything
      }
      if (!hasStar || charLike)
        continue;
      size_t amp = nextSig(close);
      if (!isPunct(amp, "&"))
        continue;
      size_t var = nextSig(amp);
      if (var >= toks_.size() || toks_[var].kind != TokKind::Identifier)
        continue;
      typePunned_.insert(toks_[lhs].text);
    }
  }

  /// Bug 45: a write through a pointer produced by an aliasing-punning cast.
  void checkTypePunnedDeref(size_t i) {
    if (isPunct(i, "*")) {
      size_t before = prevSig(i);
      // `int *p = ...` is a declarator, not a dereference write.
      if (before != kNPos && toks_[before].kind == TokKind::Identifier &&
          isTypeWord(toks_[before].text))
        return;
      size_t n = nextSig(i);
      if (n < toks_.size() && toks_[n].kind == TokKind::Identifier &&
          typePunned_.count(toks_[n].text) && isPunct(nextSig(n), "=")) {
        errorAt(toks_[i], "type-punned write through '" + toks_[n].text +
                              "' violates strict aliasing; use memcpy");
        return;
      }
    }
    if (toks_[i].kind == TokKind::Identifier &&
        typePunned_.count(toks_[i].text)) {
      size_t lb = nextSig(i);
      if (isPunct(lb, "[")) {
        size_t rb = match(lb, "[", "]");
        if (rb != kNPos && isPunct(nextSig(rb), "="))
          errorAt(toks_[i], "type-punned write through '" + toks_[i].text +
                                "' violates strict aliasing; use memcpy");
      }
    }
  }

  //--- Unaligned pointer casts (bug 46) -------------------------------------//

  /// Collect pointers assigned (via an optional cast) from malloc/calloc etc.
  /// `byteAllocPtrs_` keeps the subset whose pointee is a byte type (char/void),
  /// the only bases for which `base + N` denotes a byte offset (bug 46).
  void collectRawAllocationPointers() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isPunct(i, "="))
        continue;
      size_t lhs = prevSig(i);
      if (lhs == kNPos || toks_[lhs].kind != TokKind::Identifier)
        continue;
      if (!isAllocator(allocatorName(nextSig(i))))
        continue;
      size_t star = prevSig(lhs);
      size_t typeTok = (star != kNPos && isPunct(star, "*")) ? prevSig(star)
                                                             : kNPos;
      if (typeTok != kNPos && toks_[typeTok].kind == TokKind::Identifier) {
        const std::string &t = toks_[typeTok].text;
        if (t == "char" || t == "void")
          byteAllocPtrs_.insert(toks_[lhs].text);
      }
    }
  }

  static size_t typeAlignment(const std::string &type) {
    if (type == "char" || type == "signed char" || type == "unsigned char" ||
        type == "bool" || type == "_Bool" || type == "int8_t" ||
        type == "uint8_t")
      return 1;
    if (type == "short" || type == "unsigned short" || type == "int16_t" ||
        type == "uint16_t")
      return 2;
    if (type == "int" || type == "unsigned" || type == "unsigned int" ||
        type == "float" || type == "int32_t" || type == "uint32_t" ||
        type == "wchar_t")
      return 4;
    if (type == "long long" || type == "unsigned long long" ||
        type == "double" || type == "int64_t" || type == "uint64_t" ||
        type == "size_t" || type == "ssize_t" || type == "intptr_t" ||
        type == "uintptr_t")
      return 8;
    if (type == "long" || type == "unsigned long")
      return 8;
    if (type == "long double")
      return 16;
    return 0;
  }

  /// Casting a pointer with a known byte offset to a stricter-aligned type is
  /// undefined when that offset is not a multiple of the target alignment.
  void checkUnalignedCasts() {
    struct PointerBytes {
      long long offset = 0;
      size_t elemBytes = 0;
    };
    std::map<std::string, PointerBytes> pointers;

    auto expressionEnd = [&](size_t from) {
      int parens = 0, brackets = 0;
      for (size_t k = from; k < toks_.size(); ++k) {
        if (isPunct(k, "(")) ++parens;
        else if (isPunct(k, ")")) {
          if (parens == 0) return k;
          --parens;
        } else if (isPunct(k, "[")) ++brackets;
        else if (isPunct(k, "]")) {
          if (brackets == 0) return k;
          --brackets;
        } else if (parens == 0 && brackets == 0 &&
                   (isPunct(k, ";") || isPunct(k, ",") ||
                    isPunct(k, "=") || isPunct(k, "{")))
          return k;
      }
      return toks_.size();
    };

    auto declaredBytePointer = [&](size_t lhs) {
      size_t begin = lhs;
      while (begin > 0) {
        --begin;
        if (trivia(begin))
          continue;
        if (isPunct(begin, ";") || isPunct(begin, "{") ||
            isPunct(begin, "}") || isPunct(begin, "(") ||
            isPunct(begin, ","))
          break;
      }
      size_t stars = 0;
      std::string type;
      for (size_t k = begin + 1; k < lhs; ++k) {
        if (trivia(k))
          continue;
        if (isPunct(k, "*")) {
          ++stars;
        } else if (toks_[k].kind == TokKind::Identifier &&
                   isTypeWord(toks_[k].text)) {
          if (!type.empty()) type += " ";
          type += toks_[k].text;
        }
      }
      return stars == 1 && (type == "char" || type == "void" ||
                            type == "signed char" ||
                            type == "unsigned char");
    };

    auto resolvePointer = [&](size_t from, size_t to, const auto &self,
                             PointerBytes *out) -> bool {
      trim(&from, &to);
      if (from >= to)
        return false;
      from = stripCast(from, to);
      trim(&from, &to);
      while (to > from && isPunct(to - 1, ")")) --to;
      trim(&from, &to);
      if (from >= to)
        return false;
      if (isPunct(from, "(")) {
        // A grouped operand such as `((char *)buf + 1)`: peel the group and
        // resolve the expression inside it.
        size_t close = matchIn(from, to, "(", ")");
        if (close == kNPos || close <= from + 1)
          return false;
        return self(from + 1, close, self, out);
      }

      if (isPunct(from, "&")) {
        size_t id = nextSig(from);
        size_t lb = nextSig(id);
        if (id >= to || lb >= to || !isPunct(lb, "["))
          return false;
        size_t rb = matchIn(lb, to, "[", "]");
        long index = 0;
        const std::vector<size_t> *dims = nullptr;
        if (rb == kNPos || !constantIndexAt(lb + 1, rb, &index) ||
            !findShape(toks_[id].text, id, &dims) || dims->empty())
          return false;
        size_t width = arrayElemBytes(toks_[id].text, id);
        if (!width)
          return false;
        out->offset = static_cast<long long>(index) * width;
        out->elemBytes = width;
        return true;
      }

      if (toks_[from].kind != TokKind::Identifier)
        return false;
      const std::string &name = toks_[from].text;
      size_t after = nextSig(from);
      const std::vector<size_t> *dims = nullptr;
      if (findShape(name, from, &dims) && !dims->empty()) {
        out->offset = 0;
        out->elemBytes = arrayElemBytes(name, from);
      } else {
        auto prior = pointers.find(name);
        if (prior == pointers.end()) {
          return false;
        }
        *out = prior->second;
      }
      if (!out->elemBytes) {
        return false;
      }
      if (after >= to)
        return true;
      if (!isPunct(after, "+") && !isPunct(after, "-"))
        return false;
      long amount = 0;
      size_t amountEnd = to;
      for (size_t k = after + 1; k < to; ++k)
        if (isPunct(k, ")") || isPunct(k, ";") || isPunct(k, ",")) {
          amountEnd = k;
          break;
        }
      if (!constantIndexAt(after + 1, amountEnd, &amount))
        return false;
      const __int128 offset = static_cast<__int128>(out->offset) +
          static_cast<__int128>(amount) * out->elemBytes *
              (isPunct(after, "+") ? 1 : -1);
      if (offset < LLONG_MIN || offset > LLONG_MAX)
        return false;
      out->offset = static_cast<long long>(offset);
      return true;
    };

    for (size_t i = 0; i < toks_.size(); ++i) {
      if (functionOpens_.count(i))
        pointers.clear();
      if (isPunct(i, "=")) {
        size_t lhs = prevSig(i);
        if (lhs != kNPos && toks_[lhs].kind == TokKind::Identifier &&
            !isWriteThroughObject(lhs)) {
          size_t rhs = nextSig(i);
          const std::string &name = toks_[lhs].text;
          PointerBytes value;
          if (isAllocator(allocatorName(rhs)) && declaredBytePointer(lhs)) {
            pointers[name] = PointerBytes{};
            pointers[name].elemBytes = 1;
          } else if (resolvePointer(rhs, expressionEnd(rhs), resolvePointer, &value)) {
            pointers[name] = value;
          } else {
            pointers.erase(name);
          }
        }
        continue;
      }
      if (!isPunct(i, "("))
        continue;
      size_t close = kNPos;
      if (!castClose(i, toks_.size(), &close))
        continue;
      bool hasStar = false;
      std::string type;
      for (size_t k = i + 1; k < close; ++k) {
        if (isPunct(k, "*"))
          hasStar = true;
        if (toks_[k].kind == TokKind::Identifier) {
          if (!type.empty()) type += " ";
          type += toks_[k].text;
        }
      }
      if (!hasStar)
        continue;
      size_t align = typeAlignment(type);
      if (align <= 1)
        continue;
      size_t from = nextSig(close);
      PointerBytes value;
      if (!resolvePointer(from, expressionEnd(from), resolvePointer, &value))
        continue;
      if (value.offset % static_cast<long long>(align) != 0)
        errorAt(toks_[i], "unaligned pointer: byte offset " +
                              std::to_string(value.offset) +
                              " is not a multiple of " +
                              std::to_string(align) + " for '" + type + "'");
    }
  }

  std::vector<Token> toks_;
  std::string source_;
  std::map<std::string, size_t> arrays_;
  std::vector<ArrayShape> localArrays_;
  std::map<std::string, FuncSig> funcs_;
  std::map<std::string, long> constants_;
  // Wave 4 (red finding).  Per-function, per-parameter constant bounds
  // gathered from call sites.  Max is the provable worst case for an OOB
  // proof; Unique is used when an exact value is required (VLA extents).
  std::map<std::string, std::map<size_t, long>> paramConstMax_;
  std::map<std::string, std::map<size_t, long>> paramConstUnique_;
  std::map<std::string, std::set<size_t>> paramConstSeen_;
  std::set<size_t> constantVlaDeclPos_;
  // A realloc/cplus_realloc whose new size cannot be folded replaces a known
  // extent with an unknown one; positions here invalidate older shapes.
  std::map<std::string, std::vector<size_t>> reallocInvalidAt_;
  std::set<std::string> reallocResultNames_; // vars assigned a realloc result
  std::set<std::string> conflict_;
  std::map<std::string, size_t> returnExtents_;
  std::set<std::string> globals_;
  std::set<std::string> volatileGlobals_;
  std::set<size_t> functionOpens_;
  std::set<std::string> pointerNames_;   // bug 35
  std::set<std::string> byteAllocPtrs_;  // bug 46
  std::map<std::string, std::vector<std::pair<size_t, uint64_t>>>
      unsignedConsts_;                   // bug 41 (value keyed by source pos)
  std::map<std::string, std::set<std::string>> negFacts_; // bug 42
  std::set<std::string> typePunned_;     // bug 45
  std::map<std::string, TrackedArrayPointer> arrayPtrs_; // constant array aliases
  std::set<std::string> arrayPtrInvalid_;
  std::set<std::string> constantVlaNames_;
  std::map<std::string, std::vector<std::pair<size_t, long>>> localConsts_;
  std::map<std::string, long> returnConstants_;
  DiagnosticEngine &diags_;
  std::string filename_;
};

} // namespace

void checkStaticBounds(const std::string &source,
                       const std::map<std::string, size_t> &arrayLengths,
                       DiagnosticEngine &diags, const std::string &filename) {
  Checker checker(source, arrayLengths, diags, filename);
  checker.run();
}

} // namespace cplus

//===- StaticBoundsCheck.cpp - C+ static bounds check -------------*- C++ -*-===//
//
// This pass is deliberately small and cheap: it reuses the C+ lexer tokens
// and the extents collected by the syntax rewriter.  It never tries to be a
// C type checker; that is the host C compiler's job.
//
//===----------------------------------------------------------------------===//

#include "Sema/StaticBoundsCheck.h"

#include "Sema/CPlusLexer.h"

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
};

class Checker {
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
    for (const auto &entry : funcs_)
      functionOpens_.insert(entry.second.bodyOpen);
    collectCallConstants();
    collectAllocations();
    collectReturnExtents();
    propagateReturnExtents();
    checkMemcpyOverlap();
    checkDataRaces();
    checkStaticBufferCalls();
    checkCtypeChar();
    checkUncheckedIo();
    checkConstCastMutation();
    checkMacroStatements();
    checkForkThreads();
    checkRestrictAliasing();
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (isIdent(i, "for"))
        checkLoop(i);
      else {
        checkTypePun(i);
        checkPointerArithmetic(i);
        checkUnrelatedPointerCompare(i);
        checkEmptyInfiniteLoop(i);
        checkNanCompare(i);
        checkVlaSizeof(i);
        checkZeroAlloc(i);
        checkFflushStdin(i);
        checkVla(i);
        if (isIdent(i, "alloca"))
          errorAt(toks_[i], "alloca has no bounds checking; use cplus_alloc");
        if (isIdent(i, "setjmp") || isIdent(i, "longjmp"))
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
      if (!dims.empty())
        localArrays_.push_back({i, toks_[i].text, dims});
    }
  }

  bool findShape(const std::string &name, size_t pos,
                 const std::vector<size_t> **shape) const {
    for (size_t k = localArrays_.size(); k > 0; --k) {
      const ArrayShape &e = localArrays_[k - 1];
      if (e.name == name && e.pos <= pos) {
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
        report(toks_[cursor], v.hi, toks_[nameIdx].text, (*dims)[d]);
      cursor = nextSig(close);
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

  void checkRestrictAliasing() {
    std::set<std::string> restrictFuncs;
    for (const auto &entry : funcs_) {
      const FuncSig &sig = entry.second;
      // funcs_ stores parameter names only; rescan the header for 'restrict'.
      bool found = false;
      for (size_t i = 0; i < toks_.size(); ++i) {
        if (!isIdent(i, "restrict")) continue;
        // Attribute the restrict keyword to the nearest preceding function
        // definition header containing this function's name.
        for (size_t k = i; k > 0; --k) {
          if (isPunct(k, ")") && isPunct(nextSig(k), "{")) {
            break;
          }
        }
        found = true;
      }
      if (found) restrictFuncs.insert(entry.first);
    }
    if (restrictFuncs.empty()) return;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (toks_[i].kind != TokKind::Identifier ||
          !restrictFuncs.count(toks_[i].text))
        continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "(")) continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos || isPunct(nextSig(rp), "{")) continue;
      auto args = splitArgs(lp + 1, rp);
      if (args.size() < 2) continue;
      std::string a = baseName(args[0].first, args[0].second);
      std::string b = baseName(args[1].first, args[1].second);
      if (!a.empty() && a == b)
        errorAt(toks_[i], "call passes overlapping pointers to restrict parameters");
    }
  }

  void checkFflushStdin(size_t i) {
    if (!isIdent(i, "fflush")) return;
    size_t lp = nextSig(i);
    size_t arg = nextSig(lp);
    if (isIdent(arg, "stdin"))
      errorAt(toks_[i], "fflush(stdin) is undefined; discard input explicitly");
  }

  void checkVla(size_t i) {
    static const char *const kTypes[] = {
        "int", "char", "short", "long", "float", "double", "void",
        "unsigned", "signed", "struct", "union", "enum", "size_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t"};
    if (toks_[i].kind != TokKind::Identifier) return;
    bool isType = false;
    for (const char *type : kTypes)
      if (toks_[i].text == type) { isType = true; break; }
    if (!isType) return;
    size_t name = nextSig(i);
    size_t lb = nextSig(name);
    if (name >= toks_.size() || toks_[name].kind != TokKind::Identifier ||
        !isPunct(lb, "["))
      return;
    size_t inner = nextSig(lb);
    size_t rb = match(lb, "[", "]");
    if (rb == kNPos || inner >= rb) return;
    if (toks_[inner].kind != TokKind::Number) {
      // A function-like expression such as offsetof(...) or sizeof(...) may
      // still be a compile-time constant; do not misreport it as a VLA.
      if (toks_[inner].kind == TokKind::Identifier &&
          isPunct(nextSig(inner), "("))
        return;
      // Uppercase names are the convention for preprocessor constants; the
      // lexer cannot expand macros, so do not misreport them as VLAs.
      bool macroLike = toks_[inner].kind == TokKind::Identifier;
      for (char c : toks_[inner].text)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
          macroLike = false;
      if (!macroLike)
        errorAt(toks_[inner], "variable-length stack allocation has no bounds "
                              "check; use cplus_alloc");
    }
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
    std::string target = baseName(args[0].first, args[0].second);
    size_t eq = kNPos;
    for (size_t k = i; k > 0; --k) {
      if (isPunct(k, "=")) { eq = k; break; }
      if (isPunct(k, ";") || isPunct(k, "{") || isPunct(k, "}"))
        break;
    }
    if (eq != kNPos) {
      size_t lhs = eq == 0 ? kNPos : eq - 1;
      while (lhs != kNPos && trivia(lhs)) {
        if (lhs == 0) { lhs = kNPos; break; }
        --lhs;
      }
      if (lhs != kNPos && toks_[lhs].kind == TokKind::Identifier &&
          toks_[lhs].text == target) {
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
    if (!isPunct(lp, "("))
      return;
    size_t rp = match(lp, "(", ")");
    if (rp == kNPos)
      return;
    bool bracket = false;
    for (size_t k = lp + 1; k < rp; ++k) {
      if (isPunct(k, "[")) bracket = true;
      if (bracket && (isPunct(k, "++") || isPunct(k, "--")))
        errorAt(toks_[i], "sizeof on a VLA evaluates the expression at run time");
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
    for (size_t k = 0; k < args.size(); ++k) {
      long value = 0;
      if (constantFactor(args[k].first, args[k].second, &value) && value == 0)
        errorAt(toks_[i], "zero-size allocation is implementation-defined; "
                          "use cplus_alloc");
    }
  }

  /// Casting away const and then writing through the result is undefined for
  /// an originally const object.  A plain cast (Lua-style ABI casts) is fine.
  void checkConstCastMutation() {
    std::set<std::string> constNames;
    std::set<std::string> castPtrs;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (functionOpens_.count(i)) {
        constNames.clear();
        castPtrs.clear();
      }
      if (isIdent(i, "const")) {
        size_t n = nextSig(i);
        while (n < toks_.size() && toks_[n].kind == TokKind::Identifier) {
          size_t after = nextSig(n);
          if (isPunct(after, "=") || isPunct(after, ";") ||
              isPunct(after, "[")) {
            constNames.insert(toks_[n].text);
            break;
          }
          n = after;
        }
        continue;
      }
      if (isPunct(i, "(")) {
        size_t close = match(i, "(", ")");
        if (close != kNPos) {
          bool hasStar = false;
          for (size_t k = i + 1; k < close; ++k)
            if (isPunct(k, "*")) hasStar = true;
          size_t after = nextSig(close);
          if (hasStar && isPunct(after, "&")) {
            size_t owner = nextSig(after);
            if (owner < toks_.size() && constNames.count(toks_[owner].text)) {
              for (size_t k = i; k > 0; --k) {
                if (isPunct(k, "=")) {
                  size_t lhs = k == 0 ? kNPos : k - 1;
                  while (lhs != kNPos && trivia(lhs)) {
                    if (lhs == 0) { lhs = kNPos; break; }
                    --lhs;
                  }
                  if (lhs != kNPos && toks_[lhs].kind == TokKind::Identifier)
                    castPtrs.insert(toks_[lhs].text);
                  break;
                }
                if (isPunct(k, ";") || isPunct(k, "{")) break;
              }
            }
          }
        }
      }
      if (toks_[i].kind == TokKind::Identifier && castPtrs.count(toks_[i].text)) {
        size_t prev = i == 0 ? kNPos : i - 1;
        while (prev != kNPos && trivia(prev)) {
          if (prev == 0) { prev = kNPos; break; }
          --prev;
        }
        size_t next = nextSig(i);
        bool derefWrite = isPunct(prev, "*") && isPunct(next, "=");
        bool indexWrite = false;
        if (isPunct(next, "[")) {
          size_t rb = match(next, "[", "]");
          if (rb != kNPos && isPunct(nextSig(rb), "="))
            indexWrite = true;
        }
        if (derefWrite || indexWrite)
          errorAt(toks_[i], "casting away const and writing to the original "
                            "object is undefined");
      }
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
                                            "localtime", "inet_ntoa"};
    for (const auto &entry : funcs_) {
      for (const char *fn : kStateful) {
        unsigned count = 0;
        size_t first = kNPos, last = kNPos;
        std::set<std::string> saved;
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
              saved.insert(toks_[lhs].text);
          }
        }
        if (count > 1 && first != kNPos) {
          bool used_after = false;
          for (size_t k = last + 1; k <= entry.second.bodyClose; ++k)
            if (toks_[k].kind == TokKind::Identifier &&
                saved.count(toks_[k].text))
              used_after = true;
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

  /// memcpy with overlapping source/destination is undefined; memmove is the
  /// correct API.
  void checkMemcpyOverlap() {
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isIdent(i, "memcpy"))
        continue;
      size_t lp = nextSig(i);
      if (!isPunct(lp, "("))
        continue;
      size_t rp = match(lp, "(", ")");
      if (rp == kNPos)
        continue;
      auto args = splitArgs(lp + 1, rp);
      if (args.size() < 2)
        continue;
      std::string dst = join(args[0].first, args[0].second);
      std::string src = join(args[1].first, args[1].second);
      bool overlap = !dst.empty() && dst == src;
      if (!overlap) {
        const std::string &shorter = dst.size() < src.size() ? dst : src;
        const std::string &longer = dst.size() < src.size() ? src : dst;
        if (longer.compare(0, shorter.size(), shorter) == 0) {
          size_t pos = shorter.size();
          while (pos < longer.size() &&
                 (longer[pos] == ' ' || longer[pos] == '\t'))
            ++pos;
          if (pos < longer.size() &&
              (longer[pos] == '+' || longer[pos] == '-'))
            overlap = true;
        }
      }
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
    std::set<std::string> workers;
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
        workers.insert(name);
    }
    for (const auto &entry : funcs_) {
      if (!workers.count(entry.first))
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
      for (const auto &part : parts) {
        size_t last = kNPos;
        for (size_t k = part.first; k < part.second; ++k)
          if (toks_[k].kind == TokKind::Identifier)
            last = k;
        if (last == kNPos)
          params.push_back(std::string());
        else
          params.push_back(toks_[last].text);
      }
      FuncSig sig;
      sig.params = params;
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
        "cplus_new"};
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
        if (toks_[i].text == "cplus_realloc") {
          if (args.size() < 2)
            continue;
          from = args[1].first;
          to = args[1].second;
        }
        if (!allocationCount(from, to, &count))
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

  std::vector<Token> toks_;
  std::string source_;
  std::map<std::string, size_t> arrays_;
  std::vector<ArrayShape> localArrays_;
  struct FuncSig {
    std::vector<std::string> params;
    size_t bodyOpen = 0;
    size_t bodyClose = 0;
  };
  std::map<std::string, FuncSig> funcs_;
  std::map<std::string, long> constants_;
  std::set<std::string> conflict_;
  std::map<std::string, size_t> returnExtents_;
  std::set<std::string> globals_;
  std::set<std::string> volatileGlobals_;
  std::set<size_t> functionOpens_;
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

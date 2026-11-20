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

class Checker {
public:
  Checker(const std::string &source,
          const std::map<std::string, size_t> &arrays,
          DiagnosticEngine &diags, const std::string &filename)
      : toks_(lexCPlus(source)), source_(source), arrays_(arrays), diags_(diags),
        filename_(filename) {
  }

  void run() {
    collectFunctions();
    collectCallConstants();
    collectAllocations();
    collectReturnExtents();
    propagateReturnExtents();
    checkPointerLeaks();
    checkMemcpyOverlap();
    checkDataRaces();
    checkSignalHandlers();
    checkStaticBufferCalls();
    checkCtypeChar();
    checkUncheckedIo();
    checkMacroStatements();
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (isIdent(i, "for"))
        checkLoop(i);
      else {
        checkConstantSubscript(i);
        checkTypePun(i);
        checkPointerArithmetic(i);
        checkUnrelatedPointerCompare(i);
        checkEmptyInfiniteLoop(i);
        checkNanCompare(i);
        checkVlaSizeof(i);
        checkZeroAlloc(i);
        if (isIdent(i, "setjmp") || isIdent(i, "longjmp"))
          errorAt(toks_[i], std::string("'") + toks_[i].text +
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
          errorAt(toks_[i],
                  "raw realloc can lose the original block on failure; use "
                  "cplus_realloc");
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
        auto it = arrays_.find(toks_[v].text);
        if (it == arrays_.end())
          continue;
        if (extent && extent != it->second)
          conflict = true;
        extent = it->second;
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
      if (name != kNPos)
        arrays_[toks_[name].text] = it->second;
    }
  }

  void errorAt(const Token &at, const std::string &message) {
    Diag d;
    d.level = DiagLevel::Error;
    d.message = message;
    d.file = filename_;
    d.line = at.line;
    d.column = at.column;
    d.help = "use C+ managed allocations and cplus_* helpers";
    diags_.report(std::move(d));
  }

  void report(const Token &at, long index, const std::string &name,
              size_t extent) {
    Diag d;
    d.level = DiagLevel::Error;
    d.message = "out-of-bounds access: index " + std::to_string(index) +
                " reaches past array '" + name + "' of length " +
                std::to_string(extent);
    d.file = filename_;
    d.line = at.line;
    d.column = at.column;
    d.help = "use a bound of " + name + ".len or a smaller index";
    diags_.report(std::move(d));
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

  void checkMacroStatements() {
    std::istringstream in(source_);
    std::string line;
    unsigned lineNo = 0;
    while (std::getline(in, line)) {
      ++lineNo;
      if (line.find("#define") == std::string::npos)
        continue;
      size_t semicolon = line.find(';');
      if (semicolon == std::string::npos)
        continue;
      if (line.find("do {") != std::string::npos &&
          line.find("while (0)") != std::string::npos)
        continue;
      Diag d;
      d.level = DiagLevel::Error;
      d.message = "multi-statement macro must be wrapped in do { ... } while (0)";
      d.file = filename_;
      d.line = lineNo;
      d.column = static_cast<unsigned>(semicolon + 1);
      d.help = "wrap the macro body to keep if/else binding correct";
      diags_.report(std::move(d));
    }
  }

  void checkPointerArithmetic(size_t i) {
    if (toks_[i].kind != TokKind::Identifier)
      return;
    auto it = arrays_.find(toks_[i].text);
    if (it == arrays_.end())
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
    if (isPunct(op, "+") && amount > static_cast<long>(it->second))
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

  void checkSignalHandlers() {
    std::set<std::string> handlers;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isIdent(i, "signal"))
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
      std::string name = baseName(args[1].first, args[1].second);
      if (!name.empty())
        handlers.insert(name);
    }
    static const char *const kUnsafe[] = {
        "printf", "fprintf", "sprintf", "malloc", "calloc", "realloc",
        "free",   "fopen",   "fclose",  "puts"};
    for (const auto &entry : funcs_) {
      if (!handlers.count(entry.first))
        continue;
      for (size_t k = entry.second.bodyOpen; k <= entry.second.bodyClose; ++k) {
        if (toks_[k].kind != TokKind::Identifier)
          continue;
        for (const char *fn : kUnsafe)
          if (toks_[k].text == fn) {
            errorAt(toks_[k], std::string("'") + fn +
                                  "' is not async-signal-safe");
            break;
          }
        if (globals_.count(toks_[k].text) &&
            !volatileGlobals_.count(toks_[k].text)) {
          size_t next = nextSig(k);
          if (isPunct(next, "=") || isPunct(next, "++") ||
              isPunct(next, "--"))
            errorAt(toks_[k], "signal-modified global must be volatile "
                              "sig_atomic_t: '" + toks_[k].text + "'");
        }
      }
    }
  }

  /// strtok (and friends) keep internal static state; a second call in the
  /// same function silently invalidates the first token stream.
  void checkStaticBufferCalls() {
    static const char *const kStateful[] = {"strtok", "asctime", "gmtime",
                                            "localtime", "inet_ntoa"};
    for (const auto &entry : funcs_) {
      for (const char *fn : kStateful) {
        unsigned count = 0;
        size_t first = kNPos;
        for (size_t k = entry.second.bodyOpen; k <= entry.second.bodyClose; ++k)
          if (isIdent(k, fn)) {
            if (first == kNPos)
              first = k;
            ++count;
          }
        if (count > 1 && first != kNPos)
          errorAt(toks_[first], std::string("'") + fn +
                                    "' keeps static state; a second call "
                                    "invalidates earlier results");
      }
    }
  }

  void checkCtypeChar() {
    std::set<std::string> plainChars;
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (!isIdent(i, "char"))
        continue;
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
    }
    static const char *const kCtype[] = {
        "isalpha", "isdigit", "isalnum", "isspace", "islower", "isupper",
        "tolower", "toupper", "ispunct", "isxdigit"};
    for (size_t i = 0; i < toks_.size(); ++i) {
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
      size_t lp = nextSig(i);
      size_t arg = nextSig(lp);
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
      if (rp != kNPos && isPunct(nextSig(rp), ";"))
        errorAt(toks_[i], "check the return value of " + toks_[i].text);
    }
  }

  std::string baseName(size_t from, size_t to) const {
    std::string name;
    for (size_t k = from; k < to; ++k)
      if (toks_[k].kind == TokKind::Identifier)
        name = toks_[k].text; // last identifier survives casts/field access
    return name;
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
      std::string dst = baseName(args[0].first, args[0].second);
      std::string src = baseName(args[1].first, args[1].second);
      if (!dst.empty() && dst == src)
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

  /// Reassigning a pointer that still owns a C+ allocation loses the old
  /// block.  Reassigning from cplus_realloc(p, ...) is allowed.
  void checkPointerLeaks() {
    std::map<std::string, bool> live;
    for (size_t i = 0; i < toks_.size(); ++i) {
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
      if (count > 0)
        arrays_[toks_[name].text] = count;
    }
  }

  /// A subscript with a literal index that is known too large.
  void checkConstantSubscript(size_t i) {
    if (toks_[i].kind != TokKind::Identifier || !arrays_.count(toks_[i].text))
      return;
    // `int a[4];` is a declaration, not an access.  A declaration is
    // preceded by a type name; an access is preceded by punctuation or a
    // control keyword such as `return`.
    size_t prev = i == 0 ? kNPos : (i - 1);
    while (prev != kNPos && trivia(prev)) {
      if (prev == 0) { prev = kNPos; break; }
      --prev;
    }
    if (prev != kNPos && toks_[prev].kind == TokKind::Identifier) {
      static const char *const kControl[] = {
          "return", "if", "for", "while", "sizeof", "case", "else", "do"};
      bool control = false;
      for (const char *word : kControl)
        if (toks_[prev].text == word) {
          control = true;
          break;
        }
      if (!control)
        return;
    }
    size_t open = nextSig(i);
    if (!isPunct(open, "["))
      return;
    size_t close = match(open, "[", "]");
    size_t indexTok = nextSig(open);
    if (indexTok >= close)
      return;
    long idx = 0;
    if (!number(toks_[indexTok], &idx))
      return;
    size_t extent = arrays_.at(toks_[i].text);
    if (idx >= 0 && static_cast<size_t>(idx) >= extent)
      report(toks_[open], idx, toks_[i].text, extent);
  }

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
      if (toks_[k].kind != TokKind::Identifier ||
          !arrays_.count(toks_[k].text))
        continue;
      size_t bopen = nextSig(k);
      if (!isPunct(bopen, "["))
        continue;
      size_t bclose = match(bopen, "[", "]");
      if (bclose == kNPos)
        continue;
      size_t indexTok = nextSig(bopen);
      if (indexTok >= bclose ||
          !(toks_[indexTok].kind == TokKind::Identifier &&
            toks_[indexTok].text == var))
        continue;
      size_t extent = arrays_.at(toks_[k].text);
      if (static_cast<unsigned long>(maxIndex) >= extent)
        report(toks_[indexTok], maxIndex, toks_[k].text, extent);
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

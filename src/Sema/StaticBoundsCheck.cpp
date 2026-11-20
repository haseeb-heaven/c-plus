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
      : toks_(lexCPlus(source)), arrays_(arrays), diags_(diags),
        filename_(filename) {
  }

  void run() {
    collectFunctions();
    collectCallConstants();
    collectAllocations();
    for (size_t i = 0; i < toks_.size(); ++i) {
      if (isIdent(i, "for"))
        checkLoop(i);
      else
        checkConstantSubscript(i);
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
      funcs_[toks_[i].text] = params;
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
      if (args.size() != it->second.size())
        continue;
      for (size_t k = 0; k < args.size(); ++k) {
        if (it->second[k].empty())
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
          conflict_.insert(it->second[k]);
          continue;
        }
        auto c = constants_.find(it->second[k]);
        if (c != constants_.end() && c->second != value)
          conflict_.insert(it->second[k]);
        else
          constants_[it->second[k]] = value;
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
  std::map<std::string, size_t> arrays_;
  std::map<std::string, std::vector<std::string>> funcs_;
  std::map<std::string, long> constants_;
  std::set<std::string> conflict_;
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

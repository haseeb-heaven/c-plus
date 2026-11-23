//===- CPlusRewriter.cpp - C+ syntax lowering -------------------*- C++ -*-===//
//
// Lowers the C+ specific surface syntax to plain C:
//
//   resource struct File { ... };     -> struct File { ... };
//   class Player { fields; methods; } -> struct Player { fields; };
//                                        ret Player_m(Player *self, ...) {...}
//   managed T *p = cplus_new(T);      -> T *p = cplus_new(T);
//                                        /*cplus$mem:p*/
//   owned T h = ...;                  -> T h = ...;
//                                        /*cplus$own:h:T_drop*/
//   p.method(args)                    -> Player_method(&p, args)
//   p->method(args)                   -> Player_method(p, args)
//
// Unchanged source is copied verbatim, so rejected C constructs produce the
// same diagnostics as the host C compiler.
//
//===----------------------------------------------------------------------===//

#include "Sema/CPlusRewriter.h"

#include "LangFeatures.h"
#include "Sema/CPlusLexer.h"
#include "Sema/SemaCPlus.h"

#include <cstdio>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace cplus {

namespace {

constexpr size_t kNPos = static_cast<size_t>(-1);

std::set<std::string> &externalClasses() {
  static std::set<std::string> names;
  return names;
}

std::set<std::string> &externalCtors() {
  static std::set<std::string> names;
  return names;
}

std::set<std::string> &externalResources() {
  static std::set<std::string> names;
  return names;
}

struct MethodInfo {
  std::string name;
  size_t itemStart = 0;
  size_t nameIdx = 0;
  size_t lparen = 0;
  size_t rparen = 0;
  size_t bodyOpen = 0;
  size_t bodyClose = 0;
};

struct ClassInfo {
  std::string name;
  size_t keyword = 0;
  size_t nameIdx = 0;
  size_t bodyOpen = 0;
  size_t bodyClose = 0;
  std::vector<std::pair<size_t, size_t>> fields; // inclusive ranges
  std::vector<MethodInfo> methods;
  std::set<std::string> methodNames;
  bool hasCtor = false;
  bool hasDrop = false;
};

struct FuncDefaults {
  size_t nameIdx = 0;
  size_t lparen = 0;
  size_t rparen = 0;
  size_t paramCount = 0;
  std::vector<std::string> defaults; // per parameter, empty when none
};

struct ResourceInfo {
  std::string name;
  size_t keyword = 0;
  size_t nameIdx = 0;
  size_t bodyOpen = 0;
  size_t bodyClose = 0;
};

class TokenStream {
public:
  explicit TokenStream(const std::vector<Token> &t) : toks_(t) {}

  const Token &operator[](size_t i) const { return toks_[i]; }
  size_t size() const { return toks_.size(); }

  bool isTrivia(size_t i) const {
    return i < toks_.size() && (toks_[i].kind == TokKind::Whitespace ||
                                toks_[i].kind == TokKind::Comment);
  }

  bool isIdent(size_t i, const char *text) const {
    return i < toks_.size() && toks_[i].kind == TokKind::Identifier &&
           toks_[i].text == text;
  }

  bool isPunct(size_t i, const char *text) const {
    return i < toks_.size() && toks_[i].kind == TokKind::Punct &&
           toks_[i].text == text;
  }

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

  size_t matchBrace(size_t open) const {
    int depth = 0;
    for (size_t i = open; i < toks_.size(); ++i) {
      if (!isPunct(i, "{") && !isPunct(i, "}"))
        continue;
      depth += isPunct(i, "{") ? 1 : -1;
      if (depth == 0)
        return i;
    }
    return kNPos;
  }

  size_t matchBracket(size_t open) const {
    int depth = 0;
    for (size_t i = open; i < toks_.size(); ++i) {
      if (!isPunct(i, "[") && !isPunct(i, "]"))
        continue;
      depth += isPunct(i, "[") ? 1 : -1;
      if (depth == 0)
        return i;
    }
    return kNPos;
  }

  /// Find the matching ')' for the '(' at `open`.
  size_t matchParen(size_t open) const {
    int depth = 0;
    for (size_t i = open; i < toks_.size(); ++i) {
      if (!isPunct(i, "(") && !isPunct(i, ")"))
        continue;
      depth += isPunct(i, "(") ? 1 : -1;
      if (depth == 0)
        return i;
    }
    return kNPos;
  }

  /// Token index of the `;` that terminates a declaration starting at `i`,
  /// ignoring semicolons nested in parentheses, brackets and braces.
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

private:
  const std::vector<Token> &toks_;
};

bool isTypeStarter(const std::string &s) {
  static const std::set<std::string> kStarters = {
      "struct", "union", "enum", "const",   "volatile", "static",
      "register", "extern", "unsigned", "signed", "short",    "long",
      "_Atomic",  "auto"};
  return kStarters.count(s) != 0;
}

/// Reconstruct a declaration fragment from tokens, dropping comments and
/// collapsing whitespace to single spaces.
std::string joinTokens(const TokenStream &ts, size_t from, size_t to) {
  std::string out;
  for (size_t i = from; i < to; ++i) {
    if (ts.isTrivia(i))
      continue;
    if (!out.empty())
      out += " ";
    out += ts[i].text;
  }
  return out;
}

std::string lineDirective(const std::string &file, unsigned line) {
  std::string escaped;
  for (char c : file) {
    if (c == '\\' || c == '"')
      escaped += '\\';
    escaped += c;
  }
  return "#line " + std::to_string(line) + " \"" + escaped + "\"\n";
}

class Lowerer {
public:
  Lowerer(const std::vector<Token> &toks, DiagnosticEngine &diags,
          const std::string &filename, bool headerMode)
      : ts(toks), diags_(diags), filename_(filename),
        headerMode_(headerMode) {}

  RewrittenSource run() {
    collectDeclarations();
    collectDefaultArgs();
    collectVariableTypes();
    collectAutoClasses();
    collectArrays();
    collectDynamicPointers();
    emitTranslationUnit();

    RewrittenSource result;
    result.text = lineDirective(filename_, 1) + out_;
    result.changed = changed_;
    result.resourceTypes = resourceTypes_;
    result.classTypes = classTypes_;
    result.arrayLengths = arrays_;
    return result;
  }

private:
  // --- declaration discovery ------------------------------------------------

  void collectDeclarations() {
    for (size_t i = 0; i < ts.size(); ++i) {
      if (ts.isIdent(i, kw::kResource)) {
        ResourceInfo r;
        if (parseResource(i, &r)) {
          resourceTypes_.push_back(r.name);
          resourceNames_.insert(r.name);
          resources_.push_back(r);
          i = r.bodyClose;
          continue;
        }
      }
      if (ts.isIdent(i, kw::kClass)) {
        ClassInfo c;
        if (parseClass(i, &c)) {
          if (c.hasDrop)
            resourceTypes_.push_back(c.name);
          if (c.hasCtor)
            classNamesWithCtor_.insert(c.name);
          classNames_.insert(c.name);
          classTypes_.push_back(c.name);
          classes_.push_back(std::move(c));
          i = classes_.back().bodyClose;
          continue;
        }
      }
    }
    for (const auto &name : externalResources())
      resourceTypes_.push_back(name);
    for (const auto &name : externalClasses()) {
      classNames_.insert(name);
      classTypes_.push_back(name);
      externalClassNames_.insert(name);
      if (externalCtors().count(name))
        classNamesWithCtor_.insert(name);
    }
  }

  bool parseResource(size_t i, ResourceInfo *r) {
    size_t s = ts.nextSig(i);
    if (ts.isIdent(s, "struct") || ts.isIdent(s, "union"))
      s = ts.nextSig(s);
    if (s >= ts.size() || ts[s].kind != TokKind::Identifier)
      return false;
    size_t open = ts.nextSig(s);
    if (!ts.isPunct(open, "{"))
      return false;
    size_t close = ts.matchBrace(open);
    if (close == kNPos)
      return false;
    r->keyword = i;
    r->name = ts[s].text;
    r->nameIdx = s;
    r->bodyOpen = open;
    r->bodyClose = close;
    return true;
  }

  bool parseClass(size_t i, ClassInfo *c) {
    size_t s = ts.nextSig(i);
    if (s >= ts.size() || ts[s].kind != TokKind::Identifier)
      return false;
    size_t open = ts.nextSig(s);
    if (!ts.isPunct(open, "{"))
      return false;
    size_t close = ts.matchBrace(open);
    if (close == kNPos)
      return false;

    c->keyword = i;
    c->name = ts[s].text;
    c->nameIdx = s;
    c->bodyOpen = open;
    c->bodyClose = close;

    size_t p = open + 1;
    while (true) {
      p = ts.nextSig(p);
      if (p >= close)
        break;

      // Find the first top-level ';' or '{'.
      size_t q = p;
      int paren = 0;
      size_t lparen = kNPos, rparen = kNPos, semi = kNPos, brace = kNPos;
      for (; q < close; ++q) {
        if (ts.isPunct(q, "(")) {
          if (paren == 0 && lparen == kNPos)
            lparen = q;
          ++paren;
        } else if (ts.isPunct(q, ")")) {
          --paren;
          if (paren == 0)
            rparen = q;
        } else if (ts.isPunct(q, ";") && paren == 0) {
          semi = q;
          break;
        } else if (ts.isPunct(q, "{") && paren == 0) {
          brace = q;
          break;
        }
      }

      if (brace == kNPos && semi == kNPos) {
        diags_.error("malformed class member in class '" + c->name + "'",
                     filename_, ts[p].line, ts[p].column);
        return true;
      }

      if (semi != kNPos && (brace == kNPos || semi < brace)) {
        if (lparen != kNPos && lparen < semi) {
          diags_.error("class methods must be defined inline (no declaration "
                       "without a body)",
                       filename_, ts[p].line, ts[p].column);
          return true;
        }
        c->fields.emplace_back(p, semi);
        p = semi + 1;
        continue;
      }

      // A method: <type...> <name> ( params ) { body }
      if (lparen == kNPos || rparen == kNPos || rparen > brace) {
        diags_.error("malformed method declaration in class '" + c->name + "'",
                     filename_, ts[p].line, ts[p].column);
        return true;
      }
      size_t nameIdx = ts.prevSig(lparen);
      if (nameIdx == kNPos || ts[nameIdx].kind != TokKind::Identifier) {
        diags_.error("expected a method name before '(' in class '" +
                         c->name + "'",
                     filename_, ts[p].line, ts[p].column);
        return true;
      }
      MethodInfo m;
      m.itemStart = p;
      m.nameIdx = nameIdx;
      m.lparen = lparen;
      m.rparen = rparen;
      m.bodyOpen = brace;
      m.bodyClose = ts.matchBrace(brace);
      m.name = ts[nameIdx].text;
      if (m.bodyClose == kNPos) {
        diags_.error("unterminated method body in class '" + c->name + "'",
                     filename_, ts[brace].line, ts[brace].column);
        return true;
      }
      if (m.name == c->name) {
        if (!joinTokens(ts, m.itemStart, m.nameIdx).empty()) {
          diags_.error("a constructor cannot declare a return type",
                       filename_, ts[nameIdx].line, ts[nameIdx].column);
          return true;
        }
        c->hasCtor = true;
      }
      std::string dropParams = joinTokens(ts, m.lparen + 1, m.rparen);
      if (m.name == "drop" &&
          (dropParams.empty() || dropParams == "void"))
        c->hasDrop = true;
      c->methodNames.insert(m.name);
      c->methods.push_back(m);
      p = m.bodyClose + 1;
      if (ts.isPunct(p, ";"))
        p = ts.nextSig(p);
    }
    return true;
  }

  /// Record `ClassName var` and `ClassName *var` so member calls can be
  /// lowered without a type checker.
  void collectVariableTypes() {
    for (size_t i = 0; i < ts.size(); ++i) {
      if (ts[i].kind != TokKind::Identifier || !classNames_.count(ts[i].text))
        continue;
      // Skip the class declaration header itself.
      bool inDecl = false;
      for (const auto &c : classes_)
        if (i >= c.keyword && i <= c.bodyClose)
          inDecl = true;
      if (inDecl)
        continue;

      size_t n = ts.nextSig(i);
      if (ts.isPunct(n, "*"))
        n = ts.nextSig(n);
      if (n < ts.size() && ts[n].kind == TokKind::Identifier &&
          !isTypeStarter(ts[n].text)) {
        varClasses_[ts[n].text] = ts[i].text;
      }
    }
  }

  /// Built-in type words / qualifiers that may follow `owned` or `managed`
  /// in a genuine C+ declaration.  Kept here (rather than local to
  /// isDeclarationName) so both helpers agree on what a type token is.
  static bool isBuiltinTypeWord(const std::string &s) {
    static const std::set<std::string> kWords = {
        "int",     "char",    "short",   "long",    "float",   "double",
        "_Bool",   "void",    "size_t",  "struct",  "union",   "enum",
        "unsigned", "signed", "const",   "volatile", "static", "extern",
        "register", "_Atomic"};
    return kWords.count(s) != 0;
  }

  /// Declaration qualifiers that may precede the actual type word.
  static bool isDeclQualifier(const std::string &s) {
    static const std::set<std::string> kWords = {
        "const", "volatile", "static", "register", "extern",
        "unsigned", "signed", "short", "long", "_Atomic"};
    return kWords.count(s) != 0;
  }

  /// `owned` and `managed` are *contextual* keywords: ordinary C programs may
  /// legitimately use them as identifiers (parameters, typedef names, struct
  /// tags, function names, macro names).  Only lower them when the token
  /// begins a genuine C+ declaration:
  ///   [statement boundary] KEYWORD [quals] TYPE [*] NAME = ... ;
  /// Anything else is emitted verbatim as plain C.  This is mode-independent
  /// on purpose -- plain C that happens to use these names must never be
  /// rewritten or diagnosed, in LEGACY *or* Safe mode.
  bool looksLikeOwnedDeclaration(size_t start) const {
    // 1. Must begin a declaration, not appear inside an expression or as a
    //    declarator name following a type/keyword/punctuation.
    size_t prev = ts.prevSig(start);
    if (prev != kNPos &&
        !(ts.isPunct(prev, ";") || ts.isPunct(prev, "{") ||
          ts.isPunct(prev, "}") || ts.isPunct(prev, ":")))
      return false;
    // Not part of a preprocessor directive (#define managed ...).
    size_t lineStart = start;
    while (lineStart > 0 && ts[lineStart - 1].line == ts[start].line)
      --lineStart;
    for (size_t k = lineStart; k < start; ++k)
      if (ts.isPunct(k, "#"))
        return false;

    // 2. Must have a statement terminator at this nesting level.
    size_t semi = ts.statementEnd(start);
    if (semi == kNPos)
      return false;

    // 3. Must have a top-level initializer (both C+ forms initialise).
    size_t eq = kNPos;
    int paren = 0, bracket = 0;
    for (size_t i = start + 1; i < semi; ++i) {
      if (ts.isPunct(i, "(")) ++paren;
      else if (ts.isPunct(i, ")")) --paren;
      else if (ts.isPunct(i, "[")) ++bracket;
      else if (ts.isPunct(i, "]")) --bracket;
      else if (ts.isPunct(i, "=") && paren == 0 && bracket == 0) {
        eq = i;
        break;
      }
    }
    if (eq == kNPos)
      return false;

    // 4. Skip only declaration qualifiers; the next token is the type.  We
    //    must NOT skip the core type word: `owned int v = 1;` is the genuine
    //    C+ declaration that produces the "'int' is not a resource" error.
    size_t typeIdx = ts.nextSig(start);
    while (typeIdx != kNPos && typeIdx < semi &&
           isDeclQualifier(ts[typeIdx].text))
      typeIdx = ts.nextSig(typeIdx);
    if (typeIdx == kNPos || typeIdx >= semi ||
        ts[typeIdx].kind != TokKind::Identifier)
      return false;
    const std::string typeName = ts[typeIdx].text;
    const bool isManaged = ts.isIdent(start, kw::kManaged);
    const bool knownType = isBuiltinTypeWord(typeName) ||
                           classNames_.count(typeName) ||
                           resourceNames_.count(typeName) ||
                           externalClassNames_.count(typeName);
    if (!knownType) {
      // An unknown identifier type after `owned` is a plain C typedef name
      // ("typedef int owned; owned x = 1;"), not a C+ declaration.
      if (!isManaged)
        return false;
      // `managed` always declares a pointer; require a '*' to disambiguate
      // from an ordinary C identifier.
      size_t star = ts.nextSig(typeIdx);
      if (star == kNPos || star >= semi || !ts.isPunct(star, "*"))
        return false;
    }

    // 5. A declared name must appear between the type and the '='.
    bool haveName = false;
    for (size_t i = ts.nextSig(typeIdx); i < eq; ++i)
      if (ts[i].kind == TokKind::Identifier && !isBuiltinTypeWord(ts[i].text))
        haveName = true;
    return haveName;
  }

  /// True when `nameIdx` looks like the declared name of an array rather than
  /// an array subscript in an expression.
  bool isDeclarationName(size_t nameIdx) const {
    size_t prev = ts.prevSig(nameIdx);
    if (prev == kNPos)
      return false;
    auto isPrimitive = [](const std::string &s) {
      static const std::set<std::string> kPrimitives = {
          "int",     "char",   "short",  "long",   "float", "double",
          "_Bool",   "void",   "size_t", "struct", "union", "enum",
          "unsigned", "signed", "const", "volatile", "static", "extern",
          "register", "_Atomic"};
      return kPrimitives.count(s) != 0;
    };
    if (isPrimitive(ts[prev].text) || classNames_.count(ts[prev].text) ||
        resourceNames_.count(ts[prev].text))
      return true;
    if (ts[prev].kind != TokKind::Identifier)
      return false;
    static const std::set<std::string> kControl = {
        "return", "if", "else", "for", "while", "do", "switch",
        "case", "break", "continue", "goto", "sizeof"};
    if (kControl.count(ts[prev].text))
      return false;
    size_t prev2 = ts.prevSig(prev);
    if (prev2 == kNPos)
      return false;
    return isPrimitive(ts[prev2].text) || ts.isPunct(prev2, ";") ||
           ts.isPunct(prev2, "{") || ts.isPunct(prev2, "}") ||
           ts.isPunct(prev2, "(") || ts.isPunct(prev2, ",");
  }

  /// Number of top-level elements in an aggregate initialiser.
  size_t initializerCount(size_t open, size_t close) const {
    int paren = 0, bracket = 0, brace = 0;
    size_t commas = 0;
    bool any = false;
    for (size_t k = open + 1; k < close; ++k) {
      if (ts.isPunct(k, "(")) ++paren;
      else if (ts.isPunct(k, ")")) --paren;
      else if (ts.isPunct(k, "[")) ++bracket;
      else if (ts.isPunct(k, "]")) --bracket;
      else if (ts.isPunct(k, "{")) ++brace;
      else if (ts.isPunct(k, "}")) --brace;
      else if (ts.isPunct(k, ",") && paren == 0 && bracket == 0 && brace == 0)
        ++commas;
      else if (!ts.isTrivia(k))
        any = true;
    }
    return any ? commas + 1 : 0;
  }

  /// `auto state = (Class){...}` is a class-typed variable too, so method
  /// call sugar must know its class.
  void collectAutoClasses() {
    for (size_t i = 0; i < ts.size(); ++i) {
      if (!ts.isIdent(i, "auto"))
        continue;
      size_t name = ts.nextSig(i);
      size_t eq = ts.nextSig(name);
      size_t tok = ts.nextSig(eq);
      if (name >= ts.size() || ts[name].kind != TokKind::Identifier ||
          !ts.isPunct(eq, "=") || tok >= ts.size())
        continue;
      size_t cls = kNPos;
      if (ts.isPunct(tok, "(")) {
        cls = ts.nextSig(tok);          // auto x = (Class){...}
      } else if (ts[tok].kind == TokKind::Identifier &&
                 ts.isPunct(ts.nextSig(tok), "(")) {
        cls = tok;                      // auto x = Class(...)
      }
      if (cls == kNPos || cls >= ts.size() ||
          ts[cls].kind != TokKind::Identifier ||
          !classNames_.count(ts[cls].text))
        continue;
      varClasses_[ts[name].text] = ts[cls].text;
    }
  }

  /// Record fixed-size arrays so `arr.len` can be replaced by a constant.
  void collectArrays() {
    for (size_t i = 0; i < ts.size(); ++i) {
      if (ts[i].kind != TokKind::Identifier)
        continue;
      size_t open = ts.nextSig(i);
      if (!ts.isPunct(open, "[") || !isDeclarationName(i))
        continue;
      size_t close = ts.matchBracket(open);
      if (close == kNPos)
        continue;
      size_t len = kNPos;
      size_t inner = ts.nextSig(open);
      if (inner == close) {
        size_t eq = ts.nextSig(close);
        if (!ts.isPunct(eq, "="))
          continue;
        size_t brace = ts.nextSig(eq);
        if (!ts.isPunct(brace, "{"))
          continue;
        size_t end = ts.matchBrace(brace);
        if (end == kNPos)
          continue;
        len = initializerCount(brace, end);
      } else if (ts[inner].kind == TokKind::Number) {
        len = static_cast<size_t>(strtoull(ts[inner].text.c_str(), nullptr, 0));
      }
      if (len != kNPos && len > 0)
        arrays_[ts[i].text] = len;
    }
  }

  /// Split a token range on top-level commas.
  std::vector<std::pair<size_t, size_t>>
  splitTopLevel(size_t from, size_t to) const {
    std::vector<std::pair<size_t, size_t>> out;
    int paren = 0, bracket = 0, brace = 0;
    size_t start = from;
    for (size_t k = from; k < to; ++k) {
      if (ts.isPunct(k, "(")) ++paren;
      else if (ts.isPunct(k, ")")) --paren;
      else if (ts.isPunct(k, "[")) ++bracket;
      else if (ts.isPunct(k, "]")) --bracket;
      else if (ts.isPunct(k, "{")) ++brace;
      else if (ts.isPunct(k, "}")) --brace;
      else if (ts.isPunct(k, ",") && paren == 0 && bracket == 0 && brace == 0) {
        out.push_back({start, k});
        start = k + 1;
      }
    }
    out.push_back({start, to});
    return out;
  }

  /// Collect function definitions whose parameters use C+ default arguments.
  void collectDefaultArgs() {
    static const std::set<std::string> kControl = {
        "if", "for", "while", "switch", "return", "sizeof", "do", "case"};
    for (size_t i = 0; i < ts.size(); ++i) {
      if (ts[i].kind != TokKind::Identifier || kControl.count(ts[i].text))
        continue;
      bool inClass = false;
      for (const auto &c : classes_)
        if (i >= c.keyword && i <= c.bodyClose)
          inClass = true;
      if (inClass)
        continue;
      size_t lp = ts.nextSig(i);
      if (!ts.isPunct(lp, "("))
        continue;
      size_t rp = ts.matchParen(lp);
      if (rp == kNPos || !ts.isPunct(ts.nextSig(rp), "{"))
        continue;

      auto parts = splitTopLevel(lp + 1, rp);
      if (parts.size() == 1) {
        std::string only = joinTokens(ts, parts[0].first, parts[0].second);
        if (only.empty() || only == "void")
          continue;
      }
      FuncDefaults f;
      f.nameIdx = i;
      f.lparen = lp;
      f.rparen = rp;
      f.paramCount = parts.size();
      bool any = false;
      for (const auto &part : parts) {
        size_t eq = kNPos;
        int paren = 0, bracket = 0;
        for (size_t k = part.first; k < part.second; ++k) {
          if (ts.isPunct(k, "(")) ++paren;
          else if (ts.isPunct(k, ")")) --paren;
          else if (ts.isPunct(k, "[") ) ++bracket;
          else if (ts.isPunct(k, "]")) --bracket;
          else if (ts.isPunct(k, "=") && paren == 0 && bracket == 0) {
            eq = k;
            break;
          }
        }
        if (eq == kNPos) {
          f.defaults.push_back(std::string());
        } else {
          any = true;
          f.defaults.push_back(joinTokens(ts, eq + 1, part.second));
        }
      }
      if (!any)
        continue;
      defaultsByName_[ts[i].text] = funcsWithDefaults_.size();
      funcsWithDefaults_.push_back(f);
    }
  }

  bool tryEmitDefaultDefinition(size_t i) {
    auto it = defaultsByName_.find(ts[i].text);
    if (it == defaultsByName_.end())
      return false;
    const FuncDefaults &f = funcsWithDefaults_[it->second];
    if (f.nameIdx != i)
      return false;
    auto parts = splitTopLevel(f.lparen + 1, f.rparen);
    emitToken(i);
    emit("(");
    for (size_t k = 0; k < parts.size(); ++k) {
      if (k)
        emit(", ");
      size_t end = parts[k].second;
      int paren = 0, bracket = 0;
      for (size_t n = parts[k].first; n < parts[k].second; ++n) {
        if (ts.isPunct(n, "(")) ++paren;
        else if (ts.isPunct(n, ")")) --paren;
        else if (ts.isPunct(n, "[")) ++bracket;
        else if (ts.isPunct(n, "]")) --bracket;
        else if (ts.isPunct(n, "=") && paren == 0 && bracket == 0) {
          end = n;
          break;
        }
      }
      emitRange(parts[k].first, end);
    }
    emit(")");
    skipTo_ = f.rparen;
    changed_ = true;
    return true;
  }

  bool tryEmitDefaultCall(size_t i) {
    auto it = defaultsByName_.find(ts[i].text);
    if (it == defaultsByName_.end())
      return false;
    const FuncDefaults &f = funcsWithDefaults_[it->second];
    if (f.nameIdx == i)
      return false;
    size_t lp = ts.nextSig(i);
    if (!ts.isPunct(lp, "("))
      return false;
    size_t rp = ts.matchParen(lp);
    if (rp == kNPos)
      return false;
    auto args = splitTopLevel(lp + 1, rp);
    size_t argc = (args.size() == 1 && joinTokens(ts, args[0].first,
                                                  args[0].second).empty())
                      ? 0
                      : args.size();
    emitToken(i);
    emit("(");
    if (argc > 0)
      emitRange(lp + 1, rp);
    for (size_t k = argc; k < f.paramCount; ++k) {
      if (f.defaults[k].empty()) {
        diags_.error("no default argument for parameter " + std::to_string(k + 1) +
                         " in call to '" + ts[i].text + "'",
                     filename_, ts[i].line, ts[i].column);
        continue;
      }
      if (argc > 0 || k > argc)
        emit(", ");
      emit(f.defaults[k]);
    }
    emit(")");
    skipTo_ = rp;
    changed_ = true;
    return true;
  }

  // --- emission -------------------------------------------------------------

  void emit(const std::string &s) { out_ += s; }

  void emitToken(size_t i) {
    bool instance = ts.isIdent(i, "self") || ts.isIdent(i, "this");
    if (ctorBody_ && instance) {
      out_ += "_cplus_self";
      return;
    }
    if (inMethod_ && ts.isIdent(i, "this")) {
      out_ += "self";          // C++-style this -> lowered self parameter
      return;
    }
    if (ctorBody_ && ts.isPunct(i, "->")) {
      out_ += ".";
      return;
    }
    out_ += ts[i].text;
  }

  size_t classAt(size_t i) const {
    for (size_t k = 0; k < classes_.size(); ++k)
      if (classes_[k].keyword == i)
        return k;
    return kNPos;
  }

  size_t resourceAt(size_t i) const {
    for (size_t k = 0; k < resources_.size(); ++k)
      if (resources_[k].keyword == i)
        return k;
    return kNPos;
  }

  void emitTranslationUnit() {
    for (size_t i = 0; i < ts.size(); ++i) {
      if (tryEmitRangeFor(i) || tryEmitDefaultDefinition(i) ||
          tryEmitDefaultCall(i)) {
        i = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      if (size_t ci = classAt(i); ci != kNPos) {
        emitClass(classes_[ci]);
        i = classes_[ci].bodyClose;
        if (ts.isPunct(i + 1, ";"))
          ++i;
        size_t next = ts.nextSig(i);
        if (next < ts.size())
          emit(lineDirective(filename_, ts[next].line));
        continue;
      }
      if (size_t ri = resourceAt(i); ri != kNPos) {
        emitResource(resources_[ri]);
        i = resources_[ri].bodyClose;
        size_t semi = ts.nextSig(i);
        if (ts.isPunct(semi, ";"))
          i = semi;
        size_t next = ts.nextSig(i);
        if (next < ts.size())
          emit(lineDirective(filename_, ts[next].line));
        continue;
      }
      if ((ts.isIdent(i, kw::kManaged) || ts.isIdent(i, kw::kOwned)) &&
          looksLikeOwnedDeclaration(i)) {
        if (emitOwnedDeclaration(i)) {
          i = skipTo_;
          skipTo_ = kNPos;
          continue;
        }
      }
      if (ts.isIdent(i, "auto") && tryEmitAuto(i)) {
        i = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      if (tryEmitCountof(i) || tryEmitIfDecl(i) || tryEmitOctal(i) ||
          tryEmitCtorCall(i) ||
          tryEmitArrayLen(i) || tryEmitMethodCall(i)) {
        i = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      emitToken(i);
      auto extra = extraBraces_.find(i);
      if (extra != extraBraces_.end()) {
        for (int n = 0; n < extra->second; ++n)
          emit("}");
        extraBraces_.erase(extra);
      }
    }
  }

  /// `.hp` headers are lowered into each including translation unit; class
  /// members get internal linkage so repeated inclusion cannot duplicate
  /// symbols. `static inline` also avoids unused-function warnings.
  void emitMemberLinkage() {
    if (headerMode_)
      emit("static inline ");
  }

  void emitResource(const ResourceInfo &r) {
    // Drop the `resource` keyword.  If the user wrote `resource Name {`,
    // synthesise the `struct` keyword.
    size_t after = ts.nextSig(r.keyword);
    if (!ts.isIdent(after, "struct") && !ts.isIdent(after, "union"))
      emit("struct ");
    for (size_t i = r.keyword + 1; i <= r.bodyClose; ++i)
      emitToken(i);
    size_t semi = ts.nextSig(r.bodyClose);
    if (ts.isPunct(semi, ";"))
      emitToken(semi);
    // C+ type names are usable without the `struct` tag, like typedefs.
    emit("\ntypedef struct ");
    emit(r.name);
    emit(" ");
    emit(r.name);
    emit(";\n");
    changed_ = true;
  }

  void emitClass(const ClassInfo &c) {
    changed_ = true;
    emit("struct ");
    emit(c.name);
    emit(" {\n");
    if (c.fields.empty())
      emit("    char _cplus_empty;\n");
    for (const auto &f : c.fields) {
      for (size_t i = f.first; i <= f.second; ++i)
        emitToken(i);
      emit("\n");
    }
    emit("};\n");
    emit("typedef struct " + c.name + " " + c.name + ";\n");
    if (c.hasDrop) {
      emitMemberLinkage();
      emit("void " + c.name + "_drop(" + c.name + " *self);\n");
    }

    for (const auto &m : c.methods) {
      emit(lineDirective(filename_, ts[m.itemStart].line));
      if (m.name == c.name) {
        emitMemberLinkage();
        emit(c.name + " " + c.name + "_ctor(");
        std::string params = joinTokens(ts, m.lparen + 1, m.rparen);
        params.erase(0, params.find_first_not_of(' '));
        while (!params.empty() && params.back() == ' ')
          params.pop_back();
        if (!params.empty() && params != "void")
          emit(params);
        emit(") {\n" + c.name + " _cplus_self = {0};\n");
        emit(lineDirective(filename_, ts[m.bodyOpen].line));
        bool previousCtor = ctorBody_;
        ctorBody_ = true;
        emitMethodBody(m.bodyOpen, m.bodyClose, &c);
        ctorBody_ = previousCtor;
        emit("\nreturn _cplus_self;\n}\n");
        continue;
      }
      std::string ret = joinTokens(ts, m.itemStart, m.nameIdx);
      if (ret.empty()) {
        diags_.error("method '" + m.name + "' is missing a return type",
                     filename_, ts[m.nameIdx].line, ts[m.nameIdx].column);
        emitMemberLinkage();
        emit("void ");
      } else {
        emitMemberLinkage();
        emit(ret);
        emit(" ");
      }
      emit(c.name + "_" + m.name + "(" + c.name + " *self");
      std::string params = joinTokens(ts, m.lparen + 1, m.rparen);
      params.erase(0, params.find_first_not_of(' '));
      while (!params.empty() && params.back() == ' ')
        params.pop_back();
      if (!params.empty() && params != "void") {
        emit(", ");
        emit(params);
      }
      emit(")\n");
      // Restore diagnostics to the method's own source lines before emitting
      // the body, because the synthesised signature may have added lines.
      emit(lineDirective(filename_, ts[m.bodyOpen].line));
      emitMethodBody(m.bodyOpen, m.bodyClose, &c);
      emit("\n");
    }
  }

  void emitMethodBody(size_t open, size_t close, const ClassInfo *selfClass) {
    bool previousMethod = inMethod_;
    inMethod_ = true;
    for (size_t i = open; i <= close; ++i) {
      if (tryEmitRangeFor(i) || tryEmitDefaultCall(i)) {
        i = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      if (ts.isIdent(i, "auto") && tryEmitAuto(i)) {
        i = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      if (tryEmitCountof(i) || tryEmitIfDecl(i) || tryEmitOctal(i) ||
          tryEmitCtorCall(i) ||
          tryEmitArrayLen(i) || tryEmitMethodCall(i, selfClass)) {
        i = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      emitToken(i);
    }
    inMethod_ = previousMethod;
  }

  bool emitOwnedDeclaration(size_t start) {
    bool isManaged = ts.isIdent(start, kw::kManaged);
    size_t semi = ts.statementEnd(start);
    if (semi == kNPos) {
      diags_.error(std::string("unterminated '") +
                       (isManaged ? kw::kManaged : kw::kOwned) +
                       "' declaration",
                   filename_, ts[start].line, ts[start].column);
      return false;
    }

    // Find the initializer '=' at the top level.
    size_t eq = kNPos;
    int paren = 0, bracket = 0;
    for (size_t i = start + 1; i < semi; ++i) {
      if (ts.isPunct(i, "("))
        ++paren;
      else if (ts.isPunct(i, ")"))
        --paren;
      else if (ts.isPunct(i, "["))
        ++bracket;
      else if (ts.isPunct(i, "]"))
        --bracket;
      else if (ts.isPunct(i, "=") && paren == 0 && bracket == 0) {
        eq = i;
        break;
      }
    }

    size_t nameIdx = kNPos;
    size_t limit = eq == kNPos ? semi : eq;
    for (size_t i = limit; i > start; --i) {
      size_t k = ts.prevSig(i);
      if (k == kNPos || k <= start)
        break;
      if (ts[k].kind == TokKind::Identifier) {
        nameIdx = k;
        break;
      }
    }

    size_t typeIdx = ts.nextSig(start);
    if (ts.isIdent(typeIdx, "struct") || ts.isIdent(typeIdx, "union"))
      typeIdx = ts.nextSig(typeIdx);

    if (nameIdx == kNPos || typeIdx == kNPos ||
        ts[typeIdx].kind != TokKind::Identifier) {
      // Not a C+ resource declaration we understand; leave ordinary C alone.
      return false;
    }
    const std::string typeName = ts[typeIdx].text;
    const std::string varName = ts[nameIdx].text;

    if (eq == kNPos) {
      diags_.error("'" + varName +
                       "' owns a resource and must be initialised",
                   filename_, ts[nameIdx].line, ts[nameIdx].column);
    }

    if (!isManaged && !resourceNames_.count(typeName)) {
      diags_.error("'owned' requires a resource type, but '" + typeName +
                       "' is not a resource",
                   filename_, ts[typeIdx].line, ts[typeIdx].column);
    }

    // Emit the declaration without the language keyword.
    for (size_t i = start + 1; i <= semi; ++i)
      emitToken(i);
    if (isManaged)
      emit(" /*cplus$mem:" + varName + "*/");
    else
      emit(" /*cplus$own:" + varName + ":" + typeName + "_drop*/");
    changed_ = true;
    skipTo_ = semi;
    return true;
  }

  /// Record pointers initialised by the C+ allocator.  When the allocation
  /// size is visible in the source, `.len` can be computed as
  /// `(size_expression) / sizeof(*ptr)`, which the C compiler folds to a
  /// constant for fixed-size allocations.  Only genuinely dynamic sizes need
  /// the runtime side-table lookup.
  void collectDynamicPointers() {
    static const std::set<std::string> kAllocators = {
        "cplus_alloc", "cplus_zalloc", "cplus_realloc", "cplus_new",
        "cplus_new_array", "malloc", "calloc"};
    for (size_t i = 0; i < ts.size(); ++i) {
      if (ts[i].kind != TokKind::Identifier ||
          !kAllocators.count(ts[i].text))
        continue;

      size_t lp = ts.nextSig(i);
      if (!ts.isPunct(lp, "("))
        continue;
      size_t rp = ts.matchParen(lp);
      if (rp == kNPos)
        continue;

      // Split the argument list at its first top-level comma.
      size_t comma = kNPos;
      int depth = 0;
      for (size_t k = lp + 1; k < rp; ++k) {
        if (ts.isPunct(k, "(") || ts.isPunct(k, "[") || ts.isPunct(k, "{"))
          ++depth;
        else if (ts.isPunct(k, ")") || ts.isPunct(k, "]") || ts.isPunct(k, "}"))
          --depth;
        else if (ts.isPunct(k, ",") && depth == 0) {
          comma = k;
          break;
        }
      }

      size_t argStart = ts.nextSig(lp);
      size_t argEnd = comma == kNPos ? rp : comma;
      std::string expr;
      size_t exprFrom = kNPos, exprTo = kNPos;
      if (ts.isIdent(i, "cplus_new")) {
        expr = "1";
      } else if (ts.isIdent(i, "malloc") || ts.isIdent(i, "calloc")) {
        // Derive the element count from the explicit allocation size only when
        // it is a constant expression. Never infer a length from pointer
        // arithmetic or a runtime value: those remain untracked.
        size_t sizeFrom = argStart, sizeTo = argEnd;
        if (ts.isIdent(i, "calloc")) {
          if (comma == kNPos)
            continue;
          sizeFrom = ts.nextSig(comma);
          sizeTo = rp;
          // `calloc(count, size)` gives count elements of `size` bytes, and
          // the pointee type is `*name`, so count = count*size / sizeof(*name).
        }
        if (!isConstantSizeExpr(sizeFrom, sizeTo))
          continue;
        exprFrom = sizeFrom;
        exprTo = sizeTo;
        std::string sizeExpr = "(" + joinTokens(ts, sizeFrom, sizeTo) + ")";
        expr = sizeExpr + " / sizeof(*@NAME@)";
      } else if (ts.isIdent(i, "cplus_new_array")) {
        if (comma == kNPos)
          continue;
        exprFrom = ts.nextSig(comma);
        exprTo = rp;
        expr = "(" + joinTokens(ts, exprFrom, exprTo) + ")";
      } else {
        if (ts.isIdent(i, "cplus_realloc")) {
          if (comma == kNPos)
            continue;
          argStart = ts.nextSig(comma);
          argEnd = rp;
        }
        if (argStart >= argEnd)
          continue;
        exprFrom = argStart;
        exprTo = argEnd;
        expr = "(" + joinTokens(ts, argStart, argEnd) + ") / sizeof(*@NAME@)";
      }

      size_t start = i;
      while (start > 0) {
        --start;
        if (ts.isPunct(start, ";") || ts.isPunct(start, "{") ||
            ts.isPunct(start, "}")) {
          ++start;
          break;
        }
      }
      int paren = 0, bracket = 0;
      size_t eq = kNPos;
      for (size_t k = start; k < i; ++k) {
        if (ts.isPunct(k, "(")) ++paren;
        else if (ts.isPunct(k, ")")) --paren;
        else if (ts.isPunct(k, "[")) ++bracket;
        else if (ts.isPunct(k, "]")) --bracket;
        else if (ts.isPunct(k, "=") && paren == 0 && bracket == 0) {
          eq = k;
          break;
        }
      }
      if (eq == kNPos)
        continue;
      size_t name = ts.prevSig(eq);
      if (name == kNPos || name >= eq ||
          ts[name].kind != TokKind::Identifier)
        continue;
      const std::string var = ts[name].text;
      dynamicPointers_.insert(var);
      size_t at = expr.find("@NAME@");
      if (at != std::string::npos)
        expr.replace(at, 6, var);
      dynamicLengths_.push_back({i, var, expr, exprFrom, exprTo});
    }
    // Propagate length metadata to simple aliases: `int *q = p;` where p is
    // already a tracked managed allocation. No arithmetic is inferred.
    for (size_t i = 0; i < ts.size(); ++i) {
      if (ts[i].kind != TokKind::Identifier || !ts.isPunct(ts.nextSig(i), "="))
        continue;
      size_t rhs = ts.nextSig(ts.nextSig(i));
      if (rhs >= ts.size() || ts[rhs].kind != TokKind::Identifier)
        continue;
      const std::string alias = ts[i].text, src = ts[rhs].text;
      if (src == alias || dynamicPointers_.count(alias) ||
          !dynamicPointers_.count(src))
        continue;
      dynamicPointers_.insert(alias);
      for (const auto &entry : dynamicLengths_)
        if (entry.name == src) {
          std::string e = entry.expr;
          size_t at = e.find(src);
          if (at != std::string::npos)
            e.replace(at, src.size(), alias);
          dynamicLengths_.push_back({i, alias, e, entry.exprFrom, entry.exprTo});
        }
    }
    // Record explicit releases so a `.len` query after release can be made
    // to trap instead of returning the folded constant.
    for (size_t i = 0; i < ts.size(); ++i) {
      if (!ts.isIdent(i, "cplus_release"))
        continue;
      size_t lp = ts.nextSig(i);
      if (!ts.isPunct(lp, "("))
        continue;
      size_t rp = ts.matchParen(lp);
      if (rp == kNPos)
        continue;
      std::string released;
      for (size_t k = lp + 1; k < rp; ++k)
        if (ts[k].kind == TokKind::Identifier)
          released = ts[k].text;
      if (!released.empty())
        releases_.push_back({i, released});
    }
  }

  /// Emit a range of tokens, applying the same C+ rewrites as the top level.
  void emitRange(size_t from, size_t to, const ClassInfo *selfClass = nullptr) {
    for (size_t k = from; k < to; ++k) {
      if (tryEmitRangeFor(k) || tryEmitDefaultCall(k)) {
        k = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      if (ts.isIdent(k, "auto") && tryEmitAuto(k)) {
        k = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      if (tryEmitCountof(k) || tryEmitIfDecl(k) || tryEmitOctal(k) ||
          tryEmitCtorCall(k) || tryEmitArrayLen(k) ||
          tryEmitMethodCall(k, selfClass)) {
        k = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      emitToken(k);
    }
  }

  /// True when a size expression contains no variables, so it is safe to
  /// re-emit at a different use site.  Identifiers that are type names or
  /// `sizeof` are allowed; everything else is a variable and would break
  /// scope if copied.
  bool isConstantSizeExpr(size_t from, size_t to) const {
    if (from == kNPos || to == kNPos || from >= to)
      return true;
    static const std::set<std::string> kTypeWords = {
        "sizeof", "int",     "char",   "short",  "long",   "unsigned",
        "signed", "float",   "double", "_Bool",  "void",   "size_t",
        "const",  "volatile", "struct", "union",  "enum"};
    for (size_t k = from; k < to; ++k) {
      if (ts.isTrivia(k))
        continue;
      if (ts[k].kind == TokKind::Number || ts[k].kind == TokKind::Punct)
        continue;
      if (ts[k].kind == TokKind::Identifier &&
          (kTypeWords.count(ts[k].text) || classNames_.count(ts[k].text) ||
           resourceNames_.count(ts[k].text)))
        continue;
      return false;
    }
    return true;
  }

  std::string lengthExprFor(const std::string &name, size_t pos) const {
    auto it = arrays_.find(name);
    if (it != arrays_.end())
      return "(" + std::to_string(it->second) + ")";
    for (const auto &entry : dynamicLengths_)
      if (entry.pos < pos && entry.name == name &&
          isConstantSizeExpr(entry.exprFrom, entry.exprTo))
        return "(" + entry.expr + ")";
    if (dynamicPointers_.count(name))
      return "cplus_alloc_count(" + name + ", sizeof(*" + name + "))";
    return std::string();
  }

  /// Rewrite `for (auto v : arr) body` into a counted C loop that uses the
  /// range's `.len` property.  The range must be a simple lvalue (array or
  /// C+ allocation); side-effecting range expressions are not supported.
  bool tryEmitRangeFor(size_t i) {
    if (!ts.isIdent(i, "for"))
      return false;
    size_t lp = ts.nextSig(i);
    if (!ts.isPunct(lp, "("))
      return false;
    size_t rp = ts.matchParen(lp);
    if (rp == kNPos)
      return false;

    size_t first = ts.nextSig(lp);
    if (first >= rp ||
        !(ts.isIdent(first, "auto") || ts[first].kind == TokKind::Identifier))
      return false;
    size_t name = ts.nextSig(first);
    size_t colon = ts.nextSig(name);
    if (name >= rp || ts[name].kind != TokKind::Identifier ||
        !ts.isPunct(colon, ":"))
      return false;
    size_t exprStart = ts.nextSig(colon);
    if (exprStart >= rp || ts[exprStart].kind != TokKind::Identifier ||
        ts.nextSig(exprStart) != rp)
      return false;

    size_t body = ts.nextSig(rp);
    size_t bodyClose = kNPos;
    if (ts.isPunct(body, "{")) {
      bodyClose = ts.matchBrace(body);
    } else {
      bodyClose = ts.statementEnd(body); // single-statement loop body
    }
    if (bodyClose == kNPos)
      return false;

    std::string range = ts[exprStart].text;
    std::string var = ts[name].text;
    std::string len = lengthExprFor(range, i);
    if (len.empty()) {
      diags_.error("range requires a known .len property: '" + range + "'",
                   filename_, ts[exprStart].line, ts[exprStart].column);
      return false;
    }
    std::string idx = "__cplus_range_i" + std::to_string(rangeCounter_++);
    emit("for (size_t " + idx + " = 0; " + idx + " < " + len + "; " +
         idx + "++) { __typeof__(" + range + "[" + idx + "]) " + var + " = " +
         range + "[" + idx + "];");
    extraBraces_[bodyClose]++;
    skipTo_ = rp; // resume at the original body's opening brace
    changed_ = true;
    return true;
  }

  /// Rewrite `auto name = init;` to `__typeof__(init) name = init;`.
  /// This is C++-style type inference lowered to a GNU C extension; the
  /// initialiser is not evaluated by __typeof__, so there is no double work.
  bool tryEmitAuto(size_t i) {
    size_t name = ts.nextSig(i);
    if (name >= ts.size() || ts[name].kind != TokKind::Identifier)
      return false;
    size_t eq = ts.nextSig(name);
    if (!ts.isPunct(eq, "="))
      return false;

    int paren = 0, bracket = 0, brace = 0;
    size_t end = kNPos;
    for (size_t k = eq + 1; k < ts.size(); ++k) {
      if (ts.isPunct(k, "(")) ++paren;
      else if (ts.isPunct(k, ")")) --paren;
      else if (ts.isPunct(k, "[")) ++bracket;
      else if (ts.isPunct(k, "]")) --bracket;
      else if (ts.isPunct(k, "{")) ++brace;
      else if (ts.isPunct(k, "}")) --brace;
      else if (ts.isPunct(k, ";") || ts.isPunct(k, ",")) {
        if (paren == 0 && bracket == 0 && brace == 0) {
          end = k;
          break;
        }
      }
    }
    if (end == kNPos)
      return false;

    // C23 auto decays arrays to pointers.  Our __typeof__ based lowering
    // would otherwise try to declare an array variable from an array value,
    // so emit a pointer type for known fixed arrays.
    size_t firstInit = ts.nextSig(eq);
    if (firstInit < end && ts[firstInit].kind == TokKind::Identifier &&
        arrays_.count(ts[firstInit].text) &&
        ts.nextSig(firstInit) == end) {
      emit("__typeof__(&" + ts[firstInit].text + "[0])");
    } else {
      emit("__typeof__(");
      emitRange(eq + 1, end);
      emit(")");
    }
    skipTo_ = i; // the caller resumes at the declared name
    changed_ = true;
    return true;
  }

  /// C2y if-declarations: `if (T x = init; cond) body else other` becomes
  /// `{ T x = init; if (cond) body else other }` so the variable is scoped.
  bool tryEmitIfDecl(size_t i) {
    if (!ts.isIdent(i, "if"))
      return false;
    size_t lp = ts.nextSig(i);
    if (!ts.isPunct(lp, "("))
      return false;
    size_t rp = ts.matchParen(lp);
    if (rp == kNPos)
      return false;

    size_t semi = kNPos;
    int paren = 0, bracket = 0;
    for (size_t k = lp + 1; k < rp; ++k) {
      if (ts.isPunct(k, "(")) ++paren;
      else if (ts.isPunct(k, ")")) --paren;
      else if (ts.isPunct(k, "[") ) ++bracket;
      else if (ts.isPunct(k, "]")) --bracket;
      else if (ts.isPunct(k, ";") && paren == 0 && bracket == 0) {
        semi = k;
        break;
      }
    }
    if (semi == kNPos || ts.nextSig(semi) >= rp)
      return false;

    size_t decl = ts.nextSig(lp);
    size_t cond = ts.nextSig(semi);
    emit("{ ");
    emitRange(decl, semi);
    emit("; if (");
    emitRange(cond, rp);
    emit(")");

    size_t body = ts.nextSig(rp);
    size_t end = body;
    if (ts.isPunct(body, "{")) {
      end = ts.matchBrace(body);
    } else {
      end = ts.statementEnd(body);
    }
    if (end == kNPos)
      return false;
    size_t after = ts.nextSig(end);
    if (ts.isIdent(after, "else")) {
      size_t other = ts.nextSig(after);
      if (ts.isPunct(other, "{"))
        end = ts.matchBrace(other);
      else
        end = ts.statementEnd(other);
      if (end == kNPos)
        return false;
    }
    extraBraces_[end]++;
    skipTo_ = rp;
    changed_ = true;
    return true;
  }

  /// C2y `_Countof(expr)` is an alias for the C+ countof() helper.
  bool tryEmitCountof(size_t i) {
    if (!ts.isIdent(i, "_Countof"))
      return false;
    size_t lp = ts.nextSig(i);
    if (!ts.isPunct(lp, "("))
      return false;
    emit("countof");
    skipTo_ = i; // resume at '(' so arguments are emitted normally
    changed_ = true;
    return true;
  }

  /// C2y 0o/0O octal literals: rewrite to classic leading-zero octal so the
  /// gnu17 backend accepts them.
  bool tryEmitOctal(size_t i) {
    if (ts[i].kind == TokKind::Number && ts[i].text.size() > 2 &&
        ts[i].text[0] == '0' &&
        (ts[i].text[1] == 'o' || ts[i].text[1] == 'O')) {
      emit("0" + ts[i].text.substr(2));
      skipTo_ = i;
      changed_ = true;
      return true;
    }
    if (ts[i].kind == TokKind::Number && ts[i].text == "0" &&
        i + 1 < ts.size() && ts[i + 1].kind == TokKind::Identifier &&
        ts[i + 1].text.size() > 1 &&
        (ts[i + 1].text[0] == 'o' || ts[i + 1].text[0] == 'O')) {
      emit("0" + ts[i + 1].text.substr(1));
      skipTo_ = i + 1;
      changed_ = true;
      return true;
    }
    return false;
  }

  /// `T(args)` calls a synthesized constructor for class T.
  bool tryEmitCtorCall(size_t i) {
    if (ts[i].kind != TokKind::Identifier ||
        !classNamesWithCtor_.count(ts[i].text))
      return false;
    size_t lp = ts.nextSig(i);
    if (!ts.isPunct(lp, "("))
      return false;
    emit(ts[i].text + "_ctor");
    skipTo_ = i;
    changed_ = true;
    return true;
  }

  /// Rewrite `x.len`: a fixed array becomes a literal extent; a pointer
  /// produced by the C+ allocator becomes a side-table lookup divided by
  /// the element size.
  bool tryEmitArrayLen(size_t i) {
    if (ts[i].kind != TokKind::Identifier)
      return false;
    bool isStatic = arrays_.count(ts[i].text) != 0;
    bool isDynamic = dynamicPointers_.count(ts[i].text) != 0;
    size_t dot = ts.nextSig(i);
    if (!ts.isPunct(dot, "."))
      return false;
    size_t member = ts.nextSig(dot);
    if (!ts.isIdent(member, "len"))
      return false;
    // `obj.len()` is a method call on a class/String/Vector, not the array
    // length property; leave it to the method-call rewriter.
    if (ts.isPunct(ts.nextSig(member), "("))
      return false;
    if (!isStatic && !isDynamic) {
      // A pointer with no C+ length metadata (e.g. raw malloc) cannot have
      // `.len` inferred: pointer arithmetic is never used to guess a length.
      // Replace with 0 so Clang does not emit a cascade of follow-on errors.
      diags_.error("'.len' requires a static array or a C+ managed allocation; "
                   "pointer '" + ts[i].text + "' has no tracked length",
                   filename_, ts[i].line, ts[i].column);
      emit("0");
      skipTo_ = member;
      changed_ = true;
      return true;
    }
    if (isStatic) {
      emit("(" + std::to_string(arrays_[ts[i].text]) + ")");
    } else {
      std::string expr = lengthExprFor(ts[i].text, i);
      bool released = false;
      for (const auto &r : releases_)
        if (r.pos < i && r.name == ts[i].text)
          released = true;
      if (released)
        emit("(cplus_check_live(" + ts[i].text + "), " + expr + ")");
      else
        emit(expr);
    }
    skipTo_ = member;
    changed_ = true;
    return true;
  }

  bool tryEmitMethodCall(size_t i, const ClassInfo *selfClass = nullptr) {
    if (ts[i].kind != TokKind::Identifier)
      return false;

    std::string className;
    auto it = varClasses_.find(ts[i].text);
    if (it != varClasses_.end())
      className = it->second;
    else if (selfClass && (ts[i].text == kw::kSelf ||
                           ts[i].text == "this"))
      className = selfClass->name;
    else {
      // A method-style call on a plain pointer/array is invalid. Give a
      // clear C+ error instead of Clang's "not a structure or union".
      size_t dot = ts.nextSig(i);
      if (ts.isPunct(dot, ".") || ts.isPunct(dot, "->")) {
        size_t member = ts.nextSig(dot);
        if (member < ts.size() && ts.isIdent(member, "len") &&
            ts.isPunct(ts.nextSig(member), "(")) {
          diags_.error("'len()' cannot be called on '" + ts[i].text +
                           "'; use the .len property on a static "
                           "array or C+ managed allocation",
                       filename_, ts[i].line, ts[i].column);
          emit("0");
          skipTo_ = member;
          changed_ = true;
          return true;
        }
      }
      return false;
    }

    size_t op = ts.nextSig(i);
    // Allow a method call on an array element: `vec[0].method()`.
    size_t receiverEnd = i;
    if (ts.isPunct(op, "[")) {
      size_t close = ts.matchBracket(op);
      if (close == kNPos || close <= op)
        return false;
      receiverEnd = close;
      op = ts.nextSig(close);
    }
    if (!ts.isPunct(op, ".") && !ts.isPunct(op, "->"))
      return false;
    bool arrow = ts.isPunct(op, "->");
    size_t methodIdx = ts.nextSig(op);
    if (methodIdx >= ts.size() || ts[methodIdx].kind != TokKind::Identifier)
      return false;
    size_t lparen = ts.nextSig(methodIdx);
    if (!ts.isPunct(lparen, "("))
      return false;

    const ClassInfo *cls = nullptr;
    for (const auto &c : classes_)
      if (c.name == className)
        cls = &c;
    bool external = externalClassNames_.count(className) != 0;
    if (!cls && !external)
      return false;
    if (cls && !cls->methodNames.count(ts[methodIdx].text) && !external)
      return false;

    emit(className + "_" + ts[methodIdx].text + "(");
    if (!arrow)
      emit("&");
    for (size_t k = i; k <= receiverEnd; ++k)
      emitToken(k);
    size_t firstArg = ts.nextSig(lparen);
    if (!ts.isPunct(firstArg, ")"))
      emit(", ");
    // Skip the receiver and the original '('; the loop continues at the
    // first argument so the closing ')' is emitted normally.
    skipTo_ = lparen;
    return true;
  }

  TokenStream ts;
  DiagnosticEngine &diags_;
  std::string filename_;
  std::string out_;
  bool changed_ = false;
  bool ctorBody_ = false;
  bool inMethod_ = false;
  bool headerMode_ = false;
  size_t skipTo_ = kNPos;

  std::map<size_t, int> extraBraces_;
  unsigned rangeCounter_ = 0;
  std::vector<FuncDefaults> funcsWithDefaults_;
  std::map<std::string, size_t> defaultsByName_;
  std::vector<ResourceInfo> resources_;
  std::vector<ClassInfo> classes_;
  std::vector<std::string> resourceTypes_;
  std::vector<std::string> classTypes_;
  std::set<std::string> resourceNames_;
  std::set<std::string> classNames_;
  std::set<std::string> classNamesWithCtor_;
  std::set<std::string> externalClassNames_;
  std::map<std::string, std::string> varClasses_;
  std::map<std::string, size_t> arrays_;
  std::set<std::string> dynamicPointers_;
  struct DynamicLength {
    size_t pos;
    std::string name;
    std::string expr;
    size_t exprFrom = kNPos;
    size_t exprTo = kNPos;
  };
  std::vector<DynamicLength> dynamicLengths_;
  struct ReleaseInfo {
    size_t pos;
    std::string name;
  };
  std::vector<ReleaseInfo> releases_;
};

} // namespace

RewrittenSource rewriteCPlusSource(const std::string &source,
                                   DiagnosticEngine &diags,
                                   const std::string &filename,
                                   bool headerMode) {
  std::vector<Token> toks = lexCPlus(source);
  Lowerer lowerer(toks, diags, filename, headerMode);
  RewrittenSource result = lowerer.run();
  // Preserve the original source's final newline.  Token reconstruction drops
  // trailing trivia; keeping the newline makes lowered plain C byte-identical
  // to the input after the mandatory leading #line directive.
  if (!source.empty() && source.back() == '\n' && !result.text.empty() &&
      result.text.back() != '\n')
    result.text += '\n';
  return result;
}

void registerExternalClass(const std::string &name, bool hasConstructor) {
  externalClasses().insert(name);
  if (hasConstructor)
    externalCtors().insert(name);
}

void registerExternalResource(const std::string &name) {
  externalResources().insert(name);
}

void clearExternalClasses() {
  externalClasses().clear();
  externalCtors().clear();
  externalResources().clear();
}

namespace {

std::vector<CheckedArithmeticSite> &checkedArithmeticSiteRegistry() {
  static std::vector<CheckedArithmeticSite> sites;
  return sites;
}

} // namespace

void setCheckedArithmeticSites(std::vector<CheckedArithmeticSite> sites) {
  checkedArithmeticSiteRegistry() = std::move(sites);
}

const std::vector<CheckedArithmeticSite> &checkedArithmeticSites() {
  return checkedArithmeticSiteRegistry();
}

std::vector<CheckedPointerSite> &checkedPointerSiteRegistry() {
  static std::vector<CheckedPointerSite> sites;
  return sites;
}

void setCheckedPointerSites(std::vector<CheckedPointerSite> sites) {
  checkedPointerSiteRegistry() = std::move(sites);
}

const std::vector<CheckedPointerSite> &checkedPointerSites() {
  return checkedPointerSiteRegistry();
}

std::vector<CheckedBoundsSite> &checkedBoundsSiteRegistry() {
  static std::vector<CheckedBoundsSite> sites;
  return sites;
}

void setCheckedBoundsSites(std::vector<CheckedBoundsSite> sites) {
  checkedBoundsSiteRegistry() = std::move(sites);
}

const std::vector<CheckedBoundsSite> &checkedBoundsSites() {
  return checkedBoundsSiteRegistry();
}

std::vector<CheckedAllocSite> &checkedAllocSiteRegistry(bool freeSite) {
  static std::vector<CheckedAllocSite> allocs;
  static std::vector<CheckedAllocSite> frees;
  return freeSite ? frees : allocs;
}

void setCheckedAllocSites(std::vector<CheckedAllocSite> sites) {
  checkedAllocSiteRegistry(false) = std::move(sites);
}
const std::vector<CheckedAllocSite> &checkedAllocSites() {
  return checkedAllocSiteRegistry(false);
}
void setCheckedFreeSites(std::vector<CheckedAllocSite> sites) {
  checkedAllocSiteRegistry(true) = std::move(sites);
}
const std::vector<CheckedAllocSite> &checkedFreeSites() {
  return checkedAllocSiteRegistry(true);
}

namespace {

/// One text edit applied to the lowered C. `priority` orders nested edits at
/// the same offset: prefixes outside-in, operator separators, suffixes
/// inside-out. `span` breaks ties between prefixes/suffixes of nested
/// expressions; bounds checks use the maximum span so they wrap arithmetic.
struct SafetyInsertion {
  size_t offset = 0;
  size_t span = 0;
  int priority = 0; // 0 = prefix, 1 = operator separator, 2 = suffix
  std::string text;
  size_t replace = 0; // source bytes consumed at `offset`
};

const char *checkedOpName(CheckedArithmeticSite::Op kind) {
  switch (kind) {
  case CheckedArithmeticSite::Op::Add: return "add";
  case CheckedArithmeticSite::Op::Sub: return "sub";
  case CheckedArithmeticSite::Op::Mul: return "mul";
  case CheckedArithmeticSite::Op::Div: return "div";
  case CheckedArithmeticSite::Op::Rem: return "rem";
  case CheckedArithmeticSite::Op::Shl: return "shl";
  case CheckedArithmeticSite::Op::Shr: return "shr";
  case CheckedArithmeticSite::Op::Narrow: return "narrow";
  case CheckedArithmeticSite::Op::FloatToInt: return "f2i";
  case CheckedArithmeticSite::Op::AllocExtent: return "size_mul";
  case CheckedArithmeticSite::Op::Inc: return "add";
  case CheckedArithmeticSite::Op::Dec: return "sub";
  case CheckedArithmeticSite::Op::Neg: return "sub";
  }
  return "add";
}

/// Byte slice clamped to the source.  Producers may record degenerate
/// offsets for implicit sites; never let an out-of-range slice crash the
/// rewriter or silently drop a site.
std::string sourceSlice(const std::string &s, size_t from, size_t to) {
  if (from > s.size())
    from = s.size();
  if (to < from)
    to = from;
  if (to > s.size())
    to = s.size();
  return s.substr(from, to - from);
}

// ---------------------------------------------------------------------------
// Token-level fallback site recovery.
//
// The AST pass cannot record every guard-worthy spelling: compound operators
// are distinct opcodes, macro/header locations are intentionally skipped, and
// implicit conversions have no CStyleCastExpr.  The rewriter therefore recovers
// the unambiguous forms directly from declaration/operator tokens.  No new
// CheckedArithmeticSite kinds are needed -- the existing single-evaluation
// emission below consumes these sites unchanged.
// ---------------------------------------------------------------------------

struct TokenType {
  std::string spelling; ///< spelling usable in a generated C cast
  unsigned bits = 0;
  bool isSigned = true;
  bool isFloat = false;
  bool valid = false;
};

static bool isTriviaTok(const Token &t) {
  return t.kind == TokKind::Whitespace || t.kind == TokKind::Comment;
}

static size_t nextSigTok(const std::vector<Token> &toks, size_t i) {
  ++i;
  while (i < toks.size() && isTriviaTok(toks[i]))
    ++i;
  return i;
}

static size_t prevSigTok(const std::vector<Token> &toks, size_t i) {
  while (i > 0) {
    --i;
    if (!isTriviaTok(toks[i]))
      return i;
  }
  return kNPos;
}

static size_t endOfToken(const Token &t) { return t.offset + t.text.size(); }

static TokenType lookupVar(const std::map<std::string, TokenType> &vars,
                           const std::string &name) {
  auto it = vars.find(name);
  return it == vars.end() ? TokenType() : it->second;
}

static bool isDeclQualifierTok(const std::string &s) {
  return s == "const" || s == "volatile" || s == "static" ||
         s == "extern" || s == "register" || s == "restrict" ||
         s == "_Atomic" || s == "inline" || s == "auto";
}

/// Parse a built-in / fixed-width integer (or float) type spelling at `i`.
static TokenType parseTokenType(const std::vector<Token> &toks, size_t i,
                                size_t limit, size_t &next) {
  TokenType out;
  std::vector<std::string> words;
  size_t k = i;
  while (k < limit && toks[k].kind == TokKind::Identifier) {
    const std::string &w = toks[k].text;
    if (isDeclQualifierTok(w)) {
      ++k;
      continue;
    }
    words.push_back(w);
    ++k;
  }
  next = k;
  if (words.empty())
    return out;

  bool uns = false, sgn = false, ch = false, sh = false;
  int lng = 0;
  bool flt = false, dbl = false, bl = false;
  std::string fixed;
  for (const std::string &w : words) {
    if (w == "unsigned") uns = true;
    else if (w == "signed") sgn = true;
    else if (w == "int") { /* default integer base word */ }
    else if (w == "char") ch = true;
    else if (w == "short") sh = true;
    else if (w == "long") ++lng;
    else if (w == "float") flt = true;
    else if (w == "double") dbl = true;
    else if (w == "_Bool") bl = true;
    else if (w == "size_t" || w == "uintptr_t" || w == "uint64_t" ||
             w == "uint32_t" || w == "uint16_t" || w == "uint8_t") {
      fixed = w;
      uns = true;
    } else if (w == "ptrdiff_t" || w == "intptr_t" || w == "int64_t" ||
               w == "int32_t" || w == "int16_t" || w == "int8_t") {
      fixed = w;
      sgn = true;
    } else {
      return out; // user-defined type: not modelled
    }
  }
  for (size_t j = 0; j < words.size(); ++j) {
    if (j)
      out.spelling += " ";
    out.spelling += words[j];
  }
  if (flt) {
    out.bits = 32; out.isFloat = true; out.isSigned = true;
  } else if (dbl) {
    out.bits = 64; out.isFloat = true; out.isSigned = true;
  } else if (bl) {
    out.bits = 1; out.isSigned = false;
  } else if (!fixed.empty()) {
    if (fixed == "uint8_t" || fixed == "int8_t") out.bits = 8;
    else if (fixed == "uint16_t" || fixed == "int16_t") out.bits = 16;
    else if (fixed == "uint32_t" || fixed == "int32_t") out.bits = 32;
    else out.bits = 64;
    out.isSigned = sgn;
  } else if (ch) {
    out.bits = 8; out.isSigned = !uns;
  } else if (sh) {
    out.bits = 16; out.isSigned = !uns;
  } else {
    out.bits = (lng >= 1) ? 64 : 32;
    out.isSigned = !uns;
  }
  out.valid = true;
  return out;
}

/// Index of the first `;`/`,`/`}` at declaration nesting depth zero.
static size_t findStmtEnd(const std::vector<Token> &toks, size_t from) {
  int par = 0, brk = 0;
  for (size_t k = from; k < toks.size(); ++k) {
    if (toks[k].kind != TokKind::Punct)
      continue;
    const std::string &p = toks[k].text;
    if (p == "(") ++par;
    else if (p == ")") { if (par > 0) --par; }
    else if (p == "[") ++brk;
    else if (p == "]") { if (brk > 0) --brk; }
    else if (par == 0 && brk == 0 &&
             (p == ";" || p == "," || p == "}"))
      return k;
  }
  return kNPos;
}

/// Whether a token can end a value operand.  Keywords such as `sizeof` are
/// unary operators, not factors: `sizeof *p` is a dereference, not a product.
static bool canEndFactor(const Token &t) {
  if (t.kind == TokKind::Identifier) {
    static const std::set<std::string> kKeywords = {
        "sizeof", "return", "if", "while", "for", "switch", "case", "else",
        "do", "goto", "break", "continue", "typedef", "struct", "union",
        "enum", "const", "volatile", "unsigned", "signed", "long", "short",
        "int", "char", "float", "double", "void", "static", "extern",
        "register", "auto", "inline", "_Atomic"};
    return kKeywords.count(t.text) == 0;
  }
  return t.kind == TokKind::Number || t.kind == TokKind::CharLit ||
         t.kind == TokKind::StringLit ||
         (t.kind == TokKind::Punct && (t.text == ")" || t.text == "]"));
}

static bool canStartFactor(const Token &t) {
  return t.kind == TokKind::Identifier || t.kind == TokKind::Number ||
         t.kind == TokKind::CharLit ||
         (t.kind == TokKind::Punct && t.text == "(");
}

/// True when `inner` is a type spelling such as `int`, `long`, `unsigned int`
/// or `uint32_t` (and not a parenthesized expression like `(d)`).
static bool isTypeSpelling(const std::string &inner) {
  const std::vector<Token> ts = lexCPlus(inner);
  size_t next = 0;
  const TokenType ty = parseTokenType(ts, 0, ts.size(), next);
  if (!ty.valid)
    return false;
  for (size_t i = next; i < ts.size(); ++i)
    // lexCPlus appends an EndOfFile token; it is not trivia but must not
    // invalidate an otherwise complete type spelling such as "int".
    if (!isTriviaTok(ts[i]) && ts[i].kind != TokKind::EndOfFile)
      return false;
  return true;
}

/// Offset just past any leading `(TYPE)` cast prefixes in `source[begin,end)`.
/// The caller consumes those bytes so the f2i guard sees the original float
/// expression rather than the already-truncated integer result.
static size_t skipLeadingCasts(const std::string &source, size_t begin,
                               size_t end) {
  size_t p = begin;
  while (p < end && source[p] == '(') {
    size_t q = p + 1;
    int par = 1;
    while (q < end && par > 0) {
      if (source[q] == '(') ++par;
      else if (source[q] == ')') --par;
      if (par > 0) ++q;
    }
    if (par != 0 || q >= end)
      break;
    if (!isTypeSpelling(sourceSlice(source, p + 1, q)))
      break;
    p = q + 1;
    while (p < end && (source[p] == ' ' || source[p] == '\t' ||
                       source[p] == '\n' || source[p] == '\r'))
      ++p;
  }
  return p;
}

static std::vector<CheckedArithmeticSite>
collectTokenArithmeticSites(const std::string &source) {
  std::vector<Token> toks = lexCPlus(source);
  std::vector<CheckedArithmeticSite> sites;
  if (toks.empty())
    return sites;

  std::map<std::string, TokenType> vars;

  // Phase 1: recover declared types, and implicit
  // narrowing in simple `TYPE name = identifier;` initializers.
  for (size_t i = 0; i < toks.size(); ++i) {
    size_t after = kNPos;
    TokenType ty = parseTokenType(toks, i, toks.size(), after);
    if (!ty.valid || after == kNPos || after >= toks.size())
      continue;
    size_t n = after;
    bool isPointer = false;
    while (n < toks.size() && toks[n].kind == TokKind::Punct &&
           toks[n].text == "*") {
      isPointer = true;
      ++n;
    }
    if (n >= toks.size() || toks[n].kind != TokKind::Identifier)
      continue;
    // Pointer arithmetic has its own extent-based guard path; never treat a
    // pointer-typed declarator as an integer variable.
    if (isPointer)
      continue;
    size_t afterName = nextSigTok(toks, n);
    if (afterName == kNPos || toks[afterName].kind != TokKind::Punct)
      continue;
    const std::string &p = toks[afterName].text;
    if (p != "=" && p != "," && p != ";" && p != "[" && p != ")" &&
        p != "(")
      continue;
    vars[toks[n].text] = ty;

    if (p == "=") {
      size_t rhs = nextSigTok(toks, afterName);
      if (rhs == kNPos || toks[rhs].kind != TokKind::Identifier)
        continue;
      // Only a bare identifier has a source type we can prove here.  A
      // compound RHS (e.g. `char c = a + b`) must be guarded on the *result*,
      // which is the AST producer's job; guessing from its first token would
      // narrow the wrong operand.
      size_t afterRhs = nextSigTok(toks, rhs);
      if (afterRhs == kNPos || toks[afterRhs].kind != TokKind::Punct ||
          (toks[afterRhs].text != ";" && toks[afterRhs].text != ","))
        continue;
      TokenType src = lookupVar(vars, toks[rhs].text);
      if (!src.valid || ty.isFloat)
        continue;
      CheckedArithmeticSite s;
      s.begin = toks[rhs].offset;
      s.op = s.begin;
      s.opLength = 0;
      s.end = endOfToken(toks[rhs]);
      s.bits = ty.bits;
      s.isSigned = ty.isSigned;
      s.type = ty.spelling;
      if (src.isFloat) {
        // Implicit float -> integer in an initializer: guard the float value.
        s.kind = CheckedArithmeticSite::Op::FloatToInt;
      } else {
        if (src.bits <= ty.bits)
          continue;
        s.kind = CheckedArithmeticSite::Op::Narrow;
        s.srcBits = src.bits;
        s.srcIsSigned = src.isSigned;
      }
      sites.push_back(std::move(s));
    }
  }

  // Phase 2: operator spellings.  Skip preprocessor lines -- macro bodies are
  // not rewritable at their expansion point.
  for (size_t i = 0; i < toks.size(); ++i) {
    const Token &tk = toks[i];
    if (isTriviaTok(tk))
      continue;
    size_t ls = i;
    while (ls > 0 && toks[ls - 1].line == tk.line)
      --ls;
    bool ppLine = false;
    for (size_t k = ls; k <= i; ++k)
      if (toks[k].kind == TokKind::Punct && toks[k].text == "#")
        ppLine = true;
    if (ppLine)
      continue;

    const std::string op = tk.text;

    // Allocation extent: malloc(count * size) can wrap before the allocator
    // sees the request.  Wrap the multiplication in the checked size helper.
    if (tk.kind == TokKind::Identifier &&
        (op == "malloc" || op == "calloc" || op == "realloc")) {
      size_t lp = nextSigTok(toks, i);
      if (lp != kNPos && toks[lp].kind == TokKind::Punct &&
          toks[lp].text == "(") {
        int par = 0;
        size_t star = kNPos, argStart = kNPos, argEnd = kNPos;
        for (size_t k = lp + 1; k < toks.size(); ++k) {
          const Token &a = toks[k];
          if (isTriviaTok(a))
            continue;
          if (a.kind == TokKind::Punct && a.text == "(") { ++par; continue; }
          if (a.kind == TokKind::Punct && a.text == ")") {
            if (par == 0) { argEnd = k; break; }
            --par;
            continue;
          }
          if (par != 0)
            continue;
          if (a.kind == TokKind::Punct && a.text == ",") { argEnd = k; break; }
          if (argStart == kNPos)
            argStart = k;
          if (a.kind == TokKind::Punct && a.text == "*")
            star = k;
        }
        const size_t last = (argEnd == kNPos) ? kNPos : prevSigTok(toks, argEnd);
        // A `*` is a multiplication only when it separates two complete
        // factors.  In `malloc(sizeof *p)` it is a unary dereference whose
        // left "operand" is the `sizeof` keyword, so neither side is a valid
        // factor and no size_mul guard may be emitted.
        if (star != kNPos && argStart != kNPos && star > argStart &&
            last != kNPos && last > star && prevSigTok(toks, star) > argStart &&
            canEndFactor(toks[prevSigTok(toks, star)]) &&
            canStartFactor(toks[nextSigTok(toks, star)])) {
          CheckedArithmeticSite s;
          s.kind = CheckedArithmeticSite::Op::AllocExtent;
          s.begin = toks[argStart].offset;
          s.op = toks[star].offset;
          s.opLength = 1;
          s.end = endOfToken(toks[last]);
          sites.push_back(std::move(s));
        }
      }
      continue;
    }

    if (tk.kind == TokKind::Punct &&
        (op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
         op == "%=" || op == "<<=" || op == ">>=")) {
      size_t lhs = prevSigTok(toks, i);
      TokenType ty = lookupVar(vars, lhs == kNPos ? std::string()
                                                 : toks[lhs].text);
      if (!ty.valid || ty.isFloat)
        continue;
      if (!ty.isSigned && (op == "+=" || op == "-=" || op == "*="))
        continue; // unsigned add/sub/mul wrap by definition
      CheckedArithmeticSite::Op kind = CheckedArithmeticSite::Op::Add;
      if (op == "-=") kind = CheckedArithmeticSite::Op::Sub;
      else if (op == "*=") kind = CheckedArithmeticSite::Op::Mul;
      else if (op == "/=") kind = CheckedArithmeticSite::Op::Div;
      else if (op == "%=") kind = CheckedArithmeticSite::Op::Rem;
      else if (op == "<<=") kind = CheckedArithmeticSite::Op::Shl;
      else if (op == ">>=") kind = CheckedArithmeticSite::Op::Shr;
      const size_t stop = findStmtEnd(toks, nextSigTok(toks, i));
      if (stop == kNPos || stop <= i)
        continue;
      const size_t last = prevSigTok(toks, stop);
      if (last == kNPos || last < i || endOfToken(toks[last]) <= tk.offset)
        continue;
      CheckedArithmeticSite s;
      s.kind = kind;
      s.compound = true;
      s.begin = toks[lhs].offset;
      s.op = tk.offset;
      s.opLength = op.size();
      s.end = endOfToken(toks[last]);
      s.bits = ty.bits;
      s.isSigned = ty.isSigned;
      s.type = ty.spelling;
      sites.push_back(std::move(s));
      continue;
    }

    if (tk.kind == TokKind::Punct && (op == "++" || op == "--")) {
      size_t prev = prevSigTok(toks, i);
      size_t nxt = nextSigTok(toks, i);
      size_t target = kNPos;
      bool postfix = false;
      if (prev != kNPos &&
          (toks[prev].kind == TokKind::Identifier ||
           toks[prev].kind == TokKind::Number ||
           (toks[prev].kind == TokKind::Punct &&
            (toks[prev].text == ")" || toks[prev].text == "]" ||
             toks[prev].text == "++" || toks[prev].text == "--")))) {
        target = prev;
        postfix = true;
      } else if (nxt != kNPos && toks[nxt].kind == TokKind::Identifier) {
        target = nxt;
      }
      TokenType ty = lookupVar(vars, target == kNPos ? std::string()
                                                    : toks[target].text);
      if (!ty.valid || ty.isFloat || !ty.isSigned)
        continue;
      CheckedArithmeticSite s;
      s.kind = (op == "++") ? CheckedArithmeticSite::Op::Inc
                           : CheckedArithmeticSite::Op::Dec;
      s.postfix = postfix;
      s.begin = postfix ? toks[target].offset : tk.offset;
      s.op = tk.offset;
      s.opLength = 2;
      s.end = postfix ? endOfToken(tk) : endOfToken(toks[target]);
      s.bits = ty.bits;
      s.isSigned = ty.isSigned;
      s.type = ty.spelling;
      sites.push_back(std::move(s));
      continue;
    }

    if (tk.kind == TokKind::Punct && op == "-") {
      size_t prev = prevSigTok(toks, i);
      bool unary = true;
      if (prev != kNPos) {
        const Token &p = toks[prev];
        if (p.kind == TokKind::Identifier || p.kind == TokKind::Number ||
            p.kind == TokKind::StringLit || p.kind == TokKind::CharLit ||
            (p.kind == TokKind::Punct &&
             (p.text == ")" || p.text == "]" || p.text == "++" ||
              p.text == "--")))
          unary = false;
      }
      if (!unary)
        continue;
      size_t nxt = nextSigTok(toks, i);
      if (nxt == kNPos || toks[nxt].kind != TokKind::Identifier)
        continue;
      TokenType ty = lookupVar(vars, toks[nxt].text);
      if (!ty.valid || ty.isFloat)
        continue;
      CheckedArithmeticSite s;
      s.kind = CheckedArithmeticSite::Op::Neg;
      s.begin = tk.offset;
      s.op = tk.offset;
      s.opLength = 1;
      s.end = endOfToken(toks[nxt]);
      s.bits = ty.bits;
      s.isSigned = ty.isSigned;
      s.type = ty.spelling;
      sites.push_back(std::move(s));
      continue;
    }

    // Explicit integer cast of a known float operand: (int)d, (long)x, ...
    if (tk.kind == TokKind::Punct && op == "(") {
      size_t ts = nextSigTok(toks, i);
      if (ts == kNPos || toks[ts].kind != TokKind::Identifier)
        continue;
      size_t after = kNPos;
      TokenType dst = parseTokenType(toks, ts, toks.size(), after);
      if (!dst.valid || dst.isFloat || after == kNPos || after >= toks.size())
        continue;
      if (!(toks[after].kind == TokKind::Punct && toks[after].text == ")"))
        continue;
      size_t val = nextSigTok(toks, after);
      if (val == kNPos || toks[val].kind != TokKind::Identifier)
        continue;
      TokenType src = lookupVar(vars, toks[val].text);
      if (!src.valid || !src.isFloat)
        continue;
      CheckedArithmeticSite s;
      s.kind = CheckedArithmeticSite::Op::FloatToInt;
      s.begin = toks[val].offset;
      s.op = s.begin;
      s.opLength = 0;
      s.end = endOfToken(toks[val]);
      s.bits = dst.bits;
      s.isSigned = dst.isSigned;
      s.type = dst.spelling;
      sites.push_back(std::move(s));
      continue;
    }
  }
  return sites;
}

} // namespace

std::string instrumentDynamicArrayBounds(
    const std::string &source,
    const std::map<std::string, size_t> &arrayLengths) {
  // Merge producer sites with the token-level fallback.  Dedup on the *full*
  // (begin, op, opLength, end) tuple: nested sites legitimately share `begin`
  // (e.g. `a+b+c`), so deduping on begin alone would drop required inner
  // guards.
  std::vector<CheckedArithmeticSite> arith = checkedArithmeticSites();
  std::set<std::tuple<size_t, size_t, size_t, size_t>> seenArith;
  for (const CheckedArithmeticSite &s : arith)
    seenArith.insert(std::make_tuple(s.begin, s.op, s.opLength, s.end));
  for (CheckedArithmeticSite &s : collectTokenArithmeticSites(source)) {
    if (seenArith.insert(std::make_tuple(s.begin, s.op, s.opLength, s.end))
            .second)
      arith.push_back(std::move(s));
  }
  const std::vector<CheckedBoundsSite> &bounds = checkedBoundsSites();
  // RaiiLowering can emit `cplus_zero(&owner, sizeof(owner))` for an
  // explicitly invoked destructor.  Those TUs may otherwise have no safety
  // sites at all, so the declaration need must be part of the early-return
  // condition (LEGACY never calls this function, so its bytes are untouched).
  const bool needZeroDecl = source.find("cplus_zero") != std::string::npos;
  if (arrayLengths.empty() && arith.empty() && bounds.empty() &&
      checkedPointerSites().empty() && checkedAllocSites().empty() &&
      checkedFreeSites().empty() && !needZeroDecl)
    return source;

  // Enum initializers must be integer constant expressions.  A checked helper
  // call is not an ICE, so every site located inside an enum body is left as
  // plain source (the static checker still validates the constants).
  std::vector<std::pair<size_t, size_t>> enumRanges;
  {
    const std::vector<Token> etoks = lexCPlus(source);
    for (size_t i = 0; i < etoks.size(); ++i) {
      if (!(etoks[i].kind == TokKind::Identifier && etoks[i].text == "enum"))
        continue;
      size_t k = nextSigTok(etoks, i);
      if (k != kNPos && etoks[k].kind == TokKind::Identifier)
        k = nextSigTok(etoks, k);
      if (k == kNPos || !(etoks[k].kind == TokKind::Punct &&
                          etoks[k].text == "{"))
        continue;
      int depth = 0;
      size_t close = kNPos;
      for (size_t j = k; j < etoks.size(); ++j) {
        if (etoks[j].kind != TokKind::Punct)
          continue;
        if (etoks[j].text == "{") ++depth;
        else if (etoks[j].text == "}") {
          if (--depth == 0) { close = j; break; }
        }
      }
      if (close != kNPos)
        enumRanges.emplace_back(etoks[k].offset, endOfToken(etoks[close]));
    }
  }

  std::vector<SafetyInsertion> insertions;
  // Offsets already guarded by the token-level pass; a resolved AST site for
  // the same access must not emit a duplicate check.
  std::set<size_t> guardedIndexOffsets;
  bool haveBounds = false;
  bool havePtrFormation = false;
  bool haveRuntimeRegistry = false;
  bool haveFloatToInt = false;
  bool haveSizeMul = false;

  if (!arrayLengths.empty()) {
    std::vector<Token> toks = lexCPlus(source);
    auto significant = [&](size_t i) -> bool {
      return toks[i].kind != TokKind::Whitespace &&
             toks[i].kind != TokKind::Comment;
    };
    auto nextSig = [&](size_t i) -> size_t {
      while (i < toks.size() && !significant(i))
        ++i;
      return i < toks.size() ? i : kNPos;
    };
    for (size_t i = 0; i < toks.size(); ++i) {
      if (toks[i].kind != TokKind::Identifier)
        continue;
      auto ai = arrayLengths.find(toks[i].text);
      if (ai == arrayLengths.end())
        continue;
      size_t open = nextSig(i + 1);
      if (open == kNPos || toks[open].text != "[")
        continue;
      int depth = 0;
      size_t close = kNPos;
      for (size_t k = open; k < toks.size(); ++k) {
        if (toks[k].kind == TokKind::Punct) {
          if (toks[k].text == "[") ++depth;
          else if (toks[k].text == "]") {
            if (--depth == 0) { close = k; break; }
          }
        }
      }
      if (close == kNPos)
        continue;
      size_t first = nextSig(open + 1);
      if (first >= close)
        continue; // empty subscript: declaration, not an access
      // Constant indices are diagnosed statically; no runtime check needed.
      size_t last = close;
      do { --last; } while (last > first && !significant(last));
      size_t sigCount = 0;
      bool onlyNumber = true;
      for (size_t k = first; k <= last; ++k) {
        if (!significant(k)) continue;
        ++sigCount;
        if (toks[k].kind != TokKind::Number) onlyNumber = false;
      }
      if (sigCount == 1 && onlyNumber)
        continue;
      size_t idxBegin = toks[first].offset;
      size_t outer = kNPos;
      haveBounds = true;
      guardedIndexOffsets.insert(idxBegin);
      insertions.push_back({idxBegin, outer, 0,
                            "__cplus_bounds_check((size_t)(", 0});
      insertions.push_back({toks[close].offset, outer, 2,
                            "), " + std::to_string(ai->second) + ", \"" +
                                toks[i].text + "\")", 0});
    }
  }

  // Resolved sites from the AST pass.  These cover every aliased form the
  // purely lexical pass cannot see (cast aliases, struct/union fields,
  // multidim row pointers, interior pointers, typedef aliases, ...).  Wrap
  // the index expression in place so it is evaluated exactly once; the helper
  // returns the checked index.
  for (const CheckedBoundsSite &site : bounds) {
    if (guardedIndexOffsets.count(site.indexBegin))
      continue; // same access already guarded by the token-level pass
    // Clamp malformed producer offsets instead of dropping the site: a
    // recorded site must never be silently skipped.
    size_t idxBegin = site.indexBegin;
    size_t idxEnd = site.indexEnd;
    size_t close = site.close ? site.close : idxEnd;
    if (idxBegin > source.size()) idxBegin = source.size();
    if (idxEnd < idxBegin) idxEnd = idxBegin;
    if (idxEnd > source.size()) idxEnd = source.size();
    if (close < idxEnd) close = idxEnd;
    if (close > source.size()) close = source.size();
    insertions.push_back({idxBegin, 0, 0,
                          "__cplus_bounds_check((size_t)(", 0});
    insertions.push_back({close, 0, 2,
                          "), (size_t)(" + site.count + "), \"" + site.name +
                              "\")", 0});
    guardedIndexOffsets.insert(idxBegin);
    haveBounds = true;
  }

  for (const CheckedArithmeticSite &site : arith) {
    bool inEnum = false;
    for (const auto &r : enumRanges)
      if (site.begin >= r.first && site.end <= r.second) {
        inEnum = true;
        break;
      }
    if (inEnum)
      continue; // integer constant expression: no runtime guard is legal
    const size_t span = site.end > site.begin ? site.end - site.begin : 0;
    const std::string fn =
        std::string("__cplus_inline_") + checkedOpName(site.kind) + "_i64";
    const std::string bits = std::to_string(site.bits);
    const std::string sgn = site.isSigned ? "1" : "0";
    const std::string dst = site.type.empty() ? std::string("int") : site.type;
    const std::string opText =
        sourceSlice(source, site.op, site.op + site.opLength);

    // Classify the syntactic form.  Producers may either set the explicit
    // Op/flag fields or reuse a plain binary Op at a compound/inc/dec/unary
    // token; both are recognised here so no recorded site is skipped.
    CheckedArithmeticSite::Op kind = site.kind;
    bool compound = site.compound;
    bool postfix = site.postfix;
    // Implicit conversion / extent sites carry no meaningful operator token;
    // leave their classification to the producer.
    if (kind != CheckedArithmeticSite::Op::Narrow &&
        kind != CheckedArithmeticSite::Op::FloatToInt &&
        kind != CheckedArithmeticSite::Op::AllocExtent) {
      if (opText == "+=") { kind = CheckedArithmeticSite::Op::Add; compound = true; }
      else if (opText == "-=") { kind = CheckedArithmeticSite::Op::Sub; compound = true; }
      else if (opText == "*=") { kind = CheckedArithmeticSite::Op::Mul; compound = true; }
      else if (opText == "/=") { kind = CheckedArithmeticSite::Op::Div; compound = true; }
      else if (opText == "%=") { kind = CheckedArithmeticSite::Op::Rem; compound = true; }
      else if (opText == "<<=") { kind = CheckedArithmeticSite::Op::Shl; compound = true; }
      else if (opText == ">>=") { kind = CheckedArithmeticSite::Op::Shr; compound = true; }
      else if (opText == "++") {
        kind = CheckedArithmeticSite::Op::Inc;
        postfix = site.op > site.begin;
      } else if (opText == "--") {
        kind = CheckedArithmeticSite::Op::Dec;
        postfix = site.op > site.begin;
      } else if (opText == "-" && site.op == site.begin &&
                 kind == CheckedArithmeticSite::Op::Sub) {
        kind = CheckedArithmeticSite::Op::Neg;
      }
    }

    // --- value conversions: wrap the operand, no lvalue update needed ---
    if (kind == CheckedArithmeticSite::Op::Narrow ||
        kind == CheckedArithmeticSite::Op::FloatToInt) {
      if (kind == CheckedArithmeticSite::Op::FloatToInt) {
        // Guard the ORIGINAL floating value.  A recorded site may span the
        // explicit cast itself (`(int)d`); consuming that prefix here avoids
        // checking the already-clamped integer result, which would silently
        // accept an out-of-range conversion.
        const size_t castEnd = skipLeadingCasts(source, site.begin, site.end);
        insertions.push_back({site.begin, span, 0,
                              "((" + dst + ")__cplus_inline_f2i_i64((double)(",
                              castEnd > site.begin ? castEnd - site.begin : 0});
        insertions.push_back({site.end, span, 2, "), " + bits + ", " + sgn +
                                                    "))", 0});
        haveFloatToInt = true;
      } else {
        // ((DST)__cplus_inline_narrow_i64((SRC)(value), sb, ss, db, ds))
        const unsigned sb = site.srcBits ? site.srcBits : site.bits;
        insertions.push_back({site.begin, span, 0,
                              "((" + dst + ")" + fn + "((uint64_t)(", 0});
        insertions.push_back({site.end, span, 2,
                              "), " + std::to_string(sb) + ", " +
                                  (site.srcIsSigned ? "1" : "0") + ", " +
                                  bits + ", " + sgn + "))", 0});
      }
      continue;
    }

    // --- allocation extent size multiplication ---
    if (kind == CheckedArithmeticSite::Op::AllocExtent) {
      if (span == 0) {
        // Implicit extent: use producer-supplied expression text verbatim.
        const std::string c = site.countExpr.empty() ? std::string("1")
                                                    : site.countExpr;
        const std::string e = site.elemExpr.empty() ? std::string("1")
                                                   : site.elemExpr;
        insertions.push_back({site.begin, 0, 0,
                              "__cplus_inline_size_mul((size_t)(" + c +
                                  "), (size_t)(" + e + "))", 0});
      } else {
        // Split at the '*' so nested insertions inside the operands survive.
        insertions.push_back({site.begin, span, 0,
                              "__cplus_inline_size_mul((size_t)(", 0});
        insertions.push_back({site.op, span, 1, "), (size_t)(",
                              site.opLength});
        insertions.push_back({site.end, span, 2, "))", 0});
      }
      haveSizeMul = true;
      continue;
    }

    // --- ++ / -- : add/sub one with a single-evaluation lvalue update ---
    if (kind == CheckedArithmeticSite::Op::Inc ||
        kind == CheckedArithmeticSite::Op::Dec) {
      const std::string tail = ", " + dst + ", " + bits + ", " + sgn +
                               ", 1, " + (postfix ? "1" : "0") + ")";
      if (postfix) {
        // `a++`: prefix before `a`, convert the operator into macro args.
        insertions.push_back({site.begin, span, 0, "CPLUS_INC(", 0});
        insertions.push_back({site.op, span, 1, tail, site.opLength});
      } else {
        // `++a`: swallow the operator, append macro args after the operand.
        insertions.push_back({site.begin, span, 0, "CPLUS_INC(", 0});
        insertions.push_back({site.op, span, 1, "", site.opLength});
        insertions.push_back({site.end, span, 2, tail, 0});
      }
      continue;
    }

    // --- unary minus: checked 0 - operand ---
    if (kind == CheckedArithmeticSite::Op::Neg) {
      // A literal operand (`-1`, `-0x10u`) is a compile-time constant; the
      // static checker already handles it and a macro call here would break
      // integer constant expressions (enum bodies, case labels, array sizes).
      const std::string operand =
          sourceSlice(source, site.op + site.opLength, site.end);
      auto isConstDigit = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
      };
      size_t p = 0;
      while (p < operand.size() &&
             (operand[p] == ' ' || operand[p] == '\t' || operand[p] == '\n' ||
              operand[p] == '\r'))
        ++p;
      bool literal = p < operand.size() && isConstDigit(operand[p]);
      for (size_t k = p; literal && k < operand.size(); ++k) {
        const char c = operand[k];
        if (!(isConstDigit(c) || c == 'x' || c == 'X' || c == 'u' ||
              c == 'U' || c == 'l' || c == 'L' || c == ' ' || c == '\t'))
          literal = false;
      }
      if (literal)
        continue;
      insertions.push_back({site.begin, span, 0, "CPLUS_NEG(", 0});
      insertions.push_back({site.op, span, 1, "", site.opLength});
      insertions.push_back({site.end, span, 2,
                            ", " + dst + ", " + bits + ", " + sgn + ")", 0});
      continue;
    }

    // --- compound assignment: preserve the lvalue update ---
    if (compound) {
      insertions.push_back({site.begin, span, 0,
                            "CPLUS_CMPD_BIN(" + std::string(checkedOpName(kind)) +
                                ", ", 0});
      insertions.push_back({site.op, span, 1, ", " + dst + ", ",
                            site.opLength});
      insertions.push_back({site.end, span, 2,
                            ", " + bits + ", " + sgn + ")", 0});
      continue;
    }

    // --- plain binary: ((T)helper((int64_t)(lhs), (int64_t)(rhs), bits)) ---
    insertions.push_back({site.begin, span, 0,
                          "((" + dst + ")" + fn + "((int64_t)(", 0});
    insertions.push_back({site.op, span, 1, "), (int64_t)(", site.opLength});
    insertions.push_back({site.end, span, 2,
                          "), " + bits + ", " + sgn + "))", 0});
  }
  // Route ABI-visible malloc/free calls through the runtime side table.  The
  // helper returns the same pointer value, so no source semantics change.
  const auto &allocSites = checkedAllocSites();
  for (const CheckedAllocSite &site : allocSites) {
    size_t span = site.end > site.begin ? site.end - site.begin : 0;
    if (span == 0) {
      // Degenerate/implicit extent: never silently drop a recorded site.
      const std::string arg = site.argText.empty() ? std::string("0")
                                                   : site.argText;
      const std::string bytes = std::to_string(site.bytes);
      insertions.push_back({site.begin, 0, 0,
                            site.isRealloc
                                ? "cplus_realloc_registered((0), (" + arg +
                                      "), " + bytes + ", __FILE__, __LINE__)"
                                : "cplus_register_allocation((" + arg + "), " +
                                      bytes + ", __FILE__, __LINE__)",
                            0});
      haveRuntimeRegistry = true;
      continue;
    }
    if (site.isRealloc && !site.argText.empty()) {
      insertions.push_back({site.begin, span, 0,
                            "cplus_realloc_registered((" + site.argText +
                                "), (", 0});
      insertions.push_back({site.end, span, 2,
                            "), " + std::to_string(site.bytes) +
                                ", __FILE__, __LINE__)", 0});
    } else {
      insertions.push_back({site.begin, span, 0,
                            "cplus_register_allocation((", 0});
      insertions.push_back({site.end, span, 2,
                            "), " + std::to_string(site.bytes) +
                                ", __FILE__, __LINE__)", 0});
    }
    haveRuntimeRegistry = true;
  }
  const auto &freeSites = checkedFreeSites();
  for (const CheckedAllocSite &site : freeSites) {
    size_t span = site.end > site.begin ? site.end - site.begin : 0;
    const std::string arg = site.argText.empty() ? std::string("0")
                                                 : site.argText;
    std::string repl = "cplus_free_registered(" + arg +
                       ", __FILE__, __LINE__)";
    insertions.push_back({site.begin, span, 0, repl, span ? span : 0});
    haveRuntimeRegistry = true;
  }
  const auto &ptrSites = checkedPointerSites();
  for (const CheckedPointerSite &site : ptrSites) {
    size_t span = site.end > site.begin ? site.end - site.begin : 0;
    const std::string macro = site.compound ? "CPLUS_PTR_CHECK_ASSIGN("
                                            : "CPLUS_PTR_CHECK(";
    const std::string tail =
        ", " + (site.count.empty() ? std::string("0") : site.count) + ", " +
        (site.negative ? "1" : "0") + ", \"" + site.expr + "\", " +
        (site.deref ? "1" : "0") + ")";
    if (span == 0) {
      // No source extent (implicit site): use producer-supplied text verbatim.
      const std::string call = macro + "(" + site.base + ", " + site.index +
                               tail;
      insertions.push_back({site.begin, 0, 0, call, 0});
    } else {
      size_t opPos = site.op;
      size_t opLen = site.opLength;
      if (opPos < site.begin || opPos + opLen > site.end || opLen == 0) {
        opPos = source.find_first_of("+-", site.begin);
        opLen = 1;
        if (opPos != std::string::npos && opPos < site.end &&
            opPos + 1 < source.size() && source[opPos + 1] == '=')
          opLen = 2;
      }
      if (opPos == std::string::npos || opPos >= site.end)
        opPos = site.begin; // never skip a recorded site
      insertions.push_back({site.begin, span, 0, macro, 0});
      insertions.push_back({opPos, span, 1, ", ", opLen});
      insertions.push_back({site.end, span, 2, tail, 0});
    }
    havePtrFormation = true;
  }
  if (insertions.empty())
    return source;

  // Nested sites can share an offset: for `a+b+c` the inner expression ends
  // exactly where the outer operator sits.  At a shared offset the required
  // order is prefix (outermost first), then suffix (innermost first), then the
  // operator separator (which consumes the operator token).  Emitting a
  // separator before the inner suffix used to skip the suffix entirely and
  // leave unbalanced parentheses in the generated C.
  auto insertionRank = [](int p) -> int {
    if (p == 0) return 0; // prefix
    if (p == 2) return 1; // suffix
    return 2;             // operator separator
  };
  std::stable_sort(insertions.begin(), insertions.end(),
                   [&](const SafetyInsertion &a, const SafetyInsertion &b) {
                     if (a.offset != b.offset)
                       return a.offset < b.offset;
                     const int ra = insertionRank(a.priority);
                     const int rb = insertionRank(b.priority);
                     if (ra != rb)
                       return ra < rb;
                     if (a.priority == 0 && b.priority == 0)
                       return a.span > b.span; // outer prefix first
                     if (a.priority == 2 && b.priority == 2)
                       return a.span < b.span; // inner suffix first
                     return false;
                   });

  std::string out;
  out.reserve(source.size() + 64 * insertions.size());
  size_t pos = 0, next = 0;
  while (pos < source.size() || next < insertions.size()) {
    // A wide insertion (e.g. a free-call replacement) may consume source past
    // a weaker insertion recorded inside it; skip those defensively so the
    // loop always makes progress.
    while (next < insertions.size() && insertions[next].offset < pos)
      ++next;
    if (next < insertions.size() && insertions[next].offset > pos) {
      const size_t upto = std::min(insertions[next].offset, source.size());
      out.append(source, pos, upto - pos);
      pos = upto;
    }
    // Emit *all* insertions at this offset before consuming source bytes.
    // Taking the max replace length (rather than advancing per insertion) is
    // what preserves the inner suffix that shares an outer operator offset.
    size_t consume = 0;
    while (next < insertions.size() && insertions[next].offset == pos) {
      out += insertions[next].text;
      consume = std::max(consume, insertions[next].replace);
      ++next;
    }
    if (consume != 0) {
      pos += consume;
      if (pos > source.size())
        pos = source.size();
    } else {
      if (pos >= source.size())
        break;
      out += source[pos];
      ++pos;
    }
  }

  // Emit the helper declarations before the first #line directive so the
  // original source line numbers are preserved.
  std::string prelude;
  if (needZeroDecl)
    prelude += "#include <stddef.h>\n"
               "extern void cplus_zero(void *, size_t);\n";
  if (haveBounds || havePtrFormation)
    prelude += "#include <stddef.h>\n";
  if (haveBounds)
    prelude += "extern size_t __cplus_bounds_check(size_t, size_t, "
               "const char *);\n";
  if (havePtrFormation)
    prelude += "#include <stdint.h>\n"
               "extern void cplus_check_ptr_formation(const void *, size_t, "
               "int64_t, const char *, int, const char *);\n"
               "extern void cplus_check_bounds(const void *, size_t, size_t, "
               "const char *, int, const char *);\n"
               // Bind the base address and the index once each (GNU statement
               // expressions; Clang accepts them in gnu89/gnu17/c99).  This
               // avoids the double evaluation of base/index that a bare comma
               // guard would cause for side-effecting operands.
               "#if defined(__GNUC__) || defined(__clang__)\n"
               "#define CPLUS_PTR_BODY(B, I, C, N, T, D) \\\n"
               "  __typeof__(&(B)) __cplus_bp = &(B); \\\n"
               "  __typeof__(I) __cplus_ip = (I); \\\n"
               "  if (D) cplus_check_bounds((const void *)(*__cplus_bp), \\\n"
               "      (size_t)(C), (size_t)(__cplus_ip), __FILE__, __LINE__, T); \\\n"
               "  else cplus_check_ptr_formation((const void *)(*__cplus_bp), \\\n"
               "      (size_t)(C), (int64_t)(__cplus_ip) * ((N) ? -1 : 1), \\\n"
               "      __FILE__, __LINE__, T);\n"
               "#define CPLUS_PTR_CHECK(B, I, C, N, T, D) \\\n"
               "  ({ CPLUS_PTR_BODY(B, I, C, N, T, D) \\\n"
               "     ((N) ? (*__cplus_bp - __cplus_ip) \\\n"
               "          : (*__cplus_bp + __cplus_ip)); })\n"
               "#define CPLUS_PTR_CHECK_ASSIGN(B, I, C, N, T, D) \\\n"
               "  ({ CPLUS_PTR_BODY(B, I, C, N, T, D) \\\n"
               "     *__cplus_bp = ((N) ? (*__cplus_bp - __cplus_ip) \\\n"
               "                       : (*__cplus_bp + __cplus_ip)); \\\n"
               "     *__cplus_bp; })\n"
               "#else\n"
               "#define CPLUS_PTR_CHECK(B, I, C, N, T, D) \\\n"
               "  ((D) ? (cplus_check_bounds((const void *)(B), (size_t)(C), \\\n"
               "       (size_t)(I), __FILE__, __LINE__, T), (B) + (I)) \\\n"
               "       : (cplus_check_ptr_formation((const void *)(B), \\\n"
               "       (size_t)(C), (int64_t)(I) * ((N) ? -1 : 1), \\\n"
               "       __FILE__, __LINE__, T), (B) + (I)))\n"
               "#define CPLUS_PTR_CHECK_ASSIGN(B, I, C, N, T, D) \\\n"
               "  ((B) = CPLUS_PTR_CHECK(B, I, C, N, T, D))\n"
               "#endif\n";
  if (haveRuntimeRegistry)
    prelude += "#include <stddef.h>\n"
               "extern void *cplus_register_allocation(void *, size_t, "
               "const char *, int);\n"
               "extern void cplus_free_registered(void *, const char *, "
               "int);\n"
               "extern void *cplus_realloc_registered(void *, void *, "
               "size_t, const char *, int);\n";
  if (!arith.empty()) {
    // The runtime helpers produce the deterministic trap, but an ordinary
    // cross-TU call on every operation is far too expensive in hot loops.
    // Emit inline wrappers: the fast path folds into surrounding code and
    // only the failure path calls the runtime helper.  Predicates follow
    // Clang's EmitUndefinedBehaviorIntegerDivAndRemCheck / EmitShl and UBSan's
    // handleDivremOverflowImpl (Apache-2.0 WITH LLVM-exception), expressed as
    // plain C so no sanitizer runtime is required.
    static const char *ops[] = {"add", "sub", "mul", "div",
                                "rem", "shl", "shr", "narrow"};
    prelude += "#include <stdint.h>\n";
    for (const char *op : ops) {
      if (std::strcmp(op, "narrow") == 0) {
        prelude += "extern int64_t __cplus_checked_narrow_i64(uint64_t, "
                   "unsigned, unsigned, unsigned, unsigned);\n";
        continue;
      }
      prelude += std::string("extern int64_t __cplus_checked_") + op +
                 "_i64(int64_t, int64_t, unsigned, unsigned);\n";
    }
    // Float->int and allocation-extent helpers are emitted only when a site
    // of that kind was actually recorded, so TUs that do not need them gain
    // no new external dependencies.  The wrappers keep the check inline and
    // defer diagnostics to the existing runtime trap symbols.
    if (haveFloatToInt)
      prelude +=
          "extern int64_t __cplus_checked_f2i_i64(double, unsigned, "
          "unsigned);\n"
          "static inline int64_t __cplus_inline_f2i_i64(double v, "
          "unsigned bits, unsigned sgn) {\n"
          "  double lo, hi;\n"
          "  if (v != v) return __cplus_checked_f2i_i64(v, bits, sgn);\n"
          "  if (sgn) {\n"
          "    if (bits == 32) { lo = -2147483648.0; hi = 2147483648.0; }\n"
          "    else if (bits == 64) {\n"
          "      lo = -9223372036854775808.0; hi = 9223372036854775808.0;\n"
          "    } else {\n"
          "      int b = (int)bits, i; double p = 1.0;\n"
          "      for (i = 0; i < b - 1; ++i) p *= 2.0;\n"
          "      lo = -p; hi = p;\n"
          "    }\n"
          "  } else {\n"
          "    if (bits == 32) { lo = 0.0; hi = 4294967296.0; }\n"
          "    else if (bits == 64) {\n"
          "      lo = 0.0; hi = 18446744073709551616.0;\n"
          "    } else {\n"
          "      int b = (int)bits, i; double p = 1.0;\n"
          "      for (i = 0; i < b; ++i) p *= 2.0;\n"
          "      lo = 0.0; hi = p;\n"
          "    }\n"
          "  }\n"
          "  if (!(v >= lo && v < hi)) return __cplus_checked_f2i_i64(v, "
          "bits, sgn);\n"
          "  return (int64_t)v;\n"
          "}\n";
    if (haveSizeMul)
      prelude += "#include <stddef.h>\n"
                 "extern size_t __cplus_checked_size_mul(size_t, size_t);\n"
                 "static inline size_t __cplus_inline_size_mul(size_t a, "
                 "size_t b) {\n"
                 "  if (b != 0 && a > ((size_t)-1) / b)\n"
                 "    return __cplus_checked_size_mul(a, b);\n"
                 "  return a * b;\n"
                 "}\n";
    prelude +=
        "#if defined(__has_builtin)\n"
        "#  if __has_builtin(__builtin_add_overflow) && "
        "__has_builtin(__builtin_sub_overflow) && "
        "__has_builtin(__builtin_mul_overflow)\n"
        "#    define CPLUS_HAVE_INLINE_ARITH 1\n"
        "#  endif\n"
        "#endif\n"
        "#ifdef CPLUS_HAVE_INLINE_ARITH\n"
        "#define CPLUS_ARITH_INLINE __attribute__((unused)) static inline\n";

    auto emitWrapper = [&](const char *op, const std::string &body) {
      prelude += std::string("CPLUS_ARITH_INLINE int64_t __cplus_inline_") +
                 op + "_i64(int64_t a, int64_t b, unsigned bits, "
                      "unsigned sgn) {\n" +
                 body + "}\n";
    };
    emitWrapper("add",
                "  int64_t r; int32_t r32;\n"
                "  if (bits == 32) {\n"
                "    if (__builtin_add_overflow((int32_t)a, (int32_t)b, &r32))\n"
                "      return __cplus_checked_add_i64(a, b, bits, sgn);\n"
                "    return (int64_t)r32;\n"
                "  }\n"
                "  if (bits == 64 && sgn) {\n"
                "    if (__builtin_add_overflow(a, b, &r))\n"
                "      return __cplus_checked_add_i64(a, b, bits, sgn);\n"
                "    return r;\n"
                "  }\n"
                "  return __cplus_checked_add_i64(a, b, bits, sgn);\n");
    emitWrapper("sub",
                "  int64_t r; int32_t r32;\n"
                "  if (bits == 32) {\n"
                "    if (__builtin_sub_overflow((int32_t)a, (int32_t)b, &r32))\n"
                "      return __cplus_checked_sub_i64(a, b, bits, sgn);\n"
                "    return (int64_t)r32;\n"
                "  }\n"
                "  if (bits == 64 && sgn) {\n"
                "    if (__builtin_sub_overflow(a, b, &r))\n"
                "      return __cplus_checked_sub_i64(a, b, bits, sgn);\n"
                "    return r;\n"
                "  }\n"
                "  return __cplus_checked_sub_i64(a, b, bits, sgn);\n");
    emitWrapper("mul",
                "  int64_t r; int32_t r32;\n"
                "  if (bits == 32) {\n"
                "    if (__builtin_mul_overflow((int32_t)a, (int32_t)b, &r32))\n"
                "      return __cplus_checked_mul_i64(a, b, bits, sgn);\n"
                "    return (int64_t)r32;\n"
                "  }\n"
                "  if (bits == 64 && sgn) {\n"
                "    if (__builtin_mul_overflow(a, b, &r))\n"
                "      return __cplus_checked_mul_i64(a, b, bits, sgn);\n"
                "    return r;\n"
                "  }\n"
                "  return __cplus_checked_mul_i64(a, b, bits, sgn);\n");
    emitWrapper("div",
                "  if (b == 0)\n"
                "    return __cplus_checked_div_i64(a, b, bits, sgn);\n"
                "  if (sgn && bits > 1 && bits <= 64 && b == -1 &&\n"
                "      a == (bits == 32 ? (int64_t)INT32_MIN : INT64_MIN))\n"
                "    return __cplus_checked_div_i64(a, b, bits, sgn);\n"
                "  return a / b;\n");
    emitWrapper("rem",
                "  if (b == 0)\n"
                "    return __cplus_checked_rem_i64(a, b, bits, sgn);\n"
                "  if (sgn && bits > 1 && bits <= 64 && b == -1 &&\n"
                "      a == (bits == 32 ? (int64_t)INT32_MIN : INT64_MIN))\n"
                "    return __cplus_checked_rem_i64(a, b, bits, sgn);\n"
                "  return a % b;\n");
    emitWrapper("shl",
                "  if (b < 0 || (unsigned long long)b >= bits)\n"
                "    return __cplus_checked_shl_i64(a, b, bits, sgn);\n"
                "  if (sgn) {\n"
                "    uint64_t ua = (uint64_t)a; unsigned c = (unsigned)b;\n"
                "    uint64_t limit = (~(uint64_t)0) >> (64 - bits);\n"
                "    if (a < 0 || ((ua << c) & ~limit))\n"
                "      return __cplus_checked_shl_i64(a, b, bits, sgn);\n"
                "  }\n"
                "  return (int64_t)((uint64_t)a << (uint64_t)b);\n");
    emitWrapper("shr",
                "  if (b < 0 || (unsigned long long)b >= bits)\n"
                "    return __cplus_checked_shr_i64(a, b, bits, sgn);\n"
                "  return (int64_t)(a >> (uint64_t)b);\n");
    // Round-trip predicate from Clang's any-extend + compare, expressed in C.
    prelude +=
        "CPLUS_ARITH_INLINE int64_t __cplus_inline_narrow_i64(\n"
        "    uint64_t v, unsigned sb, unsigned ss, unsigned db, unsigned ds) {\n"
        "  uint64_t sv = v;\n"
        "  if (ss && sb > 0 && sb < 64 && ((v >> (sb - 1)) & 1u))\n"
        "    sv = v | (~((uint64_t)0) << sb);\n"
        "  if (db < 64) {\n"
        "    uint64_t m = (((uint64_t)1) << db) - 1;\n"
        "    uint64_t t = sv & m;\n"
        "    if (ds && ((t >> (db - 1)) & 1u)) t |= ~m;\n"
        "    if (t == sv) return (int64_t)t;\n"
        "  } else if (sv == v) {\n"
        "    return (int64_t)v;\n"
        "  }\n"
        "  return __cplus_checked_narrow_i64(v, sb, ss, db, ds);\n"
        "}\n";
    prelude += "#else\n";
    for (const char *op : ops)
      prelude += std::string("#define __cplus_inline_") + op +
                 "_i64 __cplus_checked_" + op + "_i64\n";
    prelude += "#endif\n";

    // Lvalue-preserving forms.  The GNU variants bind the lvalue address once
    // so operands with side effects (e.g. `a[i++] += f()`) are evaluated
    // exactly once; the portable fallback covers strict ISO C compilers.
    prelude +=
        "#if defined(__GNUC__) || defined(__clang__)\n"
        "#define CPLUS_CMPD_BIN(OP, P, T, B, BT, SG) \\\n"
        "  ({ __typeof__(&(P)) __cplus_ap = &(P); \\\n"
        "     *__cplus_ap = (T)__cplus_inline_##OP##_i64( \\\n"
        "         (int64_t)(*__cplus_ap), (int64_t)(B), (BT), (SG)); \\\n"
        "     *__cplus_ap; })\n"
        "#define CPLUS_INC(P, T, BT, SG, D, POST) \\\n"
        "  ({ __typeof__(&(P)) __cplus_ap = &(P); \\\n"
        "     __typeof__(*__cplus_ap) __cplus_ov = *__cplus_ap; \\\n"
        "     *__cplus_ap = (T)__cplus_inline_add_i64( \\\n"
        "         (int64_t)__cplus_ov, (int64_t)(D), (BT), (SG)); \\\n"
        "     (POST) ? __cplus_ov : *__cplus_ap; })\n"
        "#else\n"
        "#define CPLUS_CMPD_BIN(OP, P, T, B, BT, SG) \\\n"
        "  ((P) = (T)__cplus_inline_##OP##_i64( \\\n"
        "      (int64_t)(P), (int64_t)(B), (BT), (SG)))\n"
        "#define CPLUS_INC(P, T, BT, SG, D, POST) \\\n"
        "  ((P) = (T)__cplus_inline_add_i64((int64_t)(P), \\\n"
        "      (int64_t)(D), (BT), (SG)))\n"
        "#endif\n"
        "#define CPLUS_NEG(X, T, BT, SG) \\\n"
        "  ((T)__cplus_inline_sub_i64(0, (int64_t)(X), (BT), (SG)))\n";
  }
  return prelude + out;
}

} // namespace cplus

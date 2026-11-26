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

#include <cstring> // std::strcmp (used by helper-name reservation checks)

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

  std::string fieldClass(const ClassInfo *owner, const std::string &name) const {
    if (!owner)
      return std::string();
    for (const auto &range : owner->fields)
      for (size_t k = range.first + 1; k <= range.second; ++k) {
        if (ts[k].text != name)
          continue;
        size_t p = ts.prevSig(k == 0 ? 0 : k - 1);
        if (p != kNPos && classNames_.count(ts[p].text))
          return ts[p].text;
      }
    return std::string();
  }

  bool tryEmitMethodCall(size_t i, const ClassInfo *selfClass = nullptr) {
    if (ts[i].kind != TokKind::Identifier)
      return false;

    // Nested receiver: `w.c.inc()` or `this->c.inc()`. The receiver is a
    // class-typed MEMBER of another object, so the whole chain `w.c` / 
    // `self->c` must be passed as the hidden self argument. Handling this at
    // the base identifier avoids emitting a stray `w.` before the rewrite.
    {
      size_t op = ts.nextSig(i);
      size_t sep = kNPos, memberEnd = kNPos, methodIdx = kNPos;
      std::string nestedClass;
      if (ts.isPunct(op, ".") || ts.isPunct(op, "->")) {
        size_t cur = i;
        for (;;) {
          size_t s = ts.nextSig(cur);
          if (!ts.isPunct(s, ".") && !ts.isPunct(s, "->"))
            break;
          size_t mem = ts.nextSig(s);
          if (mem >= ts.size() || ts[mem].kind != TokKind::Identifier)
            break;
          auto mit = varClasses_.find(ts[mem].text);
          std::string cls = mit != varClasses_.end() ? mit->second : std::string();
          if (cls.empty() && selfClass)
            cls = fieldClass(selfClass, ts[mem].text);
          if (cls.empty())
            break;
          nestedClass = cls;
          sep = s;
          memberEnd = mem;
          size_t after = ts.nextSig(mem);
          if (ts.isPunct(after, ".") || ts.isPunct(after, "->")) {
            size_t candidate = ts.nextSig(after);
            // The identifier after the separator is the method when it is
            // followed by '('; otherwise the chain goes one level deeper.
            if (candidate < ts.size() &&
                ts[candidate].kind == TokKind::Identifier &&
                ts.isPunct(ts.nextSig(candidate), "(")) {
              sep = after;
              memberEnd = mem;
              methodIdx = candidate;
              break;
            }
            cur = mem; // deeper chain: w.a.b.method() / this->a->b.method()
            continue;
          }
          break;
        }
      }
      if (!nestedClass.empty() && memberEnd != kNPos &&
          (ts.isPunct(sep, ".") || ts.isPunct(sep, "->"))) {
        size_t method =
            methodIdx != kNPos ? methodIdx : ts.nextSig(memberEnd);
        if (method < ts.size() && ts[method].kind == TokKind::Identifier) {
          bool known = false;
          for (const auto &c : classes_)
            if (c.name == nestedClass && c.methodNames.count(ts[method].text))
              known = true;
          if (known || externalClassNames_.count(nestedClass)) {
            size_t lp = ts.nextSig(method);
            if (ts.isPunct(lp, "(")) {
              emit(nestedClass + "_" + ts[method].text + "(");
              // Object members need their address; pointer members already
              // are pointers. This also covers 3+ level chains.
              if (ts.isPunct(sep, "."))
                emit("&");
              for (size_t k = i; k <= memberEnd; ++k)
                emitToken(k);
              if (!ts.isPunct(ts.nextSig(lp), ")"))
                emit(", ");
              skipTo_ = lp;
              changed_ = true;
              return true;
            }
          }
        }
      }
    }

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
  // Preserve the original source's trailing newlines exactly.  Token
  // reconstruction drops trailing trivia, so a naive single-newline restore
  // collapsed multiple blank lines at EOF and broke byte identity (e.g. an
  // input ending `}\n\n\n` came back as `}\n`).  Normalise ALL trailing
  // newlines of the reconstructed text to the exact count in the source.
  size_t trailingNewlines = 0;
  while (trailingNewlines < source.size() &&
         source[source.size() - 1 - trailingNewlines] == '\n')
    ++trailingNewlines;
  while (!result.text.empty() && result.text.back() == '\n')
    result.text.pop_back();
  result.text.append(trailingNewlines, '\n');
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

/// Build a self-contained guarded expression for one conversion site.  Unlike
/// the prefix/suffix insertions used for binary operators, this returns a
/// complete C expression so nested conversion chains can be composed
/// innermost-first without their argument lists colliding at a shared offset.
static std::string wrapConversionText(const CheckedArithmeticSite &s,
                                      const std::string &operand) {
  const std::string dst = s.type.empty() ? std::string("int") : s.type;
  const std::string bits = std::to_string(s.bits);
  const std::string sgn = s.isSigned ? "1" : "0";
  if (s.kind == CheckedArithmeticSite::Op::FloatToInt)
    return "((" + dst + ")__cplus_inline_f2i_i64((double)(" + operand +
           "), " + bits + ", " + sgn + "))";
  const unsigned sb = s.srcBits ? s.srcBits : s.bits;
  return "((" + dst + ")__cplus_inline_narrow_i64((uint64_t)(" + operand +
         "), " + std::to_string(sb) + ", " +
         (s.srcIsSigned ? "1" : "0") + ", " + bits + ", " + sgn + "))";
}

/// Nesting order for conversion chains.  Float->int happens first, then
/// wider-to-narrower integer casts.  Smaller rank means "inner".
static int conversionRank(const CheckedArithmeticSite &s) {
  if (s.kind == CheckedArithmeticSite::Op::FloatToInt)
    return 0;
  return 1 + (s.srcBits >= 64 ? 0 : static_cast<int>(64 - s.srcBits));
}

/// Byte ranges of file-scope (static/global) initialisers.  C requires these
/// to be constant expressions, so a runtime guard call must never be inserted
/// into one: clang would reject the lowered C with "initializer element is not
/// a compile-time constant".  The static checker validates the constants; the
/// rewriter leaves them untouched.
static std::vector<std::pair<size_t, size_t>>
staticInitializerRanges(const std::string &source) {
  const std::vector<Token> toks = lexCPlus(source);
  std::vector<std::pair<size_t, size_t>> ranges;
  int brace = 0;
  for (size_t i = 0; i < toks.size(); ++i) {
    if (toks[i].kind == TokKind::Punct && toks[i].text == "{") { ++brace; continue; }
    if (toks[i].kind == TokKind::Punct && toks[i].text == "}") {
      if (brace > 0) --brace;
      continue;
    }
    if (brace != 0 || toks[i].kind != TokKind::Punct || toks[i].text != "=")
      continue;
    int par = 0, brk = 0, brc = 0;
    size_t end = kNPos;
    for (size_t k = i + 1; k < toks.size(); ++k) {
      if (toks[k].kind != TokKind::Punct) continue;
      const std::string &p = toks[k].text;
      if (p == "(") ++par;
      else if (p == ")") { if (par > 0) --par; }
      else if (p == "[") ++brk;
      else if (p == "]") { if (brk > 0) --brk; }
      else if (p == "{") ++brc;
      else if (p == "}") { if (brc > 0) --brc; }
      else if ((p == ";" || p == ",") && par == 0 && brk == 0 && brc == 0) {
        end = toks[k].offset;
        break;
      }
    }
    if (end != kNPos)
      ranges.emplace_back(toks[i].offset + toks[i].text.size(), end);
  }
  return ranges;
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

  // Phase 1b: function signatures.  Record return types and parameter types so
  // implicit narrowing in `return x;` and `f(x)` can be recovered lexically in
  // DEFAULT mode (where the AST producer intentionally emits no native guards
  // because the backend UBSan handles signed overflow but NOT integer
  // truncation).  Only concrete definitions with a known type are modelled.
  struct FnSig {
    TokenType ret;
    std::vector<TokenType> params;
  };
  std::map<std::string, FnSig> fnSigs;
  std::vector<std::tuple<size_t, size_t, TokenType>> fnBodies; // [open, close]
  for (size_t i = 0; i < toks.size(); ++i) {
    size_t after = kNPos;
    TokenType ret = parseTokenType(toks, i, toks.size(), after);
    const bool voidRet = !ret.valid &&
                         toks[i].kind == TokKind::Identifier &&
                         toks[i].text == "void";
    if (voidRet)
      after = nextSigTok(toks, i);
    if ((!ret.valid && !voidRet) || after == kNPos || after >= toks.size())
      continue;
    size_t n = after;
    if (n < toks.size() && toks[n].kind == TokKind::Punct &&
        toks[n].text == "*")
      continue; // pointer return: no integer narrowing
    if (toks[n].kind != TokKind::Identifier)
      continue;
    const std::string name = toks[n].text;
    size_t lp = nextSigTok(toks, n);
    if (lp == kNPos || toks[lp].kind != TokKind::Punct ||
        toks[lp].text != "(")
      continue;
    int par = 0;
    size_t rp = kNPos;
    for (size_t k = lp; k < toks.size(); ++k) {
      if (toks[k].kind != TokKind::Punct)
        continue;
      if (toks[k].text == "(") ++par;
      else if (toks[k].text == ")" && --par == 0) { rp = k; break; }
    }
    if (rp == kNPos)
      continue;
    size_t body = nextSigTok(toks, rp);
    if (body == kNPos || toks[body].kind != TokKind::Punct ||
        toks[body].text != "{")
      continue;
    size_t close = kNPos;
    int depth = 0;
    for (size_t k = body; k < toks.size(); ++k) {
      if (toks[k].kind != TokKind::Punct)
        continue;
      if (toks[k].text == "{") ++depth;
      else if (toks[k].text == "}" && --depth == 0) { close = k; break; }
    }
    if (close == kNPos)
      continue;
    FnSig sig;
    sig.ret = ret;
    size_t p = nextSigTok(toks, lp + 1);
    while (p != kNPos && p < rp) {
      size_t pa = kNPos;
      TokenType pt = parseTokenType(toks, p, rp, pa);
      if (!pt.valid) {
        sig.params.push_back(TokenType()); // keep argument indices aligned
        p = nextSigTok(toks, p);
        continue;
      }
      size_t q = pa;
      while (q < rp && toks[q].kind == TokKind::Punct &&
             toks[q].text == "*") ++q;
      if (q < rp && toks[q].kind == TokKind::Identifier) {
        sig.params.push_back(pt);
        vars[toks[q].text] = pt; // parameter is a known-typed variable too
      }
      p = q;
      while (p < rp && !(toks[p].kind == TokKind::Punct &&
                         toks[p].text == ",")) ++p;
      p = nextSigTok(toks, p);
    }
    fnSigs[name] = sig;
    fnBodies.emplace_back(toks[body].offset, endOfToken(toks[close]), ret);
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

    // Helper: a bare identifier `value` converted to a known narrower type.
    auto emitNarrowAt = [&](size_t value, const TokenType &dst) {
      if (value == kNPos || toks[value].kind != TokKind::Identifier ||
          !dst.valid || dst.isFloat)
        return;
      TokenType src = lookupVar(vars, toks[value].text);
      if (!src.valid || src.isFloat || src.bits <= dst.bits)
        return;
      CheckedArithmeticSite s;
      s.kind = CheckedArithmeticSite::Op::Narrow;
      s.begin = toks[value].offset;
      s.op = s.begin;
      s.opLength = 0;
      s.end = endOfToken(toks[value]);
      s.bits = dst.bits;
      s.isSigned = dst.isSigned;
      s.type = dst.spelling;
      s.srcBits = src.bits;
      s.srcIsSigned = src.isSigned;
      s.srcType = src.spelling;
      sites.push_back(std::move(s));
    };

    // Implicit narrowing in a plain assignment `c = x;` where both sides are
    // bare, typed identifiers.  Declarations (`TYPE c = x;`) are handled by
    // Phase 1 and dedup by the full site tuple; conditions (`if (c = x)`)
    // are skipped because the value is not followed by ';'/','.
    if (tk.kind == TokKind::Punct && op == "=") {
      size_t lhs = prevSigTok(toks, i);
      size_t rhs = nextSigTok(toks, i);
      size_t afterRhs = rhs == kNPos ? kNPos : nextSigTok(toks, rhs);
      if (lhs != kNPos && rhs != kNPos && afterRhs != kNPos &&
          toks[lhs].kind == TokKind::Identifier &&
          toks[afterRhs].kind == TokKind::Punct &&
          (toks[afterRhs].text == ";" || toks[afterRhs].text == ","))
        emitNarrowAt(rhs, lookupVar(vars, toks[lhs].text));
    }

    // Implicit narrowing in `return x;` against the enclosing function's
    // return type.
    if (tk.kind == TokKind::Identifier && op == "return") {
      size_t value = nextSigTok(toks, i);
      for (const auto &b : fnBodies) {
        if (tk.offset < std::get<0>(b) || tk.offset >= std::get<1>(b))
          continue;
        emitNarrowAt(value, std::get<2>(b));
        break;
      }
    }

    // Implicit narrowing of a bare identifier passed to a known parameter.
    if (tk.kind == TokKind::Identifier) {
      auto fit = fnSigs.find(op);
      if (fit != fnSigs.end()) {
        size_t lp = nextSigTok(toks, i);
        if (lp != kNPos && toks[lp].kind == TokKind::Punct &&
            toks[lp].text == "(") {
          size_t rp = kNPos;
          {
            int pd = 0;
            for (size_t q = lp; q < toks.size(); ++q) {
              if (toks[q].kind != TokKind::Punct) continue;
              if (toks[q].text == "(") ++pd;
              else if (toks[q].text == ")" && --pd == 0) { rp = q; break; }
            }
          }
          size_t after = rp == kNPos ? kNPos : nextSigTok(toks, rp);
          // Function definition, not a call: its parameters were handled in
          // Phase 1b and must not be re-treated as arguments.
          bool definition = after != kNPos && toks[after].kind == TokKind::Punct &&
                            toks[after].text == "{";
          if (!definition && rp != kNPos) {
            size_t arg = 0;
            int depth = 0;
            size_t t = lp + 1;
            while (t < rp) {
              if (isTriviaTok(toks[t])) { ++t; continue; }
              if (toks[t].kind == TokKind::Punct && toks[t].text == ",") {
                ++arg; ++t; continue; // empty / already-consumed argument
              }
              if (toks[t].kind == TokKind::Punct && toks[t].text == "(") { ++depth; ++t; continue; }
              if (toks[t].kind == TokKind::Punct && toks[t].text == ")") {
                if (depth == 0) break;
                --depth; ++t; continue;
              }
              if (depth == 0) {
                if (arg < fit->second.params.size())
                  emitNarrowAt(t, fit->second.params[arg]);
                ++arg;
                // Skip the rest of this argument up to a depth-0 ',' or ')'.
                while (t < rp &&
                       !(toks[t].kind == TokKind::Punct && toks[t].text == ",") &&
                       !(toks[t].kind == TokKind::Punct && toks[t].text == ")"))
                  ++t;
                continue;
              }
              ++t;
            }
          }
        }
      }
    }

    // Allocation extent: malloc(count * size) can wrap before the allocator
    // sees the request.  Guard *every* depth-0 multiplication in the first
    // argument, including nested chains (`n * size * 2`) and a multiplicative
    // term inside a larger expression (`n * size + hdr`).  Each guard spans the
    // whole left-associative prefix so an intermediate product that fits but
    // whose extension overflows is still caught.
    if (tk.kind == TokKind::Identifier &&
        (op == "malloc" || op == "calloc" || op == "realloc")) {
      size_t lp = nextSigTok(toks, i);
      if (lp != kNPos && toks[lp].kind == TokKind::Punct &&
          toks[lp].text == "(") {
        int par = 0;
        size_t argStart = kNPos, argEnd = kNPos;
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
        }
        // Significant tokens at nesting depth zero of the first argument.
        // A parenthesised group is kept as a single factor (its '(' and ')'
        // are recorded); tokens inside it are not separate operators here.
        std::vector<size_t> d0;
        if (argStart != kNPos && argEnd != kNPos && argEnd > argStart) {
          int depth = 0;
          for (size_t k = argStart; k < argEnd; ++k) {
            if (isTriviaTok(toks[k]))
              continue;
            bool open = false, close = false;
            if (toks[k].kind == TokKind::Punct) {
              const std::string &p = toks[k].text;
              open = p == "(" || p == "[" || p == "{";
              close = p == ")" || p == "]" || p == "}";
            }
            if (close && depth > 0) --depth;
            if (depth == 0) d0.push_back(k);
            if (open) ++depth;
          }
        }
        auto isLowPrec = [](const Token &t) {
          if (t.kind != TokKind::Punct)
            return false;
          static const std::set<std::string> kBoundary = {
              "+",  "-",  "<<", ">>", "<",  ">",  "<=", ">=",
              "==", "!=", "&",  "^",  "|",  "&&", "||", "?", ":"};
          return kBoundary.count(t.text) != 0;
        };
        auto isMulOp = [](const Token &t) {
          return t.kind == TokKind::Punct &&
                 (t.text == "*" || t.text == "/" || t.text == "%");
        };
        for (size_t d = 0; d < d0.size(); ++d) {
          const size_t k = d0[d];
          if (toks[k].kind != TokKind::Punct || toks[k].text != "*" ||
              d == 0 || d + 1 >= d0.size())
            continue;
          // A `*` is binary multiplication only when it separates two complete
          // factors.  In `malloc(sizeof *p)` it is a unary dereference.
          if (!canEndFactor(toks[d0[d - 1]]) ||
              !canStartFactor(toks[d0[d + 1]]))
            continue;
          // Left boundary: first token after the nearest low-precedence
          // separator.  Multiplicative operators do NOT reset it, so the k-th
          // `*` guard covers the whole left-associative prefix.
          size_t beginIdx = d0[0];
          for (size_t q = d; q > 0; --q)
            if (isLowPrec(toks[d0[q - 1]])) { beginIdx = d0[q]; break; }
          // Right boundary: last significant token before the next
          // multiplicative or low-precedence operator at this level.
          size_t endIdx = kNPos;
          for (size_t q = d + 1; q < d0.size(); ++q)
            if (isMulOp(toks[d0[q]]) || isLowPrec(toks[d0[q]])) {
              endIdx = prevSigTok(toks, d0[q]);
              break;
            }
          if (endIdx == kNPos) {
            endIdx = argEnd;
            while (endIdx > argStart && isTriviaTok(toks[endIdx - 1])) --endIdx;
            if (endIdx > argStart) --endIdx;
          }
          if (endIdx == kNPos || endIdx <= k)
            continue;
          CheckedArithmeticSite s;
          s.kind = CheckedArithmeticSite::Op::AllocExtent;
          s.begin = toks[beginIdx].offset;
          s.op = toks[k].offset;
          s.opLength = toks[k].text.size();
          s.end = endOfToken(toks[endIdx]);
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
      if (!src.valid)
        continue;
      CheckedArithmeticSite s;
      s.begin = toks[val].offset;
      s.op = s.begin;
      s.opLength = 0;
      s.end = endOfToken(toks[val]);
      s.bits = dst.bits;
      s.isSigned = dst.isSigned;
      s.type = dst.spelling;
      if (src.isFloat) {
        s.kind = CheckedArithmeticSite::Op::FloatToInt;
      } else {
        // `(T)x` where x is a wider integer: a genuine truncating cast.  A
        // nested cast such as `(char)(int)x` leaves the outer operand as '('
        // and is therefore handled by the AST producer / composed chain; we
        // only recover the unambiguous bare-identifier form here.
        if (dst.isFloat || src.bits <= dst.bits)
          continue;
        s.kind = CheckedArithmeticSite::Op::Narrow;
        s.srcBits = src.bits;
        s.srcIsSigned = src.isSigned;
        s.srcType = src.spelling;
      }
      sites.push_back(std::move(s));
      continue;
    }
  }
  return sites;
}

// ---------------------------------------------------------------------------
// Null-dereference site recovery.
//
// The runtime exposes cplus_check_nonnull() ("emitted by the compiler for
// every checked pointer dereference"), but the AST pass does not record plain
// `*p` / `p->f` / `p[i]` sites.  Recover the unambiguous identifier-base forms
// directly from the lowered C tokens.  Restricting the base to a bare
// identifier keeps evaluation single, avoids the array-vs-pointer ambiguity
// for subscripts, and never injects a call into a declaration.  Forms we
// cannot classify confidently are left untouched so the generated C stays
// valid; a redundant check is harmless (it only traps on an actual null).
// ---------------------------------------------------------------------------

struct NonnullSite {
  size_t baseBegin = 0; ///< first byte of the pointer expression's spelling
  size_t baseEnd = 0;   ///< one past its last byte
  std::string expr;     ///< spelling used in the runtime diagnostic
};

static bool isDeclTypeTok(const std::string &s) {
  static const std::set<std::string> kTypes = {
      "void", "char", "short", "int", "long", "float", "double", "signed",
      "unsigned", "_Bool", "struct", "union", "enum", "const", "volatile",
      "static", "extern", "register", "restrict", "auto", "typedef",
      "inline", "_Atomic", "size_t", "ptrdiff_t", "uintptr_t", "intptr_t",
      "uint64_t", "int64_t", "uint32_t", "int32_t", "uint16_t", "int16_t",
      "uint8_t", "int8_t"};
  return kTypes.count(s) != 0;
}

/// True when this `*` is a declarator star in a type such as `char **argv`,
/// `int **q` or `struct S **p`.  Walk back over the whole run of stars: if
/// the token before the run is a type keyword (or a struct/union/enum tag),
/// the stars declare a name rather than dereference a value.  A run preceded
/// by an expression token (`=`, `{`, `return`, ...) is a nested deref and is
/// intentionally left to the normal expression path.
static bool isDeclaratorStarRun(const std::vector<Token> &toks,
                                const std::vector<size_t> &sig, size_t si) {
  size_t r = si;
  while (r > 0 && toks[sig[r - 1]].kind == TokKind::Punct &&
         toks[sig[r - 1]].text == "*")
    --r;
  if (r == 0)
    return false;
  const Token &before = toks[sig[r - 1]];
  if (isDeclTypeTok(before.text))
    return true;
  if (r >= 2 && before.kind == TokKind::Identifier) {
    const Token &pre = toks[sig[r - 2]];
    if (pre.kind == TokKind::Identifier &&
        (pre.text == "struct" || pre.text == "union" || pre.text == "enum"))
      return true;
  }
  return false;
}

/// True when an obvious local guard proves `name` non-null before baseSi.
/// We keep this deliberately narrow (short-circuit RHS and `if(!p) return;`)
/// and fall back to emitting a check whenever there is any doubt.
static bool hasObviousNonnullGuard(const std::vector<Token> &toks,
                                   const std::vector<size_t> &sig,
                                   size_t baseSi, const std::string &name) {
  size_t stmtStart = 0, blockStart = 0;
  for (size_t k = baseSi; k > 0; --k) {
    const Token &t = toks[sig[k]];
    if (t.kind != TokKind::Punct)
      continue;
    if (stmtStart == 0 && (t.text == ";" || t.text == "}"))
      stmtStart = k + 1;
    if (blockStart == 0 && t.text == "{")
      blockStart = k + 1;
    if (stmtStart != 0 && blockStart != 0)
      break;
  }

  // `p && ...<deref>` / `!p || ...<deref>` in the same expression.
  for (size_t k = stmtStart; k < baseSi; ++k) {
    const Token &t = toks[sig[k]];
    if (t.kind != TokKind::Punct || k == 0)
      continue;
    const Token &prev = toks[sig[k - 1]];
    if (t.text == "&&" && prev.kind == TokKind::Identifier &&
        prev.text == name)
      return true;
    if (t.text == "||" && prev.kind == TokKind::Identifier &&
        prev.text == name && k >= 2 &&
        toks[sig[k - 2]].kind == TokKind::Punct &&
        toks[sig[k - 2]].text == "!")
      return true;
  }

  // `if (!p) return;` / `if (p == 0) return;` earlier in the same block,
  // provided p is not reassigned between the guard and the dereference.
  for (size_t k = blockStart; k + 3 < baseSi; ++k) {
    if (!(toks[sig[k]].kind == TokKind::Identifier &&
          toks[sig[k]].text == "if"))
      continue;
    if (!(toks[sig[k + 1]].kind == TokKind::Punct &&
          toks[sig[k + 1]].text == "("))
      continue;
    size_t c = k + 2;
    bool negated = false;
    if (c + 1 < baseSi && toks[sig[c]].kind == TokKind::Punct &&
        toks[sig[c]].text == "!") {
      negated = true;
      ++c;
    }
    if (c >= baseSi || !(toks[sig[c]].kind == TokKind::Identifier &&
                         toks[sig[c]].text == name))
      continue;
    size_t close = c + 1;
    if (!negated && close + 1 < baseSi &&
        toks[sig[close]].kind == TokKind::Punct &&
        toks[sig[close]].text == "==") {
      const Token &z = toks[sig[close + 1]];
      if (!((z.kind == TokKind::Number && z.text == "0") ||
            (z.kind == TokKind::Identifier && z.text == "NULL")))
        continue;
      close += 2;
    }
    if (!(close + 1 < baseSi && toks[sig[close]].kind == TokKind::Punct &&
          toks[sig[close]].text == ")" &&
          toks[sig[close + 1]].kind == TokKind::Identifier &&
          toks[sig[close + 1]].text == "return"))
      continue;
    bool reassigned = false;
    for (size_t q = close + 2; q + 1 < baseSi; ++q)
      if (toks[sig[q]].kind == TokKind::Identifier &&
          toks[sig[q]].text == name &&
          toks[sig[q + 1]].kind == TokKind::Punct &&
          toks[sig[q + 1]].text == "=") {
        reassigned = true;
        break;
      }
    if (!reassigned)
      return true;
  }
  return false;
}

/// Type names declared in this translation unit (typedef aliases plus
/// struct/union/enum tags).  Derived from the same token stream the rewriter
/// uses for its variable/type tables; no new persistent global registry.
static std::set<std::string> collectDeclTypeNames(
    const std::vector<Token> &toks, const std::vector<size_t> &sig) {
  std::set<std::string> names;
  for (size_t si = 0; si < sig.size(); ++si) {
    const Token &t = toks[sig[si]];
    if (t.kind != TokKind::Identifier)
      continue;
    if (t.text == "struct" || t.text == "union" || t.text == "enum") {
      if (si + 1 < sig.size() &&
          toks[sig[si + 1]].kind == TokKind::Identifier)
        names.insert(toks[sig[si + 1]].text);
    }
    if (t.text != "typedef")
      continue;
    // Last identifier at declaration nesting depth zero before the `;`.
    // Brace tracking skips identifiers inside a `typedef struct T {...} T;`
    // body so the trailing alias name is the one recorded.
    int par = 0, brk = 0, brc = 0;
    size_t last = kNPos;
    for (size_t k = si + 1; k < sig.size(); ++k) {
      const Token &u = toks[sig[k]];
      if (u.kind == TokKind::Punct) {
        if (u.text == "(") ++par;
        else if (u.text == ")") { if (par > 0) --par; }
        else if (u.text == "[") ++brk;
        else if (u.text == "]") { if (brk > 0) --brk; }
        else if (u.text == "{") ++brc;
        else if (u.text == "}") { if (brc > 0) --brc; }
        else if (u.text == ";" && par == 0 && brk == 0 && brc == 0) break;
      } else if (u.kind == TokKind::Identifier && par == 0 && brk == 0 &&
                 brc == 0) {
        last = k;
      }
    }
    if (last != kNPos)
      names.insert(toks[sig[last]].text);
  }
  return names;
}

/// True when the `[` at `si` follows the declared name of an array, e.g.
/// `int buf[N];`, `char text[] = "...";`, `void *slots[N];` or a struct
/// field declaration.  The name is NOT a pointer: array-to-pointer decay at
/// a subscript site cannot produce NULL, and binding it with __typeof__
/// would emit an invalid array initializer in the generated C.
static bool isArrayDeclaratorBracket(const std::vector<Token> &toks,
                                     const std::vector<size_t> &sig, size_t si,
                                     const std::set<std::string> &typeNames) {
  if (si < 2 || toks[sig[si - 1]].kind != TokKind::Identifier)
    return false;
  const Token &before = toks[sig[si - 2]];
  const std::string &bt = before.text;
  if (isDeclTypeTok(bt) || bt == "*" || bt == "typedef" ||
      typeNames.count(bt) != 0)
    return true;
  if (si >= 3 && before.kind == TokKind::Identifier) {
    const Token &pre = toks[sig[si - 3]];
    if (pre.kind == TokKind::Identifier &&
        (pre.text == "struct" || pre.text == "union" || pre.text == "enum"))
      return true;
  }
  if (bt == ",") {
    size_t stmtStart = 0;
    for (size_t k = si - 2; k > 0; --k) {
      const Token &u = toks[sig[k]];
      if (u.kind == TokKind::Punct &&
          (u.text == ";" || u.text == "{" || u.text == "}")) {
        stmtStart = k + 1;
        break;
      }
    }
    if (stmtStart < si && isDeclTypeTok(toks[sig[stmtStart]].text))
      return true;
  }
  return false;
}

static std::vector<NonnullSite>
collectNonnullDerefSites(const std::string &source,
                         const std::map<std::string, size_t> &arrayLengths) {
  std::vector<NonnullSite> sites;
  const std::vector<Token> toks = lexCPlus(source);
  std::vector<size_t> sig;
  for (size_t i = 0; i < toks.size(); ++i)
    if (!isTriviaTok(toks[i]) && toks[i].kind != TokKind::EndOfFile)
      sig.push_back(i);

  // Collect declared array names syntactically.  `arrayLengths` only covers
  // arrays whose extent the rewriter could evaluate; macro extents
  // (`buf[BUFSIZE]`), inferred extents (`text[] = "..."`) and typedef element
  // types (`Node nodes[NODES];`) are absent, yet wrapping them with
  // __typeof__ is still invalid C.
  const std::set<std::string> typeNames = collectDeclTypeNames(toks, sig);
  std::set<std::string> declaredArrays;
  for (size_t si = 1; si < sig.size(); ++si) {
    if (!(toks[sig[si]].kind == TokKind::Punct &&
          toks[sig[si]].text == "["))
      continue;
    if (toks[sig[si - 1]].kind != TokKind::Identifier)
      continue;
    if (isArrayDeclaratorBracket(toks, sig, si, typeNames))
      declaredArrays.insert(toks[sig[si - 1]].text);
    // A single identifier followed by `[...]` that is NOT a subscript is a
    // declared array even when its element type is an unknown typedef (e.g.
    // `pthread_t`).  `isArrayDeclaratorBracket` only recognises element types
    // it knows (builtins / C+ classes / resources / struct/union/enum tags),
    // so a bare typedef'd array declarator is otherwise missed and later
    // treated as a nonnull deref site.
    else {
      // Walk back to the start of this declaration's declarator list to be
      // confident this `[` follows a declared name rather than an expression
      // subscript.  A declaration-bound `[` is preceded at the top level by
      // `;`, `{`, `}`, `(`, `,` or a type keyword.
      bool declBound = false;
      for (int k = static_cast<int>(si) - 2; k >= 0; --k) {
        const Token &u = toks[sig[k]];
        if (u.kind == TokKind::Punct &&
            (u.text == ";" || u.text == "{" || u.text == "}" ||
             u.text == "(" || u.text == ","))
        {
          declBound = true;
          break;
        }
        if (u.kind == TokKind::Punct && (u.text == ")" || u.text == "]" ||
                                         u.text == "}"))
          break;
        if (u.kind == TokKind::Punct && u.text == "[")
          break; // expression subscript `x[0]` — not a declarator
      }
      if (declBound)
        declaredArrays.insert(toks[sig[si - 1]].text);
    }
  }

  std::set<size_t> seen;
  auto tryAdd = [&](size_t baseSi) {
    const Token &b = toks[sig[baseSi]];
    if (b.kind != TokKind::Identifier)
      return;
    // An array is never a NULL pointer at a dereference site; binding one
    // with __typeof__ would emit an invalid array initializer in the C.
    if (arrayLengths.count(b.text) != 0 || declaredArrays.count(b.text) != 0)
      return;
    if (seen.count(b.offset) != 0)
      return;
    if (hasObviousNonnullGuard(toks, sig, baseSi, b.text))
      return;
    seen.insert(b.offset);
    sites.push_back(NonnullSite{b.offset, endOfToken(b), b.text});
  };

  for (size_t si = 0; si < sig.size(); ++si) {
    const Token &t = toks[sig[si]];
    if (t.kind != TokKind::Punct)
      continue;

    if (t.text == "->") {
      if (si == 0)
        continue;
      const Token &base = toks[sig[si - 1]];
      if (base.kind != TokKind::Identifier)
        continue;
      if (si >= 2) {
        const std::string &bt = toks[sig[si - 2]].text;
        if (bt == "." || bt == "->" || isDeclTypeTok(bt))
          continue; // struct field / declarator, not a standalone pointer
      }
      tryAdd(si - 1);
      continue;
    }

    if (t.text == "*") {
      if (si == 0 || si + 1 >= sig.size())
        continue;
      const Token &prev = toks[sig[si - 1]];
      // `int *p;` is a declaration; `a * b` is multiplication.  `canEndFactor`
      // also treats unary operators (return, sizeof, ...) as non-factors.
      if (canEndFactor(prev) || isDeclTypeTok(prev.text) ||
          prev.text == "sizeof" || prev.text == "&")
        continue;
      if (isDeclaratorStarRun(toks, sig, si))
        continue; // `char **argv`, `int **q`, `struct T **p`
      const Token &base = toks[sig[si + 1]];
      if (base.kind != TokKind::Identifier)
        continue; // nested/parenthesised derefs: leave as-is
      if (si + 2 < sig.size()) {
        const Token &after = toks[sig[si + 2]];
        if (after.kind == TokKind::Punct &&
            (after.text == "." || after.text == "->" || after.text == "[" ||
             after.text == "(" || after.text == ")" || after.text == "++" ||
             after.text == "--"))
          continue;
      }
      tryAdd(si + 1);
      continue;
    }

    if (t.text == "[") {
      if (si == 0 || si + 1 >= sig.size())
        continue;
      if (toks[sig[si + 1]].kind == TokKind::Punct &&
          toks[sig[si + 1]].text == "]")
        continue; // empty subscript: declaration / incomplete type
      const Token &base = toks[sig[si - 1]];
      if (base.kind != TokKind::Identifier)
        continue;
      if (si >= 2) {
        const Token &before = toks[sig[si - 2]];
        const std::string &bt = before.text;
        if (isDeclTypeTok(bt) || bt == "typedef" || bt == "*" || bt == "." ||
            bt == "->")
          continue; // declarator or struct field, not a pointer variable
        if (si >= 3 && before.kind == TokKind::Identifier) {
          const Token &pre = toks[sig[si - 3]];
          if (pre.kind == TokKind::Identifier &&
              (pre.text == "struct" || pre.text == "union" ||
               pre.text == "enum"))
            continue; // `struct S arr[N];` declares an array
        }
        if (bt == ",") {
          size_t stmtStart = 0;
          for (size_t k = si - 2; k > 0; --k) {
            const Token &u = toks[sig[k]];
            if (u.kind == TokKind::Punct &&
                (u.text == ";" || u.text == "{" || u.text == "}")) {
              stmtStart = k + 1;
              break;
            }
          }
          if (stmtStart < si && isDeclTypeTok(toks[sig[stmtStart]].text))
            continue; // declarator list: `int a[N], b[M];`
        }
      }
      tryAdd(si - 1);
      continue;
    }
  }
  return sites;
}

// ---------------------------------------------------------------------------
// Safe-mode lowering of C99 variable-length arrays.
//
// Rationale: Safe mode promises that an allocation whose element count is only
// known at run time is bounded and deterministically released.  Rejecting a
// plain `T a[n];` out of hand forbids ordinary C99 code for no safety benefit:
// the runtime already exposes cplus_alloc_array (overflow-checked count *
// element-size) and cplus_release.  We therefore lower the VLA declaration in
// the *final* lowered C to a managed allocation with the same element type and
// count, preserving the identifier so bounds instrumentation and all later
// uses keep working.  An explicit `cplus_release(name);` is emitted at every
// scope exit that ends the array's lifetime, plus before each `return`.
//
// This helper is called only from instrumentDynamicArrayBounds(), which the
// driver invokes exclusively in Safe mode.  LEGACY mode never reaches it, so
// its byte-for-byte source preservation is untouched.  No C99 inline, no VLA
// and no new subsystem are introduced -- only declarations of the existing
// runtime helpers in the generated prelude.
// ---------------------------------------------------------------------------

static bool isVlaTypeWord(const Token &t) {
  if (t.kind != TokKind::Identifier)
    return false;
  static const std::set<std::string> kWords = {
      "int",      "char",     "short",    "long",     "float",
      "double",   "void",     "unsigned", "signed",   "struct",
      "union",    "enum",     "size_t",   "ptrdiff_t", "_Bool",
      "int8_t",   "uint8_t",  "int16_t",  "uint16_t", "int32_t",
      "uint32_t", "int64_t",  "uint64_t", "intptr_t", "uintptr_t"};
  return kWords.count(t.text) != 0;
}

static bool isVlaQualifier(const Token &t) {
  return t.kind == TokKind::Identifier &&
         (t.text == "const" || t.text == "volatile" || t.text == "_Atomic");
}

/// Constant-expression exclusions mirror the old static check: numeric
/// extents, function-like constants (offsetof/sizeof/...) and ALL-CAPS macro
/// names are not VLAs and must stay as ordinary arrays.
static bool isDynamicVlaExtent(const TokenStream &ts, size_t inner, size_t rb) {
  if (inner >= rb)
    return false;
  if (ts[inner].kind == TokKind::Number)
    return false;
  if (ts[inner].kind == TokKind::Identifier &&
      ts.isPunct(ts.nextSig(inner), "("))
    return false;
  if (ts[inner].kind == TokKind::Identifier) {
    bool macroLike = true;
    for (char c : ts[inner].text)
      if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
        macroLike = false;
    if (macroLike)
      return false;
  }
  return true;
}

/// The token immediately before a declarator name must belong to a
/// declaration specifier (type word, qualifier, '*' or comma).  This rejects
/// ordinary subscripts such as `f(a[n])`, `return a[n]` and `x = a[n]` while
/// accepting `int a[n]`, `int *p[n]` and `int a[n], b[m]`.
static bool hasDeclarationPrefix(const TokenStream &ts, size_t name) {
  size_t p = ts.prevSig(name);
  if (p == kNPos)
    return false;
  if (isVlaTypeWord(ts[p]) || isVlaQualifier(ts[p]) || ts.isPunct(p, "*"))
    return true;
  if (ts.isPunct(p, ",")) {
    // A comma separates declarators only at file/block scope.  Inside `(` it
    // is a call-argument separator (`f(x, a[i])`), i.e. a real subscript.
    int depth = 0;
    for (size_t k = 0; k < p; ++k) {
      if (ts.isPunct(k, "(")) ++depth;
      else if (ts.isPunct(k, ")")) --depth;
    }
    return depth == 0;
  }
  if (ts[p].kind == TokKind::Identifier) {
    size_t pp = ts.prevSig(p);
    if (pp != kNPos && (ts.isIdent(pp, "struct") || ts.isIdent(pp, "union") ||
                        ts.isIdent(pp, "enum")))
      return true; // struct/union/enum tag before the declarator
  }
  return false;
}

/// Lower every single-dimensional VLA declaration in `source`, appending text
/// edits to `out`.  Returns true when at least one VLA was lowered.  `source`
/// is already the fully lowered C (post-RAII), so the edits operate on final
/// tokens and cannot disturb earlier passes.
static bool lowerVariableLengthArrays(
    const std::string &source, std::vector<SafetyInsertion> &out,
    std::map<std::string, std::string> &vlaCounts) {
  std::vector<Token> toks = lexCPlus(source);
  TokenStream ts(toks);
  struct LiveVla {
    size_t scope;
    std::string release;
  };
  std::vector<LiveVla> live;
  std::set<std::string> loweredNames;
  std::vector<SafetyInsertion> edits;
  std::vector<SafetyInsertion> sizeofEdits;
  size_t brace = 0, paren = 0;
  bool haveVla = false;

  for (size_t i = 0; i < ts.size(); ++i) {
    const Token &t = ts[i];
    if (t.kind == TokKind::Whitespace || t.kind == TokKind::Comment)
      continue;
    // Preprocessor directives may hold unbalanced braces in conditional
    // compilation; the token stream cannot resolve them, so skip the line.
    if (ts.isPunct(i, "#")) {
      size_t e = i;
      while (e < ts.size() && toks[e].line == t.line)
        ++e;
      i = e > i ? e - 1 : i;
      continue;
    }
    if (ts.isPunct(i, "{")) { ++brace; continue; }
    if (ts.isPunct(i, "}")) {
      // Reverse declaration order, innermost scopes first.
      for (size_t k = live.size(); k > 0; --k)
        if (live[k - 1].scope == brace)
          edits.push_back({t.offset, 0, 0, live[k - 1].release, 0});
      std::vector<LiveVla> kept;
      for (const auto &v : live)
        if (v.scope != brace)
          kept.push_back(v);
      live.swap(kept);
      if (brace)
        --brace;
      continue;
    }
    if (ts.isPunct(i, "(")) { ++paren; continue; }
    if (ts.isPunct(i, ")")) { if (paren) --paren; continue; }
    if (ts.isIdent(i, "return") && !live.empty()) {
      // A GNU statement expression keeps the release inside the single
      // `return` statement (so an unbraced `if (c) return x;` stays intact),
      // and captures the value BEFORE releasing: releasing first would make
      // `return a[i];` read freed memory.  __typeof__ mirrors the existing
      // RAII lowering's __cplus_ret pattern.  The original expression tokens
      // stay in place, so bounds-check insertions inside it are preserved.
      size_t semi = ts.statementEnd(i);
      if (semi != kNPos && semi < ts.size()) {
        size_t exprStart = ts.nextSig(i);
        if (exprStart < semi) {
          std::string expr =
              sourceSlice(source, toks[exprStart].offset, toks[semi].offset);
          size_t b = expr.find_first_not_of(" \t\n\r");
          size_t e = expr.find_last_not_of(" \t\n\r");
          expr = b == std::string::npos ? std::string()
                                        : expr.substr(b, e - b + 1);
          edits.push_back({toks[exprStart].offset, 0, 0,
                           "({ __typeof__(" + expr +
                               ") __cplus_vla_ret = (",
                           0});
        } else {
          edits.push_back({toks[semi].offset, 0, 0, "({ ", 0});
        }
        std::string tail = exprStart < semi ? "); " : "";
        for (size_t k = live.size(); k > 0; --k)
          tail += live[k - 1].release;
        tail += exprStart < semi ? " __cplus_vla_ret; });"
                                 : " });";
        edits.push_back({toks[semi].offset, 0, 0, tail, 0});
      }
      continue;
    }
    if (t.kind != TokKind::Identifier || brace == 0 || paren != 0)
      continue;
    if (!hasDeclarationPrefix(ts, i))
      continue;
    size_t lb = ts.nextSig(i);
    if (!ts.isPunct(lb, "["))
      continue;
    size_t rb = ts.matchBracket(lb);
    if (rb == kNPos || rb == ts.nextSig(lb))
      continue;
    // Multi-dimensional VLAs need pointer-to-row types and a VLA element size
    // in sizeof; leave them to the STRICT policy rather than lower them wrong.
    if (ts.isPunct(ts.nextSig(rb), "["))
      continue;
    size_t inner = ts.nextSig(lb);
    if (!isDynamicVlaExtent(ts, inner, rb))
      continue;

    // Recover the declaration specifiers from the start of the statement.
    size_t start = i;
    while (start > 0) {
      size_t p = ts.prevSig(start);
      if (p == kNPos)
        break;
      if (ts.isPunct(p, ";") || ts.isPunct(p, "{") || ts.isPunct(p, "}")) {
        start = ts.nextSig(p);
        break;
      }
      start = p;
    }
    size_t lastComma = kNPos;
    for (size_t k = start; k < i; ++k)
      if (ts.isPunct(k, ","))
        lastComma = k;
    std::string base;
    int stars = 0;
    for (size_t k = start; k < i;) {
      if (ts.isPunct(k, ",") || ts.isPunct(k, "[") || ts.isPunct(k, "]")) {
        ++k;
        continue;
      }
      if (k > lastComma && ts.isPunct(k, "*")) {
        ++stars;
        ++k;
        continue;
      }
      if (isVlaQualifier(ts[k])) { ++k; continue; } // strip from the cast
      if (isVlaTypeWord(ts[k])) {
        base += ts[k].text + " ";
        if (ts.isIdent(k, "struct") || ts.isIdent(k, "union") ||
            ts.isIdent(k, "enum")) {
          size_t tag = ts.nextSig(k);
          if (tag < i && toks[tag].kind == TokKind::Identifier) {
            base += toks[tag].text + " ";
            k = tag;
          }
        }
        ++k;
        continue;
      }
      ++k;
    }
    if (base.empty())
      continue; // not a real declaration after all

    std::string count = sourceSlice(source, endOfToken(toks[lb]),
                                    toks[rb].offset);
    size_t b = count.find_first_not_of(" \t\n\r");
    size_t e = count.find_last_not_of(" \t\n\r");
    count = b == std::string::npos ? std::string()
                                   : count.substr(b, e - b + 1);
    if (count.empty())
      continue;

    const std::string name = t.text;
    // Bind the extent exactly once.  The temporary is declared immediately
    // before the VLA declaration (still a declaration, so C89-valid) and the
    // allocation reads the temporary rather than re-evaluating the original
    // expression.  Reusing `n` directly would be wrong when it changes value
    // later, and would double-evaluate side effects such as `n++`.
    const std::string countVar =
        "__cplus_vla_count_" + std::to_string(vlaCounts.size()) + "_" + name;
    edits.push_back({toks[start].offset, 0, 0,
                     "size_t " + countVar + " = (size_t)(" + count + "); ", 0});
    const std::string elem = base + std::string(stars, '*');
    const std::string cast = base + std::string(stars + 1, '*');
    // `T a[n];` -> `size_t tmp = (size_t)(n); T *a = (T *)cplus_alloc_array(tmp, sizeof(T));`
    edits.push_back({t.offset, 0, 0, " *", 0});
    edits.push_back({toks[lb].offset, 0, 0,
                     " = (" + cast + ")cplus_alloc_array(" + countVar +
                         ", sizeof(" + elem + "))",
                     endOfToken(toks[rb]) - toks[lb].offset});
    // Export the runtime extent so instrumentDynamicArrayBounds() can guard
    // every `a[index]` with the very same count the allocation used.
    vlaCounts[name] = countVar;
    live.push_back({brace,
                    "cplus_release((void *)" + name + "); "});
    loweredNames.insert(name);
    haveVla = true;
  }

  // `sizeof(a)` on a lowered VLA must report the allocation's byte count, not
  // sizeof(pointer).  cplus_alloc_bytes() is the existing introspection helper
  // for exactly this; it aborts if the pointer is not a live C+ allocation.
  for (size_t i = 0; i + 3 < ts.size(); ++i) {
    if (!ts.isIdent(i, "sizeof") || !ts.isPunct(ts.nextSig(i), "("))
      continue;
    size_t nm = ts.nextSig(ts.nextSig(i));
    if (nm >= ts.size() || toks[nm].kind != TokKind::Identifier ||
        !loweredNames.count(toks[nm].text))
      continue;
    size_t rp = ts.nextSig(nm);
    if (!ts.isPunct(rp, ")"))
      continue;
    sizeofEdits.push_back(
        {ts[i].offset, 0, 0, "cplus_alloc_bytes(" + toks[nm].text + ")",
         endOfToken(toks[rp]) - ts[i].offset});
  }

  edits.insert(edits.end(), sizeofEdits.begin(), sizeofEdits.end());
  out.insert(out.end(), edits.begin(), edits.end());
  return haveVla;
}

// ---------------------------------------------------------------------------
// Alias-extent recovery for pointer subscripts.
//
// Fixed-array subscripts are handled by name above, but an access through a
// pointer that provably points into a tracked object (`int a[4]; int *p=a;
// p[i]`) previously received no runtime guard at all.  The AST pass records
// such facts in CheckedBoundsSite, but no producer wires that registry today;
// this lexical pass recovers the same facts from the lowered C so no
// recorded site is required.  Only objects whose extent is *proved* enter the
// fact table, so unrelated pointers are never blanket-wrapped.
// ---------------------------------------------------------------------------

struct AliasExtent {
  std::string count; ///< C expression for the element count
  std::string name;  ///< diagnostic name
};

static std::string trimAlias(const std::string &s) {
  size_t b = s.find_first_not_of(" \t\n\r");
  if (b == std::string::npos) return std::string();
  size_t e = s.find_last_not_of(" \t\n\r");
  return s.substr(b, e - b + 1);
}

static std::string sliceAlias(const std::string &src,
                              const std::vector<Token> &toks, size_t from,
                              size_t to) {
  if (from >= to || from >= toks.size() || to > toks.size())
    return std::string();
  size_t b = toks[from].offset;
  size_t e = toks[to - 1].offset + toks[to - 1].text.size();
  return trimAlias(src.substr(b, e - b));
}

/// Skip a leading run of C casts `(type)` so an aliasing initialiser such as
/// `p = (int *)v` resolves to `v`.
static size_t skipAliasCasts(const TokenStream &ts, size_t from, size_t to) {
  size_t i = from;
  while (i < to && ts.isPunct(i, "(")) {
    size_t p = i + 1, depth = 1;
    while (p < to && depth) {
      if (ts.isPunct(p, "(")) ++depth;
      else if (ts.isPunct(p, ")")) --depth;
      ++p;
    }
    if (depth) break;
    bool typey = false;
    for (size_t k = i + 1; k + 1 < p; ++k)
      if (ts[k].kind == TokKind::Identifier || ts.isPunct(k, "*")) {
        typey = true;
        break;
      }
    if (!typey) break;
    i = p;
    while (i < to && ts.isTrivia(i)) ++i;
  }
  return i;
}

/// Index of the first top-level ',' or ';' in [from, limit), else `limit`.
static size_t topLevelEnd(const TokenStream &ts, size_t from, size_t limit) {
  int par = 0, brk = 0, brc = 0;
  for (size_t i = from; i < limit; ++i) {
    if (!ts.isPunct(i, "(") && !ts.isPunct(i, ")") && !ts.isPunct(i, "[") &&
        !ts.isPunct(i, "]") && !ts.isPunct(i, "{") && !ts.isPunct(i, "}") &&
        !ts.isPunct(i, ",") && !ts.isPunct(i, ";"))
      continue;
    if (ts.isPunct(i, "(")) ++par;
    else if (ts.isPunct(i, ")")) { if (par > 0) --par; }
    else if (ts.isPunct(i, "[")) ++brk;
    else if (ts.isPunct(i, "]")) { if (brk > 0) --brk; }
    else if (ts.isPunct(i, "{")) ++brc;
    else if (ts.isPunct(i, "}")) { if (brc > 0) --brc; }
    else if (par == 0 && brk == 0 && brc == 0) return i;
  }
  return limit;
}

/// Token range of argument `index` of the call whose '(' is `lp`.
static std::pair<size_t, size_t> callArgRange(const TokenStream &ts, size_t lp,
                                              size_t rp, unsigned index) {
  unsigned arg = 0;
  size_t start = ts.nextSig(lp);
  int par = 0, brk = 0, brc = 0;
  for (size_t i = start; i < rp; ++i) {
    if (ts.isPunct(i, "(")) ++par;
    else if (ts.isPunct(i, ")")) --par;
    else if (ts.isPunct(i, "[")) ++brk;
    else if (ts.isPunct(i, "]")) --brk;
    else if (ts.isPunct(i, "{")) ++brc;
    else if (ts.isPunct(i, "}")) --brc;
    else if (ts.isPunct(i, ",") && par == 0 && brk == 0 && brc == 0) {
      if (arg++ == index) return {start, i};
      start = ts.nextSig(i);
    }
  }
  if (arg == index && start < rp) return {start, rp};
  return {kNPos, kNPos};
}

/// Derive an element count from an allocation argument expression.
static std::string allocCount(const std::string &countText,
                              const std::string &name, bool hasSizeof) {
  std::string c = trimAlias(countText);
  if (c.empty())
    return std::string();
  if (!hasSizeof)
    return "((size_t)(" + c + ") / sizeof(*" + name + "))";
  // `n * sizeof(T)` / `n*sizeof(T)`: take the factor before the last '*'
  // that precedes sizeof.
  size_t sp = c.find("sizeof");
  size_t star = c.rfind('*', sp == std::string::npos ? c.size() : sp);
  if (star != std::string::npos && star > 0)
    return trimAlias(c.substr(0, star));
  return c;
}

static bool resolveAliasExtent(
    const std::string &src, const std::vector<Token> &toks,
    const TokenStream &ts, size_t from, size_t end,
    const std::map<std::string, AliasExtent> &facts,
    const std::map<std::string, AliasExtent> &members,
    const std::map<std::string, AliasExtent> &rows,
    const std::map<std::string, std::string> &ptrAlias, AliasExtent &out) {
  if (from >= end || from >= toks.size())
    return false;
  from = skipAliasCasts(ts, from, end);
  if (from >= end)
    return false;

  // Trailing interior offset: `a + 4` / `a - 2`.
  std::string adjust;
  size_t last = ts.prevSig(end - 1);
  if (last != kNPos && last > from && toks[last].kind == TokKind::Number) {
    size_t op = ts.prevSig(last);
    if (op != kNPos && op > from &&
        (ts.isPunct(op, "+") || ts.isPunct(op, "-"))) {
      adjust = std::string(ts.isPunct(op, "-") ? " - " : " + ") +
               toks[last].text;
      end = op;
    }
  }

  bool found = false;
  if (ts.isPunct(from, "&")) {
    // `int **q = &p` : dereferencing q yields p's extent.
    size_t nm = ts.nextSig(from);
    if (nm < end && toks[nm].kind == TokKind::Identifier &&
        ts.nextSig(nm) >= end) {
      auto a = ptrAlias.find(toks[nm].text);
      if (a != ptrAlias.end()) {
        auto f = facts.find(a->second);
        if (f != facts.end()) { out = f->second; found = true; }
      }
    }
  } else if (toks[from].kind == TokKind::Identifier) {
    const std::string name = toks[from].text;
    if (ts.prevSig(end) == from) { // exactly one significant token
      auto f = facts.find(name);
      if (f != facts.end()) { out = f->second; found = true; }
    }
    if (!found) {
      size_t op = ts.nextSig(from);
      if (op < end && ts.isPunct(op, "[") && ts.nextSig(op) < end) {
        // `m[i]` selects a row of a multidimensional array.
        auto r = rows.find(name);
        if (r != rows.end()) { out = r->second; found = true; }
      }
    }
    if (!found) {
      // Member access `s.a` / `s->a` / `o.in.x`.
      size_t id = ts.prevSig(end - 1);
      if (id != kNPos && toks[id].kind == TokKind::Identifier) {
        size_t dot = ts.prevSig(id);
        if (dot != kNPos && (ts.isPunct(dot, ".") || ts.isPunct(dot, "->"))) {
          auto m = members.find(toks[id].text);
          if (m != members.end()) { out = m->second; found = true; }
        }
      }
    }
  }
  if (!found)
    return false;
  if (!adjust.empty()) {
    out.count = "((" + out.count + ")" + adjust + ")";
    out.name += adjust;
  }
  return true;
}

/// Collect runtime bounds sites for pointer aliases of tracked objects.
static void collectAliasBoundsSites(
    const std::string &src, const std::map<std::string, size_t> &arrayLengths,
    const std::map<std::string, std::string> &vlaCounts,
    std::vector<CheckedBoundsSite> &out) {
  const std::vector<Token> toks = lexCPlus(src);
  TokenStream ts(toks);
  std::vector<size_t> sig;
  for (size_t i = 0; i < toks.size(); ++i)
    if (!isTriviaTok(toks[i]) && toks[i].kind != TokKind::EndOfFile)
      sig.push_back(i);

  std::map<std::string, AliasExtent> facts, members, rows;
  std::map<std::string, std::string> ptrAlias;
  std::map<std::string, std::string> typedefCounts;
  std::set<std::string> countParams = {"n", "N", "count", "cnt", "len",
                                       "length", "size", "num"};

  // Fixed-size and member array declarations.
  for (size_t si = 1; si < sig.size(); ++si) {
    if (!ts.isPunct(sig[si], "["))
      continue;
    size_t prev = ts.prevSig(sig[si]);
    if (prev == kNPos || toks[prev].kind != TokKind::Identifier)
      continue;
    if (!hasDeclarationPrefix(ts, prev))
      continue;
    size_t rb = ts.matchBracket(sig[si]);
    if (rb == kNPos || rb == ts.nextSig(sig[si]))
      continue;
    const std::string count = sliceAlias(src, toks, ts.nextSig(sig[si]), rb);
    if (count.empty())
      continue;
    const std::string name = toks[prev].text;
    AliasExtent e{count, name};
    facts[name] = e;
    members[name] = e;
    auto al = arrayLengths.find(name);
    if (al != arrayLengths.end() && count.find_first_not_of("0123456789") !=
                                        std::string::npos)
      facts[name].count = std::to_string(al->second);
    // `int m[2][3]` : m[i] is a row of 3 elements.
    size_t rb2 = ts.nextSig(rb);
    if (rb2 < toks.size() && ts.isPunct(rb2, "[")) {
      size_t close2 = ts.matchBracket(rb2);
      if (close2 != kNPos && close2 > rb2)
        rows[name] =
            AliasExtent{sliceAlias(src, toks, ts.nextSig(rb2), close2),
                        name + "[]"};
    }
  }

  // Typedef arrays: `typedef int I4[4]; I4 a;`
  for (size_t si = 0; si + 1 < sig.size(); ++si) {
    if (!ts.isIdent(sig[si], "typedef"))
      continue;
    for (size_t k = si + 1; k + 1 < sig.size(); ++k) {
      if (toks[sig[k]].kind != TokKind::Identifier)
        continue;
      size_t lb = ts.nextSig(sig[k]);
      if (lb >= toks.size() || !ts.isPunct(lb, "["))
        continue;
      size_t rb = ts.matchBracket(lb);
      if (rb == kNPos || rb == ts.nextSig(lb))
        continue;
      typedefCounts[toks[sig[k]].text] =
          sliceAlias(src, toks, ts.nextSig(lb), rb);
      break;
    }
  }

  // VLAs are lowered to cplus_alloc_array with a bound count temporary.
  for (const auto &v : vlaCounts)
    facts[v.first] = AliasExtent{v.second, v.first};

  // Assignments and allocation calls.
  for (size_t si = 0; si < sig.size(); ++si) {
    size_t eq = sig[si];
    if (!ts.isPunct(eq, "="))
      continue;
    size_t lhs = ts.prevSig(eq);
    if (lhs == kNPos || toks[lhs].kind != TokKind::Identifier)
      continue;
    // Pointer-to-array declarator `int (*row)[3] = m`.
    if (ts.isPunct(lhs, "]")) {
      size_t k = lhs, star = ts.prevSig(k);
      while (star != kNPos && !ts.isPunct(star, "*")) {
        k = star;
        star = ts.prevSig(k);
      }
      if (star != kNPos && toks[k].kind == TokKind::Identifier)
        lhs = k;
    }
    if (toks[lhs].kind != TokKind::Identifier)
      continue;
    const std::string name = toks[lhs].text;
    size_t semi = ts.statementEnd(eq);
    if (semi == kNPos || semi > toks.size())
      semi = toks.size();
    size_t rhs = ts.nextSig(eq);
    size_t rend = topLevelEnd(ts, rhs, semi);
    if (rhs >= rend)
      continue;

    AliasExtent ext;
    if (resolveAliasExtent(src, toks, ts, rhs, rend, facts, members, rows,
                           ptrAlias, ext)) {
      facts[name] = ext;
      continue;
    }
    // `&p` pointer alias.
    if (ts.isPunct(rhs, "&")) {
      size_t id = ts.nextSig(rhs);
      if (id < rend && toks[id].kind == TokKind::Identifier)
        ptrAlias[name] = toks[id].text;
      continue;
    }
    // Typedef array declarator `I4 a = ...`.
    size_t before = ts.prevSig(lhs);
    if (before != kNPos && toks[before].kind == TokKind::Identifier) {
      auto td = typedefCounts.find(toks[before].text);
      if (td != typedefCounts.end())
        facts[name] = AliasExtent{td->second, name};
    }
    // Allocation helpers.
    bool hasSizeof = false;
    unsigned countArg = 0;
    for (size_t i = rhs; i < rend; ++i) {
      if (!ts.isIdent(i, "malloc") && !ts.isIdent(i, "calloc") &&
          !ts.isIdent(i, "realloc") && !ts.isIdent(i, "cplus_alloc_array") &&
          !ts.isIdent(i, "cplus_new_array"))
        continue;
      size_t lp = ts.nextSig(i);
      if (lp >= rend || !ts.isPunct(lp, "("))
        continue;
      size_t rp = ts.matchParen(lp);
      if (rp == kNPos || rp >= rend)
        continue;
      if (ts.isIdent(i, "calloc")) { countArg = 0; hasSizeof = true; }
      else if (ts.isIdent(i, "realloc")) { countArg = 1; }
      else if (ts.isIdent(i, "cplus_alloc_array")) {
        countArg = 0;
        hasSizeof = true; // first argument is already an element count
      } else if (ts.isIdent(i, "cplus_new_array")) {
        countArg = 1;
        hasSizeof = true; // (T, N): N is the element count
      } else {
        countArg = 0;
        for (size_t q = lp; q < rp; ++q)
          if (ts.isIdent(q, "sizeof")) hasSizeof = true;
      }
      auto ar = callArgRange(ts, lp, rp, countArg);
      if (ar.first == kNPos)
        break;
      const std::string c = allocCount(sliceAlias(src, toks, ar.first, ar.second),
                                       name, hasSizeof);
      if (!c.empty())
        facts[name] = AliasExtent{c, name};
      break;
    }
  }

  // Interior pointer increments: `p++` / `++p` / `p += K`.
  for (size_t si = 0; si < sig.size(); ++si) {
    size_t i = sig[si];
    std::string name;
    long long k = 1;
    if (ts.isPunct(i, "++") || ts.isPunct(i, "--")) {
      size_t prev = ts.prevSig(i);
      if (prev == kNPos || toks[prev].kind != TokKind::Identifier)
        continue;
      name = toks[prev].text;
      if (ts.isPunct(i, "--")) k = -1;
    } else if (ts.isPunct(i, "+=") || ts.isPunct(i, "-=")) {
      size_t prev = ts.prevSig(i);
      if (prev == kNPos || toks[prev].kind != TokKind::Identifier)
        continue;
      name = toks[prev].text;
      size_t nx = ts.nextSig(i);
      if (nx < toks.size() && toks[nx].kind == TokKind::Number) {
        try { k = std::stoll(toks[nx].text); } catch (...) { continue; }
      }
      if (ts.isPunct(i, "-=")) k = -k;
    }
    if (name.empty())
      continue;
    auto f = facts.find(name);
    if (f != facts.end())
      f->second.count = "((" + f->second.count + ")" +
                        (k >= 0 ? " - " : " + ") +
                        std::to_string(k >= 0 ? k : -k) + ")";
  }

  // Call-site argument propagation: `sum(a, n)` gives parameter `p` the
  // extent of argument `a`.  Parameter names are recovered from definitions.
  std::map<std::string, std::vector<std::string>> params;
  for (size_t si = 0; si < sig.size(); ++si) {
    if (!ts.isPunct(sig[si], "("))
      continue;
    size_t rp = ts.matchParen(sig[si]);
    if (rp == kNPos || rp == ts.nextSig(sig[si]))
      continue;
    size_t after = ts.nextSig(rp);
    if (after >= toks.size() || !ts.isPunct(after, "{"))
      continue; // function definition only
    std::vector<std::string> names;
    for (size_t a = 0;; ++a) {
      auto ar = callArgRange(ts, sig[si], rp, a);
      if (ar.first == kNPos)
        break;
      size_t id = ar.first;
      for (size_t q = ar.first; q < ar.second; ++q)
        if (toks[q].kind == TokKind::Identifier) id = q;
      names.push_back(toks[id].text);
    }
    // Companion count heuristic: a signature with a `n`/`len`/`count`
    // parameter lends that extent to its other (pointer) parameter.  Names
    // like the index `i` are deliberately excluded to avoid false guards.
    std::string cparam;
    for (const std::string &pn : names)
      if (countParams.count(pn) != 0) cparam = pn;
    if (!cparam.empty() && names.size() >= 2) {
      for (const std::string &pn : names)
        // A name-keyed companion guess must NEVER overwrite an extent that is
        // already known from a declaration. In a TU with `int arr[N]` and
        // `void count(int *arr, int size)`, overwriting facts["arr"] with
        // "size" made the call site prime(arr) lend the bogus extent "size"
        // to prime's parameter p, emitting __cplus_bounds_check(i, size)
        // where `size` is not in scope (real_apps/prime_sieve DEFECT).
        if (pn != cparam && facts.find(pn) == facts.end()) {
          facts[pn] = AliasExtent{cparam, pn};
          break;
        }
    }
    // Record the parameter list under the preceding identifier (function
    // name) for call-site propagation below.
    size_t fn = ts.prevSig(sig[si]);
    if (fn != kNPos && toks[fn].kind == TokKind::Identifier)
      params[toks[fn].text] = names;
  }
  for (const auto &p : params) {
    // Find calls `name(...)` outside the definition header.
    for (size_t si = 0; si < sig.size(); ++si) {
      if (!ts.isIdent(sig[si], p.first.c_str()))
        continue;
      size_t lp = ts.nextSig(sig[si]);
      if (lp >= toks.size() || !ts.isPunct(lp, "("))
        continue;
      size_t rp = ts.matchParen(lp);
      if (rp == kNPos)
        continue;
      for (size_t a = 0; a < p.second.size(); ++a) {
        auto ar = callArgRange(ts, lp, rp, a);
        if (ar.first == kNPos)
          continue;
        AliasExtent ext;
        if (resolveAliasExtent(src, toks, ts, ar.first, ar.second, facts,
                               members, rows, ptrAlias, ext))
          facts[p.second[a]] = ext;
      }
    }
  }

  // Emit subscript sites.  `guarded` keeps one guard per index expression;
  // the fixed-array and VLA passes run earlier and are deduplicated by the
  // caller through guardedIndexOffsets.
  std::set<size_t> guarded;
  auto addSite = [&](size_t indexBegin, size_t close, const AliasExtent &ext) {
    if (indexBegin == kNPos || close == kNPos || close <= indexBegin)
      return;
    if (guarded.count(indexBegin))
      return;
    guarded.insert(indexBegin);
    out.push_back(CheckedBoundsSite{indexBegin, close, close, ext.count,
                                    ext.name});
  };
  for (size_t si = 0; si < sig.size(); ++si) {
    size_t i = sig[si];
    // p[i] / row[i] / s.a[i]
    if (ts.isPunct(i, "[")) {
      size_t close = ts.matchBracket(i);
      if (close == kNPos || close == ts.nextSig(i))
        continue;
      AliasExtent ext;
      size_t first = ts.nextSig(i);
      if (first >= close)
        continue;
      if (toks[first].kind == TokKind::Number && ts.nextSig(first) >= close)
        continue; // constant index: static checker handles it
      size_t prev = ts.prevSig(i);
      if (prev == kNPos)
        continue;
      // Declaration bracket: skip.
      if (hasDeclarationPrefix(ts, prev))
        continue;
      // `row[1][i]`: second subscript uses the row extent.
      if (ts.isPunct(prev, "]")) {
        size_t depth = 0, k = prev + 1;
        size_t open = kNPos;
        for (; k > 0; --k) {
          if (ts.isPunct(k - 1, "]")) ++depth;
          else if (ts.isPunct(k - 1, "[")) {
            if (--depth == 0) { open = k - 1; break; }
          }
        }
        if (open != kNPos) {
          size_t base = ts.prevSig(open);
          // A multi-dimensional DECLARATOR (`static int grid[H][W]`,
          // `int board[N][M]`) is not a subscript access. Guarding W here
          // turns a fixed file-scope array into a VLA and breaks compilation.
          if (base != kNPos && hasDeclarationPrefix(ts, base))
            continue;
          if (base != kNPos && toks[base].kind == TokKind::Identifier) {
            auto r = rows.find(toks[base].text);
            if (r != rows.end()) ext = r->second;
          }
        }
      } else if (ts.isPunct(prev, ")")) {
        // `(*q)[i]` : q is a pointer-to-pointer alias of a tracked object.
        size_t depth = 0, k = prev + 1, open = kNPos;
        for (; k > 0; --k) {
          if (ts.isPunct(k - 1, ")")) ++depth;
          else if (ts.isPunct(k - 1, "(")) {
            if (--depth == 0) { open = k - 1; break; }
          }
        }
        if (open != kNPos) {
          size_t star = ts.nextSig(open);
          size_t id = ts.nextSig(star);
          if (ts.isPunct(star, "*") && id < toks.size() &&
              toks[id].kind == TokKind::Identifier && ts.nextSig(id) >= prev) {
            auto a = ptrAlias.find(toks[id].text);
            if (a != ptrAlias.end()) {
              auto f = facts.find(a->second);
              if (f != facts.end()) ext = f->second;
            }
          }
        }
      } else if (toks[prev].kind == TokKind::Identifier) {
        auto f = facts.find(toks[prev].text);
        if (f != facts.end()) ext = f->second;
      }
      if (!ext.count.empty())
        addSite(toks[first].offset, toks[close].offset, ext);
      continue;
    }
    // `*(p + i)` : guard the integer operand once.
    if (!ts.isPunct(i, "*"))
      continue;
    size_t lp = ts.nextSig(i);
    if (lp >= toks.size() || !ts.isPunct(lp, "("))
      continue;
    size_t id = ts.nextSig(lp);
    if (id >= toks.size() || toks[id].kind != TokKind::Identifier)
      continue;
    auto f = facts.find(toks[id].text);
    if (f == facts.end())
      continue;
    size_t op = ts.nextSig(id);
    if (op >= toks.size() || (!ts.isPunct(op, "+") && !ts.isPunct(op, "-")))
      continue;
    size_t idx = ts.nextSig(op), rp = ts.nextSig(idx);
    if (rp >= toks.size() || !ts.isPunct(rp, ")"))
      continue;
    AliasExtent ext = f->second;
    if (ts.isPunct(op, "-"))
      ext.name += " (negative)";
    addSite(toks[idx].offset, toks[rp].offset, ext);
  }
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
  std::vector<CheckedBoundsSite> bounds = checkedBoundsSites();
  // RaiiLowering can emit `cplus_zero(&owner, sizeof(owner))` for an
  // explicitly invoked destructor.  Those TUs may otherwise have no safety
  // sites at all, so the declaration need must be part of the early-return
  // condition (LEGACY never calls this function, so its bytes are untouched).
  const bool needZeroDecl = source.find("cplus_zero") != std::string::npos;
  // Plain pointer dereferences are recovered lexically (the AST pass does not
  // record them).  This pass runs only in Safe mode, so LEGACY source bytes
  // remain untouched.
  const std::vector<NonnullSite> nonnullSites =
      collectNonnullDerefSites(source, arrayLengths);
  // Safe-mode C99 VLA lowering (see rationale above).  This runs here, and
  // only here, because the driver calls this function exclusively in Safe
  // mode; LEGACY byte preservation therefore remains exact.
  std::vector<SafetyInsertion> vlaEdits;
  std::map<std::string, std::string> vlaCounts; // VLA name -> runtime count temp
  const bool haveVla =
      lowerVariableLengthArrays(source, vlaEdits, vlaCounts);
  // Recover pointer-alias subscripts (arrays, VLAs, heap allocations, rows,
  // interior pointers) from the lowered C; these are emitted by the same
  // CheckedBoundsSite path as AST-recorded sites.
  collectAliasBoundsSites(source, arrayLengths, vlaCounts, bounds);
  if (arrayLengths.empty() && arith.empty() && bounds.empty() &&
      checkedPointerSites().empty() && checkedAllocSites().empty() &&
      checkedFreeSites().empty() && nonnullSites.empty() && !needZeroDecl &&
      !haveVla)
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
  const std::vector<std::pair<size_t, size_t>> staticRanges =
      staticInitializerRanges(source);

  std::vector<SafetyInsertion> insertions;
  // Offsets already guarded by the token-level pass; a resolved AST site for
  // the same access must not emit a duplicate check.
  std::set<size_t> guardedIndexOffsets;
  bool haveBounds = false;
  bool havePtrFormation = false;
  bool haveNonnull = false;
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

  // VLA subscripts have a *runtime* extent, so a constant index such as
  // `a[9]` is NOT statically provable in bounds and must be checked too.
  // The extent is the single-evaluation temporary emitted by the lowering
  // above, which keeps the check consistent with the allocation even when the
  // original count expression has side effects or is later reassigned.
  if (!vlaCounts.empty()) {
    std::vector<Token> toks = lexCPlus(source);
    TokenStream ts(toks);
    for (size_t i = 0; i < toks.size(); ++i) {
      if (toks[i].kind != TokKind::Identifier)
        continue;
      auto vi = vlaCounts.find(toks[i].text);
      if (vi == vlaCounts.end())
        continue;
      // Skip the now-lowered declaration itself (`int a[n];` in the source).
      if (hasDeclarationPrefix(ts, i))
        continue;
      size_t open = ts.nextSig(i);
      if (open >= toks.size() || !ts.isPunct(open, "["))
        continue;
      size_t close = ts.matchBracket(open);
      if (close == kNPos || close == ts.nextSig(open))
        continue;
      // nextSig() pre-increments, so nextSig(open) is the first token inside
      // the brackets; nextSig(open+1) would skip a single-token index.
      size_t first = ts.nextSig(open);
      if (first >= close || first >= toks.size())
        continue;
      size_t idxBegin = toks[first].offset;
      if (guardedIndexOffsets.count(idxBegin))
        continue; // already guarded by the fixed-array or AST pass
      haveBounds = true;
      guardedIndexOffsets.insert(idxBegin);
      // Match the fixed-array bounds pass: span kNPos makes this the OUTERMOST
      // prefix (sorted first at a shared offset) and the OUTERMOST suffix
      // (sorted last).  A span of 0 let an arithmetic site's prefix sort ahead
      // of ours, splicing the bounds macro into its argument list and
      // producing calls with the wrong arity.
      const size_t outer = kNPos;
      insertions.push_back({idxBegin, outer, 0, "__cplus_bounds_check((size_t)(",
                            0});
      insertions.push_back({toks[close].offset, outer, 2,
                            "), (size_t)(" + vi->second + "), \"" +
                                toks[i].text + "\")",
                            0});
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
    // kNPos span keeps this the OUTERMOST wrapper at a shared offset: a
    // smaller span let a checked-arithmetic prefix sort ahead and splice the
    // guard into the helper's argument list (wrong arity).
    insertions.push_back({idxBegin, kNPos, 0,
                          "__cplus_bounds_check((size_t)(", 0});
    insertions.push_back({close, kNPos, 2,
                          "), (size_t)(" +
                              (site.count.empty() ? std::string("1")
                                                  : site.count) +
                              "), \"" + site.name + "\")", 0});
    guardedIndexOffsets.insert(idxBegin);
    haveBounds = true;
  }

  // A guard call is illegal wherever C requires a constant expression: enum
  // bodies, file-scope/static initialisers, array sizes and case labels.  The
  // static checker already validates those constants; skip the site here.
  auto inConstantContext = [&](const CheckedArithmeticSite &s) {
    for (const auto &r : enumRanges)
      if (s.begin >= r.first && s.end <= r.second) return true;
    for (const auto &r : staticRanges)
      if (s.begin >= r.first && s.begin < r.second) return true;
    return false;
  };

  // --- nested conversion chains: compose innermost-first -------------------
  // A cast chain such as `(char)(int)d` yields one conversion site per cast.
  // Emitting them as independent prefix/suffix insertions makes their suffixes
  // collide at the operand's end offset and swaps the argument lists (f2i gets
  // narrow's 5 args, narrow gets f2i's 3), producing malformed C.  Compose the
  // whole chain into one self-contained replacement, innermost guard first.
  std::set<size_t> composedConversions;
  {
    std::vector<size_t> convIdx;
    for (size_t i = 0; i < arith.size(); ++i) {
      const CheckedArithmeticSite::Op k = arith[i].kind;
      if (k == CheckedArithmeticSite::Op::Narrow ||
          k == CheckedArithmeticSite::Op::FloatToInt)
        convIdx.push_back(i);
    }
    for (size_t root = 0; root < convIdx.size(); ++root) {
      const size_t ri = convIdx[root];
      if (composedConversions.count(ri))
        continue;
      const CheckedArithmeticSite &R = arith[ri];
      if (R.end <= R.begin)
        continue;
      if (inConstantContext(R))
        continue; // integer constant expression: no runtime call is legal
      // Gather the other conversion sites that guard the same operand.  We
      // allow a shared `end` (nested casts on one value) as well as strictly
      // contained spans (a cast wrapped around a larger expression).
      std::vector<size_t> chain;
      for (size_t j : convIdx) {
        if (j == ri || composedConversions.count(j))
          continue;
        const CheckedArithmeticSite &C = arith[j];
        if (C.end <= C.begin || C.begin < R.begin || C.end > R.end)
          continue;
        chain.push_back(j);
      }
      if (chain.empty())
        continue;
      // Never drop an independent guard (bounds, allocation, pointer, other
      // arithmetic) that lives inside the region we would replace wholesale.
      bool hasForeign = false;
      for (const SafetyInsertion &ins : insertions)
        if (ins.offset >= R.begin && ins.offset < R.end) {
          hasForeign = true;
          break;
        }
      if (hasForeign)
        continue; // keep the independent insertions; emit normally below
      chain.push_back(ri);
      std::sort(chain.begin(), chain.end(), [&](size_t a, size_t b) {
        const int ra = conversionRank(arith[a]);
        const int rb = conversionRank(arith[b]);
        if (ra != rb)
          return ra < rb;
        return arith[a].begin > arith[b].begin;
      });
      const size_t first = chain.front();
      // Drop any leading (TYPE) casts already present in the source: the
      // wrappers we compose below re-emit the destination casts, and keeping
      // the originals would check an already-truncated value.
      const size_t opStart = skipLeadingCasts(source, arith[first].begin,
                                              arith[first].end);
      std::string text = sourceSlice(source, opStart, arith[first].end);
      for (size_t j : chain) {
        if (arith[j].kind == CheckedArithmeticSite::Op::FloatToInt)
          haveFloatToInt = true;
        text = wrapConversionText(arith[j], text);
      }
      insertions.push_back({R.begin, R.end - R.begin, 0, text,
                            R.end - R.begin});
      for (size_t j : chain)
        composedConversions.insert(j);
    }
  }

  for (size_t arithIdx = 0; arithIdx < arith.size(); ++arithIdx) {
    const CheckedArithmeticSite &site = arith[arithIdx];
    if (composedConversions.count(arithIdx))
      continue; // emitted as a composed chain replacement above
    if (inConstantContext(site))
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
      // A decrement must SUBTRACT one, not add it: using `add` with +1 for
      // `j--` silently turned real programs (Boyer-Moore, shunting-yard)
      // into infinite/overflowing loops.
      const std::string op = kind == CheckedArithmeticSite::Op::Dec
                                 ? "sub" : "add";
      const std::string tail = ", " + dst + ", " + bits + ", " + sgn +
                               ", 1, " + (postfix ? "1" : "0") + ")";
      if (postfix) {
        // `a++`: prefix before `a`, convert the operator into macro args.
        insertions.push_back({site.begin, span, 0, "CPLUS_INC(" + op + ", ", 0});
        insertions.push_back({site.op, span, 1, tail, site.opLength});
      } else {
        // `++a`: swallow the operator, append macro args after the operand.
        insertions.push_back({site.begin, span, 0, "CPLUS_INC(" + op + ", ", 0});
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
  // Route ABI-visible malloc/calloc/realloc calls through the runtime side
  // table.  Managed allocation helpers (cplus_realloc, cplus_realloc_registered,
  // cplus_alloc/cplus_alloc_array/... ) already register or transfer the block
  // inside the runtime: wrapping their result again makes the pointer "already
  // a live C+ allocation" (rc=104).  Raw realloc remains handled by the
  // isRealloc path below, which emits exactly one cplus_realloc_registered.
  auto managedAllocIn = [](const std::string &text) {
    static const char *names[] = {"cplus_alloc", "cplus_zalloc", "cplus_realloc",
                                  "cplus_alloc_array", "cplus_new",
                                  "cplus_new_array"};
    auto ident = [](char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '_';
    };
    for (const char *n : names) {
      size_t len = std::strlen(n);
      for (size_t i = 0; i + len <= text.size(); ++i)
        if (text.compare(i, len, n) == 0 && (i == 0 || !ident(text[i - 1])) &&
            (i + len == text.size() || !ident(text[i + len])))
          return true;
    }
    return false;
  };
  const auto &allocSites = checkedAllocSites();
  for (const CheckedAllocSite &site : allocSites) {
    if (managedAllocIn(sourceSlice(source, site.begin, site.end)) ||
        managedAllocIn(site.argText))
      continue; // runtime already owns the registration/transfer
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

  // Plain pointer dereferences: wrap the base once so it is evaluated a
  // single time.  CPLUS_NONNULL yields the original pointer value after the
  // runtime check, so `*p`, `p->f` and `p[i]` keep their exact semantics.
  for (const NonnullSite &site : nonnullSites) {
    const size_t span = site.baseEnd - site.baseBegin;
    insertions.push_back({site.baseBegin, span, 0, "CPLUS_NONNULL(", 0});
    insertions.push_back({site.baseEnd, span, 2, ")", 0});
    haveNonnull = true;
  }

  // VLA lowering edits are already in final lowered-C coordinates; merge them
  // with the instrumentation edits so one sorted pass emits everything.
  insertions.insert(insertions.end(), vlaEdits.begin(), vlaEdits.end());

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
  // Opt-in "auto runtime": when the generated C is compiled with
  // -DCPLUS_AUTO_RUNTIME=1, install a constructor that switches the runtime
  // into catchable-violation mode before main runs.  The preprocessor guard
  // keeps ordinary builds byte-for-byte unchanged (LEGACY never reaches this
  // prelude at all), and placing this ahead of the first #line preserves the
  // original source line numbers in every diagnostic.
  prelude +=
      "#if defined(CPLUS_AUTO_RUNTIME)\n"
      "extern void cplus_set_auto_runtime(int);\n"
      "static void __attribute__((constructor)) __cplus_auto_runtime_init(void) {\n"
      "  cplus_set_auto_runtime(1);\n"
      "}\n"
      "#endif\n";
  if (needZeroDecl)
    prelude += "#include <stddef.h>\n"
               "extern void cplus_zero(void *, size_t);\n";
  if (haveVla)
    // Signatures mirror runtime/include/cplus/cplus.h.  Declared before the
    // first #line so original source line numbers are preserved, and reused
    // from the existing runtime instead of adding a new subsystem.
    prelude += "#include <stddef.h>\n"
               "extern void *cplus_alloc_array(size_t, size_t);\n"
               "extern void cplus_release(void *);\n"
               "extern size_t cplus_alloc_bytes(const void *);\n";
  if ((haveBounds && !haveVla) || havePtrFormation)
    prelude += "#include <stddef.h>\n"; // haveVla already included it
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
  if (haveNonnull)
    // The signature must match runtime/include/cplus/cplus.h exactly.  Bind
    // the pointer once (GNU statement expression where available; the
    // portable fallback is safe here because the base is a bare identifier).
    prelude += "extern void cplus_check_nonnull(const void *, const char *, "
               "int, const char *);\n"
               "#if defined(__GNUC__) || defined(__clang__)\n"
               "#define CPLUS_NONNULL(P) ({ __typeof__(P) __cplus_nn_p = "
               "(P); \\\n"
               "  cplus_check_nonnull((const void *)__cplus_nn_p, "
               "__FILE__, __LINE__, #P); \\\n"
               "  __cplus_nn_p; })\n"
               "#else\n"
               "#define CPLUS_NONNULL(P) \\\n"
               "  (cplus_check_nonnull((const void *)(P), __FILE__, "
               "__LINE__, #P), (P))\n"
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
    prelude += "#include <stdint.h>\n"
               // `inline` is not a C89 keyword.  A plain `static` definition
               // is accepted by every C standard and keeps internal linkage;
               // the GNU unused attribute (where available) still silences
               // unused-function diagnostics.  Semantics are unchanged.
               "#if defined(__GNUC__) || defined(__clang__)\n"
               "#  define CPLUS_STATIC_INLINE __attribute__((unused)) static\n"
               "#else\n"
               "#  define CPLUS_STATIC_INLINE static\n"
               "#endif\n";
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
          "CPLUS_STATIC_INLINE int64_t __cplus_inline_f2i_i64(double v, "
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
                 "CPLUS_STATIC_INLINE size_t __cplus_inline_size_mul(size_t a, "
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
        "#define CPLUS_ARITH_INLINE CPLUS_STATIC_INLINE\n";

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
        "#define CPLUS_INC(OP, P, T, BT, SG, D, POST) \\\n"
        "  ({ __typeof__(&(P)) __cplus_ap = &(P); \\\n"
        "     __typeof__(*__cplus_ap) __cplus_ov = *__cplus_ap; \\\n"
        "     *__cplus_ap = (T)__cplus_inline_##OP##_i64( \\\n"
        "         (int64_t)__cplus_ov, (int64_t)(D), (BT), (SG)); \\\n"
        "     (POST) ? __cplus_ov : *__cplus_ap; })\n"
        "#else\n"
        "#define CPLUS_CMPD_BIN(OP, P, T, B, BT, SG) \\\n"
        "  ((P) = (T)__cplus_inline_##OP##_i64( \\\n"
        "      (int64_t)(P), (int64_t)(B), (BT), (SG)))\n"
        "#define CPLUS_INC(OP, P, T, BT, SG, D, POST) \\\n"
        "  ((P) = (T)__cplus_inline_##OP##_i64((int64_t)(P), \\\n"
        "      (int64_t)(D), (BT), (SG)))\n"
        "#endif\n"
        "#define CPLUS_NEG(X, T, BT, SG) \\\n"
        "  ((T)__cplus_inline_sub_i64(0, (int64_t)(X), (BT), (SG)))\n";
  }
  return prelude + out;
}

//===----------------------------------------------------------------------===//
// C+ exception lowering (Safe mode only)
//
// `try { body } catch (Exception e) { handler }` becomes a setjmp/longjmp
// frame using additive runtime helpers. This keeps plain C pointers and the
// existing ABI: no C++ unwinding tables, no fat pointers, no new object file
// format. LEGACY source is never passed through this pass.
//===----------------------------------------------------------------------===//
namespace {

bool isWordAt(const std::string &s, size_t i, const char *word) {
  size_t n = std::strlen(word);
  if (i + n > s.size() || s.compare(i, n, word) != 0)
    return false;
  auto ident = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
  };
  if (i > 0 && ident(s[i - 1]))
    return false;
  if (i + n < s.size() && ident(s[i + n]))
    return false;
  return true;
}

size_t skipSpace(const std::string &s, size_t i) {
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                          s[i] == '\r'))
    ++i;
  return i;
}

bool isIdentCharC(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

/// Advance past a string/char literal beginning at `i`.  A keyword such as
/// `try`/`throw` or a brace inside a literal is data, not syntax; scanning it
/// as code used to corrupt the generated C (unterminated strings, unbalanced
/// braces, wrong `throw` operands).  Escape sequences and adjacent literals are
/// handled so `";"` and `"\\\""` cannot terminate the scan early.
size_t skipLiteralC(const std::string &s, size_t i) {
  const char quote = s[i];
  ++i;
  while (i < s.size() && s[i] != quote) {
    if (s[i] == '\\' && i + 1 < s.size()) { i += 2; continue; }
    if (s[i] == '\n') break; // unterminated literal: resync at the newline
    ++i;
  }
  if (i < s.size() && s[i] == quote) ++i;
  return i;
}

/// Advance past a // or /* ... */ comment beginning at `i`.
size_t skipCommentC(const std::string &s, size_t i) {
  if (i + 1 >= s.size() || s[i] != '/')
    return i;
  if (s[i + 1] == '/') {
    size_t nl = s.find('\n', i + 2);
    return nl == std::string::npos ? s.size() : nl + 1;
  }
  if (s[i + 1] == '*') {
    size_t end = s.find("*/", i + 2);
    return end == std::string::npos ? s.size() : end + 2;
  }
  return i;
}

/// True if `s[i]` starts a // or /* comment.
bool isCommentStart(const std::string &s, size_t i) {
  return i + 1 < s.size() && s[i] == '/' &&
         (s[i + 1] == '/' || s[i + 1] == '*');
}

size_t matchBrace(const std::string &s, size_t open) {
  int depth = 0;
  for (size_t i = open; i < s.size();) {
    char c = s[i];
    if (c == '"' || c == '\'') { i = skipLiteralC(s, i); continue; }
    if (isCommentStart(s, i)) { i = skipCommentC(s, i); continue; }
    if (c == '{') ++depth;
    else if (c == '}') {
      if (--depth == 0)
        return i;
    }
    ++i;
  }
  return std::string::npos;
}

/// Find the closing ')' for the '(' at `open`, ignoring literals/comments.
size_t matchParenText(const std::string &s, size_t open) {
  if (open >= s.size() || s[open] != '(')
    return std::string::npos;
  int depth = 0;
  for (size_t i = open; i < s.size();) {
    char c = s[i];
    if (c == '"' || c == '\'') { i = skipLiteralC(s, i); continue; }
    if (isCommentStart(s, i)) { i = skipCommentC(s, i); continue; }
    if (c == '(') ++depth;
    else if (c == ')') {
      if (--depth == 0) return i;
    }
    ++i;
  }
  return std::string::npos;
}

/// Find the next whole-word occurrence of `word` that is real C code (not
/// inside a string/char literal or a comment).
size_t findWordC(const std::string &s, size_t from, const char *word) {
  for (size_t i = from; i < s.size();) {
    char c = s[i];
    if (c == '"' || c == '\'') { i = skipLiteralC(s, i); continue; }
    if (isCommentStart(s, i)) { i = skipCommentC(s, i); continue; }
    if (c == word[0] && isWordAt(s, i, word)) return i;
    ++i;
  }
  return std::string::npos;
}

/// Find the ';' that terminates a `throw` expression, skipping literals,
/// comments and any nested (), [] or {} so a semicolon inside a string or a
/// compound literal cannot truncate the operand.
size_t findThrowSemicolon(const std::string &s, size_t from) {
  int par = 0, brk = 0, brc = 0;
  for (size_t i = from; i < s.size();) {
    char c = s[i];
    if (c == '"' || c == '\'') { i = skipLiteralC(s, i); continue; }
    if (isCommentStart(s, i)) { i = skipCommentC(s, i); continue; }
    if (c == '(') ++par;
    else if (c == ')') { if (par > 0) --par; }
    else if (c == '[') ++brk;
    else if (c == ']') { if (brk > 0) --brk; }
    else if (c == '{') ++brc;
    else if (c == '}') { if (brc > 0) --brc; }
    else if (c == ';' && par == 0 && brk == 0 && brc == 0) return i;
    ++i;
  }
  return std::string::npos;
}

std::string trimC(const std::string &s) {
  size_t b = s.find_first_not_of(" \t\n\r");
  if (b == std::string::npos) return std::string();
  size_t e = s.find_last_not_of(" \t\n\r");
  return s.substr(b, e - b + 1);
}

/// Extract a usable C identifier for a catch parameter.  The producer may
/// spell `catch (Exception e)`, `catch (const Exception &e)`, `catch (Exception)`
/// or add trailing whitespace; taking the last identifier (and rejecting C
/// keywords) keeps the emitted `Exception <name> = ...;` valid C.
std::string catchNameFromDecl(const std::string &decl) {
  static const std::set<std::string> keywords = {
      "Exception", "const", "volatile", "struct", "union", "enum",
      "unsigned", "signed", "int", "long", "short", "char", "void",
      "return", "if", "else", "while", "for", "do", "switch", "case"};
  size_t end = decl.size();
  while (end > 0 && !isIdentCharC(decl[end - 1])) --end;
  size_t begin = end;
  while (begin > 0 && isIdentCharC(decl[begin - 1])) --begin;
  std::string name = decl.substr(begin, end - begin);
  if (name.empty() || keywords.count(name) != 0 ||
      (name[0] >= '0' && name[0] <= '9'))
    return "ex";
  return name;
}

std::string lowerThrows(const std::string &s, bool &changed) {
  std::string out;
  size_t pos = 0;
  while (pos < s.size()) {
    size_t t = findWordC(s, pos, "throw");
    if (t == std::string::npos) {
      out += s.substr(pos);
      break;
    }
    out += s.substr(pos, t - pos);
    size_t semi = findThrowSemicolon(s, t + 5);
    if (semi == std::string::npos) {
      // Malformed throw: keep the remaining bytes verbatim but remove the
      // C+ keyword so the emitted C is still parseable.
      out += s.substr(t + 5);
      changed = true;
      break;
    }
    const std::string expr = trimC(sourceSlice(s, skipSpace(s, t + 5), semi));
    if (expr.empty()) {
      // Bare `throw;` rethrows the exception captured on entry to the
      // innermost active handler instead of silently raising code 1.
      out += "cplus_exception_raise(cplus_rethrow_exc.code, "
             "cplus_rethrow_exc.message, cplus_rethrow_exc.file, "
             "cplus_rethrow_exc.line);";
    } else {
      // A call in the operand (`throw 4 print("x");`) means the source is
      // malformed: consume the whole statement but raise a neutral code
      // rather than splicing a call into an integer argument list.
      bool callLike = false;
      for (size_t k = 1; k < expr.size(); ++k)
        if (expr[k] == '(' && isIdentCharC(expr[k - 1])) {
          callLike = true;
          break;
        }
      std::string code = "1", msg = "0";
      if (!callLike && expr[0] == '"') {
        msg = expr; // string throw: message, default code
      } else if (!callLike) {
        // Use the operand for the code, not just leading decimal digits.
        // This preserves negative literals, parenthesised values and
        // variables instead of silently turning them into `throw 1;`.
        code = "(int)(" + expr + ")";
      }
      out += "cplus_exception_raise(" + code + ", " + msg +
             ", __FILE__, __LINE__);";
    }
    changed = true;
    pos = semi + 1;
  }
  return out;
}

/// Recursive worker.  `counter` is shared across nesting so frame names are
/// unique even for `try { try {...} ... try {...} }`; previously each recursive
/// call restarted at 0 and could redeclare the same frame in one scope.
std::string lowerExceptionsImpl(const std::string &source, unsigned &counter,
                                bool &changed) {
  std::string out;
  size_t pos = 0;
  while (pos < source.size()) {
    size_t t = findWordC(source, pos, "try");
    if (t == std::string::npos) {
      out += source.substr(pos);
      break;
    }
    size_t open = skipSpace(source, t + 3);
    if (open >= source.size() || source[open] != '{') {
      // Malformed `try` (no compound statement).  Drop the keyword rather than
      // emitting it verbatim into C, where it is not a valid identifier.
      out += source.substr(pos, t - pos);
      pos = t + 3;
      changed = true;
      continue;
    }
    size_t close = matchBrace(source, open);
    if (close == std::string::npos) {
      out += source.substr(pos);
      break;
    }
    const std::string frame = "__cplus_exc_" + std::to_string(counter++);
    changed = true;
    out += source.substr(pos, t - pos);
    const std::string body = lowerExceptionsImpl(
        lowerThrows(source.substr(open + 1, close - open - 1), changed),
        counter, changed);

    // C+ has a single `Exception` type, so extra catch clauses cannot be
    // type-matched.  Consume them (so no `catch` syntax leaks into the C) but
    // run only the first handler; emitting every handler sequentially was
    // silent wrong behaviour.
    std::string handlerCode;
    size_t resume = close + 1;
    size_t scan = skipSpace(source, resume);
    bool firstHandler = true;
    while (scan < source.size() && isWordAt(source, scan, "catch")) {
      size_t lp = skipSpace(source, scan + 5);
      if (lp >= source.size() || source[lp] != '(')
        break;
      size_t rp = matchParenText(source, lp);
      if (rp == std::string::npos || rp <= lp)
        break;
      const std::string name =
          catchNameFromDecl(source.substr(lp + 1, rp - lp - 1));
      size_t ho = skipSpace(source, rp + 1);
      if (ho >= source.size() || source[ho] != '{')
        break;
      size_t hc = matchBrace(source, ho);
      if (hc == std::string::npos)
        break;
      if (firstHandler) {
        const std::string handler = lowerExceptionsImpl(
            lowerThrows(source.substr(ho + 1, hc - ho - 1), changed), counter,
            changed);
        handlerCode = " Exception " + name + " = " + frame + ".base.exc; " +
                      handler;
        firstHandler = false;
      }
      resume = hc + 1;
      scan = skipSpace(source, resume);
    }

    out += "do { CPLUS_EXC_FRAME(" + frame + "); " + frame + ".active = 1; "
           "if (setjmp(" + frame + ".base.env) == 0) { "
           "cplus_exception_enter(&" + frame + ".base); " + body +
           " " + frame + ".active = 0; cplus_exception_pop(); }";
    if (!handlerCode.empty())
      out += " else { " + frame + ".active = 0; cplus_rethrow_exc = " +
             frame + ".base.exc; " + handlerCode + " }";
    out += " } while (0);";
    pos = resume;
  }
  // Throws outside a try block are lowered too: they become deterministic
  // "unhandled C+ exception" fatals at runtime rather than compile errors.
  return lowerThrows(out, changed);
}

} // namespace

std::string lowerExceptions(const std::string &source) {
  unsigned counter = 0;
  bool changed = false;
  std::string out = lowerExceptionsImpl(source, counter, changed);
  if (!changed)
    return out;
  // Guard declarations so a source that includes "cplus/cplus.h" (which uses
  // the same CPLUS_EXCEPTION_DEFINED guard) does not redefine the types, while
  // a source that omits the header still emits valid C.  Placed before every
  // #line so original source line numbers are preserved.
  return "#ifndef CPLUS_EXCEPTION_DEFINED\n"
         "#define CPLUS_EXCEPTION_DEFINED\n"
         "#include <setjmp.h>\n"
         "#include <stdint.h>\n"
         "typedef struct Exception { int code; const char *message; "
         "const char *file; int line; } Exception;\n"
         "typedef struct CPlusExceptionFrame { jmp_buf env; "
         "struct CPlusExceptionFrame *prev; Exception exc; "
         "uint64_t generation; } CPlusExceptionFrame;\n"
         "#endif\n"
         "extern void cplus_exception_enter(CPlusExceptionFrame *);\n"
         "extern void cplus_exception_pop(void);\n"
         "extern void cplus_exception_raise(int, const char *, "
         "const char *, int);\n"
         // A bare `throw;` rethrows the most recently caught exception.  The
         // runtime pops the active frame before longjmp, so handlers copy the
         // exception into this slot on entry; a rethrow then raises it again.
         "#if defined(__GNUC__) || defined(__clang__)\n"
         "#  define CPLUS_EXC_TLS __thread\n"
         "#else\n"
         "#  define CPLUS_EXC_TLS\n"
         "#endif\n"
         "static CPLUS_EXC_TLS Exception cplus_rethrow_exc = "
         "{1, 0, 0, 0};\n"
         // Scope guard: `cplus_exception_pop()` on the setjmp==0 path alone
         // left a stale runtime frame when a try was left by return/break/
         // continue/goto.  A cleanup attribute pops on *every* exit edge.
         // `active` is volatile so its value survives a longjmp; the catch
         // path clears it because cplus_exception_raise() already popped the
         // frame before jumping, which prevents a double pop.
         "typedef struct CPlusExcGuard { CPlusExceptionFrame base; "
         "volatile int active; } CPlusExcGuard;\n"
         "static void cplus_exc_guard_cleanup(CPlusExcGuard *g) {\n"
         "  if (g->active) { g->active = 0; cplus_exception_pop(); }\n"
         "}\n"
         "#if defined(__GNUC__) || defined(__clang__)\n"
         "#  define CPLUS_EXC_FRAME(N) CPlusExcGuard N "
         "__attribute__((cleanup(cplus_exc_guard_cleanup)))\n"
         "#else\n"
         "#  define CPLUS_EXC_FRAME(N) CPlusExcGuard N\n"
         "#endif\n" +
         out;
}

} // namespace cplus

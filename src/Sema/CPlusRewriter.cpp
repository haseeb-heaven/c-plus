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
#include <vector>

namespace cplus {

namespace {

constexpr size_t kNPos = static_cast<size_t>(-1);

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
          const std::string &filename)
      : ts(toks), diags_(diags), filename_(filename) {}

  RewrittenSource run() {
    collectDeclarations();
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
          classNames_.insert(c.name);
          classTypes_.push_back(c.name);
          classes_.push_back(std::move(c));
          i = classes_.back().bodyClose;
          continue;
        }
      }
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
        diags_.error("constructors are not part of C+ MVP; initialise '" +
                         c->name + "' with an aggregate initialiser instead",
                     filename_, ts[nameIdx].line, ts[nameIdx].column);
        return true;
      }
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
      size_t lparen = ts.nextSig(eq);
      size_t cls = ts.nextSig(lparen);
      if (name >= ts.size() || ts[name].kind != TokKind::Identifier ||
          !ts.isPunct(eq, "=") || !ts.isPunct(lparen, "(") ||
          cls >= ts.size() || ts[cls].kind != TokKind::Identifier ||
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

  // --- emission -------------------------------------------------------------

  void emit(const std::string &s) { out_ += s; }

  void emitToken(size_t i) { out_ += ts[i].text; }

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
      if (ts.isIdent(i, kw::kManaged) || ts.isIdent(i, kw::kOwned)) {
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
      if (tryEmitArrayLen(i) || tryEmitMethodCall(i)) {
        i = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      emitToken(i);
    }
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

    for (const auto &m : c.methods) {
      emit(lineDirective(filename_, ts[m.itemStart].line));
      std::string ret = joinTokens(ts, m.itemStart, m.nameIdx);
      if (ret.empty()) {
        diags_.error("method '" + m.name + "' is missing a return type",
                     filename_, ts[m.nameIdx].line, ts[m.nameIdx].column);
        emit("void ");
      } else {
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
    for (size_t i = open; i <= close; ++i) {
      if (ts.isIdent(i, "auto") && tryEmitAuto(i)) {
        i = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      if (tryEmitArrayLen(i) || tryEmitMethodCall(i, selfClass)) {
        i = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      emitToken(i);
    }
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
      diags_.error("cannot determine the declared name in this declaration",
                   filename_, ts[start].line, ts[start].column);
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
        "cplus_new_array"};
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
      if (ts.isIdent(i, "cplus_new")) {
        expr = "1";
      } else if (ts.isIdent(i, "cplus_new_array")) {
        if (comma == kNPos)
          continue;
        expr = "(" + joinTokens(ts, ts.nextSig(comma), rp) + ")";
      } else {
        if (ts.isIdent(i, "cplus_realloc")) {
          if (comma == kNPos)
            continue;
          argStart = ts.nextSig(comma);
          argEnd = rp;
        }
        if (argStart >= argEnd)
          continue;
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
      dynamicLengths_.push_back({i, var, expr});
    }
  }

  /// Emit a range of tokens, applying the same C+ rewrites as the top level.
  void emitRange(size_t from, size_t to, const ClassInfo *selfClass = nullptr) {
    for (size_t k = from; k < to; ++k) {
      if (ts.isIdent(k, "auto") && tryEmitAuto(k)) {
        k = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      if (tryEmitArrayLen(k) || tryEmitMethodCall(k, selfClass)) {
        k = skipTo_;
        skipTo_ = kNPos;
        continue;
      }
      emitToken(k);
    }
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

    emit("__typeof__(");
    emitRange(eq + 1, end);
    emit(")");
    skipTo_ = i; // the caller resumes at the declared name
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
    if (!isStatic && !isDynamic)
      return false;
    size_t dot = ts.nextSig(i);
    if (!ts.isPunct(dot, "."))
      return false;
    size_t member = ts.nextSig(dot);
    if (!ts.isIdent(member, "len"))
      return false;
    if (isStatic) {
      emit("(" + std::to_string(arrays_[ts[i].text]) + ")");
    } else {
      // Use the most recent allocation of this name before this use site.
      std::string expr;
      for (const auto &entry : dynamicLengths_)
        if (entry.pos < i && entry.name == ts[i].text)
          expr = entry.expr;
      if (expr.empty())
        emit("cplus_alloc_count(" + ts[i].text + ", sizeof(*" + ts[i].text +
             "))");
      else
        emit("(" + expr + ")");
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
    else if (selfClass && ts[i].text == kw::kSelf)
      className = selfClass->name;
    else
      return false;

    size_t op = ts.nextSig(i);
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
    if (!cls || !cls->methodNames.count(ts[methodIdx].text))
      return false;

    emit(className + "_" + ts[methodIdx].text + "(");
    if (arrow)
      emitToken(i);
    else {
      emit("&");
      emitToken(i);
    }
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
  size_t skipTo_ = kNPos;

  std::vector<ResourceInfo> resources_;
  std::vector<ClassInfo> classes_;
  std::vector<std::string> resourceTypes_;
  std::vector<std::string> classTypes_;
  std::set<std::string> resourceNames_;
  std::set<std::string> classNames_;
  std::map<std::string, std::string> varClasses_;
  std::map<std::string, size_t> arrays_;
  std::set<std::string> dynamicPointers_;
  struct DynamicLength {
    size_t pos;
    std::string name;
    std::string expr;
  };
  std::vector<DynamicLength> dynamicLengths_;
};

} // namespace

RewrittenSource rewriteCPlusSource(const std::string &source,
                                   DiagnosticEngine &diags,
                                   const std::string &filename) {
  std::vector<Token> toks = lexCPlus(source);
  Lowerer lowerer(toks, diags, filename);
  return lowerer.run();
}

} // namespace cplus

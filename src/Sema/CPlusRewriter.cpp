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
    emitTranslationUnit();

    RewrittenSource result;
    result.text = lineDirective(filename_, 1) + out_;
    result.changed = changed_;
    result.resourceTypes = resourceTypes_;
    result.classTypes = classTypes_;
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
      if (tryEmitMethodCall(i)) {
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
      if (tryEmitMethodCall(i, selfClass)) {
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

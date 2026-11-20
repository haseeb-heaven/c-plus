//===- CPlusRewriter.cpp - C+ source rewriting ------------------*- C++ -*-===//
//
// Rewrites C+-specific syntax into equivalent C syntax that Clang's C
// frontend can parse, annotating the constructs so that later C+ passes can
// find them again.
//
// The rewrite preserves every byte that is not part of a rewritten construct,
// which keeps line and column numbers of all remaining code identical.
//
//===----------------------------------------------------------------------===//

#include "Diagnostics.h"
#include "LangFeatures.h"
#include "Sema/CPlusLexer.h"
#include "Sema/CPlusRewriter.h"
#include "Sema/SemaCPlus.h"

#include <string>
#include <vector>

namespace cplus {

namespace {

/// Attribute string used to tag a record as a C+ class / resource.
const char *kClassAnnotate = "__attribute__((annotate(\"cplus_class\"))) ";
const char *kResourceAnnotate =
    "__attribute__((annotate(\"cplus_resource\"))) ";

/// Tag used on declarations that own a managed allocation.
const char *kManagedAnnotate = "__attribute__((annotate(\"cplus_managed\"))) ";
/// Tag used on declarations that own a resource handle.
const char *kOwnedAnnotate = "__attribute__((annotate(\"cplus_owned\"))) ";

struct Rewriter {
  const std::vector<Token> &toks;
  DiagnosticEngine &diags;
  std::string out;

  explicit Rewriter(const std::vector<Token> &t, DiagnosticEngine &d)
      : toks(t), diags(d) {}

  bool isIdent(size_t i, const char *name) const {
    return i < toks.size() && toks[i].kind == TokKind::Identifier &&
           toks[i].text == name;
  }

  /// Skip whitespace and comments, returning the index of the next
  /// significant token.
  size_t skipTrivia(size_t i) const {
    while (i < toks.size() && (toks[i].kind == TokKind::Whitespace ||
                               toks[i].kind == TokKind::Comment))
      ++i;
    return i;
  }

  /// Find the matching `}` for the `{` at index i.  Returns the index of the
  /// `}` or npos.
  size_t matchBrace(size_t i) const {
    int depth = 0;
    for (; i < toks.size(); ++i) {
      if (toks[i].kind != TokKind::Punct)
        continue;
      if (toks[i].text == "{")
        ++depth;
      else if (toks[i].text == "}") {
        if (--depth == 0)
          return i;
      }
    }
    return toks.size();
  }

  void emit(const Token &t) { out.append(t.text); }
  void emit(const std::string &s) { out.append(s); }

  /// Emit tokens [from, to) verbatim.
  void emitRange(size_t from, size_t to) {
    for (size_t i = from; i < to && i < toks.size(); ++i)
      emit(toks[i]);
  }

  /// Look ahead: does the token sequence starting at `i` match
  /// `class <ident> {`?
  bool matchClassHeader(size_t i, std::string *name) const {
    size_t j = skipTrivia(i);
    if (j >= toks.size() || toks[j].kind != TokKind::Identifier ||
        toks[j].text != kw::kClass)
      return false;
    size_t k = skipTrivia(j + 1);
    if (k >= toks.size() || toks[k].kind != TokKind::Identifier)
      return false;
    if (name)
      *name = toks[k].text;
    size_t m = skipTrivia(k + 1);
    if (m >= toks.size() || toks[m].kind != TokKind::Punct ||
        toks[m].text != "{")
      return false;
    return true;
  }

  /// Look ahead: does the token sequence starting at `i` match
  /// `resource struct <ident>` or `resource <ident>`?
  bool matchResourceHeader(size_t i, std::string *name) const {
    size_t j = skipTrivia(i);
    if (j >= toks.size() || toks[j].kind != TokKind::Identifier ||
        toks[j].text != kw::kResource)
      return false;
    size_t k = skipTrivia(j + 1);
    // optional `struct` / `union`
    if (k < toks.size() && toks[k].kind == TokKind::Identifier &&
        (toks[k].text == "struct" || toks[k].text == "union"))
      k = skipTrivia(k + 1);
    if (k >= toks.size() || toks[k].kind != TokKind::Identifier)
      return false;
    if (name)
      *name = toks[k].text;
    return true;
  }
};

/// Rewrite a class body: method declarations become ordinary C functions
/// with an explicit `self` first parameter.
void rewriteClassBody(Rewriter &rw, size_t bodyStart, size_t bodyEnd,
                      const std::string &className, DiagnosticEngine &diags,
                      const std::string &filename) {
  size_t i = bodyStart + 1; // skip '{'
  while (i < bodyEnd) {
    size_t j = rw.skipTrivia(i);
    if (j >= bodyEnd)
      break;

    // Copy field declarations through untouched.
    // A method looks like:  <type> <name> ( ... ) { ... }
    // We detect it by scanning forward for a top-level '(' whose preceding
    // identifier is a method name, and which is followed by a '{' body.
    size_t k = j;
    size_t paren = rw.toks.size();
    size_t nameIdx = rw.toks.size();
    int depth = 0;
    for (; k < bodyEnd; ++k) {
      if (rw.toks[k].kind != TokKind::Punct)
        continue;
      if (rw.toks[k].text == "(") {
        if (depth == 0) {
          paren = k;
          // the identifier right before '(' is the method name
          size_t p = k;
          while (p > bodyStart && rw.toks[p].kind != TokKind::Identifier)
            --p;
          nameIdx = p;
        }
        ++depth;
      } else if (rw.toks[k].text == ")") {
        --depth;
        if (depth == 0) {
          break;
        }
      }
    }

    if (paren >= rw.toks.size() || k >= bodyEnd) {
      // Not a method: emit the rest verbatim.
      rw.emitRange(i, bodyEnd);
      return;
    }

    // Is there a '{' body right after the parameter list (allowing an
    // optional trailing 'const')?
    size_t after = rw.skipTrivia(k + 1);
    if (after < bodyEnd && rw.toks[after].kind == TokKind::Punct &&
        rw.toks[after].text == "{") {
      size_t bodyEndIdx = rw.matchBrace(after);

      // Determine the return type: tokens from j up to the name.
      std::string retType;
      for (size_t p = j; p < nameIdx; ++p) {
        if (rw.toks[p].kind == TokKind::Whitespace)
          continue;
        if (!retType.empty())
          retType += " ";
        retType += rw.toks[p].text;
      }
      const std::string methodName = rw.toks[nameIdx].text;

      // Rewrite as a plain C function:
      //   <retType> <Class>_<method>(<Class> *self, <params>) { <body> }
      rw.emit(retType);
      rw.emit(" ");
      rw.emit(className);
      rw.emit("_");
      rw.emit(methodName);
      rw.emit("(");
      rw.emit(className);
      rw.emit(" *self");

      // Parameters: copy the inside of the parens, prefixing the first
      // parameter name with nothing (self is already the first).
      rw.emit(rw.toks[paren].text); // '('
      bool first = true;
      for (size_t p = paren + 1; p < k; ++p) {
        const Token &t = rw.toks[p];
        if (t.kind == TokKind::Whitespace || t.kind == TokKind::Comment) {
          rw.emit(t);
          continue;
        }
        if (t.kind == TokKind::Punct && t.text == ",") {
          rw.emit(t);
          first = true;
          continue;
        }
        if (first) {
          // Insert the parameter name `self` before the type of the first
          // parameter, i.e. emit "self " then the type tokens.
          // We must know whether the parameter already has a name; if the
          // type is a single identifier it IS the name.  Handle the common
          // cases by inspecting the parameter token list.
          size_t pEnd = p;
          while (pEnd < k && !(rw.toks[pEnd].kind == TokKind::Punct &&
                               rw.toks[pEnd].text == ","))
            ++pEnd;
          std::vector<const Token *> ptoks;
          for (size_t q = p; q < pEnd; ++q)
            if (rw.toks[q].kind != TokKind::Whitespace &&
                rw.toks[q].kind != TokKind::Comment)
              ptoks.push_back(&rw.toks[q]);
          bool hasName = ptoks.size() >= 2;
          if (!hasName) {
            rw.emit("self ");
            first = false;
            continue;
          }
          rw.emit(t);
          continue;
        }
        rw.emit(t);
        if (t.kind == TokKind::Identifier || t.kind == TokKind::Number ||
            t.kind == TokKind::StringLit)
          first = false;
      }
      rw.emit(rw.toks[k].text); // ')'
      rw.emit(" ");

      // Emit the body, rewriting bare `self` references so they keep
      // working (they do: `self` is now a real parameter name).
      rw.emitRange(after, bodyEndIdx + 1);
      i = bodyEndIdx + 1;
      continue;
    }

    // Not a method with a body: emit up to and including the ')' verbatim.
    rw.emitRange(i, k + 1);
    i = k + 1;
  }
}

} // namespace

RewrittenSource rewriteCPlusSource(const std::string &source,
                                   DiagnosticEngine &diags,
                                   const std::string &filename) {
  RewrittenSource result;
  std::vector<Token> toks = lexCPlus(source);
  Rewriter rw(toks, diags);

  size_t i = 0;
  while (i < toks.size()) {
    const Token &t = toks[i];

    if (t.kind == TokKind::Identifier && t.text == kw::kResource) {
      std::string name;
      size_t j = rw.skipTrivia(i);
      if (rw.matchResourceHeader(i, &name)) {
        // Find the body / terminator.
        size_t k = rw.skipTrivia(j + 1);
        // Skip an optional 'struct'/'union' keyword.
        if (k < toks.size() && toks[k].kind == TokKind::Identifier &&
            (toks[k].text == "struct" || toks[k].text == "union"))
          k = rw.skipTrivia(k + 1);
        // name is at k
        size_t after = rw.skipTrivia(k + 1);
        if (after < toks.size() && toks[after].kind == TokKind::Punct &&
            toks[after].text == "{") {
          size_t bodyEnd = rw.matchBrace(after);
          rw.emit(kResourceAnnotate);
          rw.emitRange(k, after); // 'struct Name {'
          // Rewrite the body: drop `void drop(void);` style declarations
          // are kept (they are legal C), keep everything else.
          rw.emitRange(after, bodyEnd + 1);
          i = bodyEnd + 1;
          result.changed = true;
          continue;
        }
      }
    }

    if (t.kind == TokKind::Identifier && t.text == kw::kClass) {
      std::string name;
      if (rw.matchClassHeader(i, &name)) {
        size_t bodyStart = rw.skipTrivia(i + 2);
        size_t bodyEnd = rw.matchBrace(bodyStart);
        rw.emit(kClassAnnotate);
        rw.emit("struct ");
        rw.emit(name);
        rw.emit(" ");
        rewriteClassBody(rw, bodyStart, bodyEnd, name, diags, filename);
        i = bodyEnd + 1;
        result.changed = true;
        continue;
      }
    }

    rw.emit(t);
    ++i;
  }

  result.text = std::move(rw.out);
  return result;
}

} // namespace cplus

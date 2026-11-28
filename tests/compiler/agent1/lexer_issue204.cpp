//===- lexer_issue204.cpp - CPlusLexer regression for issue #204 ---*- C++ -*-===//
//
// Dependency-free unit checks for backslash-spliced // comments and for
// unterminated string/char literals in disabled preprocessor branches.
// Built and run by regression_issue204.sh against libcplus_core.a.
//
//===----------------------------------------------------------------------===//

#include "Sema/CPlusLexer.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace cplus;

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
  if (!ok)
    ++failures;
}

const Token *findIdent(const std::vector<Token> &toks,
                       const std::string &name) {
  for (const Token &t : toks)
    if (t.kind == TokKind::Identifier && t.text == name)
      return &t;
  return nullptr;
}

bool lossless(const std::vector<Token> &toks, const std::string &src) {
  std::string rebuilt;
  for (const Token &t : toks)
    rebuilt += t.text;
  return rebuilt == src;
}

void testSplicedLineComment() {
  const std::string src =
      "// first line continues \\\nclass Hidden {};\nclass Real {};\n";
  const std::vector<Token> toks = lexCPlus(src);
  check(lossless(toks, src), "spliced comment: token text covers every byte");
  check(findIdent(toks, "Hidden") == nullptr,
        "spliced comment: continued line is still a comment");
  check(findIdent(toks, "Real") != nullptr,
        "spliced comment: next physical line is live code");
  const Token *real = findIdent(toks, "Real");
  check(real && real->line == 3,
        "spliced comment: line numbers follow physical lines");
}

void testUnterminatedApostrophe() {
  const std::string src =
      "#if 0\ndon't do this\n#endif\nclass Real { int x; };\n";
  const std::vector<Token> toks = lexCPlus(src);
  check(lossless(toks, src), "apostrophe: token text covers every byte");
  check(findIdent(toks, "Real") != nullptr,
        "apostrophe: later class is not swallowed by a fake CharLit");
  const Token *real = findIdent(toks, "Real");
  check(real && real->line == 4, "apostrophe: line counter survives the gap");
}

void testUnterminatedQuote() {
  const std::string src =
      "#if 0\nshe said \"hello\n#endif\nclass Real2 { int x; };\n";
  const std::vector<Token> toks = lexCPlus(src);
  check(lossless(toks, src), "quote: token text covers every byte");
  check(findIdent(toks, "Real2") != nullptr,
        "quote: later class is not swallowed by a fake StringLit");
}

void testSplicedStringLiteral() {
  const std::string src =
      "const char *s = \"ab\\\ncd\";\nclass After { int x; };\n";
  const std::vector<Token> toks = lexCPlus(src);
  check(lossless(toks, src), "spliced literal: token text covers every byte");
  bool joined = false;
  for (const Token &t : toks)
    if (t.kind == TokKind::StringLit &&
        t.text.find("ab") != std::string::npos &&
        t.text.find("cd") != std::string::npos)
      joined = true;
  check(joined, "spliced literal stays one StringLit across the newline");
  const Token *after = findIdent(toks, "After");
  check(after && after->line == 3, "spliced literal: line numbers stay exact");
}

} // namespace

int main() {
  testSplicedLineComment();
  testUnterminatedApostrophe();
  testUnterminatedQuote();
  testSplicedStringLiteral();
  if (failures) {
    std::printf("%d lexer check(s) failed\n", failures);
    return 1;
  }
  std::puts("issue #204 CPlusLexer regression: PASS");
  return 0;
}

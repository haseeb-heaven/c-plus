//===- CPlusLexer.h - C+ source lexer ---------------------------*- C++ -*-===//
//
// A small, dependency-free lexer used by the C+ source rewriter.
//
// It is deliberately *not* a C lexer: it only needs to find C+ keywords
// outside of comments and string/char literals, while tracking line numbers
// so that the rewritten text keeps the original line/column layout and
// Clang's diagnostics remain accurate.
//
//===----------------------------------------------------------------------===//

#ifndef CPLUS_CPLUSLEXER_H
#define CPLUS_CPLUSLEXER_H

#include <string>
#include <vector>

namespace cplus {

/// A token kind produced by the lexer.
enum class TokKind {
  Identifier,
  Number,
  StringLit,
  CharLit,
  Punct,
  Comment,
  Whitespace,
  EndOfFile
};

struct Token {
  TokKind kind = TokKind::EndOfFile;
  std::string text;   ///< raw spelling
  size_t offset = 0;  ///< byte offset in the source
  unsigned line = 1;  ///< 1-based line
  unsigned column = 1;///< 1-based column
};

/// Split `source` into tokens.  Comments are returned as tokens so that the
/// rewriter can preserve them verbatim.
std::vector<Token> lexCPlus(const std::string &source);

} // namespace cplus

#endif // CPLUS_CPLUSLEXER_H

//===- CPlusLexer.cpp - C+ source lexer ---------------------------*- C++ -*-===//

#include "Sema/CPlusLexer.h"

#include <cctype>

namespace cplus {

namespace {

bool isIdentStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool isIdentCont(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

/// Multi-character punctuators, longest first so that greedy matching works.
const char *const kPunctuators[] = {
    "...", "<<=", ">>=", "->*", "##",  "<:", ":>", "<%", "%>", "%:", "::",
    "->",  "++",  "--",  "<<",  ">>",  "<=",  ">=",  "==",  "!=", "&&", "||",
    "+=",  "-=",  "*=",  "/=",  "%=",  "&=",  "|=",  "^=", ".*", "#",
};

/// Length of an ISO C phase-2 line splice at `i`: a backslash immediately
/// followed by LF or CRLF.  Returns 0 when there is no splice.  Splicing
/// happens before comment/literal recognition, so `//` continues past a
/// spliced newline and a spliced newline cannot terminate a literal.
size_t lineSpliceLength(const std::string &src, size_t i) {
  if (i >= src.size() || src[i] != '\\')
    return 0;
  if (i + 1 < src.size() && src[i + 1] == '\n')
    return 2;
  if (i + 2 < src.size() && src[i + 1] == '\r' && src[i + 2] == '\n')
    return 3;
  return 0;
}

} // namespace

std::vector<Token> lexCPlus(const std::string &src) {
  std::vector<Token> toks;
  size_t i = 0;
  unsigned line = 1;
  unsigned col = 1;
  const size_t n = src.size();

  auto advance = [&](size_t count) {
    for (size_t k = 0; k < count && i < n; ++k, ++i) {
      if (src[i] == '\n') {
        ++line;
        col = 1;
      } else {
        ++col;
      }
    }
  };

  while (i < n) {
    Token t;
    t.offset = i;
    t.line = line;
    t.column = col;
    char c = src[i];

    // Whitespace (keep newlines so we can reproduce them exactly).
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' ||
        c == '\f') {
      size_t start = i;
      while (i < n && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' ||
                       src[i] == '\n' || src[i] == '\v' || src[i] == '\f'))
        advance(1);
      t.kind = TokKind::Whitespace;
      t.text = src.substr(start, i - start);
      toks.push_back(std::move(t));
      continue;
    }

    // Comments.
    if (c == '/' && i + 1 < n && src[i + 1] == '/') {
      size_t start = i;
      while (i < n) {
        if (size_t splice = lineSpliceLength(src, i)) {
          advance(splice); // spliced newline: the comment continues
          continue;
        }
        if (src[i] == '\n')
          break;
        advance(1);
      }
      t.kind = TokKind::Comment;
      t.text = src.substr(start, i - start);
      toks.push_back(std::move(t));
      continue;
    }
    if (c == '/' && i + 1 < n && src[i + 1] == '*') {
      size_t start = i;
      advance(2);
      while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/'))
        advance(1);
      if (i + 1 < n)
        advance(2);
      else
        advance(n - i);
      t.kind = TokKind::Comment;
      t.text = src.substr(start, i - start);
      toks.push_back(std::move(t));
      continue;
    }

    // String literals (including prefixes) and char literals.
    bool prefixedLiteral =
        (c == 'u' || c == 'U' || c == 'L') && i + 1 < n &&
        (src[i + 1] == '"' || src[i + 1] == '\'');
    if (!prefixedLiteral && c == 'u' && i + 2 < n && src[i + 1] == '8' &&
        (src[i + 2] == '"' || src[i + 2] == '\''))
      prefixedLiteral = true; // u8"..." / u8'...'
    if (c == '"' || c == '\'' || prefixedLiteral) {
      size_t start = i;
      char quote;
      if (c == '"' || c == '\'') {
        quote = c;
        advance(1);
      } else {
        // Consume the encoding prefix.
        if (c == 'u' && i + 2 < n && src[i + 1] == '8') {
          advance(2);
        } else {
          advance(1);
        }
        quote = src[i];
        advance(1);
      }
      // An unclosed quote in prose (e.g. inside `#if 0`) must not swallow
      // the rest of the translation unit: a raw newline always ends the
      // token.  A spliced newline is not raw and stays inside the literal.
      while (i < n && src[i] != quote && src[i] != '\n' && src[i] != '\r') {
        if (size_t splice = lineSpliceLength(src, i)) {
          advance(splice);
          continue;
        }
        if (src[i] == '\\' && i + 1 < n) {
          advance(2); // escaped character, including an escaped quote
          continue;
        }
        advance(1);
      }
      if (i < n && src[i] == quote)
        advance(1); // closing quote; leave a raw newline for the next token
      t.kind = (quote == '"') ? TokKind::StringLit : TokKind::CharLit;
      t.text = src.substr(start, i - start);
      toks.push_back(std::move(t));
      continue;
    }

    // Identifiers / keywords.
    if (isIdentStart(c)) {
      size_t start = i;
      while (i < n && isIdentCont(src[i]))
        advance(1);
      t.kind = TokKind::Identifier;
      t.text = src.substr(start, i - start);
      toks.push_back(std::move(t));
      continue;
    }

    // Numbers.
    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '.' && i + 1 < n &&
         std::isdigit(static_cast<unsigned char>(src[i + 1])))) {
      size_t start = i;
      while (i < n) {
        char d = src[i];
        if (std::isalnum(static_cast<unsigned char>(d)) || d == '.' ||
            d == '_') {
          advance(1);
        } else if (d == '\'' && i > start && i + 1 < n &&
                   (std::isalnum(static_cast<unsigned char>(src[i + 1])) ||
                    src[i + 1] == '_')) {
          advance(1); // C23 digit separator: 1'000'000
        } else if ((d == '+' || d == '-') && i > start &&
                   (src[i - 1] == 'e' || src[i - 1] == 'E' ||
                    src[i - 1] == 'p' || src[i - 1] == 'P')) {
          advance(1);
        } else {
          break;
        }
      }
      t.kind = TokKind::Number;
      t.text = src.substr(start, i - start);
      toks.push_back(std::move(t));
      continue;
    }

    // Punctuators.
    {
      bool matched = false;
      for (const char *p : kPunctuators) {
        size_t len = std::char_traits<char>::length(p);
        if (src.compare(i, len, p) == 0) {
          advance(len);
          t.kind = TokKind::Punct;
          t.text = p;
          matched = true;
          break;
        }
      }
      if (!matched) {
        advance(1);
        t.kind = TokKind::Punct;
        t.text = std::string(1, c);
      }
      toks.push_back(std::move(t));
      continue;
    }
  }

  Token eof;
  eof.kind = TokKind::EndOfFile;
  eof.offset = n;
  eof.line = line;
  eof.column = col;
  toks.push_back(std::move(eof));
  return toks;
}

} // namespace cplus

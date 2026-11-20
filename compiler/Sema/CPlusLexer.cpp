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
      while (i < n && src[i] != '\n')
        advance(1);
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
    if (c == '"' || c == '\'' ||
        ((c == 'u' || c == 'u8' || c == 'U' || c == 'L') && i + 1 < n &&
         (src[i + 1] == '"' || src[i + 1] == '\''))) {
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
      while (i < n && src[i] != quote) {
        if (src[i] == '\\' && i + 1 < n)
          advance(1);
        advance(1);
      }
      if (i < n)
        advance(1); // closing quote
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

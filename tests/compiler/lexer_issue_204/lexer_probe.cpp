//===- lexer_probe.cpp - standalone lexer probe for issue #204 -------------===//
//
// Reads a source fragment from stdin and prints one line per token:
//
//   <kind> <offset> <line> <column> <hex-encoded-text>
//
// Text is hex-encoded so LF/CR and quotes can be compared byte-for-byte by
// the Python driver.  This probes cplus::lexCPlus directly; it needs no built
// `c+` binary and no project build.
//
//===----------------------------------------------------------------------===//

#include "Sema/CPlusLexer.h"

#include <iostream>
#include <iterator>
#include <string>

namespace {

const char *kindName(cplus::TokKind kind) {
  switch (kind) {
  case cplus::TokKind::Identifier:
    return "Identifier";
  case cplus::TokKind::Number:
    return "Number";
  case cplus::TokKind::StringLit:
    return "StringLit";
  case cplus::TokKind::CharLit:
    return "CharLit";
  case cplus::TokKind::Punct:
    return "Punct";
  case cplus::TokKind::Comment:
    return "Comment";
  case cplus::TokKind::Whitespace:
    return "Whitespace";
  case cplus::TokKind::EndOfFile:
    return "EndOfFile";
  }
  return "Unknown";
}

std::string hexEncode(const std::string &text) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(text.size() * 2);
  for (unsigned char ch : text) {
    out.push_back(kHex[ch >> 4]);
    out.push_back(kHex[ch & 0x0f]);
  }
  return out;
}

} // namespace

int main() {
  const std::string src((std::istreambuf_iterator<char>(std::cin)),
                        std::istreambuf_iterator<char>());
  for (const cplus::Token &tok : cplus::lexCPlus(src)) {
    std::cout << kindName(tok.kind) << ' ' << tok.offset << ' ' << tok.line
              << ' ' << tok.column << ' ' << hexEncode(tok.text) << '\n';
  }
  return 0;
}

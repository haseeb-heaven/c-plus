# Lexer regression suite for issue #204

`compiler/Sema/CPlusLexer.cpp` had two ways of hiding live C+ tokens from
`CPlusRewriter` / `RaiiLowering` / `StaticBoundsCheck`:

1. A `//` comment stopped at the first raw LF, ignoring ISO C phase-2 line
   splicing. A trailing `\` (or `\` + CRLF) must continue the comment onto the
   next physical line; otherwise keywords on the continued line were lexed as
   live tokens.
2. String/char literal scanning ran past raw newlines until the next matching
   quote or EOF. A lone apostrophe in `#if 0` prose therefore produced one
   fake literal token spanning the rest of the file, hiding every C+ token in
   that span.

This suite probes `cplus::lexCPlus` directly. It compiles a tiny standalone
probe (`lexer_probe.cpp`) against the real lexer source with `c++` and checks
the complete token stream (kind, offset, line, column and hex-encoded text)
for each case. No full project build, `c+` binary or Clang is required.

## Run

```sh
python3 tests/compiler/lexer_issue_204/run_suite.py
```

Set `CXX` to select a different C++ compiler:

```sh
CXX=clang++ python3 tests/compiler/lexer_issue_204/run_suite.py
```

Exit status is `0` when every case passes, `1` on any mismatch, `2` if the
probe cannot be built. There are no timestamps or generated result files, so
output is deterministic.

## Cases

- `line_comment_lf_splice`, `line_comment_crlf_splice`,
  `line_comment_even_backslashes` — spliced `//` comments swallow the continued
  line; comments remain verbatim for source preservation.
- `line_comment_no_splice_stops` — an ordinary comment still ends at its LF.
- `unterminated_char_lf`, `unterminated_string_lf`, `unterminated_char_crlf` —
  an unclosed quote ends at the raw line break; later lines stay tokenized.
- `escaped_quote_still_works`, `spliced_string_still_works` — escapes and
  spliced newlines inside literals are unchanged.
- `if_zero_apostrophe_keeps_later_tokens` — issue reproducer shape: C+
  keywords after an apostrophe in `#if 0` prose remain visible to the rewriter.

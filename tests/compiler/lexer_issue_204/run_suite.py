#!/usr/bin/env python3
"""Deterministic lexer regressions for GitHub issue #204.

Two failure modes are covered, both at the `cplus::lexCPlus` level so the
result does not depend on a built `c+` binary or on Clang:

  1. A trailing backslash in a `//` comment is an ISO C phase-2 line splice;
     the comment must continue onto the next physical line instead of leaking
     `class` / `resource` / `owned` tokens into the rewriter and RAII lowering.
  2. An unclosed `'` or `"` in prose (the classic `#if 0` apostrophe) must not
     swallow the remainder of the translation unit: a raw LF/CRLF terminates
     a literal token, while a spliced LF/CRLF stays inside it.

The probe prints kind, byte offset, 1-based line/column and hex-encoded text
for every token (including EndOfFile).  Expected records are derived from an
explicit token/text list; offsets, lines and columns are recomputed from the
source so full token-stream equality is checked, not a substring heuristic.

Run:  python3 tests/compiler/lexer_issue_204/run_suite.py
CXX may override the compiler used for the tiny standalone probe.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
LEXER = os.path.join(ROOT, "compiler", "Sema", "CPlusLexer.cpp")
PROBE_SRC = os.path.join(HERE, "lexer_probe.cpp")

I = "Identifier"
N = "Number"
S = "StringLit"
Q = "CharLit"
P = "Punct"
M = "Comment"
W = "Whitespace"
E = "EndOfFile"

CASES = [
    dict(
        name="line_comment_lf_splice",
        note="backslash-LF continues a // comment onto the next line",
        src=b"// hidden \\\nclass Resource;\n",
        specs=[(M, b"// hidden \\\nclass Resource;"), (W, b"\n"), (E, b"")],
    ),
    dict(
        name="line_comment_crlf_splice",
        note="backslash-CRLF continues a // comment onto the next line",
        src=b"// hidden \\\r\nclass Resource;",
        specs=[(M, b"// hidden \\\r\nclass Resource;"), (E, b"")],
    ),
    dict(
        name="line_comment_even_backslashes",
        note="the backslash directly before LF is the splice, even after another backslash",
        src=b"// hidden \\\\\nclass Resource;\n",
        specs=[(M, b"// hidden \\\\\nclass Resource;"), (W, b"\n"),
               (E, b"")],
    ),
    dict(
        name="line_comment_no_splice_stops",
        note="an ordinary // comment still ends at the first raw LF",
        src=b"// plain\nclass Resource;\n",
        specs=[(M, b"// plain"), (W, b"\n"), (I, b"class"), (W, b" "),
               (I, b"Resource"), (P, b";"), (W, b"\n"), (E, b"")],
    ),
    dict(
        name="unterminated_char_lf",
        note="an unclosed apostrophe must not consume the following line",
        src=b"don't run this\nclass Resource;\n",
        specs=[(I, b"don"), (Q, b"'t run this"), (W, b"\n"), (I, b"class"),
               (W, b" "), (I, b"Resource"), (P, b";"), (W, b"\n"),
               (E, b"")],
    ),
    dict(
        name="unterminated_string_lf",
        note="an unclosed quote must not consume the following line",
        src=b"\"oops\nclass Resource;\n",
        specs=[(S, b"\"oops"), (W, b"\n"), (I, b"class"), (W, b" "),
               (I, b"Resource"), (P, b";"), (W, b"\n"), (E, b"")],
    ),
    dict(
        name="unterminated_char_crlf",
        note="CRLF also terminates an unclosed literal",
        src=b"don't run this\r\nclass Resource;\r\n",
        specs=[(I, b"don"), (Q, b"'t run this"), (W, b"\r\n"),
               (I, b"class"), (W, b" "), (I, b"Resource"), (P, b";"),
               (W, b"\r\n"), (E, b"")],
    ),
    dict(
        name="escaped_quote_still_works",
        note="escaped quotes and normal char literals are unchanged",
        src=b"'\\'' class Resource;\n",
        specs=[(Q, b"'\\''"), (W, b" "), (I, b"class"), (W, b" "),
               (I, b"Resource"), (P, b";"), (W, b"\n"), (E, b"")],
    ),
    dict(
        name="spliced_string_still_works",
        note="backslash-LF inside a string is a splice, not a terminator",
        src=b"\"ab\\\ncd\" class Resource;\n",
        specs=[(S, b"\"ab\\\ncd\""), (W, b" "), (I, b"class"), (W, b" "),
               (I, b"Resource"), (P, b";"), (W, b"\n"), (E, b"")],
    ),
    dict(
        name="if_zero_apostrophe_keeps_later_tokens",
        note="issue reproducer shape: C+ keywords after an #if 0 apostrophe stay visible",
        src=(b"#if 0\ndon't run this\n#endif\n"
             b"resource struct R { int x; };\n"
             b"int main(void){ return 0; }\n"),
        specs=[
            (P, b"#"), (I, b"if"), (W, b" "), (N, b"0"), (W, b"\n"),
            (I, b"don"), (Q, b"'t run this"), (W, b"\n"), (P, b"#"),
            (I, b"endif"), (W, b"\n"), (I, b"resource"), (W, b" "),
            (I, b"struct"),
            (W, b" "), (I, b"R"), (W, b" "), (P, b"{"), (W, b" "),
            (I, b"int"), (W, b" "), (I, b"x"), (P, b";"), (W, b" "),
            (P, b"}"), (P, b";"), (W, b"\n"), (I, b"int"), (W, b" "),
            (I, b"main"), (P, b"("), (I, b"void"), (P, b")"), (P, b"{"),
            (W, b" "), (I, b"return"), (W, b" "), (N, b"0"), (P, b";"),
            (W, b" "), (P, b"}"), (W, b"\n"), (E, b""),
        ],
    ),
]


def expected_records(src, specs):
    """Build full (kind, offset, line, column, hex) records from token texts."""
    records = []
    offset = 0
    for kind, text in specs:
        prefix = src[:offset]
        line = prefix.count(b"\n") + 1
        column = offset - prefix.rfind(b"\n")  # rfind == -1 => 1-based col 1
        records.append((kind, offset, line, column, text.hex()))
        offset += len(text)
    if offset != len(src):
        raise ValueError("specs cover %d of %d bytes" % (offset, len(src)))
    return records


def build_probe(workdir):
    cxx = os.environ.get("CXX", "c++")
    exe = os.path.join(workdir, "lexer_probe")
    cmd = [cxx, "-std=c++17", "-I", os.path.join(ROOT, "compiler"), LEXER,
           PROBE_SRC, "-o", exe]
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=120)
    except (OSError, subprocess.TimeoutExpired) as exc:
        print("probe build error: %s" % exc, file=sys.stderr)
        return None
    if p.returncode != 0:
        print("probe build failed: %s" % p.stderr.decode(errors="replace"),
              file=sys.stderr)
        return None
    return exe


def run_probe(exe, src):
    try:
        p = subprocess.run([exe], input=src, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=15)
    except subprocess.TimeoutExpired as exc:
        return exc, []
    records = []
    for line in p.stdout.decode(errors="replace").splitlines():
        fields = line.split(" ")  # keep the empty hex field of EndOfFile
        if len(fields) != 5:
            return p, []
        kind, offset, row, column, text_hex = fields
        records.append((kind, int(offset), int(row), int(column), text_hex))
    return p, records


def main():
    for path in (LEXER, PROBE_SRC):
        if not os.path.isfile(path):
            print("missing input: %s" % path, file=sys.stderr)
            return 2
    with tempfile.TemporaryDirectory(prefix="cplus_lexer_204_") as workdir:
        exe = build_probe(workdir)
        if exe is None:
            return 2
        failures = []
        for case in CASES:
            expected = expected_records(case["src"], case["specs"])
            p, actual = run_probe(exe, case["src"])
            ok = (getattr(p, "returncode", None) == 0 and not p.stderr and
                  actual == expected)
            if ok:
                print("ok   %s" % case["name"])
            else:
                failures.append(case["name"])
                print("FAIL %s: %s" % (case["name"], case["note"]))
                print("  expected: %r" % (expected,))
                print("  actual:   %r" % (actual,))
                print("  rc=%r stderr=%r" % (getattr(p, "returncode", None),
                                             p.stderr[:200]))
    print("lexer_issue_204: %d cases, %d failures" % (len(CASES),
                                                       len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

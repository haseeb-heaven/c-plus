#!/usr/bin/env python3
"""Deterministic generator for the hostile-input fixtures.

Every byte written here is derived from a fixed literal or a fixed count, so
re-running this script reproduces byte-identical fixtures. No random sources,
no wall-clock values, no host-specific paths.

Usage: python3 tests/security/hostile_inputs/gen_fixtures.py [output_dir]
Default output_dir is the fixtures/ directory next to this script.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "fixtures")

# (name, bytes) - all values are compile-time constants or fixed repetitions.
FIXTURES = [
    # io.hp readers (fed on stdin, one fixture per hostile class)
    ("io_overflow_int.txt", b"2147483648\n"),
    ("io_overflow_long.txt", b"99999999999999999999999999\n"),
    ("io_negative_int.txt", b"-2147483648\n"),
    ("io_float_overflow.txt", b"1e999\n"),
    ("io_float_nan.txt", b"nan\n"),
    ("io_double_inf.txt", b"inf\n"),
    ("io_not_integer.txt", b"12abc\n"),
    ("io_truncated_token.txt", b"x" * 200 + b"\n"),
    ("io_long_line.txt", b"y" * 300 + b"\n"),
    ("io_unterminated.txt", b"abc"),                 # no trailing newline
    ("io_repeated_crlf.txt", b"a\r\r\n"),
    ("io_embedded_nul.txt", b"a\x00b\n"),             # NUL is not whitespace

    # file.hp binary-safe reads
    ("file_binary_nul.bin", b"\x00\x01A\x00\xff\nB\x00"),

    # json.hp malformed / adversarial documents
    ("json_unterminated.json", b'{"a":"unterminated}'),
    ("json_embedded_nul.json", b'{"a":1}\x00{"b":2}'),
    ("json_embedded_nul_bad.json", b'{"a":\x001}'),
    ("json_invalid_utf8.json", b'{"a":"\xc0\xaf"}'),  # overlong 2-byte form
    ("json_deep_256.json", b"[" * 256 + b"1" + b"]" * 256),
    ("json_deep_4096.json", b"[" * 4096 + b"1" + b"]" * 4096),

    # string.hp encodings / delimiters
    ("string_invalid_utf8.bin",
     b"\xc0\xaf\xed\xa0\x80\xf4\x90\x80\x80\x80\xe2\x82"),
    ("string_delims.txt", b"a,,,b,,c"),
]

# Excessive but bounded: 524287 elements -> 1048575 bytes (<= 1 MiB).
LARGE_COUNT = 524287
LARGE_DOC = b"[" + b",".join([b"1"] * LARGE_COUNT) + b"]"
assert len(LARGE_DOC) <= 1024 * 1024, len(LARGE_DOC)
FIXTURES.append(("json_large_1mib.json", LARGE_DOC))


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, data in FIXTURES:
        path = os.path.join(OUT, name)
        with open(path, "wb") as handle:
            handle.write(data)
        print("%s %d" % (name, len(data)))


if __name__ == "__main__":
    main()

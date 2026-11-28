#!/usr/bin/env python3
"""Static structural regressions for issue #235 (no `c+` build required).

The gate-bypass fixes are small enough that a structural guard is
deterministic: it checks the exact invariants that were violated.

  1. Short-option allow-list tests (-I/-D/-L/-l) inspect the RAW token, never
     normalizeFlag()'s double-dash-folded form (--ld-path used to pose as -l).
  2. Language selectors (-x and --language) are recognized by both
     stripLanguageFlags() and extraArgsChangeParse().
  3. sanitizerSupported() creates every file with O_EXCL|0600 and uses
     ExecuteAndWait with redirects; no shell command or derived log path.

Run: python3 tests/compiler/driver_issue_235/source_guard.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DRIVER = os.path.join(ROOT, "compiler", "Driver", "Driver.cpp")

FAILURES = []


def check(ok, message):
    if not ok:
        FAILURES.append(message)


def between(text, start, end):
    i = text.index(start)
    j = text.index(end, i)
    return text[i:j]


def main():
    with open(DRIVER, encoding="utf-8") as fh:
        text = fh.read()

    san = between(text, "static bool sanitizerSupported(",
                  "/// Issue #34: warnings are errors")
    check("system(" not in san,
          "sanitizerSupported still shells out through system()")
    check("createTemporaryFile" not in san,
          "sanitizerSupported still uses bare createTemporaryFile")
    check(san.count("writeUniqueTempFile(") >= 4,
          "sanitizerSupported must O_EXCL-create source, exe and both logs")
    check(san.count("ExecuteAndWait(") >= 2,
          "sanitizerSupported must use ExecuteAndWait for compile and probe")
    check("2>" not in san and " > " not in san,
          "sanitizerSupported still derives shell redirection paths")

    strict = between(text, "static bool strictSafePassthroughAllowed(",
                     "/// The driver reserves the CPLUS* gate macros")
    for letter in "IDLl":
        needle = "isSingleDashOption(raw, '%s')" % letter
        check(needle in strict,
              "STRICT allow-list must test raw -%s short option: %s" %
              (letter, needle))
    for pat in ('s.starts_with("-I")', 's.starts_with("-D")',
                's.starts_with("-L")', 's.starts_with("-l")',
                's == "-I"', 's == "-D"', 's == "-L"', 's == "-l"'):
        check(pat not in strict,
              "STRICT allow-list must not fold double-dash flags via %s" % pat)
    check('raw.starts_with("@")' in strict,
          "STRICT response-file check must inspect the raw token")

    strip = between(text, "stripLanguageFlags(const std::vector<std::string> &extra)",
                    "/// Pass-through flags that change what the *real* clang")
    parse = between(text, "static bool extraArgsChangeParse(",
                    "/// Split a -f...= / -W...= value")
    for name, body in (("stripLanguageFlags", strip),
                       ("extraArgsChangeParse", parse)):
        check("isLanguageOption(a)" in body,
              "%s must recognize --language as well as -x" % name)
    check('a == "-language"' in text and 'a.starts_with("-language=")' in text,
          "isLanguageOption must cover --language and --language=")

    if FAILURES:
        for f in FAILURES:
            print("FAIL: %s" % f)
        print("%d structural check(s) failed" % len(FAILURES))
        return 1
    print("driver issue #235 source guard: 0 failures")
    return 0


if __name__ == "__main__":
    sys.exit(main())

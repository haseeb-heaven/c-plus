#!/usr/bin/env python3
"""Static structural regressions for issue #245 (no c+ build required).

Guards the three root causes that cannot be exercised without a sanitizer
runtime or a shared /tmp attacker:

  1. Main.cpp returns 1 after the dangling -I/-D/-U/-L/-l diagnostic.
  2. sanitizerSupported() never derives log paths from the executable name,
     creates all four probe files exclusively, and closes probeFd by writing
     it before any later create can fail.
  3. Temporary .o files are registered with the RAII TempFile guard vector.

Run: python3 tests/compiler/driver_issue_245/source_guard.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
MAIN = os.path.join(ROOT, "compiler", "Main.cpp")
DRIVER = os.path.join(ROOT, "compiler", "Driver", "Driver.cpp")

FAILURES = []


def check(ok, message):
    if not ok:
        FAILURES.append(message)


def section(text, start, end):
    i = text.find(start)
    if i < 0:
        return ""
    j = text.find(end, i + len(start))
    return text[i:j] if j > i else text[i:]


def main():
    with open(MAIN, encoding="utf-8") as fh:
        main_text = fh.read()
    with open(DRIVER, encoding="utf-8") as fh:
        driver = fh.read()

    # 1. Dangling short options must stop before argv[++i].
    start = main_text.find('if (a == "-I" || a == "-D" || a == "-U"')
    check(start >= 0, "Main.cpp: short-option branch not found")
    if start >= 0:
        block = main_text[start:start + 520]
        guard = block.find("if (i + 1 >= argc)")
        check(guard >= 0, "Main.cpp: missing argc guard for short options")
        if guard >= 0:
            snippet = block[guard:block.find("std::string value", guard)]
            check("return 1;" in snippet,
                  "Main.cpp: dangling short option must return 1 before "
                  "argv[++i]")

    san = section(driver, "static bool sanitizerSupported(",
                  "/// Issue #34: warnings are errors")
    check(san != "", "Driver.cpp: sanitizerSupported() not found")
    if san:
        check('= exe + ".compile.log"' not in san and
              '= exe + ".log"' not in san,
              "sanitizerSupported still derives predictable log names from exe")
        check(san.count("createTemporaryFile(") >= 4,
              "sanitizerSupported must create source, exe and both logs "
              "with O_EXCL names")
        check(san.count("TempFile ") >= 4,
              "sanitizerSupported must RAII-own all four probe files")
        write_at = san.find("writeFd(probeFd")
        next_create = san.find('createTemporaryFile("cplus_san", ".out"')
        check(write_at >= 0 and next_create >= 0 and write_at < next_create,
              "probeFd must be written (and closed) before a later create "
              "can fail and leak it")

    obj_decl = driver.find("std::vector<std::string> objects;")
    guards_decl = driver.find("std::vector<TempFile> guards;")
    runtime_block = driver.find(
        "if (san != Sanitizer::None && !isCheckCommand(opts))")
    check(obj_decl >= 0 and guards_decl > obj_decl and
          runtime_block > guards_decl,
          "TempFile guard vector must be declared before the runtime .o block")
    check("guards.push_back(std::move(rtGuard))" in driver,
          "instrumented-runtime .o is not RAII-owned")
    check("guards.push_back(std::move(objGuard))" in driver,
          "per-input .o is not RAII-owned")

    for failure in FAILURES:
        print("FAIL: " + failure)
    print("issue #245 source guard: %d checks failed" % len(FAILURES))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())

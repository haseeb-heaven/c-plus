#!/usr/bin/env python3
"""Deterministic regressions for CPlusRewriter issue #236.

A translation-unit-wide array name -> extent map let a later declaration of
the same name in an unrelated function overwrite the extent of an earlier
array.  Dynamic subscripts were checked against the wrong bound, so an
out-of-bounds stack write passed STRICT bounds checking.  These cases check
that both the runtime guard and the array-length lowering use the declaration
visible at each use site.

Run: CPLUS_BIN=/path/to/c+ python3 tests/compiler/rewriter_issue_236/run_suite.py
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
SUM = {"pass": 0, "fail": 0}


def check(name, ok, detail=""):
    SUM["pass" if ok else "fail"] += 1
    print(("  ok   " if ok else "  FAIL ") + name +
          ("" if ok else ": " + detail))


def run(argv, timeout=90):
    p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=timeout, check=False)
    return p.returncode, p.stdout.decode(errors="replace"), p.stderr.decode(errors="replace")


SMASH = r'''
#include <stdio.h>
void smash(int idx) {
  int buf[4] = {0};
  buf[idx] = 0x41414141;
  printf("wrote buf[%d]=%x\n", idx, buf[idx]);
}
void unrelated(void) { int buf[1024]; (void)buf; }
int main(int argc, char **argv) { (void)argv; smash(argc + 5); return 0; }
'''

TWO = r'''
#include <stdio.h>
static void small(int i, int v) { int a[8] = {0}; a[i] = v; printf("S=%d\n", a[2]); }
static void large(int i, int v) { int a[64] = {0}; a[i] = v; printf("L=%d\n", a[40]); }
int main(void) { small(2, 7); large(40, 9); return 0; }
'''

NESTED = r'''
#include <stdio.h>
static void f(int idx) {
  int a[4] = {0};
  a[idx] = 1;
  { int a[8] = {0}; a[idx] = 2; }
  a[idx] = 3;
  printf("ok=%d\n", a[2]);
}
int main(void) { volatile int i = 2; f(i); return 0; }
'''

LEN = r'''
#include <stdio.h>
static void small(void) { int buf[4] = {0}; printf("small=%d\n", buf.len); }
static void large(void) { int buf[1024] = {0}; printf("large=%d\n", buf.len); }
int main(void) { small(); large(); return 0; }
'''

# (name, source, required emitted substrings, forbidden substrings,
#  expected stdout on success, expected stderr fragment on trap)
CASES = [
    ("issue236_smash", SMASH, [', 4, "buf")'], [', 1024, "buf")'],
     None, "index 6 is outside the bounds of 'buf' (length 4)"),
    ("issue236_two_functions", TWO, [', 8, "a")', ', 64, "a")'], [],
     "S=7\nL=9\n", None),
    ("issue236_nested", NESTED, [', 4, "a")', ', 8, "a")'], [],
     "ok=3\n", None),
    ("issue236_len", LEN, ['(4)', '(1024)'], [],
     "small=4\nlarge=1024\n", None),
]


def check_case(td, name, source, contain, absent, stdout, stderr_fragment):
    cp = os.path.join(td, name + ".c")
    c = os.path.join(td, name + ".lowered.c")
    exe = os.path.join(td, name + ".bin")
    with open(cp, "w") as f:
        f.write(source.lstrip("\n"))
    rc, _, err = run([BIN, "build", cp, "--strict", "--no-sanitize",
                      "--no-werror", "--emit-c", c, "-o", exe])
    check(name + " build", rc == 0, "rc=%d %s" % (rc, err[:300]))
    if rc != 0:
        return
    with open(c) as f:
        lowered = f.read()
    check(name + " extents", all(s in lowered for s in contain) and
          not any(s in lowered for s in absent),
          "expected %r, forbidden %r" % (contain, absent))
    rc, out, err = run([exe], timeout=30)
    if stderr_fragment:
        check(name + " traps", rc != 0 and stderr_fragment in err,
              "rc=%d out=%r err=%r" % (rc, out[:200], err[:200]))
    else:
        check(name + " runs", rc == 0 and out == stdout,
              "rc=%d out=%r err=%r" % (rc, out[:200], err[:200]))


def main():
    if not os.path.exists(BIN):
        print("missing c+ binary at " + BIN, file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="cplus-issue236-") as td:
        for case in CASES:
            check_case(td, *case)
    print("\nrewriter_issue_236: %d checks, %d passed, %d failed" %
          (sum(SUM.values()), SUM["pass"], SUM["fail"]))
    return 1 if SUM["fail"] else 0


if __name__ == "__main__":
    sys.exit(main())

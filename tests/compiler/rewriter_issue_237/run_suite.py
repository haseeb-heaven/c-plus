#!/usr/bin/env python3
"""Deterministic regressions for issue #237: inline checked-arithmetic wrappers.

CPlusRewriter emits static inline fast paths for checked arithmetic.  The old
shl predicate masked with bits low bits instead of bits-1, so a signed
1 << 63 (or 1 << 31) never reached the runtime trap.  The div/rem and shr fast
paths used signed int64_t arithmetic even for unsigned operands, corrupting
values with bit 63 set and raising SIGFPE on (1ULL << 63) / UINT64_MAX.

Every case is built with c+ build --default --sanitize=none --emit-c, the
emitted C is checked for the inline path, and the binary is run for exact
stdout or the runtime trap diagnostic.  Artifacts live in a temp directory.

Run:
  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/rewriter_issue_237/run_suite.py
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
PRE = '#include "cplus/cplus.h"\n'

PASS = 0
FAIL = 0


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  ok   " + name)
    else:
        FAIL += 1
        print("  FAIL %s: %s" % (name, detail))


def run(argv, timeout=90):
    p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=timeout, check=False)
    return {"rc": p.returncode,
            "out": p.stdout.decode(errors="replace"),
            "err": p.stderr.decode(errors="replace")}


def compile_case(td, name, source):
    cp = os.path.join(td, name + ".cp")
    c = os.path.join(td, name + ".lowered.c")
    exe = os.path.join(td, name + ".bin")
    with open(cp, "w") as f:
        f.write(source.lstrip("\n"))
    step = run([BIN, "build", "--default", "--sanitize=none", "--no-werror",
                cp, "--emit-c", c, "-o", exe])
    text = ""
    if os.path.exists(c):
        with open(c) as f:
            text = f.read()
    return step, text, exe


def shl_source(width, a, count):
    return PRE + r'''
#include <stdint.h>
#include <stdio.h>
static int64_t vi64(int64_t v) { volatile int64_t t = v; return t; }
static int32_t vi32(int32_t v) { volatile int32_t t = v; return t; }
static int si(int v) { volatile int t = v; return t; }
static int64_t shl64(int64_t a, int b) { return a << b; }
static int32_t shl32(int32_t a, int b) { return a << b; }
int main(void) {
  printf("V=%lld\n", (long long)(SHL_CALL));
  return 0;
}
'''.replace("SHL_CALL",
           ("shl64(vi64(%s), si(%s))" % (a, count)) if width == 64
           else ("shl32(vi32(%s), si(%s))" % (a, count)))


# Call the emitted static inline wrappers directly with unsigned 64-bit bit
# patterns: this branch's AST collector skips unsigned >32-bit sites, so the
# plain /, % and >> source forms do not reach these wrappers on their own.
UNSIGNED_WRAPPERS = PRE + r'''
#include <stdint.h>
#include <stdio.h>
static int64_t __cplus_inline_div_i64(int64_t, int64_t, unsigned, unsigned);
static int64_t __cplus_inline_rem_i64(int64_t, int64_t, unsigned, unsigned);
static int64_t __cplus_inline_shr_i64(int64_t, int64_t, unsigned, unsigned);
static int64_t vi(int64_t v) { volatile int64_t t = v; return t; }
static int64_t force_wrappers(int64_t a) { int64_t x = a; x += vi(1); return x; }
static uint64_t udiv64(uint64_t a, uint64_t b) {
  return (uint64_t)__cplus_inline_div_i64((int64_t)a, (int64_t)b, 64, 0);
}
static uint64_t urem64(uint64_t a, uint64_t b) {
  return (uint64_t)__cplus_inline_rem_i64((int64_t)a, (int64_t)b, 64, 0);
}
static uint64_t ushr64(uint64_t a, unsigned s) {
  return (uint64_t)__cplus_inline_shr_i64((int64_t)a, (int64_t)s, 64, 0);
}
static uint64_t udiv32(uint32_t a, uint32_t b) {
  return (uint64_t)__cplus_inline_div_i64((int64_t)a, (int64_t)b, 32, 0);
}
static uint64_t ushr32(uint32_t a, unsigned s) {
  return (uint64_t)__cplus_inline_shr_i64((int64_t)a, (int64_t)s, 32, 0);
}
int main(void) {
  printf("D=%llu R=%llu S=%llu E=%llu F=%llu D32=%llu S32=%llu\n",
         (unsigned long long)udiv64(UINT64_MAX, 2),
         (unsigned long long)urem64(UINT64_C(1) << 63, UINT64_MAX),
         (unsigned long long)ushr64(UINT64_MAX, 1),
         (unsigned long long)udiv64(UINT64_C(1) << 63, UINT64_MAX),
         (unsigned long long)urem64(UINT64_C(1) << 63, UINT64_MAX),
         (unsigned long long)udiv32(UINT32_MAX, 2),
         (unsigned long long)ushr32(UINT32_MAX, 1));
  printf("M=%lld\n", (long long)force_wrappers(vi(1)));
  return 0;
}
'''

UNSIGNED_EXPECT = (
    "D=9223372036854775807 R=9223372036854775808 S=9223372036854775807 "
    "E=0 F=9223372036854775808 D32=2147483647 S32=2147483647\n"
    "M=2\n")


def verify_inline(name, step, text, exe, expect_out=None, diagnostic=None):
    check(name + "_builds", step["rc"] == 0,
          "rc=%s err=%r" % (step["rc"], step["err"][:400]))
    if step["rc"] != 0:
        return
    check(name + "_uses_inline_prelude",
          "#    define CPLUS_HAVE_INLINE_ARITH 1" in text,
          "no inline arithmetic prelude in emitted C")
    r = run([exe])
    combined = r["out"] + r["err"]
    if expect_out is not None:
        check(name + "_runtime", r["rc"] == 0 and r["out"] == expect_out,
              "rc=%s out=%r err=%r" % (r["rc"], r["out"], r["err"][:300]))
    if diagnostic is not None:
        check(name + "_runtime_trap",
              r["rc"] != 0 and diagnostic in combined,
              "rc=%s out=%r err=%r" % (r["rc"], r["out"], r["err"][:300]))


def main():
    if not os.path.exists(BIN):
        print("missing c+ binary at %s" % BIN, file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="cplus_issue_237_") as td:
        step, text, exe = compile_case(
            td, "issue237_shl64_trap", shl_source(64, 1, 63))
        verify_inline("issue237_shl64_trap", step, text, exe,
                      diagnostic="reaches the sign bit")
        check("issue237_shl64_trap_emits_shl_call",
              "__cplus_inline_shl_i64" in text,
              "shift site did not use the inline wrapper")

        step, text, exe = compile_case(
            td, "issue237_shl32_trap", shl_source(32, 1, 31))
        verify_inline("issue237_shl32_trap", step, text, exe,
                      diagnostic="reaches the sign bit")

        step, text, exe = compile_case(
            td, "issue237_shl64_ok", shl_source(64, 1, 62))
        verify_inline("issue237_shl64_ok", step, text, exe,
                      expect_out="V=4611686018427387904\n")

        step, text, exe = compile_case(
            td, "issue237_unsigned_wrappers", UNSIGNED_WRAPPERS)
        check("issue237_unsigned_wrappers_builds", step["rc"] == 0,
              "rc=%s err=%r" % (step["rc"], step["err"][:400]))
        if step["rc"] == 0:
            check("issue237_unsigned_wrappers_emitted",
                  "__cplus_inline_div_i64" in text and
                  "__cplus_inline_shr_i64" in text,
                  "expected inline div/shr wrappers in emitted C")
            r = run([exe])
            check("issue237_unsigned_wrappers_runtime",
                  r["rc"] == 0 and r["out"] == UNSIGNED_EXPECT,
                  "rc=%s out=%r err=%r" %
                  (r["rc"], r["out"], r["err"][:300]))
    print("\nSummary: %d passed, %d failed" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""End-to-end regression suite for Compiler issues #129-#132."""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))

PASS = 0
FAIL = 0


def run_cmd(args, cwd=None):
    return subprocess.run(
        args,
        capture_output=True,
        check=False,
        cwd=cwd,
        timeout=60,
    )


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print(f"  ok   {name}")
    else:
        FAIL += 1
        print(f"  FAIL {name}: {detail}")


def build_and_run(td, name, src, mode="--default", extra_flags=None):
    cp = os.path.join(td, f"{name}.cp")
    emitted = os.path.join(td, f"{name}.c")
    exe = os.path.join(td, f"{name}.bin")
    with open(cp, "w") as f:
        f.write(src)
    cmd = [BIN, "build", mode, "--sanitize=none", "--no-werror", "--emit-c", emitted, cp, "-o", exe]
    if extra_flags:
        cmd.extend(extra_flags)
    b = run_cmd(cmd)
    if b.returncode != 0:
        return b, None, ""
    r = run_cmd([exe])
    c_text = ""
    if os.path.exists(emitted):
        with open(emitted) as f:
            c_text = f.read()
    return b, r, c_text


def main():
    if not os.path.exists(BIN):
        print(f"missing c+ binary at {BIN}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="cplus_129_132_") as td:
        # #129: compound call args must not narrow just first token; nested call commas must not misalign
        src_129_ok = r'''#include <stdint.h>
#include <stdio.h>
static int take_u8(uint8_t v) { return (int)v; }
static int pair_sum(int a, int b) { return a + b; }
static int second_u8(int first, uint8_t second) { return first + (int)second; }
int main(void) {
  int64_t big = 0x123456AB;
  int r1 = take_u8(big & 0xFF);
  int r2 = take_u8(big >> 24);
  int r3 = second_u8(pair_sum(10, 20), 5);
  printf("%d %d %d\n", r1, r2, r3);
  return (r1 == 0xAB && r2 == 0x12 && r3 == 35) ? 0 : 1;
}
'''
        b, r, _ = build_and_run(td, "129_ok", src_129_ok)
        out = r.stdout.decode() if r else ""
        check(
            "129_compound_and_nested_call_args_ok",
            b.returncode == 0 and r is not None and r.returncode == 0 and out == "171 18 35\n",
            f"b={b.returncode} err={b.stderr.decode()[:300]} r={r.returncode if r else None} out={out!r}",
        )

        src_129_trap = r'''#include <stdint.h>
static int pair_sum(int a, int b) { return a + b; }
static int second_u8(int first, uint8_t second) { return first + (int)second; }
int main(void) {
  int64_t big = 300;
  return second_u8(pair_sum(1, 2), big);
}
'''
        b, r, _ = build_and_run(td, "129_trap", src_129_trap)
        err = r.stderr.decode(errors="replace") if r else ""
        check(
            "129_nested_call_second_arg_bare_ident_traps",
            b.returncode == 0 and r is not None and r.returncode != 0 and
            "implicit integer conversion" in err,
            f"b={b.returncode} r={r.returncode if r else None} err={err[:200]}",
        )

        # #130: realloc argument 2 and calloc argument 2 multiplication overflow checks
        src_130_realloc = r'''#include <stdlib.h>
#include <stdint.h>
static volatile size_t g_n = (SIZE_MAX / sizeof(int)) + 16;
int main(void) {
  void *p = malloc(16);
  if (!p) return 2;
  void *q = realloc(p, g_n * sizeof(int));
  if (!q) { free(p); return 0; }
  p = q;
  free(p);
  return 0;
}
'''
        b, r, c_text = build_and_run(td, "130_realloc", src_130_realloc, mode="--strict")
        check(
            "130_realloc_mul_overflow_traps",
            b.returncode == 0 and "__cplus_inline_size_mul" in c_text and
            r is not None and r.returncode != 0,
            f"b={b.returncode} err={b.stderr.decode()[:300]} r={r.returncode if r else None}",
        )

        src_130_calloc = r'''#include <stdlib.h>
#include <stdint.h>
static volatile size_t g_n = (SIZE_MAX / sizeof(int)) + 16;
int main(void) {
  void *p = calloc(1, g_n * sizeof(int));
  if (p) free(p);
  return 0;
}
'''
        b, r, c_text = build_and_run(td, "130_calloc", src_130_calloc, mode="--strict")
        check(
            "130_calloc_arg2_mul_overflow_traps",
            b.returncode == 0 and "__cplus_inline_size_mul" in c_text and r is not None and r.returncode != 0,
            f"b={b.returncode} err={b.stderr.decode()[:300]} r={r.returncode if r else None}",
        )

        # #131: scoped variable types + pointer shadowing across functions
        src_131_narrow = r'''#include <stdint.h>
static int8_t f(int64_t x) {
  int8_t y = x;
  return y;
}
static int8_t g(int8_t x) {
  return x;
}
int main(void) {
  int8_t a = f(300);
  int8_t b = g(1);
  return (a + b) & 1;
}
'''
        b, r, _ = build_and_run(td, "131_narrow", src_131_narrow)
        err = r.stderr.decode(errors="replace") if r else ""
        check(
            "131_later_narrow_var_does_not_suppress_earlier_fn",
            b.returncode == 0 and r is not None and r.returncode != 0 and
            "implicit integer conversion" in err,
            f"b={b.returncode} r={r.returncode if r else None} err={err[:200]}",
        )

        src_131_ptr = r'''#include <stdint.h>
#include <stdio.h>
static int64_t f_int(void) {
  int64_t p = 1000;
  return p;
}
static int h_ptr(int *p, int *q) {
  q = p;
  return *q;
}
int main(void) {
  int v = 42;
  int dummy = 0;
  int res = h_ptr(&v, &dummy);
  printf("%lld %d\n", (long long)f_int(), res);
  return res == 42 ? 0 : 1;
}
'''
        b, r, c_text = build_and_run(td, "131_ptr", src_131_ptr, mode="--strict")
        out = r.stdout.decode() if r else ""
        check(
            "131_pointer_shadows_earlier_int_without_bogus_narrow",
            b.returncode == 0 and r is not None and r.returncode == 0 and out == "1000 42\n" and "__cplus_inline_narrow" not in c_text,
            f"b={b.returncode} err={b.stderr.decode()[:300]} out={out!r}",
        )

        # #132: same-width sign-changing conversions trap when value changes, succeed when in range
        src_132_s2u = r'''#include <stdio.h>
int main(void) {
  volatile int x = -1;
  int val = x;
  unsigned int u = val;
  printf("%u\n", u);
  return 0;
}
'''
        b, r, _ = build_and_run(td, "132_s2u", src_132_s2u)
        err = r.stderr.decode(errors="replace") if r else ""
        check(
            "132_int_to_uint_negative_traps",
            b.returncode == 0 and r is not None and r.returncode != 0 and
            "implicit integer conversion" in err,
            f"b={b.returncode} r={r.returncode if r else None} err={err[:200]}",
        )

        src_132_u2s = r'''#include <stdint.h>
#include <stdio.h>
int main(void) {
  volatile uint64_t u0 = UINT64_MAX;
  uint64_t u = u0;
  int64_t s = u;
  printf("%lld\n", (long long)s);
  return 0;
}
'''
        b, r, _ = build_and_run(td, "132_u2s", src_132_u2s)
        err = r.stderr.decode(errors="replace") if r else ""
        check(
            "132_u64_to_i64_msb_traps",
            b.returncode == 0 and r is not None and r.returncode != 0 and
            "implicit integer conversion" in err,
            f"b={b.returncode} r={r.returncode if r else None} err={err[:200]}",
        )

        src_132_ok = r'''#include <stdint.h>
#include <stdio.h>
static unsigned int ret_u(int x) { return x; }
static int64_t take_s(int64_t s) { return s; }
int main(void) {
  int x = 5;
  unsigned int u = x;
  uint64_t u64 = 42;
  int64_t s64 = u64;
  unsigned int r1 = ret_u(x);
  int64_t r2 = take_s(u64);
  printf("%u %lld %u %lld\n", u, (long long)s64, r1, (long long)r2);
  return (u == 5u && s64 == 42 && r1 == 5u && r2 == 42) ? 0 : 1;
}
'''
        b, r, _ = build_and_run(td, "132_ok", src_132_ok)
        out = r.stdout.decode() if r else ""
        check(
            "132_same_width_in_range_succeeds",
            b.returncode == 0 and r is not None and r.returncode == 0 and out == "5 42 5 42\n",
            f"b={b.returncode} err={b.stderr.decode()[:300]} out={out!r}",
        )

    print(f"\nSummary: {PASS} passed, {FAIL} failed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Deterministic regressions for CPlusRewriter issues #231 and #232.

#231  `@numelts(n)` subscripts must evaluate the index expression exactly
      once (the old lowering emitted it twice, so `p[i++]` was checked at
      n-1 but read at n -> out-of-bounds bypass), and `@notnull` `*p` must
      remain an lvalue (the old `(check, *p)` comma expression is an rvalue
      in C and breaks `*p = v`).
#232  Compound assignments whose RHS starts with a digit must still receive
      runtime checked-arithmetic instrumentation (`x += 1`, `x /= 0`,
      `x <<= N`, `x += 1 + y`), and duplicate AST/token sites for one
      compound operator must be deduplicated instead of emitting two
      nested CPLUS_CMPD_BIN wrappers.

Every case is built with `c+ build --default --sanitize=none --emit-c`,
inspects the generated C, and (where applicable) runs the binary and checks
exact stdout or the runtime trap diagnostic.  All outputs go to a temporary
directory; nothing is written into the repository.

Run:
  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/rewriter_issues_231_232/run_suite.py
"""
import os
import re
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
    c_text = ""
    if os.path.exists(c):
        with open(c) as f:
            c_text = f.read()
    return step, c_text, exe


# --- Issue #231 sources -----------------------------------------------------

NUMELTS_POSTFIX = PRE + r'''
#include <stdio.h>
static void read_at(int *@numelts(n) p, size_t n) {
  size_t i = n - 1;
  int v = p[i++];
  printf("V=%d I=%zu\n", v, i);
}
int main(void) { int a[4] = {1,2,3,4}; read_at(a, 4); return 0; }
'''

NUMELTS_CALL = PRE + r'''
#include <stdio.h>
static int calls;
static size_t next_idx(void) { return (size_t)calls++; }
static void read_at(int *@numelts(n) p, size_t n) {
  int v = p[next_idx()];
  printf("V=%d C=%d\n", v, calls);
}
int main(void) { int a[4] = {1,2,3,4}; read_at(a, 4); return 0; }
'''

NOTNULL_STAR = PRE + r'''
static void set(int *@notnull p) {
  *p = 42;
}
int main(void) { int x = 0; set(&x); return x == 42 ? 0 : 1; }
'''

# --- Issue #232 sources -----------------------------------------------------

COMPOUND_LITERALS = PRE + r'''
#include <stdio.h>
static int val(int v) { volatile int t = v; return t; }
int main(void) {
  int x = val(1), y = val(2);
  x += 1;
  x -= 1;
  x *= 2;
  x += 1 + y;
  printf("X=%d\n", x);
  return 0;
}
'''

# RHS text starts with a digit (the case the old skip suppressed), but the
# value is opaque to the backend so no constant-expression diagnostic fires.
COMPOUND_DIV_ZERO = PRE + r'''
static int val(int v) { volatile int t = v; return t; }
int main(void) {
  int x = val(1);
  x /= 0 + val(0);
  return 0;
}
'''

COMPOUND_SHIFT = PRE + r'''
static int val(int v) { volatile int t = v; return t; }
int main(void) {
  int x = val(1);
  x <<= 32 + val(0);
  return 0;
}
'''

FOR_INCREMENT = PRE + r'''
#include <stdio.h>
int main(void) {
  int sum = 0;
  for (int i = 0; i < 3; i += 1) sum += i;
  printf("S=%d\n", sum);
  return 0;
}
'''

# A symbolic RHS used to emit two nested CPLUS_CMPD_BIN wrappers around the
# same for-loop increment and fail to build; it is the direct dedup regression.
FOR_INCREMENT_SYMBOLIC = PRE + r'''
#include <stdio.h>
static int val(int v) { volatile int t = v; return t; }
int main(void) {
  int sum = 0;
  for (int i = 0; i < 3; i += val(1)) sum += i;
  printf("S=%d\n", sum);
  return 0;
}
'''


def test_numelts_postfix(td):
    name = "issue231_numelts_postfix"
    step, c_text, exe = compile_case(td, name, NUMELTS_POSTFIX)
    check(name + "_builds", step["rc"] == 0,
          "rc=%s err=%r" % (step["rc"], step["err"][:400]))
    if step["rc"] != 0:
        return
    # The index side effect must appear exactly once in the emitted C.
    n = c_text.count("CPLUS_INC(add, i")
    check(name + "_index_evaluated_once", n == 1,
          "CPLUS_INC(add, i) count=%d" % n)
    m = re.search(r"size_t (__cplus_gs_idx_\d+)\s*=\s*\(size_t\)\(", c_text)
    check(name + "_single_eval_temp", m is not None,
          "no single-evaluation temporary in emitted C")
    if m:
        tmp = re.escape(m.group(1))
        used_by_check = re.search(r"cplus_check_bounds\([^;]*" + tmp + r"\s*,",
                                  c_text) is not None
        used_as_index = re.search(tmp + r"\s*;\s*\}\)\)*", c_text) is not None
        check(name + "_temp_used_by_both", used_by_check and used_as_index,
              "check=%s index=%s" % (used_by_check, used_as_index))
    r = run([exe])
    check(name + "_runtime",
          r["rc"] == 0 and r["out"] == "V=4 I=4\n",
          "rc=%s out=%r err=%r" % (r["rc"], r["out"], r["err"][:300]))


def test_numelts_call(td):
    name = "issue231_numelts_call"
    step, c_text, exe = compile_case(td, name, NUMELTS_CALL)
    check(name + "_builds", step["rc"] == 0,
          "rc=%s err=%r" % (step["rc"], step["err"][:400]))
    if step["rc"] != 0:
        return
    n = c_text.count("next_idx()")
    check(name + "_call_emitted_once", n == 1,
          "next_idx() count=%d in emitted C" % n)
    r = run([exe])
    check(name + "_runtime",
          r["rc"] == 0 and r["out"] == "V=1 C=1\n",
          "rc=%s out=%r err=%r" % (r["rc"], r["out"], r["err"][:300]))


def test_notnull_star(td):
    name = "issue231_notnull_star"
    step, c_text, exe = compile_case(td, name, NOTNULL_STAR)
    check(name + "_builds", step["rc"] == 0,
          "rc=%s err=%r" % (step["rc"], step["err"][:400]))
    if step["rc"] != 0:
        return
    check(name + "_star_outside_comma", "*(cplus_check_nonnull(" in c_text,
          "expected `*(cplus_check_nonnull(...), p)` in emitted C")
    check(name + "_no_rvalue_deref", ", *p)" not in c_text,
          "emitted C still contains the rvalue `(check, *p)` form")
    r = run([exe])
    check(name + "_runtime_lvalue_write", r["rc"] == 0,
          "rc=%s out=%r err=%r" % (r["rc"], r["out"], r["err"][:300]))


def test_compound_literals(td):
    name = "issue232_compound_literals"
    step, c_text, exe = compile_case(td, name, COMPOUND_LITERALS)
    check(name + "_builds", step["rc"] == 0,
          "rc=%s err=%r" % (step["rc"], step["err"][:400]))
    if step["rc"] != 0:
        return
    for op in ("add", "sub", "mul"):
        check(name + "_instrumented_" + op,
              "CPLUS_CMPD_BIN(" + op in c_text,
              "no CPLUS_CMPD_BIN(%s ...) in emitted C" % op)
    check(name + "_no_bare_literal_rhs",
          "x += 1" not in c_text and "x -= 1" not in c_text,
          "literal-RHS compound assignment left uninstrumented")
    check(name + "_no_duplicate_guard",
          "CPLUS_CMPD_BIN(add, CPLUS_CMPD_BIN" not in c_text,
          "two CPLUS_CMPD_BIN wrappers emitted for one operator")
    r = run([exe])
    check(name + "_runtime",
          r["rc"] == 0 and r["out"] == "X=5\n",
          "rc=%s out=%r err=%r" % (r["rc"], r["out"], r["err"][:300]))


def test_compound_trap(td, name, source, macro, diagnostic):
    step, c_text, exe = compile_case(td, name, source)
    check(name + "_builds", step["rc"] == 0,
          "rc=%s err=%r" % (step["rc"], step["err"][:400]))
    if step["rc"] != 0:
        return
    check(name + "_instrumented", macro in c_text,
          "%s not found in emitted C" % macro)
    r = run([exe])
    check(name + "_runtime_trap",
          r["rc"] != 0 and diagnostic in (r["out"] + r["err"]),
          "rc=%s out=%r err=%r" % (r["rc"], r["out"], r["err"][:300]))


def test_for_increment(td):
    name = "issue232_for_increment"
    step, c_text, exe = compile_case(td, name, FOR_INCREMENT)
    check(name + "_builds", step["rc"] == 0,
          "rc=%s err=%r" % (step["rc"], step["err"][:400]))
    if step["rc"] != 0:
        return
    n = c_text.count("CPLUS_CMPD_BIN(add")
    check(name + "_one_guard_per_site", n == 2,
          "CPLUS_CMPD_BIN(add count=%d (expected 2)" % n)
    check(name + "_no_duplicate_guard",
          "CPLUS_CMPD_BIN(add, CPLUS_CMPD_BIN" not in c_text,
          "for-loop increment got two nested guards")
    r = run([exe])
    check(name + "_runtime",
          r["rc"] == 0 and r["out"] == "S=3\n",
          "rc=%s out=%r err=%r" % (r["rc"], r["out"], r["err"][:300]))


def test_for_increment_symbolic(td):
    name = "issue232_for_increment_symbolic"
    step, c_text, exe = compile_case(td, name, FOR_INCREMENT_SYMBOLIC)
    check(name + "_builds", step["rc"] == 0,
          "rc=%s err=%r" % (step["rc"], step["err"][:400]))
    if step["rc"] != 0:
        return
    n = c_text.count("CPLUS_CMPD_BIN(add")
    check(name + "_one_guard_per_site", n == 2,
          "CPLUS_CMPD_BIN(add count=%d (expected 2)" % n)
    check(name + "_no_duplicate_guard",
          "CPLUS_CMPD_BIN(add, CPLUS_CMPD_BIN" not in c_text,
          "for-loop increment got two nested guards")
    r = run([exe])
    check(name + "_runtime",
          r["rc"] == 0 and r["out"] == "S=3\n",
          "rc=%s out=%r err=%r" % (r["rc"], r["out"], r["err"][:300]))


def main():
    if not os.path.exists(BIN):
        print("missing c+ binary at %s" % BIN, file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="cplus_rewriter_231_232_") as td:
        test_numelts_postfix(td)
        test_numelts_call(td)
        test_notnull_star(td)
        test_compound_literals(td)
        test_compound_trap(td, "issue232_compound_div_zero",
                           COMPOUND_DIV_ZERO, "CPLUS_CMPD_BIN(div",
                           "division by zero")
        test_compound_trap(td, "issue232_compound_shift",
                           COMPOUND_SHIFT, "CPLUS_CMPD_BIN(shl",
                           "invalid shift")
        test_for_increment(td)
        test_for_increment_symbolic(td)
    print("\nSummary: %d passed, %d failed" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())

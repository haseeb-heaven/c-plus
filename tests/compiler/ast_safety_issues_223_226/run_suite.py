#!/usr/bin/env python3
"""Regressions for AstSafetyCheck.cpp issues #223, #224, #225 and #226.

Every case is checked with `c+ check --sanitize=none --no-werror` only --
no build, no run, no network. Each case declares whether the checker must
reject it, and (where useful) the exact diagnostic fragment that must appear.
A pos_* case must compile cleanly, which also proves the compiler did not
crash (issue #226).

Usage:
    CPLUS_BIN=/path/to/c+ python3 tests/compiler/ast_safety_issues_223_226/run_suite.py
    CPLUS_BIN=/path/to/c+ python3 tests/compiler/ast_safety_issues_223_226/run_suite.py issue223_neg_branch_uaf
"""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
HERE = os.path.dirname(os.path.abspath(__file__))
CASES_DIR = os.path.join(HERE, "cases")

UAF_DETAIL = ("[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] "
              "use-after-free: dereference of released pointer")
NULL_DETAIL = "[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] null pointer dereference"
DOUBLE_FREE = "[CPLUS-OWNER] double free of"

# (case file, must_reject, required_output_fragment)
CASES = [
    # Issue #223 — release state inside if/else/loops.
    ("issue223_neg_branch_uaf.c", True, UAF_DETAIL + " 'p'"),
    ("issue223_neg_branch_double_free.c", True, DOUBLE_FREE + " 'p'"),
    ("issue223_neg_loop_uaf.c", True, UAF_DETAIL + " 'p'"),
    ("issue223_pos_branch_free_once.c", False, ""),
    ("issue223_pos_branch_free_both_arms.c", False, ""),
    # Issue #224 — keepErase scope and ==/!= distinction.
    ("issue224_neg_other_null_var.c", True, NULL_DETAIL),
    ("issue224_neg_ne_null_guard.c", True, NULL_DETAIL),
    ("issue224_neg_ne_guard_divzero.c", False, "division by zero"),
    ("issue224_pos_eq_guard.c", False, ""),
    # Issue #225 — short-circuit operators and the ternary null test.
    ("issue225_neg_and_negated_lhs.c", True, NULL_DETAIL),
    ("issue225_neg_or_plain_lhs.c", True, NULL_DETAIL),
    ("issue225_neg_ternary_released.c", True, "use-after-free"),
    ("issue225_pos_and_guarded.c", False, ""),
    # Issue #226 — no out-of-bounds getArg() / compiler crash.
    ("issue226_pos_mismatched_fp_call.c", False, ""),
]


def run_case(name, reject, fragment):
    path = os.path.join(CASES_DIR, name)
    result = subprocess.run(
        [BIN, "check", "--sanitize=none", "--no-werror", path],
        capture_output=True, check=False, timeout=60)
    output = (result.stdout + result.stderr).decode(errors="replace")
    ok = result.returncode != 0 if reject else result.returncode == 0
    if fragment:
        ok = ok and fragment in output
    return ok, "rc=%d output=%r" % (result.returncode, output[:600])


def main():
    if not os.path.exists(BIN):
        print("missing c+ binary at " + BIN, file=sys.stderr)
        return 2
    selected = sys.argv[1:]
    cases = [case for case in CASES if not selected or
             case[0] in selected or case[0][:-2] in selected]
    if not cases:
        print("unknown case(s): " + ", ".join(selected), file=sys.stderr)
        return 2
    passed = failed = 0
    for name, reject, fragment in cases:
        ok, detail = run_case(name, reject, fragment)
        if ok:
            passed += 1
            print("ok   %s" % name)
        else:
            failed += 1
            print("FAIL %s: %s" % (name, detail))
    print("\nast_safety_issues_223_226: %d cases, %d passed, %d failed" %
          (len(cases), passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

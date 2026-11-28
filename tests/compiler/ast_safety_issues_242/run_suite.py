#!/usr/bin/env python3
"""Deterministic regressions for AstSafetyCheck issue #242.

Every case is checked with `c+ check --strict --sanitize=none` only: no
build, no run, no network. Negative cases require the analyzer's own
diagnostic substring (and, where useful, a minimum occurrence count) so an
unrelated backend warning cannot make a case pass.

Usage:
    CPLUS_BIN=/path/to/c+ python3 tests/compiler/ast_safety_issues_242/run_suite.py
    CPLUS_BIN=/path/to/c+ python3 tests/compiler/ast_safety_issues_242/run_suite.py issue242_neg_truthy_divzero
"""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
HERE = os.path.dirname(os.path.abspath(__file__))
CASES_DIR = os.path.join(HERE, "cases")

NULL_DETAIL = "[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] null pointer dereference"
DIV_DETAIL = "division by zero cannot be excluded"

# (case file, must_reject, required_output_fragment, minimum occurrences)
CASES = [
    # Inverted truthy-guard polarity on the fall-through path.
    ("issue242_neg_truthy_divzero.c", True, DIV_DETAIL, 1),
    ("issue242_pos_zero_eq_divzero.c", False, "", 0),
    ("issue242_pos_truthy_then_divzero.c", False, "", 0),
    ("issue242_pos_truthy_else_returns.c", False, "", 0),
    # `if (!p)` must keep p null inside the then arm.
    ("issue242_neg_null_then_deref.c", True, NULL_DETAIL, 2),
    ("issue242_pos_negated_early_return.c", False, "", 0),
    # `0 + 0` is not a provably non-zero loop expression.
    ("issue242_neg_loop_zero_offset.c", True, DIV_DETAIL, 1),
    ("issue242_pos_loop_positive_offset.c", False, "", 0),
]


def run_case(name, reject, fragment, count):
    path = os.path.join(CASES_DIR, name)
    result = subprocess.run(
        [BIN, "check", "--strict", "--sanitize=none", path],
        capture_output=True, check=False, timeout=60)
    output = (result.stdout + result.stderr).decode(errors="replace")
    ok = result.returncode != 0 if reject else result.returncode == 0
    if fragment:
        ok = ok and output.count(fragment) >= count
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
    for name, reject, fragment, count in cases:
        ok, detail = run_case(name, reject, fragment, count)
        if ok:
            passed += 1
            print("ok   %s" % name)
        else:
            failed += 1
            print("FAIL %s: %s" % (name, detail))
    print("\nast_safety_issues_242: %d cases, %d passed, %d failed" %
          (len(cases), passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())


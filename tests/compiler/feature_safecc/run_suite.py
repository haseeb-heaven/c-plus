#!/usr/bin/env python3
"""Deterministic regressions for issue #180 (SafeCC-style CFG dataflow).

Every case is checked with `c+ check --sanitize=none` only -- no build, no
run, no network, no game assets.  A `neg_*` case must be rejected with the
asserted `[CPLUS-SAFECC]` diagnostic; a `pos_*` case must compile cleanly.

Usage:
    CPLUS_BIN=/path/to/c+ python3 tests/compiler/feature_safecc/run_suite.py
    CPLUS_BIN=/path/to/c+ python3 tests/compiler/feature_safecc/run_suite.py neg_leak_single_exit
"""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
HERE = os.path.dirname(os.path.abspath(__file__))
CASES_DIR = os.path.join(HERE, "cases")

NULL_DIAG = "[CPLUS-SAFECC] definite null dereference of 'p'"
LEAK_DIAG = "[CPLUS-SAFECC] definite memory leak: allocation held by 'p'"
CFG_LEAK_DIAG = ("[CPLUS-CFG] conditional memory leak: allocation held by 'p'")
CFG_ESCAPE_DIAG = (
    "[CPLUS-CFG] address of stack memory associated with local variable 'x' "
    "escapes through pointer 'p'")
CFG_LOCK_DIAG = ("[CPLUS-CFG] conditional lock leak: mutex 'm' is held on "
                 "some exit path")

# (case file, should_reject, required diagnostic fragment)
CASES = [
    ("neg_null_deref_star.c", True, NULL_DIAG),
    ("neg_null_deref_arrow.c", True, NULL_DIAG),
    ("neg_null_deref_arrow_join.c", True, NULL_DIAG),
    ("neg_null_deref_join.c", True, NULL_DIAG),
    ("neg_null_deref_loop.c", True, NULL_DIAG),
    ("pos_sizeof_star_no_eval.c", False, ""),
    ("pos_sizeof_subscript_no_eval.c", False, ""),
    ("pos_sizeof_arrow_no_eval.c", False, ""),
    ("neg_leak_single_exit.c", True, LEAK_DIAG),
    ("neg_leak_all_exits.c", True, LEAK_DIAG),
    ("neg_leak_loop_exit.c", True, LEAK_DIAG),
    ("pos_null_guard_early_return.c", False, ""),
    ("pos_null_guard_conjunction.c", False, ""),
    ("pos_may_null_no_report.c", False, ""),
    ("pos_leak_freed.c", False, ""),
    ("pos_leak_conditional_free.c", False, ""),
    ("pos_leak_return_transfer.c", False, ""),
    ("pos_leak_goto_cleanup.c", False, ""),
    # Issue #180 — clang::CFG forward/backward dataflow in AstSafetyCheck.cpp.
    ("pos_double_free_exclusive_goto.c", False, ""),
    ("neg_leak_conditional.c", True, CFG_LEAK_DIAG),
    ("neg_escape_inner_block.c", True, CFG_ESCAPE_DIAG),
    ("neg_lock_conditional_leak.c", True, CFG_LOCK_DIAG),
]


def run_case(name, reject, diagnostic):
    path = os.path.join(CASES_DIR, name)
    result = subprocess.run(
        [BIN, "check", "--sanitize=none", "--no-werror", path],
        capture_output=True, check=False, timeout=60)
    output = (result.stdout + result.stderr).decode(errors="replace")
    ok = result.returncode != 0 if reject else result.returncode == 0
    if reject and diagnostic:
        # No legacy-diagnostic fallback: a neg_* case must be rejected by the
        # [CPLUS-SAFECC] pass itself, not by the older lifetime checker.
        ok = ok and diagnostic in output
    return ok, "rc=%d output=%r" % (result.returncode, output[:600])


def main():
    if not os.path.exists(BIN):
        print("missing c+ binary at " + BIN, file=sys.stderr)
        return 2
    selected = sys.argv[1:]
    cases = [case for case in CASES if not selected or
             case[0] in selected or case[0][:-2] in selected]
    if len(selected) and not cases:
        print("unknown case(s): " + ", ".join(selected), file=sys.stderr)
        return 2
    passed = failed = 0
    for name, reject, diagnostic in cases:
        ok, detail = run_case(name, reject, diagnostic)
        if ok:
            passed += 1
            print("ok   %s" % name)
        else:
            failed += 1
            print("FAIL %s: %s" % (name, detail))
    print("\nfeature_safecc: %d cases, %d passed, %d failed" %
          (len(cases), passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

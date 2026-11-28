#!/usr/bin/env python3
"""Regressions for issue #243 in compiler/Sema/AstSafetyCheck.cpp.

Three independent escapes/OOB gaps are covered:

  * a stack address assigned to a pointer local / struct field after
    initialization (previously only initializers were tracked);
  * out-of-bounds constant subscripts on pointer arrays (T **);
  * negative constant indices into a heap flexible array member.

Every case is checked only with `c+ check --strict --sanitize=none`; no
build, no run, no network. Negative cases must be rejected with the stated
diagnostic fragment; positive controls must check cleanly.

Usage:
    CPLUS_BIN=/path/to/c+ python3 tests/compiler/issue_243/run_suite.py
    CPLUS_BIN=/path/to/c+ python3 tests/compiler/issue_243/run_suite.py issue243_neg_escape_assign
"""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
HERE = os.path.dirname(os.path.abspath(__file__))
CASES_DIR = os.path.join(HERE, "cases")

# (case file, must_reject, required_output_fragment)
CASES = [
    ("issue243_neg_escape_assign.c", True,
     "[CPLUS-BORROW] borrow 'p' of local 'local' cannot escape"),
    ("issue243_neg_struct_field_assign.c", True,
     "[CPLUS-LIFETIME] struct result contains address of local"),
    ("issue243_neg_heap_ptr_array_write.c", True,
     "array index 100 exceeds heap allocation 'arr'"),
    ("issue243_neg_fam_negative.c", True,
     "negative flexible array index on heap allocation"),
    ("issue243_pos_heap_ptr_array_in_bounds.c", False, ""),
    ("issue243_pos_escape_rebound.c", False, ""),
    ("issue243_pos_struct_field_global.c", False, ""),
]


def run_case(name, reject, fragment):
    path = os.path.join(CASES_DIR, name)
    result = subprocess.run(
        [BIN, "check", "--strict", "--sanitize=none", path],
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
    print("\nissue_243: %d cases, %d passed, %d failed" %
          (len(cases), passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

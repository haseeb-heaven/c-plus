#!/bin/sh
# run.sh -- bounded PR #33 gate-framework self-check.
#
# Usage: sh tests/pr33_integration/run.sh
#
# Runs the validators against their example fixtures, including negative
# controls that prove each validator REJECTS bad input. The real cross-branch
# gate is opt-in (PR33_RUN_COMBINED_GATE=1) and is otherwise reported as
# NOT_TESTED: it must be run by a human/independent verifier before merge.
#
# POSIX sh only; no Python, no `|| true`. Continues after failures and exits
# nonzero if any real check fails. Final line:
#   PR33_GATE <checks> <pass> <fail> <not_tested>

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

outdir=$(mktemp -d "${TMPDIR:-/tmp}/pr33-run.XXXXXX") || {
    printf 'FAIL run: mktemp -d failed\n' >&2
    exit 1
}
printf 'PR33_LOG_DIR=%s\n' "$outdir"

checks=0
pass=0
fail=0
not_tested=0

record_pass() {
    checks=$((checks + 1)); pass=$((pass + 1));
    printf 'PASS %s: %s\n' "$1" "$2"
}
record_fail() {
    checks=$((checks + 1)); fail=$((fail + 1));
    printf 'FAIL %s: %s\n' "$1" "$2" >&2
}
record_not_tested() {
    checks=$((checks + 1)); not_tested=$((not_tested + 1));
    printf 'NOT_TESTED %s: %s\n' "$1" "$2"
}

# expect_exit <label> <zero|nonzero> <command> [args...]
expect_exit() {
    label=$1
    kind=$2
    shift 2
    log="$outdir/$label.log"
    "$@" > "$log" 2>&1
    rc=$?
    if [ "$kind" = zero ]; then
        if [ "$rc" -eq 0 ]; then
            record_pass "$label" "exit 0 (see $log)"
        else
            record_fail "$label" "expected exit 0, got $rc (see $log)"
        fi
    else
        if [ "$rc" -ne 0 ]; then
            record_pass "$label" "bad input rejected with exit $rc (see $log)"
        else
            record_fail "$label" "bad input was accepted with exit 0 (see $log)"
        fi
    fi
}

check_report="$script_dir/check_report.sh"
check_overlap="$script_dir/check_overlap.sh"
combined_gate="$script_dir/run_combined_gate.sh"

# 1/2: report validator -- positive and negative controls.
expect_exit report_valid_positive zero \
    sh "$check_report" "$script_dir/example_valid_report.txt"
expect_exit report_invalid_negative nonzero \
    sh "$check_report" "$script_dir/example_invalid_report.txt"

# 3: negative control specifically for placeholder detection in an otherwise
# structurally complete report (no missing fields can mask the failure).
placeholder_report="$outdir/placeholder_report.txt"
cat > "$placeholder_report" <<EOF
SCOPE: placeholder negative control
DEPENDENCIES: none
COMMIT_SHA: TBD
CHANGED_PATHS_BEGIN
runtime/cplus_runtime.c
CHANGED_PATHS_END
EXACT_COMMANDS_BEGIN
sh tests/run_tests.sh
EXACT_COMMANDS_END
TEST_OUTPUT_BEGIN
placeholder output
TEST_OUTPUT_END
KNOWN_FAILURES: none
INDEPENDENT_REVIEW: PENDING
EOF
log="$outdir/report_placeholder_negative.log"
sh "$check_report" "$placeholder_report" > "$log" 2>&1
rc=$?
if [ "$rc" -ne 0 ] && grep -q 'placeholder' "$log"; then
    record_pass report_placeholder_negative "placeholder value rejected (see $log)"
else
    record_fail report_placeholder_negative "placeholder was not rejected (see $log)"
fi

# 4: overlap detector -- positive control (disjoint reports).
expect_exit overlap_disjoint_positive zero \
    sh "$check_overlap" \
    "$script_dir/example_valid_report.txt" "$script_dir/example_report_peer.txt"

# 5: overlap detector -- negative control (both path overlap and duplicate
# claim must be reported, not just a generic nonzero exit).
log="$outdir/overlap_conflict_negative.log"
sh "$check_overlap" \
    "$script_dir/example_valid_report.txt" "$script_dir/example_report_conflict.txt" \
    > "$log" 2>&1
rc=$?
if [ "$rc" -ne 0 ] &&
   grep -q 'WARNING OVERLAP path' "$log" &&
   grep -q 'WARNING DUPLICATE CLAIM' "$log"; then
    record_pass overlap_conflict_negative "overlap and duplicate claim both flagged (see $log)"
else
    record_fail overlap_conflict_negative "overlap/duplicate detection incomplete (see $log)"
fi

# 6: combined gate -- negative control for missing/unreachable branches. This
# exercises the NOT_TESTED path without building or merging anything.
log="$outdir/combined_gate_missing_branch_negative.log"
sh "$combined_gate" pr33-no-such-branch-a pr33-no-such-branch-b > "$log" 2>&1
rc=$?
if [ "$rc" -eq 0 ] && grep -q '^NOT_TESTED' "$log"; then
    record_pass combined_gate_missing_branch_negative "missing branches reported NOT_TESTED (see $log)"
else
    record_fail combined_gate_missing_branch_negative "missing branches not handled correctly (see $log)"
fi

# 7: the actual combined gate. OFF by default: no independent review is
# claimed here. A human verifier opts in and records the artifacts.
if [ "${PR33_RUN_COMBINED_GATE:-0}" = 1 ]; then
    log="$outdir/combined_gate_candidates.log"
    a=${PR33_BRANCH_A:-agent/core-safety}
    b=${PR33_BRANCH_B:-agent/modern-stdlib}
    sh "$combined_gate" "$a" "$b" > "$log" 2>&1
    rc=$?
    summary=$(grep '^COMBINED_GATE ' "$log" | sed -n '1p')
    if [ -z "$summary" ]; then
        if grep -q '^NOT_TESTED' "$log"; then
            record_not_tested combined_gate_candidates "candidate branches not testable (see $log)"
        else
            record_fail combined_gate_candidates "no gate summary produced (exit $rc, see $log)"
        fi
    else
        gate_fail=$(printf '%s\n' "$summary" | awk '{ print $4 }')
        gate_nt=$(printf '%s\n' "$summary" | awk '{ print $5 }')
        if [ "$gate_fail" -gt 0 ]; then
            record_fail combined_gate_candidates "$gate_fail suite(s) failed (see $log)"
        elif [ "$gate_nt" -gt 0 ]; then
            record_not_tested combined_gate_candidates "$gate_nt suite(s) NOT_TESTED (see $log)"
        else
            record_pass combined_gate_candidates "combined suites green (see $log)"
        fi
    fi
else
    record_not_tested combined_gate_candidates \
        "opt in with PR33_RUN_COMBINED_GATE=1; a human verifier must run and review it"
fi

printf 'PR33_LOGS=%s\n' "$outdir"
printf 'INDEPENDENT_REVIEW: NOT PERFORMED by this runner\n'
printf 'PR33_GATE %d %d %d %d\n' "$checks" "$pass" "$fail" "$not_tested"

if [ "$fail" -gt 0 ]; then
    exit 1
fi
exit 0

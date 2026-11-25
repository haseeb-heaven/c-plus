#!/bin/sh
# End-to-end coverage of the new CTL / CTL+Goose demo runners (Issue #24).
# Each runner performs deterministic byte-for-byte diffs of expected outputs,
# including the edge cases added on 2026-11-24 (duplicate accumulate, double
# drop, empty containers/scopes, nested scope, bump on missing id).
# Prints COVERAGE_RUNNER <cases> <pass> <fail>; exits nonzero on failure.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
cases=0; pass=0; fail=0

note() {
    cases=$((cases + 1))
    if [ "$1" = 0 ]; then pass=$((pass + 1)); else fail=$((fail + 1)); fi
}

run_runner() {
    sh "$1" >/dev/null 2>&1
    note $?
}

run_runner examples/ctl_demo/run.sh
run_runner examples/goose_memory/run.sh
run_runner benchmarks/goose_fast/run.sh
run_runner tests/ctl/run_ctl_tests.sh
run_runner tests/ctl/run_ctl_span.sh

echo "COVERAGE_RUNNER $cases $pass $fail"
[ "$fail" -eq 0 ]

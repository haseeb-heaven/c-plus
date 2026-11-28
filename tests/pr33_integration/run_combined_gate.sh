#!/bin/sh
# run_combined_gate.sh -- bounded gate over the COMBINED candidate branches.
#
# Usage:
#   sh tests/pr33_integration/run_combined_gate.sh [branchA] [branchB]
#   PR33_GATE_TIMEOUT=180 PR33_GATE_OUT=/path/to/logs \
#       sh tests/pr33_integration/run_combined_gate.sh
#
# Defaults: branchA=agent/core-safety, branchB=agent/modern-stdlib.
#
# Behaviour:
#   * A temporary `git worktree add -b <throwaway> ...` is created from
#     branchA and branchB is merged into it with `git merge --no-commit
#     --no-ff`. Nothing is committed, nothing is merged into develop/main,
#     and the current worktree is never modified.
#   * The combined tree runs: ABI fingerprint compare, libc tests, CRT tests,
#     and libc+safecrt coverage. Every suite is bounded by a timeout.
#   * Missing/unreachable/protected branches produce NOT_TESTED, never a
#     silent pass. A merge conflict or a suite failure is a real FAIL.
#   * Logs are kept in PR33_GATE_OUT (default a TMPDIR directory) after the
#     temporary worktree is removed.
#   * This script does NOT constitute independent review. A human verifier
#     must reproduce the suites and review the negative-test fixtures.
#
# POSIX sh only; no Python, no `|| true`, no writes outside the temp tree and
# the log directory.

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/../.." && pwd)

a_name=${1:-agent/core-safety}
b_name=${2:-agent/modern-stdlib}
timeout_secs=${PR33_GATE_TIMEOUT:-180}

case "$timeout_secs" in
    ''|*[!0-9]*)
        printf 'FAIL combined-gate: PR33_GATE_TIMEOUT must be a positive integer\n' >&2
        exit 2 ;;
esac
if [ "$timeout_secs" -lt 1 ]; then
    printf 'FAIL combined-gate: PR33_GATE_TIMEOUT must be a positive integer\n' >&2
    exit 2
fi

# Never use an integration/release branch as a merge input.
for name in "$a_name" "$b_name"; do
    case "$name" in
        develop|main|master|HEAD)
            printf 'NOT_TESTED combined-gate: refused protected branch %s (no merge into develop/main)\n' "$name"
            exit 0 ;;
    esac
done

if [ "$a_name" = "$b_name" ]; then
    printf 'NOT_TESTED combined-gate: both candidate names are %s (nothing to combine)\n' "$a_name"
    exit 0
fi

resolve_ref() {
    name=$1
    if git -C "$root" rev-parse --verify --quiet "refs/heads/$name^{commit}" >/dev/null 2>&1; then
        printf 'refs/heads/%s' "$name"
        return 0
    fi
    if git -C "$root" rev-parse --verify --quiet "refs/remotes/origin/$name^{commit}" >/dev/null 2>&1; then
        printf 'refs/remotes/origin/%s' "$name"
        return 0
    fi
    return 1
}

a_ref=$(resolve_ref "$a_name")
if [ -z "$a_ref" ]; then
    printf 'NOT_TESTED combined-gate %s: missing or unreachable branch (no local or origin ref)\n' "$a_name"
    exit 0
fi
b_ref=$(resolve_ref "$b_name")
if [ -z "$b_ref" ]; then
    printf 'NOT_TESTED combined-gate %s: missing or unreachable branch (no local or origin ref)\n' "$b_name"
    exit 0
fi

if git -C "$root" merge-base --is-ancestor "$b_ref" "$a_ref" 2>/dev/null; then
    printf 'NOT_TESTED combined-gate: %s is already contained in %s (no independent changes to combine)\n' "$b_name" "$a_name"
    exit 0
fi

outdir=${PR33_GATE_OUT:-"${TMPDIR:-/tmp}/pr33-gate-logs-$$"}
mkdir -p "$outdir" || {
    printf 'FAIL combined-gate: cannot create log directory %s\n' "$outdir" >&2
    exit 1
}
printf 'GATE_ARTIFACT_DIR=%s\n' "$outdir"

tmp_root=${TMPDIR:-/tmp}
work=$(mktemp -d "$tmp_root/pr33-combined.XXXXXX") || {
    printf 'FAIL combined-gate: mktemp -d failed\n' >&2
    exit 1
}
throwaway="pr33-combined-$$"

cleanup() {
    git -C "$root" worktree remove --force "$work" >/dev/null 2>&1
    if [ -d "$work" ]; then rm -rf "$work"; fi
    git -C "$root" branch -D "$throwaway" >/dev/null 2>&1
    return 0
}
trap cleanup 0 1 2 3 15

if ! git -C "$root" worktree add -b "$throwaway" "$work" "$a_ref" >"$outdir/worktree-add.log" 2>&1; then
    printf 'FAIL combined-gate: git worktree add failed (see %s/worktree-add.log)\n' "$outdir" >&2
    exit 1
fi

if ! git -C "$work" merge --no-commit --no-ff "$b_ref" >"$outdir/merge.log" 2>&1; then
    printf 'FAIL combined-gate: merge conflict or merge error combining %s and %s (see %s/merge.log)\n' \
        "$a_name" "$b_name" "$outdir" >&2
    exit 1
fi

checks=0
pass=0
fail=0
not_tested=0
record_pass() {
    checks=$((checks + 1)); pass=$((pass + 1));
    printf 'PASS combined-gate %s\n' "$1"
}
record_fail() {
    checks=$((checks + 1)); fail=$((fail + 1));
    printf 'FAIL combined-gate %s: %s\n' "$1" "$2" >&2
}
record_not_tested() {
    checks=$((checks + 1)); not_tested=$((not_tested + 1));
    printf 'NOT_TESTED combined-gate %s: %s\n' "$1" "$2"
}

# run_bounded <seconds> <logfile> <command> [args...]
# Uses GNU/BSD timeout when available; otherwise a POSIX watchdog subshell.
run_bounded() {
    secs=$1
    logfile=$2
    shift 2
    if command -v timeout >/dev/null 2>&1; then
        ( cd "$work" && timeout "$secs" "$@" ) > "$logfile" 2>&1
    elif command -v gtimeout >/dev/null 2>&1; then
        ( cd "$work" && gtimeout "$secs" "$@" ) > "$logfile" 2>&1
    else
        ( cd "$work" && "$@" ) > "$logfile" 2>&1 &
        child=$!
        ( sleep "$secs"; if kill -0 "$child" 2>/dev/null; then kill "$child" 2>/dev/null; fi ) >/dev/null 2>&1 &
        watcher=$!
        wait "$child"
        rc=$?
        if kill -0 "$watcher" 2>/dev/null; then kill "$watcher" 2>/dev/null; fi
        wait "$watcher" 2>/dev/null
        return "$rc"
    fi
}

# Build the libraries the ABI/symbol suites inspect. Bounded like any suite.
run_bounded "$timeout_secs" "$outdir/prepare.log" \
    sh -c 'sh tests/libc/build_libc.sh && sh scripts/build_safecrt.sh'
prepare_rc=$?
if [ "$prepare_rc" -eq 0 ]; then
    record_pass prepare
elif [ "$prepare_rc" -eq 124 ]; then
    record_fail prepare "timeout after ${timeout_secs}s (see $outdir/prepare.log)"
else
    record_fail prepare "exit $prepare_rc (see $outdir/prepare.log)"
fi

have_clang=0
if [ -x /usr/bin/clang ] || command -v clang >/dev/null 2>&1; then have_clang=1; fi
have_cov=0
if command -v llvm-cov >/dev/null 2>&1 && command -v llvm-profdata >/dev/null 2>&1; then
    have_cov=1
elif command -v xcrun >/dev/null 2>&1; then
    if xcrun --find llvm-cov >/dev/null 2>&1 && xcrun --find llvm-profdata >/dev/null 2>&1; then
        have_cov=1
    fi
fi

run_suite() {
    label=$1
    command_text=$2
    logfile="$outdir/$label.log"
    run_bounded "$timeout_secs" "$logfile" sh -c "$command_text"
    rc=$?
    if [ "$rc" -eq 0 ]; then
        record_pass "$label"
    elif [ "$rc" -eq 124 ]; then
        record_fail "$label" "timeout after ${timeout_secs}s (see $logfile)"
    else
        record_fail "$label" "exit $rc (see $logfile)"
    fi
}

skip_or_run() {
    label=$1
    available=$2
    command_text=$3
    if [ "$prepare_rc" -ne 0 ]; then
        record_not_tested "$label" "prepare step failed (see $outdir/prepare.log)"
    elif [ "$available" -eq 0 ]; then
        record_not_tested "$label" "required toolchain unavailable"
    else
        run_suite "$label" "$command_text"
    fi
}

skip_or_run abi_fingerprint "$have_clang" \
    'sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt'
skip_or_run libc_tests "$have_clang" \
    'sh tests/libc/run_libc_tests.sh'
skip_or_run crt_tests "$have_clang" \
    'sh tests/crt/run_all_crt.sh'
skip_or_run coverage "$have_cov" \
    'sh tests/libc/coverage_libc.sh && sh tests/crt/coverage_safecrt.sh'

printf 'COMBINED_GATE %d %d %d %d\n' "$checks" "$pass" "$fail" "$not_tested"
printf 'INDEPENDENT_REVIEW: NOT PERFORMED - a human verifier must reproduce the suites and review negative fixtures\n'
printf 'ARTIFACTS: %s\n' "$outdir"

if [ "$fail" -gt 0 ]; then
    exit 1
fi
exit 0

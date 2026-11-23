#!/bin/sh
# Aggregate runner for the per-method suites under tests/stdlib/method_tests/.
#
# Usage (from anywhere):
#   sh tests/stdlib/method_tests/run_all.sh
#
# Discovery contract
# ------------------
# A "group" is an immediate subdirectory of tests/stdlib/method_tests/ that
# contains a RUN_NOTES.md.  Each group must provide a compare/run shell script
# whose LAST METHOD_TEST line has exactly this shape:
#
#   METHOD_TEST <group> <total> <passed> <failed> <skipped>
#
# The run script is selected, in order, by:
#   1. one of the conventional names run.sh, compare.sh, run_tests.sh,
#      compare_run.sh, run_all.sh, check.sh;
#   2. the single top-level *.sh whose name does not start with '_';
#   3. the first top-level *.sh (sorted) whose name starts with "run" or
#      "compare"; otherwise the first sorted *.sh not starting with '_'.
# Helper scripts whose names start with '_' are never selected.
#
# Every selected script runs under `timeout 300` with a restricted PATH and
# CPLUS_CLANG=/usr/bin/clang.  A group is a HARD FAILURE if its script is
# missing, times out, exits nonzero, or does not end in a well-formed
# METHOD_TEST line whose group field matches the directory name.  No failure
# is masked with `|| true`; the exit status is captured and acted upon.
# SKIPPED methods are reported separately and are NOT counted as passes.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd) || exit 1
cd "$ROOT" || exit 1

# Deterministic toolchain: no Homebrew/nix/user bins, system clang only.
# $ROOT/build is included solely so a bare `c+` resolves to the local build.
PATH="$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin"
export PATH
CPLUS_CLANG=/usr/bin/clang
export CPLUS_CLANG
if [ -x "$ROOT/build/c+" ]; then
    CPLUS_BIN="$ROOT/build/c+"
    export CPLUS_BIN
fi

HERE="$ROOT/tests/stdlib/method_tests"
LIMIT=300
LOG_DIR=${TMPDIR:-/tmp}/cplus-method-tests-$$
mkdir -p "$LOG_DIR" || exit 1

printf '[run_all] root=%s\n' "$ROOT" >&2
printf '[run_all] PATH=%s\n' "$PATH" >&2
printf '[run_all] CPLUS_CLANG=%s\n' "$CPLUS_CLANG" >&2
printf '[run_all] logs=%s\n' "$LOG_DIR" >&2

is_num() {
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
    esac
    return 0
}

# Echo exactly one script path for a group (empty string if none).
pick_script() { # group_dir
    _g=$1
    for _n in run.sh compare.sh run_tests.sh compare_run.sh run_all.sh check.sh; do
        if [ -f "$_g/$_n" ]; then
            printf '%s\n' "$_g/$_n"
            return 0
        fi
    done
    _cands=$(find "$_g" -maxdepth 1 -type f -name '*.sh' ! -name '_*' -print \
             | LC_ALL=C sort)
    [ -n "$_cands" ] || return 0
    _count=$(printf '%s\n' "$_cands" | wc -l | tr -d ' ')
    if [ "$_count" -eq 1 ]; then
        printf '%s\n' "$_cands"
        return 0
    fi
    _pref=$(printf '%s\n' "$_cands" \
            | grep -E '/(run|compare)[^/]*\.sh$' | sed -n '1p')
    if [ -n "$_pref" ]; then
        printf '%s\n' "$_pref"
    else
        printf '%s\n' "$_cands" | sed -n '1p'
    fi
    return 0
}

TOT_TOTAL=0
TOT_PASS=0
TOT_FAIL=0
TOT_SKIP=0
NGROUP_NOTES=0
HARD_FAIL=0
ROWS=""
BAD=""

# Discover groups by RUN_NOTES.md presence; sort for deterministic output.
GROUP_NOTES=$(find "$HERE" -mindepth 2 -maxdepth 2 -type f -name RUN_NOTES.md -print \
         | LC_ALL=C sort)

if [ -z "$GROUP_NOTES" ]; then
    printf '[run_all] ERROR: no <group>/RUN_NOTES.md found under %s\n' "$HERE" >&2
    printf '[run_all] aggregate: FAIL (no groups discovered)\n' >&2
    exit 1
fi

printf '%s\n' "$GROUP_NOTES" | while IFS= read -r notes; do
    group_dir=$(dirname "$notes")
    group=$(basename "$group_dir")
    script=$(pick_script "$group_dir")

    if [ -z "$script" ]; then
        printf '[run_all] %-16s HARD FAIL: no run/compare script\n' "$group" >&2
        # Emit a marker row; the outer shell aggregates via the log files.
        printf '%s|0|0|0|0|%s\n' "$group" "missing run script" >"$LOG_DIR/$group.row"
        continue
    fi

    printf '[run_all] %-16s running %s (timeout %ss)\n' \
        "$group" "${script#$ROOT/}" "$LIMIT" >&2
    out="$LOG_DIR/$group.stdout"
    err="$LOG_DIR/$group.stderr"
    rc=0
    timeout "$LIMIT" /bin/sh "$script" >"$out" 2>"$err" || rc=$?
    if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
        printf '[run_all] %-16s HARD FAIL: timeout/exit %s\n' "$group" "$rc" >&2
        printf '%s|0|0|0|0|%s\n' "$group" "timeout/exit $rc" >"$LOG_DIR/$group.row"
        continue
    fi
    if [ "$rc" -ne 0 ]; then
        printf '[run_all] %-16s HARD FAIL: script exit %s\n' "$group" "$rc" >&2
        printf '%s|0|0|0|0|%s\n' "$group" "script exit $rc" >"$LOG_DIR/$group.row"
        continue
    fi

    # The result line is the final METHOD_TEST line in the captured stdout.
    line=$(grep -E '^METHOD_TEST[[:space:]]' "$out" | tail -n 1)
    if [ -z "$line" ]; then
        printf '[run_all] %-16s HARD FAIL: no METHOD_TEST line (see %s)\n' \
            "$group" "$out" >&2
        printf '%s|0|0|0|0|%s\n' "$group" "missing METHOD_TEST line" \
            >"$LOG_DIR/$group.row"
        continue
    fi
    printf '[run_all] %-16s %s\n' "$group" "$line" >&2

    # shellcheck disable=SC2086 -- intentional word split on the fixed line
    set -- $line
    reason=""
    if [ "$#" -ne 6 ] || [ "$1" != "METHOD_TEST" ] || [ "$2" != "$group" ] ||
       ! is_num "$3" || ! is_num "$4" || ! is_num "$5" || ! is_num "$6"; then
        reason="malformed METHOD_TEST line: $line"
    elif [ "$4" -gt "$3" ] || [ "$5" -gt "$3" ] ||
         [ $(( $4 + $5 + $6 )) -gt "$3" ]; then
        reason="counts inconsistent: $line"
    fi
    if [ -n "$reason" ]; then
        printf '[run_all] %-16s HARD FAIL: %s\n' "$group" "$reason" >&2
        printf '%s|0|0|0|0|%s\n' "$group" "$reason" >"$LOG_DIR/$group.row"
        continue
    fi
    printf '%s|%s|%s|%s|%s|\n' "$group" "$3" "$4" "$5" "$6" \
        >"$LOG_DIR/$group.row"
done

# Aggregate the per-group rows emitted above.
for row in "$LOG_DIR"/*.row; do
    [ -f "$row" ] || continue
    IFS='|' read -r g t p f s why <"$row"
    NGROUP_NOTES=$((NGROUP_NOTES + 1))
    TOT_TOTAL=$((TOT_TOTAL + t))
    TOT_PASS=$((TOT_PASS + p))
    TOT_FAIL=$((TOT_FAIL + f))
    TOT_SKIP=$((TOT_SKIP + s))
    if [ -n "$why" ]; then
        HARD_FAIL=1
        BAD="$BAD\n  - $g: $why"
    elif [ "$f" -gt 0 ]; then
        HARD_FAIL=1
        BAD="$BAD\n  - $g: $f method(s) failed"
    fi
    ROWS="$ROWS| $g | $t | $p | $f | $s |\n"
done

printf '\n'
printf '| group | total methods | passed | failed | skipped |\n'
printf '|---|---:|---:|---:|---:|\n'
printf '%b' "$ROWS"
printf '| **TOTAL (%s groups)** | **%s** | **%s** | **%s** | **%s** |\n' \
    "$NGROUP_NOTES" "$TOT_TOTAL" "$TOT_PASS" "$TOT_FAIL" "$TOT_SKIP"
printf '\n'
printf 'Note: SKIPPED methods are NOT passes; each needs an explicit reason in its group RUN_NOTES.md.\n'

if [ "$HARD_FAIL" -ne 0 ]; then
    printf '\naggregate: FAIL%b\n' "$BAD" >&2
    exit 1
fi
printf 'aggregate: ok (%s methods passed, %s skipped)\n' \
    "$TOT_PASS" "$TOT_SKIP" >&2
exit 0

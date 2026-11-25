#!/bin/sh
# Build and exercise the deterministic token-bucket rate limiter.
#
# Prints exactly one summary line:
#   SECURE_RATELIMIT <scenarios> <passed> <failed> <refused>
#
# "refused" counts scenarios that intentionally trigger a REFUSED path (these
# are expected passes, not failures).  Exit status is 0 only when every
# scenario matched its expected exit code AND its exact stdout transcript.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
SRC=examples/secure_apps/ratelimit/ratelimit.cp

BIN=$(mktemp "${TMPDIR:-/tmp}/ratelimit.XXXXXX") || exit 1
trap 'rm -f "$BIN"' EXIT HUP INT TERM

cd "$ROOT" || exit 1

if ! timeout 30 "$CPLUS" build "$SRC" --strict --sanitize=none -I. -o "$BIN" \
        >/dev/null 2>&1; then
    echo "run.sh: BUILD FAILED ($SRC)" >&2
    echo "SECURE_RATELIMIT 0 0 0 0"
    exit 1
fi

scenarios=0
passed=0
failed=0
refused=0

# check <name> <expected_rc> <expected_stdout> [args...]
check() {
    name=$1
    exp_rc=$2
    exp_out=$3
    shift 3
    scenarios=$((scenarios + 1))
    case "$exp_out" in
        REFUSED\ *) refused=$((refused + 1)) ;;
    esac
    out=$(timeout 30 "$BIN" "$@" 2>&1)
    rc=$?
    if [ "$rc" -eq "$exp_rc" ] && [ "$out" = "$exp_out" ]; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
        echo "FAIL $name: rc=$rc (expected $exp_rc)" >&2
        echo "  expected: $(printf '%s' "$exp_out" | tr '\n' '|')" >&2
        echo "  actual:   $(printf '%s' "$out" | tr '\n' '|')" >&2
    fi
}

# check_det <name> <expected_rc> <expected_stdout> [args...]
# Like check, but also re-runs the invocation and requires a byte-identical
# transcript both times (an explicit determinism assertion; no wall clock).
# Counts as one scenario with one pass/fail so the summary stays consistent.
check_det() {
    name=$1
    exp_rc=$2
    exp_out=$3
    shift 3
    scenarios=$((scenarios + 1))
    out1=$(timeout 30 "$BIN" "$@" 2>&1)
    rc1=$?
    out2=$(timeout 30 "$BIN" "$@" 2>&1)
    rc2=$?
    if [ "$rc1" -eq "$exp_rc" ] && [ "$rc2" -eq "$exp_rc" ] &&
       [ "$out1" = "$exp_out" ] && [ "$out2" = "$exp_out" ]; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
        echo "FAIL $name: rc1=$rc1 rc2=$rc2 (expected $exp_rc) or non-deterministic" >&2
    fi
}

EXP_BASIC='SECURE_RL_CONFIG 3 1 4
SECURE_RL_REQ 0 0 3 3 ALLOW 0
SECURE_RL_REQ 1 0 1 0 DENY 0
SECURE_RL_REQ 2 1 1 1 ALLOW 0
SECURE_RL_REQ 3 5 1 3 ALLOW 2
SECURE_RL_DONE 3 1 2'
check_det basic_allow_deny 0 "$EXP_BASIC" 3 1 4 0 3 0 1 1 1 5 1

EXP_EMPTY='SECURE_RL_CONFIG 5 2 0
SECURE_RL_DONE 0 0 5'
check empty_sequence 0 "$EXP_EMPTY" 5 2 0

EXP_CAP='SECURE_RL_CONFIG 5 2 5
SECURE_RL_REQ 0 0 3 5 ALLOW 2
SECURE_RL_REQ 1 1 3 4 ALLOW 1
SECURE_RL_REQ 2 2 3 3 ALLOW 0
SECURE_RL_REQ 3 5 3 5 ALLOW 2
SECURE_RL_REQ 4 10 1 5 ALLOW 4
SECURE_RL_DONE 5 0 4'
check refill_cap 0 "$EXP_CAP" 5 2 5 0 3 1 3 2 3 5 3 10 1

EXP_BOUND='SECURE_RL_CONFIG 1000000 1000000 1
SECURE_RL_REQ 0 0 1000000 1000000 ALLOW 0
SECURE_RL_DONE 1 0 0'
check boundary_max 0 "$EXP_BOUND" 1000000 1000000 1 0 1000000

MAXT=9223372036854775807

check refuse_args_few       1 'REFUSED 1 ARGS need capacity refill n [tick cost]...' 5 2
check refuse_args_pairs     1 'REFUSED 1 ARGS tick/cost pair count mismatch' 5 2 2 0 1
check refuse_parse          2 'REFUSED 2 PARSE capacity' abc 2 1 0 1
check refuse_capacity_zero  3 'REFUSED 3 CAPACITY out of range' 0 2 1 0 1
check refuse_capacity_big   3 'REFUSED 3 CAPACITY out of range' 1000001 2 1 0 1
check refuse_refill_zero    4 'REFUSED 4 REFILL out of range' 5 0 1 0 1
check refuse_refill_big     4 'REFUSED 4 REFILL out of range' 5 1000001 1 0 1
check refuse_count_neg      5 'REFUSED 5 COUNT out of range' 5 2 -1
check refuse_count_big      5 'REFUSED 5 COUNT out of range' 5 2 65
check refuse_tick_neg       6 'REFUSED 6 TICK negative tick' 5 2 1 -1 1
check refuse_tick_order     6 'REFUSED 6 TICK non-monotonic tick' 5 2 2 5 1 4 1
check refuse_cost_zero      7 'REFUSED 7 COST out of range' 5 2 1 0 0
check refuse_cost_big       7 'REFUSED 7 COST out of range' 5 2 1 0 6
check refuse_overflow_mul   8 'REFUSED 8 OVERFLOW refill multiply' 5 2 1 "$MAXT" 1
check refuse_overflow_add   8 'REFUSED 8 OVERFLOW bucket add' 5 1 1 "$MAXT" 1

echo "SECURE_RATELIMIT $scenarios $passed $failed $refused"
[ "$failed" -eq 0 ] || exit 1
exit 0

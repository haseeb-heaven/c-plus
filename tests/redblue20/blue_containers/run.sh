#!/usr/bin/env bash
# Reproduce the BLUE20 containers safety proof from a clean checkout.
#
# METHOD: one fixed-seed case per forked process, bounded by `timeout 60`.
# A crash, a runtime safety fatal, a hang (timeout), or a missing PASS line is
# counted as a failure.  The C+ program also embeds an equivalent fork()+60s
# harness (run it with no argument) as an independent cross-check.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
cd "$ROOT"

CASES=2400
BIN="$DIR/proof"
RESULTS="$DIR/results"
LOG="$RESULTS/cases.log"
ERR="$RESULTS/cases.err"

mkdir -p "$RESULTS"
: > "$LOG"
: > "$ERR"

echo "+ ./build/c+ build $DIR/proof.cp --default --sanitize=none -I. -o $BIN"
./build/c+ build "$DIR/proof.cp" --default --sanitize=none -I. -o "$BIN"

passed=0
failed=0
echo "+ fork-per-case: $CASES children, timeout 60 each"
for ((id = 0; id < CASES; id++)); do
    set +e
    out="$(timeout 60 "$BIN" "$id" 2>>"$ERR")"
    rc=$?
    set -e
    printf '%s\n' "$out" >> "$LOG"
    if [ "$rc" -eq 0 ] && [[ "$out" == *" PASS "* ]]; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
        printf 'FAILED case %d rc=%d out=%s\n' "$id" "$rc" "$out" >&2
    fi
done

SUMMARY="BLUE20 containers $CASES $passed $failed"
printf '%s\n' "$SUMMARY" | tee "$RESULTS/summary.txt"

echo "case_lines=$(grep -c '^BLK ' "$LOG" || true)"
echo "pass_lines=$(grep -c '^BLK .* PASS ' "$LOG" || true)"
echo "fail_lines=$(grep -c '^BLK .* FAIL ' "$LOG" || true)"

# Independent cross-check: the in-program fork()+60s harness.
set +e
checkout="$(timeout 3600 "$BIN" 2>>"$ERR" | grep '^BLUE20 containers ' | tail -1)"
checkrc=$?
set -e
echo "inprogram_crosscheck=$checkout"
printf '%s\n' "$checkout" > "$RESULTS/crosscheck.txt"

if [ "$failed" -ne 0 ] || [ "$checkout" != "$SUMMARY" ]; then
    echo "PROOF FAILED"
    exit 1
fi
echo "PROOF OK"
exit 0

#!/usr/bin/env bash
# RED-BLUE20 agent 1/20 (ATTACKER): modern allocator red-team runner.
# Creates/modifies ONLY files under tests/redblue20/red_alloc/.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="$HERE/out"
ARCHIVE="$ROOT/build/libc/libcplus_hardened.a"
BIN="$OUT/harness"
mkdir -p "$OUT"

echo "[red_alloc] root=$ROOT"
echo "[red_alloc] archive=$ARCHIVE"
if [[ ! -f "$ARCHIVE" ]]; then
    echo "[red_alloc] ERROR: missing $ARCHIVE" >&2
    exit 2
fi

rm -f "$BIN" "$OUT/build.stderr"
timeout 60 "$ROOT/build/c+" build "$HERE/harness.cp" \
    --default --no-werror --sanitize=none -I"$ROOT" \
    -o "$BIN" -- "$ARCHIVE" 2> "$OUT/build.stderr" || {
        cat "$OUT/build.stderr" >&2
        exit 2
    }
echo "[red_alloc] build ok (stderr bytes: $(wc -c < "$OUT/build.stderr"))"

ulimit -c 0
set +e
timeout 60 "$BIN" 2> "$OUT/harness.stderr" | tee "$OUT/harness.stdout"
RC=${PIPESTATUS[0]}
set -e
echo "[red_alloc] harness exit code: $RC"
if [[ -s "$OUT/harness.stderr" ]]; then
    echo "[red_alloc] harness stderr:" >&2
    cat "$OUT/harness.stderr" >&2
fi

echo "[red_alloc] non-REFUSED verdicts (first 120):"
awk '$0 ~ /(CRASH|CORRUPT|UNEXPECTED_OK)/' "$OUT/scenarios.log" \
    > "$OUT/nonrefused.txt"
head -120 "$OUT/nonrefused.txt"

echo "[red_alloc] summary:"
tail -1 "$OUT/scenarios.log"
echo "[red_alloc] final:"
grep '^RED20 alloc' "$OUT/harness.stdout" | tail -1
exit 0

#!/usr/bin/env bash
# RED-TEAM 20 / agent 7 - error handling + runtime liveness.
#
# Builds harness.cp with the C+ toolchain and runs >=2000 fork-per-case hostile
# scenarios.  Modifies ONLY tests/redblue20/red_error/results/.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="$HERE/results"
mkdir -p "$OUT"

SRC="$HERE/harness.cp"
BIN="$OUT/harness.bin"
LOG="$OUT/harness.log"

cd "$ROOT" || exit 2
echo "[red_error] root=$ROOT cases=2400 per-case-timeout=200ms global-timeout=60s"

# ---- build (command form required by the RED20 task) ---------------------
timeout 60 ./build/c+ build "$SRC" --default --no-werror --sanitize=none -I. \
    -o "$BIN" >"$OUT/build.log" 2>&1
BRC=$?
if [[ $BRC -ne 0 ]]; then
    echo "[red_error] BUILD FAILED rc=$BRC" >&2
    tail -40 "$OUT/build.log" >&2
    exit 2
fi
echo "[red_error] build ok"

# ---- run ------------------------------------------------------------------
set +e
timeout 60 "$BIN" >"$LOG" 2>"$OUT/harness.err"
RRC=$?
set -e
echo "[red_error] harness rc=$RRC"
if [[ $RRC -ne 0 ]]; then
    echo "[red_error] harness did not finish cleanly" >&2
    tail -20 "$OUT/harness.err" >&2
fi

grep '^CASE ' "$LOG" > "$OUT/cases.log" || true
awk '{c[$3]++} END {for (k in c) print k, c[k]}' "$OUT/cases.log" \
    | sort > "$OUT/verdict_counts.txt"

# ---- report ---------------------------------------------------------------
RED_LINE="$(grep '^RED20 error ' "$LOG" | tail -1)"
if [[ -z "$RED_LINE" ]]; then
    echo "[red_error] ERROR: no RED20 summary line" >&2
    exit 3
fi
echo "[red_error] verdict counts:"
cat "$OUT/verdict_counts.txt"
echo "[red_error] $RED_LINE"

# Non-REFUSED cases with their ids, for triage.
echo "[red_error] non-REFUSED case ids (verdict != REFUSED):"
awk '$3 != "REFUSED" {printf "%s=%s ", $2, $3} END {print ""}' "$OUT/cases.log" | fold -w 160
echo "[red_error] done."
exit 0

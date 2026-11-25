#!/bin/sh
# BLUE-TEAM agent 11 -- orchestrator for the modern-allocator safety proof.
# Creates/modifies ONLY tests/redblue20/blue_alloc/ (out/ holds all artifacts).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
HERE=tests/redblue20/blue_alloc
OUT=$HERE/out
SRC=$HERE/proof.cp
BIN=$OUT/proof
LOG=$OUT/results.log

mkdir -p "$OUT"

echo "BLUE20: root=$ROOT"
echo "BLUE20: compiler=$(./build/c+ --version 2>&1 | head -1)"
echo "BLUE20: cases=2400 (300/category x 8), seed=0xA11CE00000DEAD55, per-case alarm=60s"

# ---- build (exact required invocation; archive forwarded after --) --------
echo "+ timeout 60 ./build/c+ build $SRC --default --sanitize=none -I. -o $BIN -- build/libc/libcplus_hardened.a"
timeout 60 ./build/c+ build "$SRC" --default --sanitize=none -I. \
  -o "$BIN" -- build/libc/libcplus_hardened.a \
  >"$OUT/build.log" 2>&1
echo "BLUE20: build rc=0, binary=$BIN ($(wc -c < "$BIN") bytes)"

# ---- run (bounded 60 s; per-case watchdog alarm(60) inside harness) -------
echo "+ timeout 60 $BIN $LOG"
timeout 60 "$BIN" "$LOG" >"$OUT/stdout.log" 2>"$OUT/stderr.log"
echo "BLUE20: run rc=0"

# ---- verify -------------------------------------------------------------
SUMMARY=$(awk '$1=="BLUE20" && $2=="alloc" {print}' "$OUT/stdout.log")
DIGEST=$(awk '$1=="BLUE20_DIGEST" {print $2}' "$OUT/stdout.log")
set -- $SUMMARY
CASES=$3; PASSED=$4; FAILED=$5
echo "BLUE20: summary=$SUMMARY"
echo "BLUE20: digest=$DIGEST"
echo "BLUE20: pass lines=$(grep -c '^PASS ' "$LOG" || true), other lines=$(grep -cv '^PASS ' "$LOG" || true)"
echo "BLUE20: per-category:"
awk '/^PASS / {c[$4]++} END {for (k in c) printf "  %-8s %d\n", k, c[k]}' "$LOG" | sort

if [ "$CASES" != "2400" ] || [ "$PASSED" != "2400" ] || [ "$FAILED" != "0" ]; then
  echo "BLUE20: FAIL -- unexpected summary" >&2
  grep -v '^PASS ' "$LOG" | head -40 >&2 || true
  exit 1
fi
if [ "$(grep -c '^PASS ' "$LOG" || true)" != "2400" ]; then
  echo "BLUE20: FAIL -- per-case log incomplete" >&2
  exit 1
fi

shasum -a 256 "$LOG" >"$OUT/digest.sha256"
echo "BLUE20: log sha256=$(cat "$OUT/digest.sha256" | awk '{print $1}')"
echo "$SUMMARY"

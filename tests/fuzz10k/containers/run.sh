#!/usr/bin/env bash
# Reproduce the FUZZ10K containers evidence from a clean checkout.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
cd "$ROOT"

TOTAL=10010
BATCH=50
NBATCH=$(( (TOTAL + BATCH - 1) / BATCH ))
mkdir -p "$DIR/results"
rm -f "$DIR/results"/batch_*.log

./build/c+ build "$DIR/fuzz10k_containers.cp" --strict --sanitize=none \
  -I. -o "$DIR/fuzz10k_containers"

set +e
timeout 600 "$DIR/fuzz10k_containers" > "$DIR/results/raw.log" 2> "$DIR/results/raw.err"
RC=$?
set -e

: > "$DIR/results/scenarios.log"
for b in $(seq 0 $(( NBATCH - 1 ))); do
  grep '^S ' "$DIR/results/batch_${b}.log" >> "$DIR/results/scenarios.log"
done
grep '^FUZZ10K' "$DIR/results/raw.log" > "$DIR/results/summary.txt"
shasum -a 256 "$DIR/results/scenarios.log" | awk '{print $1}' > "$DIR/results/digest.txt"

echo "exit_code=$RC"
echo "batch_files=$(ls "$DIR/results"/batch_*.log | wc -l | tr -d ' ')"
echo "scenario_lines=$(wc -l < "$DIR/results/scenarios.log" | tr -d ' ')"
cat "$DIR/results/summary.txt"
cat "$DIR/results/digest.txt"
exit "$RC"

#!/usr/bin/env bash
# Reproduce the ADV1000 containers evidence from a clean checkout.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
cd "$ROOT"
mkdir -p "$DIR/results"

./build/c+ build "$DIR/adv1000_containers.cp" --strict --sanitize=none \
  -o "$DIR/adv1000_containers"

set +e
timeout 120 "$DIR/adv1000_containers" > "$DIR/results/raw.log" 2> "$DIR/results/raw.err"
RC=$?
set -e

grep '^S ' "$DIR/results/raw.log" > "$DIR/results/scenarios.log"
grep '^ADV1000' "$DIR/results/raw.log" > "$DIR/results/summary.txt"
shasum -a 256 "$DIR/results/scenarios.log" | awk '{print $1}' > "$DIR/results/digest.txt"

echo "exit_code=$RC"
echo "scenario_lines=$(wc -l < "$DIR/results/scenarios.log" | tr -d ' ')"
cat "$DIR/results/summary.txt"
cat "$DIR/results/digest.txt"
exit "$RC"

#!/bin/sh
# Five identical Goose rounds; writes raw ns + checksum to proof/benchmarks.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
D=benchmarks/language_rounds
OUT=proof/benchmarks/goose_rounds.txt
: > "$OUT"
printf 'round\tgoose_ns\twinner_note\n' >> "$OUT"
i=1
while [ "$i" -le 5 ]; do
  line=$(timeout 120 goose run "$D/bench.goose" 2>/dev/null | awk '/^GOOSE /{print $2, $3}')
  ns=$(printf '%s' "$line" | awk '{print $1}')
  sum=$(printf '%s' "$line" | awk '{print $2}')
  [ -n "$ns" ] || { echo "GOOSE_ROUNDS FAIL round $i"; exit 1; }
  printf '%d\t%s\tchecksum=%s\n' "$i" "$ns" "$sum" >> "$OUT"
  i=$((i+1))
done
cat "$OUT"

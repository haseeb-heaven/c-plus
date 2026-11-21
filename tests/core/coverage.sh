#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/out/coverage"
mkdir -p "$OUT"
cd "$OUT"
rm -f *.gcda *.gcno *.gcov
/usr/bin/clang --coverage -std=gnu17 -O0 -g -pthread \
  -I"$ROOT/runtime/include" \
  "$ROOT/runtime/cplus_runtime.c" \
  "$ROOT/tests/core/coverage_driver.c" -o runtime_coverage
printf "42 3.5 hello\n" | ./runtime_coverage >/dev/null
gcov -b -c runtime_coverage-cplus_runtime.gcno >/dev/null 2>&1
GCOV=cplus_runtime.c.gcov
if [ ! -f "$GCOV" ]; then
  echo "coverage: no gcov output"; exit 1
fi
total=$(awk -F: '($1 ~ /^ *[0-9]+$/ || $1 ~ /^ *#####$/) && $2 ~ /^ *[0-9]+$/ {n++} END{print n+0}' "$GCOV")
covered=$(awk -F: '$1 ~ /^ *[0-9]+$/ && $1+0 > 0 && $2 ~ /^ *[0-9]+$/ {n++} END{print n+0}' "$GCOV")
pct=$(( covered * 100 / total ))
echo "runtime line coverage: $covered/$total ($pct%)"
[ "$pct" -ge 80 ]

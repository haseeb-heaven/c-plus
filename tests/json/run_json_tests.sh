#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CC:-clang}
OUT=build/json; mkdir -p "$OUT"
pass=0; fail=0
for t in tests/json/test_*.c; do
  b="$OUT/$(basename "$t" .c)"
  if "$CC" -O2 -Wall -Wextra -std=c11 -Ithird_party/yyjson "$t" third_party/yyjson/yyjson.c -o "$b" >/tmp/json.log 2>&1 && "$b" >/tmp/json.out 2>&1; then
    pass=$((pass+1)); echo "ok $(basename "$t")"
  else
    echo "FAIL $(basename "$t")"; tail -20 /tmp/json.log || true; tail -20 /tmp/json.out || true; fail=$((fail+1))
  fi
done
echo "json tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

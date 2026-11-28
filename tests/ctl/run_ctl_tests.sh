#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CC:-clang}
INC="-Ithird_party -Ithird_party/ctl"
OUT=build/ctl; mkdir -p "$OUT"
pass=0; fail=0
for t in tests/ctl/test_*.c; do
  b="$OUT/$(basename "$t" .c)"
  if "$CC" -O2 -Wall -Wextra -std=c11 $INC "$t" -o "$b" >/tmp/ctl.log 2>&1 && "$b" >/tmp/ctl.out 2>&1; then
    pass=$((pass+1)); echo "ok $(basename "$t")"
  else
    echo "FAIL $(basename "$t")"; tail -20 /tmp/ctl.log || true; tail -20 /tmp/ctl.out || true; fail=$((fail+1))
  fi
done
echo "ctl tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

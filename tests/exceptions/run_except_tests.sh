#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CC:-clang}
INC="-Istdlib/safe -Ithird_party/exceptions4c -Ithird_party/safeclib/include"
LIB=build/safe/libcplus_safecrt.a
OUT=build/exceptions; mkdir -p "$OUT"
sh scripts/build_safecrt.sh >/dev/null
pass=0; fail=0
for t in tests/exceptions/test_*.c; do
  b="$OUT/$(basename "$t" .c)"
  if "$CC" -O2 -Wall -Wextra -std=c11 $INC "$t" "$LIB" -pthread -o "$b" >/tmp/exc.log 2>&1 && "$b" >/tmp/exc.out 2>&1; then
    pass=$((pass+1)); echo "ok $(basename "$t")"
  else
    echo "FAIL $(basename "$t")"; tail -20 /tmp/exc.log || true; tail -20 /tmp/exc.out || true; fail=$((fail+1))
  fi
done
echo "exception tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

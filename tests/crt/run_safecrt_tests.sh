#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
sh build/safe/build_safecrt.sh >/dev/null
CC=${CPLUS_CLANG:-clang}
INC="-Istdlib/safe -Ithird_party/intel-safestringlib/include"
LIB=build/safe/libcplus_safecrt.a
OUT=build/safe
pass=0; fail=0
for t in tests/crt/test_safe_*.c; do
  [ -f "$t" ] || continue
  b="$OUT/$(basename "$t" .c)"
  if "$CC" -O2 -Wall -Wextra -pedantic -std=c99 $INC "$t" "$LIB" -o "$b" && "$b" >/tmp/$(basename "$t").out 2>&1; then
    echo "ok: $(basename "$t")"; pass=$((pass+1))
  else
    echo "FAIL: $(basename "$t")"; tail -20 /tmp/$(basename "$t").out || true; fail=$((fail+1))
  fi
done
echo "safecrt tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

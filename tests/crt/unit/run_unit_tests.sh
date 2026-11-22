#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
sh scripts/build_safecrt.sh >/dev/null
sh tests/libc/build_libc.sh >/dev/null
CC=${CPLUS_CLANG:-clang}
INC="-Istdlib/safe -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
LIB=build/safe/libcplus_safecrt.a
OUT=build/safe/unit
mkdir -p "$OUT"
pass=0; fail=0
for t in tests/crt/unit/test_*.c; do
  b="$OUT/$(basename "$t" .c)"
  EXTRA_INC=""; EXTRA_LIB=""
  case "$t" in
    *safe_c_api*) EXTRA_INC="-I. -Ilibc -Ilibc/install-include"; EXTRA_LIB="build/libc/libcplus_hardened.a" ;;
  esac
  if "$CC" -O2 -Wall -Wextra -pedantic -std=c99 $INC $EXTRA_INC "$t" "$LIB" $EXTRA_LIB -lm -pthread -o "$b" >/tmp/crt_unit.log 2>&1 && "$b" >/tmp/crt_unit.out 2>&1; then
    pass=$((pass+1))
  else
    echo "FAIL $(basename "$t")"; tail -20 /tmp/crt_unit.log || true; tail -20 /tmp/crt_unit.out || true; fail=$((fail+1))
  fi
done
echo "crt unit tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

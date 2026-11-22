#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CC:-clang}
sh tests/libc/build_libc.sh >/dev/null
pass=0; fail=0
for t in tests/libc/fuzz_libc.c tests/libc/fuzz_libc2.c; do
  [ -e "$t" ] || continue
  b="build/libc/$(basename "$t" .c)"
  if "$CC" -O2 -Wall -Wextra -std=c99 -Ilibc/install-include -Ilibc "$t" build/libc/libcplus_hardened.a -o "$b" >/tmp/lf.log 2>&1 && "$b" >/tmp/lf.out 2>&1; then
    pass=$((pass+1)); echo "ok $(basename "$t")"
  else
    echo "FAIL $(basename "$t")"; tail -10 /tmp/lf.log; tail -10 /tmp/lf.out; fail=$((fail+1))
  fi
done
echo "libc fuzz: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
echo "== CRT conformance corpus =="
sh tests/crt/run_crt_tests.sh
echo "== Safe CRT unit tests =="
sh tests/crt/run_safecrt_tests.sh
echo "== Safe CRT examples =="
sh scripts/build_safecrt.sh >/dev/null
CC=${CPLUS_CLANG:-clang}
INC="-Istdlib/safe -Ithird_party/intel-safestringlib/include"
LIB=build/safe/libcplus_safecrt.a
OUT=build/safe/examples; mkdir -p "$OUT"
pass=0; fail=0
for f in examples/crt/*.c; do
  b="$OUT/$(basename "$f" .c)"
  if "$CC" -O2 -Wall -Wextra -pedantic -std=c99 $INC "$f" "$LIB" -o "$b" >/tmp/ex.log 2>&1 && "$b" >/tmp/ex.out 2>&1; then pass=$((pass+1)); else echo "FAIL $f"; fail=$((fail+1)); fi
done
echo "examples: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
echo "== Safe CRT fuzz smoke =="
pass=0; fail=0
for t in tests/crt/fuzz/fuzz_memcpy_s.c tests/crt/fuzz/fuzz_strcpy_s.c tests/crt/fuzz/fuzz_alloc_s.c tests/crt/fuzz/fuzz_format_s.c; do
  b=build/safe/fuzz_$(basename "$t" .c)
  if $CC -O1 -g -std=c99 $INC "$t" tests/crt/fuzz/fuzz_smoke.c "$LIB" -o "$b" >/tmp/fz.log 2>&1 && "$b" >/tmp/fz.out 2>&1; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL $t"; fi
done
echo "fuzz smoke: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
echo "== ALL CRT CHECKS PASSED =="

#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
echo "== CRT conformance corpus =="
CPLUS_CHECK=${CPLUS_BIN:-"$ROOT/build/c+"}
if [ -x "$CPLUS_CHECK" ]; then
  sh tests/crt/run_crt_tests.sh
else
  echo "skip CRT conformance corpus (C+ binary not built at $CPLUS_CHECK)"
fi
echo "== Safe CRT unit tests =="
sh tests/crt/run_safecrt_tests.sh
echo "== Safe CRT examples =="
sh scripts/build_safecrt.sh >/dev/null
CC=${CPLUS_CLANG:-clang}
INC="-Istdlib/safe -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
LIB=build/safe/libcplus_safecrt.a
OUT=build/safe/examples; mkdir -p "$OUT"
pass=0; fail=0
for f in apps/legacy_examples/crt/*.c; do
  b="$OUT/$(basename "$f" .c)"
  if "$CC" -O2 -Wall -Wextra -pedantic -std=c99 $INC "$f" "$LIB" -pthread -o "$b" >/tmp/ex.log 2>&1 && "$b" >/tmp/ex.out 2>&1; then pass=$((pass+1)); else echo "FAIL $f"; fail=$((fail+1)); fi
done
echo "examples: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
echo "== Adversarial mitigation suite =="
pass=0; fail=0
for t in tests/crt/adversarial/test_*.c; do
  [ -e "$t" ] || continue
  b=build/safe/adv_$(basename "$t" .c)
  if $CC -O2 -Wall -Wextra -pedantic -std=c99 $INC "$t" "$LIB" -pthread -o "$b" >/tmp/adv.log 2>&1 && "$b" >/tmp/adv.out 2>&1; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL $t"; tail -20 /tmp/adv.log || true; tail -20 /tmp/adv.out || true; fi
done
echo "adversarial mitigations: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
echo "== Safe CRT fuzz smoke =="
pass=0; fail=0
for t in tests/crt/fuzz/fuzz_memcpy_s.c tests/crt/fuzz/fuzz_strcpy_s.c tests/crt/fuzz/fuzz_alloc_s.c tests/crt/fuzz/fuzz_format_s.c; do
  b=build/safe/fuzz_$(basename "$t" .c)
  if $CC -O1 -g -std=c99 $INC "$t" tests/crt/fuzz/fuzz_smoke.c "$LIB" -pthread -o "$b" >/tmp/fz.log 2>&1 && "$b" >/tmp/fz.out 2>&1; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL $t"; fi
done
echo "fuzz smoke: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
echo "== ALL CRT CHECKS PASSED =="

#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
OUT=build/runtime; mkdir -p "$OUT"
pass=0; fail=0

# 1) Guarded recovery: detected error unwinds, exit 0.
if sh scripts/build_safe_recover.sh tests/runtime/test_runtime_recover.c "$OUT/test_runtime_recover" >/tmp/rt1.log 2>&1 \
   && "$OUT/test_runtime_recover" >/tmp/rt1.out 2>&1; then
  if grep -q "runtime_recover: ok" /tmp/rt1.out; then pass=$((pass+1)); echo "ok guarded_recover"; else fail=$((fail+1)); echo "FAIL guarded_recover (no ok line)"; fi
else
  fail=$((fail+1)); echo "FAIL guarded_recover"; tail -10 /tmp/rt1.log /tmp/rt1.out 2>/dev/null || true
fi

# 2) Unguarded OOB: reported by UBSan, must NOT abort (no SIGABRT / exit 134).
sh scripts/build_safe_recover.sh tests/runtime/demo_unguarded.c "$OUT/demo_unguarded" >/tmp/rt2.log 2>&1 || { fail=$((fail+1)); echo "FAIL demo build"; }
set +e
"$OUT/demo_unguarded" >/tmp/rt2.out 2>/tmp/rt2.err
rc=$?
set -e
# Controlled failure: exit 70 (never a crash signal 132/133/134/139).
if [ "$rc" -eq 70 ]; then pass=$((pass+1)); echo "ok unguarded_controlled_exit (70, no signal)"; else fail=$((fail+1)); echo "FAIL unguarded_controlled_exit (exit $rc)"; fi
if grep -q "out of bounds\|runtime error" /tmp/rt2.err /tmp/rt2.out; then pass=$((pass+1)); echo "ok unguarded_reports_diagnostic"; else fail=$((fail+1)); echo "FAIL unguarded_reports_diagnostic"; fi

# 3) Cleanup stack + nested guard LIFO unwind (via UBSan recover).
if sh scripts/build_safe_recover.sh tests/runtime/test_runtime_cleanup.c "$OUT/test_runtime_cleanup" >/tmp/rt3.log 2>&1 \
   && "$OUT/test_runtime_cleanup" >/tmp/rt3.out 2>&1 \
   && grep -q "runtime_cleanup: ok" /tmp/rt3.out; then
  pass=$((pass+1)); echo "ok runtime_cleanup"
else
  fail=$((fail+1)); echo "FAIL runtime_cleanup"; tail -20 /tmp/rt3.log /tmp/rt3.out 2>/dev/null || true
fi

# 4) Direct checks of the checked arithmetic helpers.  The compiler emits
# inline fast paths, so this standalone harness links the runtime source to
# pin down signedness, shift and narrowing semantics exactly.
CC=${CPLUS_CC:-${CPLUS_CLANG:-clang}}
if "$CC" -std=gnu17 -O0 -g -I"$ROOT/runtime/include" \
   "$ROOT/tests/runtime/test_checked_arithmetic.c" \
   "$ROOT/runtime/cplus_runtime.c" -lm -pthread \
   -o "$OUT/test_checked_arithmetic" >/tmp/rt4.log 2>&1 \
   && "$OUT/test_checked_arithmetic" >/tmp/rt4.out 2>/tmp/rt4.err \
   && grep -q "checked_arithmetic: ok" /tmp/rt4.out; then
  pass=$((pass+1)); echo "ok checked_arithmetic_helpers"
else
  fail=$((fail+1)); echo "FAIL checked_arithmetic_helpers"
  tail -20 /tmp/rt4.log /tmp/rt4.out /tmp/rt4.err 2>/dev/null || true
fi

echo "runtime tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

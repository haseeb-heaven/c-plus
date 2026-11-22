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
  fail=$((fail+1)); echo "FAIL guarded_recover"; tail -10 /tmp/rt1.log /tmp/rt1.out 2>/dev/null
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

echo "runtime tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

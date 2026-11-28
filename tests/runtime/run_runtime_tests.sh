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

# 5) Runtime safety regressions (#72/#73/#75/#76): concurrent print(), print
# depth unwind across throw, bounded live table under tombstone churn, working
# mutex/TLS, and realloc validation/transfer.
if "$CC" -std=gnu17 -O0 -g -I"$ROOT/runtime/include" \
    "$ROOT/tests/runtime/test_runtime_safety.c" \
    "$ROOT/runtime/cplus_runtime.c" -lm -pthread \
    -o "$OUT/test_runtime_safety" >/tmp/rt5.log 2>&1 \
    && "$OUT/test_runtime_safety" >/tmp/rt5.out 2>/tmp/rt5.err \
    && grep -q "runtime_safety: ok" /tmp/rt5.out; then
  pass=$((pass+1)); echo "ok runtime_safety_regressions"
else
  fail=$((fail+1)); echo "FAIL runtime_safety_regressions"
  tail -30 /tmp/rt5.log /tmp/rt5.out /tmp/rt5.err 2>/dev/null || true
fi

# 6) The no-thread-backend fallback must fail loudly, never silently no-op
# (#75).  CPLUS_FORCE_NO_THREAD_BACKEND compiles the same runtime without the
# POSIX/Win32 thread primitives so the fallback branch is testable here.
if "$CC" -std=gnu17 -O0 -g -DCPLUS_FORCE_NO_THREAD_BACKEND \
    -I"$ROOT/runtime/include" \
    "$ROOT/tests/runtime/test_no_thread_backend.c" \
    "$ROOT/runtime/cplus_runtime.c" -lm \
    -o "$OUT/test_no_thread_backend" >/tmp/rt6.log 2>&1 \
    && "$OUT/test_no_thread_backend" >/tmp/rt6.out 2>/tmp/rt6.err \
    && grep -q "no_thread_backend: ok" /tmp/rt6.out; then
  pass=$((pass+1)); echo "ok no_thread_backend_fails_loudly"
else
  fail=$((fail+1)); echo "FAIL no_thread_backend_fails_loudly"
  tail -30 /tmp/rt6.log /tmp/rt6.out /tmp/rt6.err 2>/dev/null || true
fi

# 7) scan() destination-size regressions (#111): undersized float/double/char/
# string destinations must trap before reading stdin, and overlong string
# tokens (including >4095 bytes) must fail closed without desynchronising
# stdin for the next scan().
if "$CC" -std=gnu17 -O0 -g -I"$ROOT/runtime/include" \
    "$ROOT/tests/runtime/test_runtime_scan.c" \
    "$ROOT/runtime/cplus_runtime.c" -lm -pthread \
    -o "$OUT/test_runtime_scan" >/tmp/rt7.log 2>&1 \
    && "$OUT/test_runtime_scan" >/tmp/rt7.out 2>/tmp/rt7.err \
    && grep -q "runtime_scan: ok" /tmp/rt7.out; then
  pass=$((pass+1)); echo "ok scan_destination_safety"
else
  fail=$((fail+1)); echo "FAIL scan_destination_safety"
  tail -30 /tmp/rt7.log /tmp/rt7.out /tmp/rt7.err 2>/dev/null || true
fi

# 8) #233: a char*/const char* scan destination must be the pointed-to buffer,
# never the pointer object.  A PROT_NONE page puts a deterministic SIGSEGV in
# the path of the old &x write; unsigned char/short must keep unsigned bounds.
if "$CC" -std=gnu17 -O0 -g -Wall -Wextra -I"$ROOT/runtime/include" \
     "$ROOT/tests/runtime/test_scan_macro_bounds.c" \
     "$ROOT/runtime/cplus_runtime.c" -lm -pthread \
     -o "$OUT/test_scan_macro_bounds" >/tmp/rt8.log 2>&1 \
    && "$OUT/test_scan_macro_bounds" >/tmp/rt8.out 2>/tmp/rt8.err \
    && grep -q "scan_macro_bounds: ok" /tmp/rt8.out; then
  pass=$((pass+1)); echo "ok scan_macro_bounds_guard_page"
else
  fail=$((fail+1)); echo "FAIL scan_macro_bounds_guard_page"
  tail -30 /tmp/rt8.log /tmp/rt8.out /tmp/rt8.err 2>/dev/null || true
fi

# 9) #234: on non-Apple platforms a retired address must be rejected without
# probing malloc_usable_size() on the freed pointer.  Compile the real runtime
# through its Linux preprocessor path with a counting stub for that query.
FAKE_INC="$OUT/fake_linux_include"
mkdir -p "$FAKE_INC"
cat >"$FAKE_INC/malloc.h" <<'EOF'
#include <stddef.h>
size_t malloc_usable_size(void *p);
EOF
if "$CC" -std=gnu17 -O0 -g -Wall -Wextra -U__APPLE__ -D__linux__ -D__unix__ \
     -I"$FAKE_INC" -I"$ROOT/runtime/include" \
     "$ROOT/runtime/cplus_runtime.c" \
     "$ROOT/tests/runtime/test_retired_probe_linux.c" -lm -pthread \
     -o "$OUT/test_retired_probe_linux" >/tmp/rt9.log 2>&1 \
    && "$OUT/test_retired_probe_linux" >/tmp/rt9.out 2>/tmp/rt9.err \
    && grep -q "retired_probe_linux: ok" /tmp/rt9.out; then
  pass=$((pass+1)); echo "ok retired_probe_linux_conservative"
else
  fail=$((fail+1)); echo "FAIL retired_probe_linux_conservative"
  tail -30 /tmp/rt9.log /tmp/rt9.out /tmp/rt9.err 2>/dev/null || true
fi

echo "runtime tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

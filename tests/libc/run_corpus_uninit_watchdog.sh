#!/bin/sh
# Self-test for the bounded hang diagnostics in tests/libc/test_corpus_uninit.c.
# POSIX sh, bounded timeouts, no Python, no swallowed failures.
# Prints CORPUS_WATCHDOG <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
CC=${CPLUS_CC:-/usr/bin/clang}
LIB=build/libc/libcplus_hardened.a
OUT=build/libc
mkdir -p "$OUT"
pass=0; fail=0; cases=0

note() {
    cases=$((cases + 1))
    if [ "$1" = 0 ]; then pass=$((pass + 1)); else fail=$((fail + 1)); fi
}

if ! timeout 120 "$CC" -O2 -std=c99 -Ilibc -Ilibc/include -Ilibc/install-include \
      tests/libc/test_corpus_uninit.c "$LIB" -lm -pthread \
      -o "$OUT/corpus_uninit_watchdog" >"$OUT/corpus_watchdog.build.log" 2>&1; then
    echo "CORPUS_WATCHDOG FAIL build"; exit 1
fi

# 1. default behavior unchanged
timeout 90 "$OUT/corpus_uninit_watchdog" >"$OUT/corpus_watchdog.default.out" 2>&1
if grep -qF 'corpus_uninit: 760 cases ok' "$OUT/corpus_watchdog.default.out"; then
    note 0
else
    note 1; echo "CORPUS_WATCHDOG FAIL default"
fi

# 2. trace reports every case plus the summary (760 + 1 lines)
CPLUS_CORPUS_TRACE=1 timeout 90 "$OUT/corpus_uninit_watchdog" \
    >"$OUT/corpus_watchdog.trace.out" 2>&1
lines=$(wc -l <"$OUT/corpus_watchdog.trace.out" | tr -d ' ')
if [ "$lines" = "761" ]; then note 0; else note 1; echo "CORPUS_WATCHDOG FAIL trace lines=$lines"; fi

# 3. watchdog bounds a forced hang and names the case
CPLUS_CORPUS_WATCHDOG=1 CPLUS_CORPUS_HANG_AT=5 \
    timeout 20 "$OUT/corpus_uninit_watchdog" \
    >"$OUT/corpus_watchdog.hang.out" 2>&1
rc=$?
if [ "$rc" -eq 70 ] && grep -qF 'hang at case 5' "$OUT/corpus_watchdog.hang.out"; then
    note 0
else
    note 1; echo "CORPUS_WATCHDOG FAIL hang rc=$rc"
fi

echo "CORPUS_WATCHDOG $cases $pass $fail"
[ "$fail" -eq 0 ]

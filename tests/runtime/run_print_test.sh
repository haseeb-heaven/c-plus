#!/bin/sh
# Issue #35 regression: print() must not truncate long strings or interleave
# nested invocations. POSIX sh, bounded, exact-output diff.
# Prints: PRINT_TEST <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=tests/runtime
pass=0; fail=0; cases=0
if ! timeout 180 ./build/c+ build "$D/test_print.cp" --default --no-werror --sanitize=none \
      -I. -o "$D/test_print" -- build/libc/libcplus_hardened.a \
      >"$D/test_print.build.log" 2>&1; then
    echo "PRINT_TEST FAIL build"; exit 1
fi
timeout 30 "$D/test_print" >"$D/test_print.out" 2>"$D/test_print.err" || {
    echo "PRINT_TEST FAIL run"; exit 1; }

cases=$((cases + 1))
if [ -s "$D/test_print.err" ]; then fail=$((fail + 1)); echo "PRINT_TEST FAIL stderr";
else pass=$((pass + 1)); fi

cases=$((cases + 1))
if diff -u "$D/expected_print.txt" "$D/test_print.out" >"$D/test_print.diff" 2>&1; then
    pass=$((pass + 1))
else
    fail=$((fail + 1)); echo "PRINT_TEST FAIL diff"; head -8 "$D/test_print.diff"
fi

# 1999 'A' + newline: the exact repro from issue #35.
cases=$((cases + 1))
if [ "$(head -1 "$D/test_print.out" | wc -c | tr -d ' ')" = "2000" ]; then
    pass=$((pass + 1))
else
    fail=$((fail + 1)); echo "PRINT_TEST FAIL long-line bytes"
fi

echo "PRINT_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]

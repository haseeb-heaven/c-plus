#!/bin/sh
# scoped-memory-style memory facade proof. Prints: SCOPED_MEM_TEST <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=tests/stdlib/scoped_mem
pass=0; fail=0; cases=0
if ! timeout 300 ${CPLUS_BIN:-./build/c+} build "$D/test.cp" --default --no-werror --sanitize=none \
      -I. -o "$D/test" -- build/libc/libcplus_hardened.a >"$D/build.log" 2>&1; then
    echo "SCOPED_MEM_TEST FAIL build"; tail -6 "$D/build.log"; exit 1
fi
timeout 30 "$D/test" >"$D/out" 2>"$D/err" || { echo "SCOPED_MEM_TEST FAIL run"; cat "$D/out"; exit 1; }
cases=$((cases + 1))
if grep -qF 'SCOPED_MEM 0' "$D/out" && [ ! -s "$D/err" ]; then pass=$((pass + 1));
else fail=$((fail + 1)); echo "SCOPED_MEM_TEST FAIL output"; cat "$D/out"; fi
echo "SCOPED_MEM_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]

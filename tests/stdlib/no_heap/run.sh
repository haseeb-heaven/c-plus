#!/bin/sh
# No-heap policy gate: positive C+ app builds/runs; raw heap call is refused.
# Prints: NO_HEAP_TEST <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=tests/stdlib/no_heap
pass=0; fail=0; cases=0
cases=$((cases + 1))
if timeout 300 ./build/c+ build "$D/ok.cp" --legacy-c --no-werror --sanitize=none \
      -DCPLUS_NO_HEAP -I. -o "$D/ok" -- build/libc/libcplus_hardened.a \
      >"$D/ok.build.log" 2>&1 && timeout 30 "$D/ok" >"$D/ok.out" 2>&1 \
      && grep -qF 'NO_HEAP_OK 0' "$D/ok.out"; then pass=$((pass + 1));
else fail=$((fail + 1)); echo "NO_HEAP_TEST FAIL positive"; tail -4 "$D/ok.build.log"; fi
cases=$((cases + 1))
if timeout 300 ./build/c+ build "$D/bad.cp" --legacy-c --no-werror --sanitize=none \
      -DCPLUS_NO_HEAP -I. -o "$D/bad" -- build/libc/libcplus_hardened.a \
      >"$D/bad.build.log" 2>&1; then
    fail=$((fail + 1)); echo "NO_HEAP_TEST FAIL heap call was not refused";
else
    pass=$((pass + 1));
fi
echo "NO_HEAP_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]

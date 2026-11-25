#!/bin/sh
# Nuklear scratch-allocator adapter proof (bounded POSIX sh).
# Prints: NK_ADAPT_TEST <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=tests/stdlib/nuklear_adapt
pass=0; fail=0; cases=0
if ! timeout 300 ./build/c+ build "$D/test.cp" --legacy-c --sanitize=none \
      -I. -Ithird_party/nuklear -o "$D/test" \
      -- build/ext/libnuklear.a build/libc/libcplus_hardened.a \
      >"$D/build.log" 2>&1; then
    echo "NK_ADAPT_TEST FAIL build"; tail -5 "$D/build.log"; exit 1
fi
timeout 30 "$D/test" >"$D/out" 2>"$D/err" || { echo "NK_ADAPT_TEST FAIL run"; cat "$D/out"; exit 1; }
cases=$((cases + 1))
if grep -qF 'NK_ADAPT 0' "$D/out" && [ ! -s "$D/err" ]; then pass=$((pass + 1));
else fail=$((fail + 1)); echo "NK_ADAPT_TEST FAIL output"; cat "$D/out"; fi
echo "NK_ADAPT_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]

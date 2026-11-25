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
# The vendored Nuklear archive is a build artifact (gitignored); rebuild it
# from the MIT source if a fresh worktree lacks it.
if [ ! -f build/ext/libnuklear.a ]; then
    mkdir -p build/ext
    # Build core Nuklear only; nuklear_impl.c needs an SDL3 backend we do not ship.
    printf '#define NK_IMPLEMENTATION\n#include "nuklear.h"\n' \
        > build/ext/nuklear_core.c
    ${CPLUS_CC:-clang} -O2 -std=c11 -Ithird_party/nuklear -c \
        build/ext/nuklear_core.c -o build/ext/nuklear_impl.o || exit 1
    /usr/bin/ar rcs build/ext/libnuklear.a build/ext/nuklear_impl.o || exit 1
fi
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

#!/bin/sh
# C+ class/TRY-CATCH/log adapter proof. Prints: NK_CPP_TEST <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=tests/stdlib/nuklear_cpp
pass=0; fail=0; cases=0
if [ ! -f build/ext/libnuklear.a ]; then
    mkdir -p build/ext
    printf '#define NK_IMPLEMENTATION\n#include "nuklear.h"\n' > build/ext/nuklear_core.c
    ${CPLUS_CC:-clang} -O2 -std=c11 -Ithird_party/nuklear -c build/ext/nuklear_core.c \
        -o build/ext/nuklear_impl.o || exit 1
    ${CPLUS_AR:-ar} rcs build/ext/libnuklear.a build/ext/nuklear_impl.o || exit 1
fi
if ! timeout 300 ./build/c+ build "$D/test.cp" --legacy-c --no-werror --sanitize=none \
      -I. -Istdlib/safe -Ithird_party/nuklear -Ithird_party/exceptions4c \
      -o "$D/test" -- build/ext/libnuklear.a build/safe/libcplus_safecrt.a \
      build/libc/libcplus_hardened.a >"$D/build.log" 2>&1; then
    echo "NK_CPP_TEST FAIL build"; tail -6 "$D/build.log"; exit 1
fi
timeout 30 "$D/test" >"$D/out" 2>"$D/err" || { echo "NK_CPP_TEST FAIL run"; cat "$D/out"; exit 1; }
cases=$((cases + 1))
if grep -qF 'NK_CPP init 0' "$D/out" && grep -qF 'NK_CPP ok 0' "$D/out"; then
    pass=$((pass + 1))
else
    fail=$((fail + 1)); echo "NK_CPP_TEST FAIL output"; cat "$D/out"
fi
echo "NK_CPP_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]

#!/bin/sh
# C+ class/TRY-CATCH/log adapter proofs. Prints: NK_CPP_TEST <cases> <pass> <fail>.
# test.cp       - refused init (CATCH path) and its observable outputs
# regression.cp - issue #87: constructor/drop rewind + no TRY-frame leak
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
for t in test regression; do
    cases=$((cases + 1))
    if ! timeout 300 ./build/c+ build "$D/$t.cp" --legacy-c --no-werror --sanitize=none \
          -I. -Istdlib/safe -Ithird_party/nuklear -Ithird_party/exceptions4c \
          -o "$D/$t" -- build/ext/libnuklear.a build/safe/libcplus_safecrt.a \
          build/libc/libcplus_hardened.a >"$D/$t.build.log" 2>&1; then
        fail=$((fail + 1)); echo "NK_CPP_TEST FAIL build $t"; tail -6 "$D/$t.build.log"; continue
    fi
    if ! timeout 30 "$D/$t" >"$D/$t.out" 2>"$D/$t.err"; then
        fail=$((fail + 1)); echo "NK_CPP_TEST FAIL run $t"; cat "$D/$t.out"; cat "$D/$t.err"; continue
    fi
    if [ "$t" = test ]; then
        if grep -qF 'NK_CPP init 0' "$D/$t.out" && grep -qF 'NK_CPP ok 0' "$D/$t.out"; then
            pass=$((pass + 1))
        else
            fail=$((fail + 1)); echo "NK_CPP_TEST FAIL output"; cat "$D/$t.out"
        fi
    else
        if grep -qF 'NK_REG fails 0' "$D/$t.out" && [ ! -s "$D/$t.err" ]; then
            pass=$((pass + 1))
        else
            fail=$((fail + 1)); echo "NK_CPP_TEST FAIL regression"; cat "$D/$t.out"; cat "$D/$t.err"
        fi
    fi
done
echo "NK_CPP_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]

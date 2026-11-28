#!/bin/sh
# Deterministic Windows-branch probe for core-memory audit finding C8:
# VM-backed thread-local DataStacks must be released with VirtualFree on
# Windows worker-thread exit. Two independent checks:
#   1. host: execute the real translation unit's Windows branch with a stub
#      <windows.h> and assert the exact VirtualFree arguments;
#   2. cross: compile that same translation unit with MinGW against the real
#      Win32 SDK and verify the emitted branch references VirtualFree with the
#      MEM_RELEASE constant. -Werror keeps every diagnostic live.
# No warnings, tests or toolchains are silently skipped: an unavailable
# cross-toolchain is reported as NOT_TESTED.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-${CC:-cc}}
OUT="$ROOT/build/win_tls_probe"
mkdir -p "$OUT"
INC="-I$ROOT/libc -I$ROOT/libc/include -I$ROOT/libc/install-include"
fail=0

HOST_BIN="$OUT/test_datastack_tls_win32_release"
if "$CC" -O1 -g -std=c99 -Wall -Wextra -pedantic -D_WIN32 \
      -I"$ROOT/tests/libc/win32_probe" $INC \
      "$ROOT/tests/libc/win32_probe/test_datastack_tls_win32_release.c" \
      -pthread -o "$HOST_BIN" >"$OUT/host.build.log" 2>&1 \
   && "$HOST_BIN" >"$OUT/host.run.log" 2>&1; then
    echo "ok   host Windows-branch argument probe"
    cat "$OUT/host.run.log"
else
    echo "FAIL host Windows-branch argument probe"
    tail -20 "$OUT/host.build.log" 2>/dev/null || true
    tail -20 "$OUT/host.run.log" 2>/dev/null || true
    fail=1
fi

cross_found=0
for triplet in x86_64-w64-mingw32 i686-w64-mingw32; do
    cross_cc="${triplet}-gcc"
    if ! command -v "$cross_cc" >/dev/null 2>&1; then
        echo "NOT_TESTED $cross_cc (toolchain unavailable)"
        continue
    fi
    cross_found=1
    obj="$OUT/datastack_tls_${triplet}.o"
    if ! "$cross_cc" -O2 -std=c99 -Wall -Wextra -pedantic -Werror $INC \
            -c "$ROOT/libc/src/stdlib/datastack_tls.c" -o "$obj" \
            >"$OUT/${triplet}.build.log" 2>&1; then
        echo "FAIL $cross_cc compile"
        tail -20 "$OUT/${triplet}.build.log" 2>/dev/null || true
        fail=1
        continue
    fi
    if "${triplet}-nm" "$obj" | grep -qi 'VirtualFree' \
       && "${triplet}-objdump" -dr "$obj" | grep -qi 'VirtualFree' \
       && "${triplet}-objdump" -dr "$obj" | grep -q '0x8000'; then
        echo "ok   $cross_cc Windows branch emits VirtualFree/MEM_RELEASE"
    else
        echo "FAIL $cross_cc Windows branch missing VirtualFree/MEM_RELEASE"
        "${triplet}-nm" "$obj" | grep -i 'VirtualFree' || true
        "${triplet}-objdump" -dr "$obj" | grep -i -C4 'VirtualFree' || true
        fail=1
    fi
done
if [ "$cross_found" -eq 0 ]; then
    echo "NOT_TESTED Windows cross-compile (no MinGW toolchain available)"
fi

exit "$fail"

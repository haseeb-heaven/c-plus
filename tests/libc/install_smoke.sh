#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BLD="$ROOT/build/libc-cmake"
INST="$ROOT/build/libc-install"
CC=${CPLUS_CLANG:-clang}
rm -rf "$BLD" "$INST"
cmake -S "$ROOT/libc" -B "$BLD" -DCMAKE_INSTALL_PREFIX="$INST" >/tmp/libc_cmake.log 2>&1
cmake --build "$BLD" >/tmp/libc_build.log 2>&1
cmake --install "$BLD" >/tmp/libc_install.log 2>&1
test -f "$INST/lib/libcplus_hardened_libc.a"
test -f "$INST/lib/libcplus_hardened_libc.dylib" || test -f "$INST/lib/libcplus_hardened_libc.so"
test -f "$INST/include/cplus-libc/cplus_libc.h"
test -f "$INST/share/cplus-libc/contracts/contracts.json"
test -f "$INST/share/cplus-libc/contracts/symbol_map.json"
test -f "$INST/lib/cmake/cplus_hardened_libc/cplus_hardened_libcConfig.cmake"
python3 "$ROOT/tests/libc/validate_contracts.py" >/tmp/libc_contracts_check.log 2>&1

# Compile and run a standalone C program against the clean install prefix
SMOKE_SRC="$BLD/smoke_sdk.c"
SMOKE_BIN="$BLD/smoke_sdk"
cat > "$SMOKE_SRC" <<'SRC'
#include <assert.h>
#include <string.h>
#include "cplus_libc.h"
int main(void) {
    char *p = (char *)cplus_malloc(16);
    assert(p != NULL);
    assert(cplus_alloc_size(p) == 16);
    cplus_memcpy(p, "sdk", 4);
    assert(cplus_strcmp(p, "sdk") == 0);
    assert(cplus_iswalpha(L'A'));
    assert(cplus_strtoimax("123", NULL, 10) == 123);
    cplus_free(p);
    return 0;
}
SRC
"$CC" -std=c99 -Wall -Wextra -I"$INST/include/cplus-libc" "$SMOKE_SRC" \
  "$INST/lib/libcplus_hardened_libc.a" -lm -pthread -o "$SMOKE_BIN"
"$SMOKE_BIN"
echo "install smoke: ok"

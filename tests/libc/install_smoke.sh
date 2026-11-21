#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BLD="$ROOT/build/libc-cmake"
INST="$ROOT/build/libc-install"
rm -rf "$BLD" "$INST"
cmake -S "$ROOT/libc" -B "$BLD" -DCMAKE_INSTALL_PREFIX="$INST" >/tmp/libc_cmake.log 2>&1
cmake --build "$BLD" >/tmp/libc_build.log 2>&1
cmake --install "$BLD" >/tmp/libc_install.log 2>&1
test -f "$INST/lib/libcplus_hardened_libc.a"
test -f "$INST/include/cplus-libc/string.h"
test -f "$INST/share/cplus-libc/contracts/contracts.json"
echo "install smoke: ok"

#!/bin/sh
# Deterministic allocation fault-injection matrix (294 checks).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-/usr/bin/clang}
mkdir -p build/libc
sh tests/libc/build_libc.sh >/dev/null
sh scripts/build_safecrt.sh >/dev/null
"$CC" -O1 -g -std=c99 -Wall -Wextra -pedantic \
  -Ilibc/install-include -I. \
  tests/libc/test_alloc_fault_matrix.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o build/libc/alloc_fault_matrix
timeout 60 build/libc/alloc_fault_matrix

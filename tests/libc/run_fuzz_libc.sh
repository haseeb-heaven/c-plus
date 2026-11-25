#!/bin/sh
# Deterministic hardened-libc string/memory fuzz harness (4800 cases).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-/usr/bin/clang}
mkdir -p build/libc
sh tests/libc/build_libc.sh >/dev/null
sh scripts/build_safecrt.sh >/dev/null
"$CC" -O1 -g -std=c99 -Wall -Wextra -pedantic \
  -Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/libc/fuzz_libc.c \
  build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
  -lm -pthread -o build/libc/fuzz_libc
timeout 60 build/libc/fuzz_libc

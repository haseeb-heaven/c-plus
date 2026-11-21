#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CC:-clang}
sh tests/libc/build_libc.sh >/dev/null
"$CC" -O2 -Wall -Wextra -std=c99 -Ilibc/install-include -Ilibc tests/libc/fuzz_libc.c build/libc/libcplus_hardened.a -o build/libc/fuzz_libc
build/libc/fuzz_libc

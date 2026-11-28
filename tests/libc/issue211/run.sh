#!/bin/sh
# Self-contained, out-of-tree verification for issue #211.
# Compiles only the libc sources needed by the bounded string layer into a
# temporary directory; nothing is written inside the repository.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CC=${CC:-${CPLUS_CLANG:-clang}}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

SYSROOT=""
if command -v xcrun >/dev/null 2>&1; then
    SDK=$(xcrun --show-sdk-path 2>/dev/null || true)
    [ -n "$SDK" ] && SYSROOT="-isysroot $SDK"
fi
CFLAGS="-O1 -g -std=c99 -Wall -Wextra -pedantic $SYSROOT \
-I$ROOT/libc -I$ROOT/libc/include -I$ROOT/libc/install-include"

for f in \
    libc/log/log.c \
    libc/src/stdlib/alloc.c \
    libc/src/string/bounded.c \
    libc/src/string/string.c \
    libc/src/string/memory.c \
    libc/src/string/extra.c; do
    $CC $CFLAGS -c "$ROOT/$f" -o "$TMP/$(basename "$f" .c).o"
done

$CC $CFLAGS "$ROOT/tests/libc/issue211/test_strncat_bounded_overflow.c" \
    "$TMP"/*.o -lm -pthread -o "$TMP/test_strncat_bounded_overflow"
"$TMP/test_strncat_bounded_overflow"

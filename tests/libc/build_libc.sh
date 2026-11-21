#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CC=${CPLUS_CLANG:-clang}
OUT="$ROOT/build/libc"
mkdir -p "$OUT"
CFLAGS="-O2 -Wall -Wextra -pedantic -std=c99 -I$ROOT/libc -I$ROOT/libc/include"
for f in "$ROOT"/libc/log/log.c "$ROOT"/libc/src/errno/errno.c "$ROOT"/libc/src/stdlib/alloc.c "$ROOT"/libc/src/stdlib/stdlib_extra.c "$ROOT"/libc/src/stdlib/stdlib_extra2.c "$ROOT"/libc/src/string/extra2.c "$ROOT"/libc/src/string/extra3.c "$ROOT"/libc/src/string/memory.c "$ROOT"/libc/src/string/string.c "$ROOT"/libc/src/string/extra.c "$ROOT"/libc/src/stdio/stdio.c "$ROOT"/libc/src/stdio/stdio2.c "$ROOT"/libc/src/ctype/ctype.c "$ROOT"/libc/src/ctype/ctype2.c "$ROOT"/libc/src/math/math.c "$ROOT"/libc/src/time/time.c "$ROOT"/libc/src/locale/locale.c; do
  b="$OUT/$(basename "$f" .c).o"
  "$CC" $CFLAGS -c "$f" -o "$b"
done
ar rcs "$OUT/libcplus_hardened.a" "$OUT"/*.o
echo "$OUT/libcplus_hardened.a"

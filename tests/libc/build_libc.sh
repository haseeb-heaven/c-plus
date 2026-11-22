#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CC=${CPLUS_CLANG:-clang}
OUT="$ROOT/build/libc"
mkdir -p "$OUT"
CFLAGS="-O2 -Wall -Wextra -pedantic -std=c99 -I$ROOT/libc -I$ROOT/libc/include"
for f in "$ROOT"/libc/log/log.c "$ROOT"/libc/src/errno/errno.c "$ROOT"/libc/src/stdlib/alloc.c "$ROOT"/libc/src/stdlib/stdlib_extra.c "$ROOT"/libc/src/stdlib/stdlib_extra2.c "$ROOT"/libc/src/stdlib/stdlib_extra3.c "$ROOT"/libc/src/stdlib/stdlib_extra4.c "$ROOT"/libc/src/stdlib/stdlib_extra5.c "$ROOT"/libc/src/stdlib/stdlib_extra6.c "$ROOT"/libc/src/stdlib/assert_extra.c "$ROOT"/libc/src/stdlib/musl_stdlib_extra.c "$ROOT"/libc/src/string/extra2.c "$ROOT"/libc/src/string/extra3.c "$ROOT"/libc/src/string/string_extra4.c "$ROOT"/libc/src/string/string_extra5.c "$ROOT"/libc/src/string/string_extra6.c "$ROOT"/libc/src/string/strtok.c "$ROOT"/libc/src/string/musl_extra.c "$ROOT"/libc/src/string/strndup.c "$ROOT"/libc/src/string/strerror.c "$ROOT"/libc/src/string/memory.c "$ROOT"/libc/src/string/string.c "$ROOT"/libc/src/string/extra.c "$ROOT"/libc/src/stdio/stdio.c "$ROOT"/libc/src/stdio/stdio2.c "$ROOT"/libc/src/ctype/ctype.c "$ROOT"/libc/src/wchar/wchar.c "$ROOT"/libc/src/wctype/wctype.c "$ROOT"/libc/src/ctype/ctype2.c "$ROOT"/libc/src/ctype/ctype3.c "$ROOT"/libc/src/math/math.c "$ROOT"/libc/src/math/math2.c "$ROOT"/libc/src/math/math3.c "$ROOT"/libc/src/math/math4.c "$ROOT"/libc/src/time/time.c "$ROOT"/libc/src/time/time_extra.c "$ROOT"/libc/src/locale/locale.c "$ROOT"/libc/src/signal/signal.c "$ROOT"/libc/src/signal/signal2.c "$ROOT"/libc/src/stdio/stdio3.c "$ROOT"/libc/src/fenv/fenv.c "$ROOT"/libc/src/complex/complex.c "$ROOT"/libc/src/thread/thread.c "$ROOT"/libc/src/fenv/fenv_extra.c "$ROOT"/libc/src/time/time_ascii.c "$ROOT"/libc/src/wchar/wchar_numeric.c "$ROOT"/libc/src/wchar/wchar_multibyte.c "$ROOT"/libc/src/wchar/wchar_format.c "$ROOT"/libc/src/wctype/wctype_extra.c "$ROOT"/libc/src/complex/complex_extra.c "$ROOT"/libc/src/math/math_float.c "$ROOT"/libc/src/math/math_ldouble.c "$ROOT"/libc/src/stdlib/stdlib_c17.c "$ROOT"/libc/src/stdlib/inttypes_wide.c; do
  b="$OUT/$(basename "$f" .c).o"
  "$CC" $CFLAGS -c "$f" -o "$b"
done
ar rcs "$OUT/libcplus_hardened.a" "$OUT"/*.o
echo "$OUT/libcplus_hardened.a"

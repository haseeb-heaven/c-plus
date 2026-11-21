#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CC=${CPLUS_CLANG:-clang}
OUT="$ROOT/build/safe"
mkdir -p "$OUT"
INC="-I$ROOT/third_party/intel-safestringlib/include -I$ROOT/third_party/intel-safestringlib/safeclib -I$ROOT/stdlib/safe"
CFLAGS_UP="-O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 -include $ROOT/third_party/intel-safestringlib/rename_symbols.h -Wno-unused-parameter $INC"
CFLAGS_W="-O2 -Wall -Wextra -pedantic -std=c99 $INC"
for f in "$ROOT"/third_party/intel-safestringlib/safeclib/*.c; do
  base=$(basename "$f" .c)
  "$CC" $CFLAGS_UP -c "$f" -o "$OUT/up_$base.o"
done
for f in "$ROOT"/stdlib/safe/src/*.c; do
  base=$(basename "$f" .c)
  "$CC" $CFLAGS_W -c "$f" -o "$OUT/w_$base.o"
done
ar rcs "$OUT/libcplus_safecrt.a" "$OUT"/up_*.o "$OUT"/w_*.o
echo "$OUT/libcplus_safecrt.a"

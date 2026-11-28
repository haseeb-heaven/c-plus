#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/out/core/bench"
mkdir -p "$OUT"
SRC="$ROOT/tests/core/libc_bench.c"
"$ROOT/build/c+" build "$SRC" -o "$OUT/cplus_safe" -- -O2 >/dev/null 2>&1
/usr/bin/clang -std=gnu17 -O2 "$SRC" -o "$OUT/clang_c" >/dev/null 2>&1
/usr/bin/clang -std=gnu17 -O2 -fsanitize=undefined,bounds,null,alignment \
  -fno-sanitize-recover=all "$SRC" -o "$OUT/clang_san" >/dev/null 2>&1
printf 'c+ safe : '; /usr/bin/time -p "$OUT/cplus_safe" >/dev/null 2>"$OUT/cplus.time"; grep real "$OUT/cplus.time"
printf 'clang C : '; /usr/bin/time -p "$OUT/clang_c" >/dev/null 2>"$OUT/clang.time"; grep real "$OUT/clang.time"
printf 'clang UB: '; /usr/bin/time -p "$OUT/clang_san" >/dev/null 2>"$OUT/clang_san.time"; grep real "$OUT/clang_san.time"

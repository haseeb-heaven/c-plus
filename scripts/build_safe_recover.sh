#!/bin/sh
# Build a C+ program with RECOVERABLE runtime checks: detected UB is reported
# and unwound to cplus_runtime_guard instead of aborting the process.
# Usage: scripts/build_safe_recover.sh <input.c|input.cp> <output>
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CPLUS_CC:-clang}
IN=${1:?input source required}
OUT=${2:?output path required}
"$CC" -std=gnu17 -O0 -g -pthread \
  -fsanitize=undefined,bounds,null,alignment -fsanitize-recover=all -fno-sanitize-trap=all \
  -fno-omit-frame-pointer -DCPLUS=1 -DCPLUS_SAFE=1 \
  -I"$ROOT/stdlib/safe" -I"$ROOT/third_party/safeclib/include" \
  "$IN" \
  "$ROOT/stdlib/safe/src/safe_runtime.c" \
  "$ROOT/stdlib/safe/src/safe_log.c" \
  "$ROOT/stdlib/safe/src/safe_stats.c" \
  -o "$OUT"
echo "$OUT"

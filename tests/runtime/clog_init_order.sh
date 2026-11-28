#!/bin/sh
# Deterministic regression for issue #246: clog_is_initialized must be
# published only after CLOG_MUTEX_INIT has completed.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/build/clog_init_order"
mkdir -p "$OUT"

CC=${CPLUS_CC:-${CPLUS_CLANG:-cc}}
"$CC" -std=c11 -O0 -g -Wall -Wextra -Werror -I"$ROOT/runtime/include" \
  "$ROOT/tests/runtime/clog_init_order.c" -pthread -o "$OUT/test" \
  >"$OUT/build.log" 2>&1 || {
  cat "$OUT/build.log" >&2
  exit 1
}

"$OUT/test" >"$OUT/run.log" 2>&1 || {
  cat "$OUT/run.log" >&2
  exit 1
}
cat "$OUT/run.log"

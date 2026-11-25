#!/bin/sh
# Defense-in-depth allocator regression runner.
# Builds the SafeCRT allocator objects directly (no other library dependency
# beyond pthread), then builds and runs tests/defense/alloc/defense_alloc.c.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
P=${CPLUS_ALLOC_DEFENSE_TMP:-/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/defense_alloc}
CC=${CPLUS_CLANG:-/usr/bin/clang}
mkdir -p "$P"

CFLAGS="-O1 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -I$ROOT/stdlib/safe"
OBJS=""
for f in safe_alloc safe_log safe_stats; do
  $CC $CFLAGS -c "$ROOT/stdlib/safe/src/$f.c" -o "$P/$f.o"
  OBJS="$OBJS $P/$f.o"
done

$CC $CFLAGS -I"$ROOT" "$ROOT/tests/defense/alloc/defense_alloc.c" $OBJS -pthread -o "$P/defense_alloc"
timeout 30 "$P/defense_alloc"

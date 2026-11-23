#!/bin/sh
# Reproduce the before/after evidence for the allocator hardening.
# Compiles the harness against HEAD's safe_alloc.{c,h} (baseline) and against
# the working tree (hardened), then runs both under `timeout 30`.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
P=${CPLUS_ALLOC_DEFENSE_TMP:-/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/defense_alloc}
CC=${CPLUS_CLANG:-/usr/bin/clang}
mkdir -p "$P/headcopy/stdlib/safe/src"

for f in safe_alloc.h safe_error.h safe_log.h safe_stats.h; do
  git -C "$ROOT" show HEAD:stdlib/safe/$f > "$P/headcopy/stdlib/safe/$f"
done
git -C "$ROOT" show HEAD:stdlib/safe/src/safe_alloc.c > "$P/headcopy/stdlib/safe/src/safe_alloc.c"

CFLAGS="-O1 -g -std=c11 -Wall -Wextra -Wno-unused-parameter"
for f in safe_log safe_stats; do
  $CC $CFLAGS -I"$ROOT/stdlib/safe" -c "$ROOT/stdlib/safe/src/$f.c" -o "$P/$f.o"
done

# Baseline: HEAD allocator + HEAD header (feature macros absent).
$CC $CFLAGS -I"$P/headcopy" -c "$P/headcopy/stdlib/safe/src/safe_alloc.c" -o "$P/head_safe_alloc.o"
$CC $CFLAGS -I"$P/headcopy" -I"$ROOT" "$ROOT/tests/defense/alloc/defense_alloc.c" \
  "$P/head_safe_alloc.o" "$P/safe_log.o" "$P/safe_stats.o" -pthread -o "$P/defense_alloc_head"

# Hardened: working-tree allocator + header (feature macros present).
$CC $CFLAGS -I"$ROOT/stdlib/safe" -c "$ROOT/stdlib/safe/src/safe_alloc.c" -o "$P/safe_alloc.o"
$CC $CFLAGS -I"$ROOT/stdlib/safe" -I"$ROOT" "$ROOT/tests/defense/alloc/defense_alloc.c" \
  "$P/safe_alloc.o" "$P/safe_log.o" "$P/safe_stats.o" -pthread -o "$P/defense_alloc_hardened"

echo '===== BEFORE (HEAD) ====='
timeout 30 "$P/defense_alloc_head"
echo '===== AFTER (HARDENED) ====='
timeout 30 "$P/defense_alloc_hardened"

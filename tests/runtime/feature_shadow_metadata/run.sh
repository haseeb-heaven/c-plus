#!/bin/sh
# Compile and run the deterministic C regressions for issues #181 / #182.
# No project build is required: the test includes runtime/cplus_runtime.c.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

OUT=build/runtime
mkdir -p "$OUT"
CC=${CC:-clang}

SDK_ARGS=""
if [ "$(uname -s)" = "Darwin" ]; then
  SDK=$(xcrun --show-sdk-path 2>/dev/null || true)
  if [ -n "$SDK" ]; then
    SDK_ARGS="-isysroot $SDK"
  fi
fi

# shellcheck disable=SC2086
"$CC" -std=gnu17 -O0 -g -Wall -Wextra \
  -I runtime/include $SDK_ARGS \
  tests/runtime/feature_shadow_metadata/test_shadow_metadata.c \
  tests/runtime/feature_shadow_metadata/uninstrumented_helper.c \
  -o "$OUT/test_shadow_metadata" -lm -lpthread

# Both sides of the CPLUS_LOWFAT_ALLOC opt-in must keep the classic side-table
# behaviour intact.  The env-on run additionally exercises the O(1) lock-free
# low-fat paths; the env-off run verifies the graceful fallback.
CPLUS_LOWFAT_ALLOC=1 "$OUT/test_shadow_metadata" env-on
CPLUS_LOWFAT_ALLOC=0 "$OUT/test_shadow_metadata" env-off

# Issue #184: deterministic fork-based epoch/conservative reclamation checks.
# The same binary covers both sides of the CPLUS_RECLAIM_MODE opt-in, proving
# a retired address is not reused while a pointer still references it.
# shellcheck disable=SC2086
"$CC" -std=gnu17 -O0 -g -Wall -Wextra \
  -I runtime/include $SDK_ARGS \
  tests/runtime/feature_shadow_metadata/test_reclaim.c \
  -o "$OUT/test_reclaim" -lm -lpthread
CPLUS_RECLAIM_MODE=epoch "$OUT/test_reclaim" gc-on
"$OUT/test_reclaim" gc-off
echo "PASS: feature_shadow_metadata"

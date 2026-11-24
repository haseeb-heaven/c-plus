#!/bin/sh
# EDGE100 adapters -- build and run the per-API fork-per-case edge harness.
#   sh tests/edge100/adapters/run.sh
# Every step is bounded by `timeout 60`.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../" && pwd)
cd "$ROOT"
export PATH="$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"
unset SDKROOT || true

OUT=tests/edge100/adapters/out
mkdir -p "$OUT"

timeout 60 ./build/c+ build tests/edge100/adapters/harness.cp \
    --default --sanitize=none -I. \
    -o "$OUT/adapters_harness" \
    -- build/libc/libcplus_hardened.a > "$OUT/build.log" 2>&1

timeout 60 "$OUT/adapters_harness" > "$OUT/results.log" 2> "$OUT/results.err"

grep -E '^(REUSE|EDGE100)' "$OUT/results.log"
# Propagate harness failure (any case failed) to the caller.
grep -q '^EDGE100 adapters [0-9]* [0-9]* [0-9]* 0$' "$OUT/results.log"

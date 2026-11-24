#!/bin/sh
# End-to-end 10,000-scenario allocator/container fuzz runner.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
export PATH="$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH"
unset SDKROOT || true
OUT=tests/fuzz10k_e2e/out
mkdir -p "$OUT"
timeout 300 ./build/c+ build tests/fuzz10k_e2e/fuzz10k_e2e.cp --default --sanitize=none \
    -I. -o "$OUT/fuzz10k_e2e" -- build/libc/libcplus_hardened.a \
    >"$OUT/build.log" 2>&1
rc=0
timeout 900 "$OUT/fuzz10k_e2e" >"$OUT/result.log" 2>"$OUT/result.err" || rc=$?
cat "$OUT/result.log"
exit "$rc"

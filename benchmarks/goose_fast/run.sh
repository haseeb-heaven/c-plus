#!/bin/sh
# Goose-style fast-path benchmark verifier (POSIX sh, bounded).
# Prints GOOSE_BENCH <checks> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
D=benchmarks/goose_fast
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
unset SDKROOT CPLUS_CLANG
mkdir -p "$D/out"
pass=0; fail=0
check() { if grep -qF "$1" "$D/out/run.log"; then
    printf 'GOOSE_BENCH PASS %s\n' "$1"; pass=$((pass+1))
  else printf 'GOOSE_BENCH FAIL %s\n' "$1"; fail=$((fail+1)); fi; }

if ! timeout 180 ./build/c+ build "$D/bump_bench.cp" --default --no-werror --sanitize=none -I. \
      -o "$D/out/bump_bench" -- build/libc/libcplus_hardened.a >"$D/out/build.log" 2>&1; then
    printf 'GOOSE_BENCH FAIL build\n'; exit 1
fi
timeout 60 "$D/out/bump_bench" >"$D/out/run.log" 2>&1 || true
# The absolute heap-time bucket is intentionally NOT a gate: wall-clock varies
# with host load. The deterministic safety/performance claims are the bump
# bucket, the one-store release and the >=2x ratio between the two loops.
for m in "BENCH bump_ms_bucket fast" "BENCH release_ms_bucket one_store" \
         "BENCH ratio_ge_2 yes" "BENCH_OK"; do
    check "$m"
done
printf 'GOOSE_BENCH %d %d %d\n' "$((pass+fail))" "$pass" "$fail"
[ "$fail" -eq 0 ]

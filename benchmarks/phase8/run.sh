#!/bin/sh
# PHASE 8 - performance measurement + ABI verification (Agent I, branch agent/modern-stdlib)
#
# Ownership: this script writes ONLY under benchmarks/phase8/.  The ABI/test
# commands it invokes generate their own artifacts under build/ and
# tools/abi_compare/.out/ as a side effect; this script does not edit them.
# No source, header, test, build file or git index entry is modified here.
#
# Run from anywhere:
#   sh benchmarks/phase8/run.sh
#
# Protocol: 1 untimed warmup + 9 interleaved measured rounds for wall-clock
# harnesses (date +%s%N); the allocator/tracking harnesses time in-process with
# clock_gettime and already use 2 warmups + 11 (or 9) rounds.  All numbers in
# RESULTS.md are outputs of these exact commands; nothing is estimated.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
HERE="$ROOT/benchmarks/phase8"
mkdir -p "$HERE/bin" "$HERE/results" "$HERE/logs"

CC=${CPLUS_BENCH_CC:-/usr/bin/clang}
PINNED_PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
INC="$ROOT/libc/install-include"
LIBC_A="$ROOT/build/libc/libcplus_hardened.a"
YYJSON_FLAGS="-I$ROOT/third_party/yyjson -L$ROOT/build/ext -lyyjson"

step() { printf '\n=== %s ===\n' "$*"; }
rc_of() { printf 'EXIT_CODE=%s\n' "$1"; }

# ---------------------------------------------------------------------------
# Performance: allocation overhead + memory-tracking overhead
# ---------------------------------------------------------------------------
step 'build allocator benchmarks (existing harness, outputs in phase8/bin)'
if [ ! -f "$LIBC_A" ]; then
  echo 'error: missing build/libc/libcplus_hardened.a (run tests/libc/build_libc.sh)' >&2
  exit 2
fi
"$CC" -O2 -std=gnu17 -I"$INC" benchmarks/stdlib/allocator/allocator_bench.c \
  "$LIBC_A" -lm -pthread -o "$HERE/bin/allocator_bench" || exit 2
"$CC" -O2 -std=gnu17 -I"$INC" benchmarks/stdlib/allocator/tracking_toggle.c \
  "$LIBC_A" -lm -pthread -o "$HERE/bin/tracking_off" || exit 2
"$CC" -O2 -std=gnu17 -I"$INC" -DCPLUS_MALLOC_TRACKING \
  benchmarks/stdlib/allocator/tracking_toggle.c "$LIBC_A" -lm -pthread \
  -o "$HERE/bin/tracking_on" || exit 2

step 'allocator matrix: 1 warmup + 9 measured rounds, all workloads/variants'
"$HERE/bin/allocator_bench" --runs 9 --warmup 1 --seed 314159265 \
  | tee "$HERE/results/allocator_current.csv"

step 'tracking ON vs OFF: 7 external passes, each 2 warmups + 11 in-process rounds'
: > "$HERE/results/tracking_toggle_current.txt"
for b in tracking_off tracking_on; do
  i=1
  while [ "$i" -le 7 ]; do
    printf 'bin=%s pass=%s ' "$b" "$i" >> "$HERE/results/tracking_toggle_current.txt"
    "$HERE/bin/$b" 200000 >> "$HERE/results/tracking_toggle_current.txt"
    i=$((i + 1))
  done
done
cat "$HERE/results/tracking_toggle_current.txt"

# ---------------------------------------------------------------------------
# Performance: string append/growth + vector growth (2M-element workload)
# ---------------------------------------------------------------------------
step 'build modern string/vector harnesses (safe-mode build is externally blocked)'
PATH="$PINNED_PATH" ./build/c+ build --release --legacy-c \
  benchmarks/safety_compare/modern.cp -o "$HERE/bin/modern_legacy" \
  > "$HERE/logs/build_modern.log" 2>&1 \
  || { echo 'modern build FAILED'; cat "$HERE/logs/build_modern.log"; exit 2; }
PATH="$PINNED_PATH" ./build/c+ build --strict --sanitize=none \
  benchmarks/safety_compare/bench_modern.cp -o "$HERE/bin/bench_modern_strict" \
  > "$HERE/logs/build_bench_modern.log" 2>&1 \
  || { echo 'bench_modern build FAILED'; cat "$HERE/logs/build_bench_modern.log"; exit 2; }

step 'string/vector wall clock: 1 warmup + 9 interleaved rounds'
sh "$HERE/time_runs.sh" 9 \
  modern_legacy="$HERE/bin/modern_legacy" \
  bench_modern_strict="$HERE/bin/bench_modern_strict" \
  | tee "$HERE/results/string_vector_wall.csv"
echo 'bench_modern internal vector_us/string_us (per run):'
for f in "$HERE"/logs/bench_modern_strict.run.*.out; do tail -1 "$f"; done

# ---------------------------------------------------------------------------
# Performance: JSON parsing + file I/O + allocation (existing stdlib harness)
# ---------------------------------------------------------------------------
step 'build tests/stdlib/bench_extra.{c,cp} and bench_{plain,compare}.{c,cp}'
"$CC" -O2 tests/stdlib/bench_extra.c $YYJSON_FLAGS -o "$HERE/bin/extra_plain_fast" \
  2> "$HERE/logs/build_extra_plain.err" || exit 2
PATH="$PINNED_PATH" ./build/c+ build --release --strict --sanitize=none \
  tests/stdlib/bench_extra.cp $YYJSON_FLAGS -o "$HERE/bin/extra_cplus_fast" \
  > "$HERE/logs/build_extra_cplus.log" 2>&1 \
  || { echo 'extra_cplus build FAILED'; cat "$HERE/logs/build_extra_cplus.log"; exit 2; }
"$CC" -O2 tests/stdlib/bench_plain.c -o "$HERE/bin/compare_plain_fast" || exit 2
PATH="$PINNED_PATH" ./build/c+ build --release --strict --sanitize=none \
  tests/stdlib/bench_compare.cp -o "$HERE/bin/compare_cplus_fast" \
  > "$HERE/logs/build_compare_cplus.log" 2>&1 \
  || { echo 'compare_cplus build FAILED'; cat "$HERE/logs/build_compare_cplus.log"; exit 2; }

step 'JSON/file/alloc + vector/string wall clock: 1 warmup + 9 interleaved rounds'
sh "$HERE/time_runs.sh" 9 \
  extra_plain_fast="$HERE/bin/extra_plain_fast" \
  extra_cplus_fast="$HERE/bin/extra_cplus_fast" \
  compare_plain_fast="$HERE/bin/compare_plain_fast" \
  compare_cplus_fast="$HERE/bin/compare_cplus_fast" \
  | tee "$HERE/results/extra_compare_wall.csv"
echo 'internal json_ms/file_ms/alloc_ms (per run):'
for bin in extra_plain_fast extra_cplus_fast; do
  for f in "$HERE"/logs/$bin.run.*.out; do printf '%s %s\n' "$bin" "$(cat "$f")"; done
done
echo 'internal vector_ms/string_ms (per run):'
for bin in compare_plain_fast compare_cplus_fast; do
  for f in "$HERE"/logs/$bin.run.*.out; do printf '%s %s\n' "$bin" "$(cat "$f")"; done
done

# ---------------------------------------------------------------------------
# ABI verification - exact commands from the task, exit codes recorded
# ---------------------------------------------------------------------------
step 'ABI 1: sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt'
sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt \
  > "$HERE/abi-1-fingerprint-compare.log" 2>&1
rc=$?; rc_of "$rc"; tail -3 "$HERE/abi-1-fingerprint-compare.log"

step 'ABI 2: CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_compat.sh'
CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_compat.sh \
  > "$HERE/abi-2-compat.log" 2>&1
rc=$?; rc_of "$rc"; tail -1 "$HERE/abi-2-compat.log"

step 'ABI 3: CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh'
CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh \
  > "$HERE/abi-3-layouts.log" 2>&1
rc=$?; rc_of "$rc"; tail -1 "$HERE/abi-3-layouts.log"

step 'ABI 4: bash tests/abi/run_legacy_labs.sh (bash here resolves to /opt/homebrew/bin/bash 5.3)'
bash tests/abi/run_legacy_labs.sh > "$HERE/abi-4-legacy-labs-bash32.log" 2>&1
rc=$?; rc_of "$rc"; tail -2 "$HERE/abi-4-legacy-labs-bash32.log"

step 'ABI 5: sh tests/sdk/run_sdk_consumer.sh (ambient compiler)'
sh tests/sdk/run_sdk_consumer.sh > "$HERE/abi-5-sdk-consumer.log" 2>&1
rc=$?; rc_of "$rc"; head -1 "$HERE/abi-5-sdk-consumer.log"

step 'ABI 5b: same script with pinned Apple clang'
CPLUS_CLANG=/usr/bin/clang sh tests/sdk/run_sdk_consumer.sh \
  > "$HERE/abi-5b-sdk-consumer-appleclang.log" 2>&1
rc=$?; rc_of "$rc"; tail -1 "$HERE/abi-5b-sdk-consumer-appleclang.log"

printf '\nphase8: all commands completed; see RESULTS.md for interpretation.\n'

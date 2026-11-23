#!/bin/sh
# Reproducible allocator benchmark runner (no Python, no external harness).
#
# Builds allocator_bench.c against the C+ hardened libc allocator and runs the
# full workload matrix with a fixed seed, warmup rounds and >=7 interleaved
# measured rounds.  Raw CSV is written to results/allocator_bench.csv.
#
# The C+ compiler (build/c+) cannot be used for this particular benchmark:
# it links libcplus_runtime.a, which also defines cplus_realloc and collides
# with the hardened libc.  We therefore use the exact same toolchain pattern
# as the C+ libc test suite (tests/libc/run_libc_tests.sh): a plain C compiler
# plus build/libc/libcplus_hardened.a.  Apple clang is the default because the
# Homebrew clang in this environment targets a missing MacOSX27 SDK.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
HERE="$ROOT/benchmarks/stdlib/allocator"
CC=${CPLUS_BENCH_CC:-/usr/bin/clang}
LIBC="$ROOT/build/libc/libcplus_hardened.a"
BIN="$HERE/allocator_bench"
RESULTS="$HERE/results"

RUNS=${CPLUS_BENCH_RUNS:-9}
WARMUP=${CPLUS_BENCH_WARMUP:-2}
SEED=${CPLUS_BENCH_SEED:-314159265}

if [ ! -f "$LIBC" ]; then
  echo "error: $LIBC missing; build the C+ hardened libc first (tests/libc/build_libc.sh)" >&2
  exit 1
fi

mkdir -p "$RESULTS"

echo "build: $CC -O2 -std=gnu17 -I$ROOT/libc/install-include $HERE/allocator_bench.c $LIBC -lm -pthread -o $BIN"
"$CC" -O2 -std=gnu17 -I"$ROOT/libc/install-include" \
  "$HERE/allocator_bench.c" "$LIBC" -lm -pthread -o "$BIN"

echo "run: $BIN --runs $RUNS --warmup $WARMUP --seed $SEED (all workloads, all variants)"
"$BIN" --runs "$RUNS" --warmup "$WARMUP" --seed "$SEED" | tee "$RESULTS/allocator_bench.csv"

echo "raw results: $RESULTS/allocator_bench.csv"

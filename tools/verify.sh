#!/bin/sh
# Full C+ verification: build, automated suites, demos, benchmarks.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

echo "== build =="
cmake --build build --target c+ -- -j4

echo "== ctest =="
ctest --test-dir build --output-on-failure

echo "== tests =="
./tests/run_tests.sh

echo "== demos =="
./demos/run_demos.sh

echo "== benchmarks (vs clang -O2) =="
./benchmarks/run_bench.sh

echo "VERIFICATION OK"

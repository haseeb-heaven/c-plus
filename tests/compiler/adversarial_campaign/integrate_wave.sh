#!/bin/sh
# Parent-owned wave integration verifier. No `|| true`; any failure aborts.
set -e
cd "$(dirname "$0")/../../.."
export CPLUS_BIN="$PWD/build/c+"
unset CPLUS_CLANG

echo "===== build ====="
cmake --build build --target c+ -- -j4

echo "===== bypass measurement (AFTER) ====="
python3 tests/compiler/adversarial_campaign/verify_bypasses.py

echo "===== before/after comparison ====="
python3 tests/compiler/adversarial_campaign/compare_waves.py \
  tests/compiler/adversarial_campaign/bypass_results.BEFORE.json \
  tests/compiler/adversarial_campaign/bypass_results.json

echo "===== core regressions ====="
./tests/run_tests.sh 2>&1 | tail -1
./tests/core/run.sh 2>&1 | tail -1
sh tests/compiler/agent1/run_all.sh --full 2>&1 | tail -1

echo "===== source / ABI / standards / real-world ====="
python3 tests/compiler/source_preservation/run.py 2>&1 | tail -1
python3 tests/compiler/real_world_c/run_suite.py 2>&1 | tail -1
python3 tests/compiler/c_standards/run.py 2>&1 | tail -3
python3 tests/compiler/abi_real_world/run_suite.py 2>&1 | tail -2

echo "===== performance (side-table overhead check) ====="
python3 benchmarks/rust_vs_cplus/run.py 2>&1 | tail -1

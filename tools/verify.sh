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

echo "== perf comparison =="
./tests/core/perf_compare.sh >/dev/null

echo "== adversarial fuzz =="
python3 tests/core/adversarial_fuzz.py

echo "== contracts and SDK smoke =="
python3 tests/core/validate_contracts.py
./tests/core/sdk_smoke.sh

echo "== native capabilities =="
./tests/core/capabilities.sh >/dev/null

echo "== quarantine semantics =="
./tests/core/quarantine/run.sh

echo "== libc contract matrix =="
./tests/core/libc_matrix/run.sh

echo "== cli compatibility =="
./tests/cli_compat_test.sh

echo "== complex examples (build only) =="
./examples/build_examples.sh

echo "== breaker v3 =="
bash tests/stress/cplus_breaker_v3/run.sh

echo "== stress pack =="
bash tests/stress/cplus_stress_10/run_10.sh

echo "== breaker suite =="
./tests/cp_breaker.sh

echo "== logging =="
./tests/logging_test.sh

echo "== demos =="
./demos/run_demos.sh

echo "== benchmarks (vs clang -O2) =="
./benchmarks/run_bench.sh

echo "VERIFICATION OK"

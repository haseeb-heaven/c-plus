#!/bin/sh
# Everything a reviewer (or CI) should run for the library, after a compiler
# change lands on develop.  Usage: tests/stdlib/run_all.sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 1
fail=0
step() { printf '\n=== %s\n' "$1"; }

step "stdlib suite (tests, examples, coverage gate)"
sh tests/stdlib/run_stdlib_tests.sh || fail=1
step "compiler/repo suite"
sh tests/run_tests.sh || fail=1
step "adversarial breaker v3"
sh tests/stress/cplus_breaker_v3/run.sh || fail=1
step "stress pack"
bash tests/stress/cplus_stress_10/run_10.sh || fail=1
step "breaker probes"
sh tests/cp_breaker.sh || fail=1
step "fuzz targets"
sh tests/stdlib/fuzz/run_fuzz.sh 200 || fail=1
step "performance gate"
sh tests/stdlib/perf_gate.sh || fail=1
step "execution coverage (informational)"
sh tests/stdlib/coverage_exec.sh || printf 'note: execution coverage unavailable on this host\n'
if [ "$fail" -ne 0 ]; then
  echo "\ncombined suites: FAILURES" >&2
  exit 1
fi
echo "\ncombined suites: ok"

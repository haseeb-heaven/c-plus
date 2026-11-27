#!/bin/sh
# Single entry point for the complete evidence set.
# Every suite writes its own logs under build/; this script only aggregates
# exit codes and prints one summary line per suite.
set -u
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
export CPLUS_CLANG=${CPLUS_CLANG:-/usr/bin/clang}
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
pass=0; fail=0
run() {
  label=$1; shift
  if "$@" >"/tmp/evidence_${label}.log" 2>&1; then
    echo "ok   $label: $(tail -1 "/tmp/evidence_${label}.log")"
    pass=$((pass+1))
  else
    echo "FAIL $label: $(tail -1 "/tmp/evidence_${label}.log")"
    tail -8 "/tmp/evidence_${label}.log"
    fail=$((fail+1))
  fi
}
run compiler-tests        sh tests/run_tests.sh
run libc-tests            sh tests/libc/run_libc_tests.sh
run crt-unit              sh tests/crt/unit/run_unit_tests.sh
run crt-all               sh tests/crt/run_all_crt.sh
run attack-suites         sh tests/libc/run_attack_tests.sh
run corpora               sh tests/libc/run_corpus_tests.sh
run scenario-suites       sh tests/scenarios/run_scenarios.sh
run method-suites         sh tests/methods/run_methods.sh
run hundred-suites        sh tests/methods/run_hundred.sh
run family-suites         sh tests/family_gaps/run_family_tests.sh
run deep-stdlib           sh tests/stdlib/run_deep_tests.sh
run compile-time-safety   sh tests/stdlib/compile_time/run_compile_time.sh
run libc-fuzz             sh tests/libc/run_fuzz_libc.sh
run alloc-fault-matrix   sh tests/libc/run_alloc_fault_matrix.sh
run abi-layouts           sh tests/abi/run_abi_layouts.sh
run integration           sh tests/integration/run_integration.sh
run rust-vs-cplus         sh tests/rust_vs_cplus/run_compare.sh
run cplus-rust-gates      sh tests/conformance/run_conformance.sh
run sdk-consumer          sh tests/sdk/run_sdk_consumer.sh
run compat-matrix         sh tests/compat/run_compat.sh
run contract-validator    sh tests/libc/run_validate_contracts.sh

# The alignment tool is a native test utility, not a shipped artifact, so a
# fresh tree has no binary. Build it here when absent. The canonical strict
# build is attempted first; the fallback only changes how the *utility* is
# lowered (bundled SDK/stdlib headers can trip the safety analyser) and never
# touches the contract assertions the tool itself executes.
ALIGN_TOOL=build/native-tools/test_compiler_contract_alignment
if [ ! -x "$ALIGN_TOOL" ]; then
  mkdir -p build/native-tools build/ext
  if [ ! -f build/ext/libyyjson.a ]; then
    ${CPLUS_CLANG:-clang} -O2 -std=c11 -c third_party/yyjson/yyjson.c \
      -Ithird_party/yyjson -o build/ext/yyjson.o \
      >/tmp/evidence_yyjson-build.log 2>&1 &&
      /usr/bin/libtool -static -o build/ext/libyyjson.a build/ext/yyjson.o \
      >>/tmp/evidence_yyjson-build.log 2>&1 || true
  fi
  ./build/c+ build tests/libc/test_compiler_contract_alignment.cp \
    --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson \
    -o "$ALIGN_TOOL" >/tmp/evidence_align-tool-build.log 2>&1 ||
    echo "blocker: native alignment tool build failed; see /tmp/evidence_align-tool-build.log" >&2
fi
run contract-alignment    "$ALIGN_TOOL"
run issue199              sh tests/compiler/agent1/regression_issue199.sh
run issue214              sh tests/stdlib/issue214/run.sh
run issue215              sh tests/stdlib/issue215/run.sh
run issue216              sh tests/stdlib/issue216/run.sh
run issue217_219          sh tests/stdlib/run_stdlib_tests.sh issue21
run coverage              sh tests/libc/coverage_libc.sh
echo "evidence aggregate: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

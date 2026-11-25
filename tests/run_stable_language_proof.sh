#!/bin/sh
# Single aggregate proof: all defined correctness/safety/app/benchmark/doc gates.
# POSIX sh, bounded, no Python, no || true. Prints STABLE_LANGUAGE_PROOF n p f.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
OUT=build/stable_proof; mkdir -p "$OUT"
cases=0; pass=0; fail=0
for runner in \
  tests/run_tests.sh \
  tests/libc/run_libc_tests.sh \
  tests/crt/run_safecrt_tests.sh \
  tests/edge1000/run_edge1000.sh \
  tests/pr31_abi/run.sh \
  tests/pr32_portability/run.sh \
  tests/pr33_integration/verify.sh \
  tests/run_modern_safer_evidence.sh \
  tests/stdlib/upstream_reuse/run_memory_safety.sh \
  tests/stdlib/no_heap/run.sh \
  tests/stdlib/goose_mem/run.sh \
  tests/stdlib/nuklear_cpp/run.sh \
  tests/docs/run_doc_check.sh \
  apps/run_all.sh \
  proof/games_gui_nuklear/run.sh \
  proof/goose_samples/run.sh \
  proof/all_apps_games_screenshots/run.sh \
  benchmarks/goose_fast/run.sh \
  benchmarks/goose_modern/run.sh; do
    cases=$((cases + 1))
    log="$OUT/$(echo "$runner" | tr '/' '_').log"
    if timeout 3600 sh "$runner" >"$log" 2>&1; then
        pass=$((pass + 1)); printf 'PROOF PASS %s: ' "$runner"; tail -1 "$log"
    else
        fail=$((fail + 1)); printf 'PROOF FAIL %s: ' "$runner"; tail -2 "$log"
    fi
done
# ctest uses the configured build tree
cases=$((cases + 1))
if timeout 1800 ctest --test-dir build --output-on-failure >"$OUT/ctest.log" 2>&1; then
    pass=$((pass + 1)); echo "PROOF PASS ctest: $(grep -m1 'tests passed' "$OUT/ctest.log" || echo ok)"
else
    fail=$((fail + 1)); echo "PROOF FAIL ctest"; tail -3 "$OUT/ctest.log"
fi
echo "STABLE_LANGUAGE_PROOF $cases $pass $fail"
[ "$fail" -eq 0 ]

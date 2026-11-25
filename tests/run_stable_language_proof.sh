#!/bin/sh
# Single aggregate proof: all defined correctness/safety/app/benchmark/doc gates.
# POSIX sh, bounded, no Python, no || true. Prints STABLE_LANGUAGE_PROOF n p f.
#
# A fresh worktree has no compiled artifacts.  Bootstrap the four artifacts
# that the gates link against once, with bounded commands, before the loop:
#   build/c+                       (C+ driver, also needed by ctest)
#   build/libc/libcplus_hardened.a
#   build/safe/libcplus_safecrt.a
#   build/ext/libnuklear.a
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
OUT=build/stable_proof; mkdir -p "$OUT"

boot_fail() { # boot_fail <log> <label>
    printf 'BOOTSTRAP FAIL %s\n' "$2" >&2
    tail -5 "$1" >&2
    exit 2
}

# C+ driver and ctest metadata both come from the same CMake configure/build.
if [ ! -x build/c+ ] || [ ! -f build/CTestTestfile.cmake ]; then
    if ! timeout 1800 cmake -S . -B build >"$OUT/bootstrap_cmake_configure.log" 2>&1; then
        boot_fail "$OUT/bootstrap_cmake_configure.log" "cmake configure"
    fi
    if ! timeout 3600 cmake --build build --target c+ -- -j4 >"$OUT/bootstrap_cmake_build.log" 2>&1; then
        boot_fail "$OUT/bootstrap_cmake_build.log" "cmake build c+"
    fi
fi

# Hardened libc archive (also rebuilt on demand by tests/libc/run_libc_tests.sh).
if [ ! -f build/libc/libcplus_hardened.a ]; then
    if ! timeout 1800 sh tests/libc/build_libc.sh >"$OUT/bootstrap_libc.log" 2>&1; then
        boot_fail "$OUT/bootstrap_libc.log" "build_libc.sh"
    fi
fi

# Safe-C runtime archive (also rebuilt on demand by tests/crt/run_safecrt_tests.sh).
if [ ! -f build/safe/libcplus_safecrt.a ]; then
    if ! timeout 1800 sh scripts/build_safecrt.sh >"$OUT/bootstrap_safecrt.log" 2>&1; then
        boot_fail "$OUT/bootstrap_safecrt.log" "build_safecrt.sh"
    fi
fi

# Vendored Nuklear archive: gitignored build artifact, rebuilt from MIT source.
if [ ! -f build/ext/libnuklear.a ]; then
    mkdir -p build/ext
    printf '#define NK_IMPLEMENTATION\n#include "nuklear.h"\n' > build/ext/nuklear_core.c
    if ! timeout 600 "${CPLUS_CC:-clang}" -O2 -std=c11 -Ithird_party/nuklear \
        -c build/ext/nuklear_core.c -o build/ext/nuklear_impl.o \
        >"$OUT/bootstrap_nuklear.log" 2>&1; then
        boot_fail "$OUT/bootstrap_nuklear.log" "nuklear compile"
    fi
    if ! timeout 300 "${CPLUS_AR:-ar}" rcs build/ext/libnuklear.a build/ext/nuklear_impl.o \
        >>"$OUT/bootstrap_nuklear.log" 2>&1; then
        boot_fail "$OUT/bootstrap_nuklear.log" "nuklear archive"
    fi
fi

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
  tests/stdlib/scoped_mem/run.sh \
  tests/stdlib/nuklear_cpp/run.sh \
  tests/docs/run_doc_check.sh \
  apps/run_all.sh \
  proof/games_gui_nuklear/run.sh \
  proof/modern_c_ports/run.sh \
  proof/all_apps_games_screenshots/run.sh \
  benchmarks/scoped_bump/run.sh \
  benchmarks/scoped_apps/run.sh; do
    cases=$((cases + 1))
    log="$OUT/$(echo "$runner" | tr '/' '_').log"
    if [ ! -f "$runner" ]; then
        fail=$((fail + 1)); printf 'PROOF FAIL %s: missing runner script\n' "$runner"
    elif timeout 3600 sh "$runner" >"$log" 2>&1; then
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

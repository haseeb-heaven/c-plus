#!/bin/sh
# Fil-C / SoftBound / GSL / lifetime-adapter and Rust-vs-C+ runtime safety
# evidence runner (POSIX sh, bounded, no Python, no silent skips).
#
# Prints: MEMSAFE <cases> <pass> <fail>.  Exit 0 == all pass.
# NOTE: outcome equality and side-table queries are OBSERVED evidence only.
# They are not a proof of Rust-equivalent or Fil-C-equivalent memory safety.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
# rustc is commonly installed under ~/.cargo/bin, outside the restricted PATH.
if [ -x "$HOME/.cargo/bin/rustc" ]; then
    CPLUS_RUSTC="$HOME/.cargo/bin/rustc"
    export CPLUS_RUSTC
fi
unset SDKROOT
unset CPLUS_CLANG
D=tests/stdlib/upstream_reuse
OUT=build/mem_safety
mkdir -p "$OUT"
cases=0; pass=0; fail=0

run_case() {   # run_case <program> <expected summary line>
    name=$1; summary=$2
    cases=$((cases + 1))
    if ! timeout 300 ./build/c+ build "$D/$name.cp" --legacy-c --sanitize=none \
            -I. -o "$OUT/$name" -- build/libc/libcplus_hardened.a \
            >"$OUT/$name.build.log" 2>&1; then
        fail=$((fail + 1)); echo "MEMSAFE FAIL build $name"
        tail -4 "$OUT/$name.build.log" 2>/dev/null || true; return
    fi
    if ! timeout 60 "$OUT/$name" >"$OUT/$name.out" 2>&1; then
        fail=$((fail + 1)); echo "MEMSAFE FAIL run $name"
        tail -4 "$OUT/$name.out" 2>/dev/null || true; return
    fi
    if grep -qF "$summary" "$OUT/$name.out"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1)); echo "MEMSAFE FAIL summary $name: $summary"
        tail -4 "$OUT/$name.out" 2>/dev/null || true
    fi
}

run_case filc_diag 'ALLOC_DIAG 26 0'
run_case softbound 'SOFTBOUND 51 0'
run_case gsl_adapt 'GSL_ADAPT 80 0'
run_case lifetime_adapt 'LIFETIME_ADAPT 25 0'

cases=$((cases + 1))
if timeout 1200 sh tests/rust_vs_cplus/run_compare.sh >"$OUT/rust_vs_cplus.out" 2>&1 &&
   grep -qF '0 failures' "$OUT/rust_vs_cplus.out"; then
    pass=$((pass + 1))
else
    fail=$((fail + 1)); echo "MEMSAFE FAIL rust_vs_cplus"
    tail -4 "$OUT/rust_vs_cplus.out" 2>/dev/null || true
fi

echo "MEMSAFE $cases $pass $fail"
[ "$fail" -eq 0 ]

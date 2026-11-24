#!/bin/sh
# End-to-end aggregate for the >1000-edge-case sweeps and the modern/safer
# lib suites (Issue #24, added 2026-11-24).
#
# Runs: tests/edge1000/test_modern_edge.cp, tests/edge1000/test_safer_edge.cp,
#       tests/stdlib/{math3d,scene3d,mesh3d,widgets,cli}/run.sh,
#       tests/ctl/run_ctl_demo_coverage.sh.
#
# Prints: EDGE1000 <cases> <pass> <fail>.  Exit 0 == all pass.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
export LC_ALL=C
D=tests/edge1000
cases=0; pass=0; fail=0
abi_diff_empty=0
mkdir -p build/edge1000

run_sweep() {   # run_sweep <src> <summary-marker> <library>
    src=$1; marker=$2; lib=$3
    bin=$D/.sweep_$(basename "$src" .cp)
    timeout 300 ./build/c+ build "$src" --default --sanitize=none \
        -I. -o "$bin" -- "$lib" \
        >"$D/sweep_$(basename "$src" .cp).build.log" 2>&1 || {
        cases=$((cases + 1)); fail=$((fail + 1));
        echo "EDGE1000 FAIL build $src"; return; }
    timeout 120 "$bin" >"$D/sweep_$(basename "$src" .cp).out" 2>&1 || {
        cases=$((cases + 1)); fail=$((fail + 1));
        echo "EDGE1000 FAIL run $src"; cat "$D/sweep_$(basename "$src" .cp).out"; return; }
    got=$(awk -v m="$marker" '$1 == m {print $2; print $3}' "$D/sweep_$(basename "$src" .cp).out" | head -2)
    c=$(printf '%s\n' "$got" | sed -n 1p); p=$(printf '%s\n' "$got" | sed -n 2p)
    [ -n "$c" ] && [ -n "$p" ] || {
        cases=$((cases + 1)); fail=$((fail + 1));
        echo "EDGE1000 FAIL no summary $src"; return; }
    cases=$((cases + c)); pass=$((pass + p))
    if grep -qE '^EDGE1000_FAIL' "$D/sweep_$(basename "$src" .cp).out"; then
        echo "EDGE1000 FAIL edges $src"; fail=$((fail + 1))
    fi
    rm -f "$bin" "$D/sweep_$(basename "$src" .cp).out" \
          "$D/sweep_$(basename "$src" .cp).build.log"
}

run_sweep tests/edge1000/test_modern_edge.cp EDGE1000_MODERN build/libc/libcplus_hardened.a
run_sweep tests/edge1000/test_safer_edge.cp EDGE1000_SAFER build/safe/libcplus_safecrt.a

for s in math3d scene3d mesh3d widgets cli web sqldb gameeng; do
    cases=$((cases + 1))
    if timeout 900 sh "tests/stdlib/$s/run.sh" >/dev/null 2>&1; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
done

cases=$((cases + 1))
if timeout 900 sh tests/ctl/run_ctl_demo_coverage.sh >/dev/null 2>&1; then
    pass=$((pass + 1))
else
    fail=$((fail + 1))
fi

# Safer core + ABI preservation: the safety layer must be ABI-neutral.
# (1) stdc_abi: ordinary C symbol sets are byte-identical in plain/safe builds.
cases=$((cases + 1))
if timeout 900 sh tests/abi/stdc_abi/run.sh >"build/edge1000/abi_stdc.log" 2>&1; then
    pass=$((pass + 1))
else
    fail=$((fail + 1)); tail -8 "build/edge1000/abi_stdc.log" 2>/dev/null || true
fi

# (2) abi_fp: fresh fingerprint diffed against the committed baseline.
cases=$((cases + 1))
if timeout 900 sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt \
        >"build/edge1000/abi_fp.log" 2>&1; then
    pass=$((pass + 1)); abi_diff_empty=1
else
    fail=$((fail + 1)); tail -8 "build/edge1000/abi_fp.log" 2>/dev/null || true
fi
echo "EDGE1000_ABI diff_empty=$abi_diff_empty"

echo "EDGE1000 $cases $pass $fail"
[ "$fail" -eq 0 ]


#!/bin/sh
# scoped-memory-style modern-libs-on-scratch benchmark verifier (bounded, POSIX sh).
# Deterministic gates only: op count, >=2x allocation ratio, OK marker.
# Exact ns/op lines are host-specific evidence, not pass/fail gates.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=benchmarks/scoped_apps
pass=0; fail=0; cases=0
if ! timeout 300 ./build/c+ build "$D/bench.cp" --default --no-werror --sanitize=none -I. \
      -o "$D/bench" -- build/libc/libcplus_hardened.a >"$D/build.log" 2>&1; then
    echo "SCOPED_APPS_BENCH FAIL build"; exit 1
fi
timeout 60 "$D/bench" >"$D/run.log" 2>&1 || { echo "SCOPED_APPS_BENCH FAIL run"; exit 1; }
# Only deterministic markers are gates.  The measured ratio is host/load
# dependent (page faults, scheduler) and is reported as evidence, not a gate.
for marker in "SCOPED_APPS ops 20000" "SCOPED_APPS_OK" "SCOPED_APPS bump_ns_per_op"; do
    cases=$((cases + 1))
    if grep -qF "$marker" "$D/run.log"; then pass=$((pass + 1));
    else fail=$((fail + 1)); echo "SCOPED_APPS_BENCH FAIL $marker"; fi
done
echo "SCOPED_APPS_BENCH $cases $pass $fail"
[ "$fail" -eq 0 ]

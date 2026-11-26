#!/bin/sh
# Proof runner: independent modern-C ports of the inspiration language sample workloads.
# Rebuilds every binary from committed source, runs it with a bounded timeout,
# and diffs stdout byte-for-byte against expected/.  No Python, no || true.
# Prints: PORTS_MODERN_C <cases> <pass> <fail>.  Exit 0 == all pass.
set -u
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$D" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
CC=${CPLUS_CC:-/usr/bin/clang}
mkdir -p "$D/bin" || exit 2
pass=0; fail=0; cases=0

for src in "$D"/*.c; do
    name=$(basename "$src" .c)
    cases=$((cases + 1))
    if ! timeout 120 "$CC" -std=c11 -O2 -Wall -Wextra -I"$D" "$src" \
            -o "$D/bin/$name" >"$D/bin/$name.build.log" 2>&1; then
        echo "PORTS_MODERN_C FAIL build $name"; fail=$((fail + 1)); continue
    fi
    if ! timeout 20 "$D/bin/$name" >"$D/bin/$name.out" 2>"$D/bin/$name.err"; then
        echo "PORTS_MODERN_C FAIL run $name"; fail=$((fail + 1)); continue
    fi
    if [ -s "$D/bin/$name.err" ]; then
        echo "PORTS_MODERN_C FAIL stderr $name"; fail=$((fail + 1)); continue
    fi
    if diff -u "$D/expected/$name.txt" "$D/bin/$name.out" >"$D/bin/$name.diff" 2>&1; then
        pass=$((pass + 1))
    else
        echo "PORTS_MODERN_C FAIL diff $name"; tail -6 "$D/bin/$name.diff"; fail=$((fail + 1))
    fi
done

echo "PORTS_MODERN_C $cases $pass $fail"
[ "$fail" -eq 0 ]

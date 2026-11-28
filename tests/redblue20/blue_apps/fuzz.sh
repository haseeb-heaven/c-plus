#!/bin/sh
# POSIX-shell bounded hostile-input driver for BLUE-TEAM 18/20.
# 106 apps x 100 fixed seeded scenarios = 10600 invocations, 10s bound each.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
BLUE=tests/redblue20/blue_apps
ART=$BLUE/artifacts
RW=$ART/real_world
CPA=$ART/cross_platform
pass=0; fail=0; total=0
for bin in "$RW"/* "$CPA"/*; do
    [ -x "$bin" ] || continue
    i=0
    while [ "$i" -lt 100 ]; do
        rc=0
        timeout 10 "$bin" "$(printf 'BAD_%d' "$i")" >/tmp/ba.out 2>/tmp/ba.err || rc=$?
        total=$((total+1))
        errb=$(wc -c </tmp/ba.err)
        if [ "$rc" -eq 0 ] && [ "$errb" -eq 0 ]; then pass=$((pass+1));
        else fail=$((fail+1)); fi
        i=$((i+1))
    done
done
printf 'BLUE20 apps %d %d %d\n' "$total" "$pass" "$fail"
[ "$fail" -eq 0 ]

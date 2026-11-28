#!/bin/sh
# Build and run all 100 apps natively on macOS (Apple clang).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
unset SDKROOT || true
D=tests/cross_platform_apps
OUT=$D/out-macos
mkdir -p "$OUT"
sh "$D/generate.sh" >/dev/null

pass=0; fail=0; bfail=0
i=0
while [ "$i" -lt 100 ]; do
    f="$D/src/app_$i.c"
    if timeout 60 /usr/bin/clang -O2 -Wall -Wextra -pedantic "$f" -o "$OUT/app_$i" \
         >"$OUT/app_$i.build.log" 2>&1; then
        rc=0
        timeout 20 "$OUT/app_$i" >"$OUT/app_$i.out" 2>"$OUT/app_$i.err" || rc=$?
        if [ "$rc" -eq 0 ] && grep -q -- '-1 -1' "$OUT/app_$i.out"; then
            pass=$((pass+1))
        else
            fail=$((fail+1)); echo "FAIL app_$i rc=$rc $(cat "$OUT/app_$i.out" 2>/dev/null)"
        fi
    else
        bfail=$((bfail+1)); echo "BUILDFAIL app_$i"
    fi
    i=$((i+1))
done
printf 'MACOS_APPS built=%d pass=%d fail=%d build_fail=%d\n' \
    "$((pass+fail))" "$pass" "$fail" "$bfail"
[ "$fail" -eq 0 ] && [ "$bfail" -eq 0 ] && [ "$pass" -eq 100 ]

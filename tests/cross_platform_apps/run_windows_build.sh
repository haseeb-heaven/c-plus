#!/bin/sh
# Cross-compile all 100 apps to Windows PE x86-64 with mingw-w64 and verify
# each binary is a real PE32+ executable with the exact exported entry.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
D=tests/cross_platform_apps
OUT=$D/out-win
mkdir -p "$OUT"
sh "$D/generate.sh" >/dev/null

CC=x86_64-w64-mingw32-gcc
command -v "$CC" >/dev/null 2>&1 || { echo "NO_MINGW $CC"; exit 2; }
pass=0; fail=0
i=0
while [ "$i" -lt 100 ]; do
    f="$D/src/app_$i.c"
    if timeout 60 "$CC" -O2 "$f" -o "$OUT/app_$i.exe" >"$OUT/app_$i.build.log" 2>&1; then
        if file "$OUT/app_$i.exe" | grep -q 'PE32+ executable.*x86-64'; then
            pass=$((pass+1))
        else
            fail=$((fail+1)); echo "BAD_PE app_$i"
        fi
    else
        fail=$((fail+1)); echo "BUILDFAIL app_$i"
    fi
    i=$((i+1))
done
printf 'WINDOWS_BUILD compiled=%d verified_pe=%d fail=%d\n' "$((pass+fail))" "$pass" "$fail"
[ "$fail" -eq 0 ] && [ "$pass" -eq 100 ]

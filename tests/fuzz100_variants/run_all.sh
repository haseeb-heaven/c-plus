#!/bin/sh
# Build and run all 100 generated allocator-fuzz variants with real exit-code
# checks, bounded timeouts, and no silent skips.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
export PATH="$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH"
unset SDKROOT || true
DIR=tests/fuzz100_variants
OUT=$DIR/out
mkdir -p "$OUT"
sh "$DIR/generate.sh" >/dev/null

built=0; bfail=0; pass=0; fail=0; timeout_n=0
i=0
while [ "$i" -lt 100 ]; do
    f="$DIR/apps/legacy_examples/variant_$i.cp"
    if timeout 60 ./build/c+ build "$f" --default --no-werror --sanitize=none -I. \
         -o "$OUT/v$i" -- build/libc/libcplus_hardened.a \
         >"$OUT/v$i.build.log" 2>&1; then
        built=$((built+1))
        rc=0
        timeout 30 "$OUT/v$i" >"$OUT/v$i.out" 2>"$OUT/v$i.err" || rc=$?
        if [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ]; then
            timeout_n=$((timeout_n+1))
            echo "TIMEOUT/SIGNAL variant_$i rc=$rc"
        elif [ "$rc" -eq 0 ] && grep -q "VARIANT $i 0" "$OUT/v$i.out"; then
            pass=$((pass+1))
        else
            fail=$((fail+1))
            echo "FAIL variant_$i rc=$rc $(tail -1 "$OUT/v$i.out" 2>/dev/null)"
        fi
    else
        bfail=$((bfail+1))
        echo "BUILD_FAIL variant_$i $(grep -m1 error: "$OUT/v$i.build.log" | head -c 80)"
    fi
    i=$((i+1))
done
printf 'FUZZ100_VARIANTS built=%d build_fail=%d pass=%d fail=%d timeout_signal=%d\n' \
    "$built" "$bfail" "$pass" "$fail" "$timeout_n"
[ "$bfail" -eq 0 ] && [ "$fail" -eq 0 ] && [ "$timeout_n" -eq 0 ]

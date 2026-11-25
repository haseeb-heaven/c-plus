#!/bin/sh
# Before/after evidence for the one genuine finding:
#   cplus_memcmp_s_auto(NULL, NULL, n>0) reported "equal" (0) instead of
#   refusing with the -1 error sentinel.
#
# BEFORE = stdlib/safe/src/safe_memory.c at HEAD (pre-fix).
# AFTER  = current working-tree safe_memory.c (with the fix).
# Only the single w_safe_memory.o archive member is swapped; every other
# object (safeclib, allocator, string) is identical in both archives.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
CC=${CPLUS_CLANG:-/usr/bin/clang}
OUT="$HERE/out"
TMP="$OUT/ba"
mkdir -p "$TMP/src"
INC="-I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include -I$ROOT/build/safe"

# Build the AFTER archive from current sources.
CPLUS_CLANG="$CC" sh "$ROOT/scripts/build_safecrt.sh" >/dev/null
cp "$ROOT/build/safe/libcplus_safecrt.a" "$TMP/after.a"

# Build the BEFORE object from HEAD and swap it into a copy of the archive.
cp "$ROOT"/stdlib/safe/safe_*.h "$TMP/"
git -C "$ROOT" show HEAD:stdlib/safe/src/safe_memory.c > "$TMP/src/safe_memory.c"
"$CC" -O2 -Wall -Wextra -std=c11 \
    -I"$ROOT/third_party/safeclib/include" -I"$ROOT/build/safe" \
    -I"$ROOT/stdlib/safe" -I"$ROOT/third_party/exceptions4c" \
    -c "$TMP/src/safe_memory.c" -o "$TMP/w_safe_memory.o"
cp "$TMP/after.a" "$TMP/before.a"
/usr/bin/ar r "$TMP/before.a" "$TMP/w_safe_memory.o"

for lib in before after; do
    "$CC" -O2 -Wall -Wextra -std=c11 $INC \
        "$HERE/regression_memcmp_null.c" "$TMP/$lib.a" \
        -pthread -lm -o "$TMP/reg_$lib"
    "$CC" -O2 -Wall -Wextra -std=c11 $INC \
        "$HERE/memory2_harness.c" "$TMP/$lib.a" \
        -pthread -lm -o "$TMP/harness_$lib"
done

echo "===== BEFORE (HEAD, pre-fix) ====="
set +e
timeout 30 "$TMP/reg_before" 2>&1; echo "regression_exit=$?"
timeout 60 "$TMP/harness_before" 2>/dev/null | grep -E 'memcmp_both_null|ATTACK_TOTALS|FUZZ_TOTALS'
set -e

echo "===== AFTER (fixed) ====="
set +e
timeout 30 "$TMP/reg_after" 2>&1; echo "regression_exit=$?"
timeout 60 "$TMP/harness_after" 2>/dev/null | grep -E 'memcmp_both_null|ATTACK_TOTALS|FUZZ_TOTALS'
set -e

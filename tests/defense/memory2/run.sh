#!/bin/sh
# PHASE 5-7 hardened build + deterministic adversarial run.
#
# Rebuilds the parent-owned SafeCRT archive from the current (fixed) sources,
# then compiles and runs the fork-based harness (16 named attacks + 1200
# fixed-seed fuzz cases) and the minimal regression under a 60s budget.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
CC=${CPLUS_CLANG:-/usr/bin/clang}
OUT="$HERE/out"
mkdir -p "$OUT"
INC="-I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include -I$ROOT/build/safe"

echo "== rebuild safecrt archive =="
CPLUS_CLANG="$CC" sh "$ROOT/scripts/build_safecrt.sh" >/dev/null

echo "== compile harness + regression =="
"$CC" -O2 -Wall -Wextra -std=c11 $INC \
    "$HERE/memory2_harness.c" "$ROOT/build/safe/libcplus_safecrt.a" \
    -pthread -lm -o "$OUT/memory2_harness"
"$CC" -O2 -Wall -Wextra -std=c11 $INC \
    "$HERE/regression_memcmp_null.c" "$ROOT/build/safe/libcplus_safecrt.a" \
    -pthread -lm -o "$OUT/regression_memcmp_null"

echo "== harness (timeout 60) =="
set +e
timeout 60 "$OUT/memory2_harness" all >"$OUT/harness.log" 2>&1
HRC=$?
set -e
cat "$OUT/harness.log"

echo "== regression (timeout 30) =="
set +e
timeout 30 "$OUT/regression_memcmp_null" >"$OUT/regression.log" 2>&1
RRC=$?
set -e
cat "$OUT/regression.log"

printf 'harness_exit=%s regression_exit=%s\n' "$HRC" "$RRC"
[ "$HRC" -eq 0 ] && [ "$RRC" -eq 0 ]

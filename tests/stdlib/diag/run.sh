#!/bin/sh
# Bounded edge-case runner for the rich diagnostic engine. POSIX sh, no Python.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
OUT=tests/stdlib/diag/out
mkdir -p "$OUT"
CC=${CPLUS_CLANG:-clang}

"$CC" -O1 -std=c11 -Wall -Wextra -Istdlib/safe \
    tests/stdlib/diag/test_diag.c build/safe/libcplus_safecrt.a \
    -o "$OUT/test_diag" 2>"$OUT/build.log"

timeout 60 "$OUT/test_diag" >"$OUT/results.log" 2>"$OUT/stderr.log"
rc=$?
grep -E '^(PASS|FAIL|DIAG_TEST) ' "$OUT/results.log" || true
[ "$rc" -eq 0 ] || { echo "diag: RUN FAILED rc=$rc"; exit 1; }
grep -q '^DIAG_TEST 23 23 0$' "$OUT/results.log" || {
    echo "diag: unexpected summary"; tail -3 "$OUT/results.log"; exit 1;
}
echo "diag: PASS (23 edge cases, exact legacy output preserved)"

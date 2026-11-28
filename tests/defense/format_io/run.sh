#!/bin/sh
# Blue-team agent 9: checked format + I/O defense regressions.
# Rebuilds the SafeCRT archive, then runs each regression binary under a 30s
# timeout.  Exit status is non-zero if any case fails.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
HERE="$ROOT/tests/defense/format_io"
TMP=${CPLUS_FMTIO_DEFENSE_TMP:-/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/defense_format_io}
CC=${CPLUS_CLANG:-/usr/bin/clang}
mkdir -p "$TMP"

INC="-I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/exceptions4c"
LIB="$ROOT/build/safe/libcplus_safecrt.a"

echo "[build] safeclib + SafeCRT archive"
CPLUS_CLANG="$CC" sh "$ROOT/scripts/build_safecrt.sh" >/dev/null 2>&1

FAIL=0
for t in test_f1_f2_regression test_format_hardening test_io_hardening; do
    echo "[build] $t"
    $CC -O1 -g -std=c11 -w $INC "$HERE/$t.c" "$LIB" -lpthread -lm -o "$TMP/$t"
    set +e
    timeout 30 "$TMP/$t" >"$TMP/$t.out" 2>&1
    rc=$?
    set -e
    tail -1 "$TMP/$t.out"
    if [ "$rc" -eq 0 ]; then
        echo "PASS $t"
    else
        echo "FAIL $t (exit $rc)"
        FAIL=1
    fi
done
exit "$FAIL"

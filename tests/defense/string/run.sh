#!/bin/sh
# Build the SafeCRT archive and run every string-hardening regression case in
# its own 30s-bounded process.  Exit status is non-zero if any case fails.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
# Prefer a compiler with a working SDK sysroot.  On this host the Homebrew
# clang defaults to a missing MacOSX27.sdk while /usr/bin/clang resolves the
# installed CommandLineTools SDK correctly; callers can override with CC.
if [ -z "${CC:-}" ]; then
    if [ -x /usr/bin/clang ] && /usr/bin/clang -x c -E - </dev/null >/dev/null 2>&1; then
        CC=/usr/bin/clang
    else
        CC=clang
    fi
fi
CPLUS_CLANG=${CPLUS_CLANG:-$CC}
INC="-I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include"
LIB="$ROOT/build/safe/libcplus_safecrt.a"
BIN="$ROOT/build/safe/test_string_hardening"

echo "[build] safeclib + SafeCRT archive"
CPLUS_CLANG="$CPLUS_CLANG" sh "$ROOT/scripts/build_safecrt.sh" >/dev/null 2>&1

echo "[build] $BIN"
$CC -O1 -g -std=c11 $INC \
    "$ROOT/tests/defense/string/test_string_hardening.c" \
    "$LIB" -lpthread -lm -o "$BIN"

FAIL=0
for c in null_args zero_length one_small overlap sizemax_smax truncation \
         codes handler \
         unterminated_src_strcpy unterminated_src_strcat \
         unterminated_dest_strcat unterminated_dest_strncat \
         auto_macro_valid; do
    set +e
    timeout 30 "$BIN" "$c"
    rc=$?
    set -e
    if [ "$rc" -eq 0 ]; then
        echo "PASS $c"
    else
        echo "FAIL $c (exit $rc)"
        FAIL=1
    fi
done
exit "$FAIL"

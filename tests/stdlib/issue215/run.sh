#!/bin/sh
# Issue 215 regression suite: initial-URL protocol policy and set_headers()
# ownership ordering.  Fully offline (loopback echo server).
#
# --no-werror is used because stdlib/http.hp carries a pre-existing analyzer
# warning under this toolchain; --sanitize=none avoids recompiling the C+
# runtime with sanitizers (the driver passes -lcurl through that step).  No
# assertion is relaxed: the C++-level checks and exact stdout diff still gate.
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$ROOT/build/stdlib-tests/issue215"
BUILD_TIMEOUT=${ISSUE215_BUILD_TIMEOUT:-90}
RUN_TIMEOUT=${ISSUE215_RUN_TIMEOUT:-30}
KILL_AFTER=${ISSUE215_KILL_AFTER:-5}

export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT

command -v timeout >/dev/null 2>&1 || {
  echo "error: 'timeout' utility not found; refusing unbounded tests" >&2
  exit 2
}
[ -x "$CPLUS" ] || {
  echo "error: C+ compiler missing: $CPLUS (build ./build/c+ first)" >&2
  exit 2
}
pkg-config --exists libcurl || {
  echo "error: libcurl development files missing" >&2
  exit 2
}

mkdir -p "$OUT" || exit 2
cd "$ROOT" || exit 2
rm -f "$OUT/test_issue215" "$OUT/build.log" "$OUT/stdout" "$OUT/stderr" \
      "$OUT/diff"

timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$DIR/test_issue215.cp" \
  --default --no-werror --sanitize=none -I. $(pkg-config --cflags --libs libcurl) \
  -o "$OUT/test_issue215" >"$OUT/build.log" 2>&1
brc=$?
if [ "$brc" -ne 0 ]; then
  echo "BUILD-FAIL rc=$brc (see $OUT/build.log)"
  tail -n 12 "$OUT/build.log" | sed 's/^/           /'
  exit 1
fi

timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$OUT/test_issue215" \
  </dev/null >"$OUT/stdout" 2>"$OUT/stderr"
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "RUN-FAIL rc=$rc"
  head -n 12 "$OUT/stderr" | sed 's/^/           /'
  head -n 12 "$OUT/stdout" | sed 's/^/           /'
  exit 1
fi
if ! diff -u "$DIR/expected.txt" "$OUT/stdout" >"$OUT/diff" 2>&1; then
  echo "STDOUT-MISMATCH (see $OUT/diff)"
  head -n 16 "$OUT/diff" | sed 's/^/           /'
  exit 1
fi

echo "ISSUE215 SUITE PASS"
exit 0

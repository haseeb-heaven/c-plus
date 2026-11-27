#!/bin/sh
# Issue 216 regression suite: sign-aware serialization size and bounded
# label writes.  Bounded timeouts; exact stdout diff gates every assertion.
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$ROOT/build/stdlib-tests/issue216"
BUILD_TIMEOUT=${ISSUE216_BUILD_TIMEOUT:-90}
RUN_TIMEOUT=${ISSUE216_RUN_TIMEOUT:-30}
KILL_AFTER=${ISSUE216_KILL_AFTER:-5}

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

mkdir -p "$OUT" || exit 2
cd "$ROOT" || exit 2
rm -f "$OUT/test_issue216" "$OUT/build.log" "$OUT/stdout" "$OUT/stderr" \
      "$OUT/diff"

# --no-werror: stdlib/widgets.hp has pre-existing analyzer warnings under
# this toolchain (the repository's widgets suite uses the same relaxation).
timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$DIR/test_issue216.cp" \
  --default --no-werror --sanitize=none -I. -o "$OUT/test_issue216" \
  >"$OUT/build.log" 2>&1
brc=$?
if [ "$brc" -ne 0 ]; then
  echo "BUILD-FAIL rc=$brc (see $OUT/build.log)"
  tail -n 12 "$OUT/build.log" | sed 's/^/           /'
  exit 1
fi

timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$OUT/test_issue216" \
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

echo "ISSUE216 SUITE PASS"
exit 0

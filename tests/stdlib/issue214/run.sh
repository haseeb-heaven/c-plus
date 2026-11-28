#!/bin/sh
# Issue 214 regression suite: bounded writes in sqldb_load().
#
# Builds with the repository C+ compiler in DEFAULT mode with warnings-as-
# errors relaxed (the header carries pre-existing ownership warnings under
# this toolchain), but KEEPS the default undefined-behaviour sanitizer: the
# original 1-byte stack overflow is a runtime out-of-bounds subscript and is
# reported as such.  Bounded timeouts on every compiler/program invocation.
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$ROOT/build/stdlib-tests/issue214"
BUILD_TIMEOUT=${ISSUE214_BUILD_TIMEOUT:-90}
RUN_TIMEOUT=${ISSUE214_RUN_TIMEOUT:-30}
KILL_AFTER=${ISSUE214_KILL_AFTER:-5}

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
rm -f "$OUT/test_issue214" "$OUT/build.log" "$OUT/stdout" "$OUT/stderr" \
      "$OUT/diff" "$OUT/rows.txt"

timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$DIR/test_issue214.cp" \
  --default --no-werror -I. -o "$OUT/test_issue214" >"$OUT/build.log" 2>&1
brc=$?
if [ "$brc" -ne 0 ]; then
  echo "BUILD-FAIL rc=$brc (see $OUT/build.log)"
  tail -n 12 "$OUT/build.log" | sed 's/^/           /'
  exit 1
fi

timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$OUT/test_issue214" \
  </dev/null >"$OUT/stdout" 2>"$OUT/stderr"
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "RUN-FAIL rc=$rc"
  head -n 8 "$OUT/stderr" | sed 's/^/           /'
  head -n 8 "$OUT/stdout" | sed 's/^/           /'
  exit 1
fi
if ! diff -u "$DIR/expected.txt" "$OUT/stdout" >"$OUT/diff" 2>&1; then
  echo "STDOUT-MISMATCH (see $OUT/diff)"
  head -n 12 "$OUT/diff" | sed 's/^/           /'
  exit 1
fi

echo "ISSUE214 SUITE PASS"
exit 0

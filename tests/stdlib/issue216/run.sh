#!/bin/sh
# Issue #216 regression harness (deterministic, no sanitizer, no compiler build).
#
#   sh tests/stdlib/issue216/run.sh
#
# Uses the already-built C+ compiler (CPLUS_BIN overrides ./build/c+). Builds
# only test_issue216.cp, then runs it twice and requires identical stdout,
# empty stderr, no "FAIL " line, and a final "ISSUE216 <checks> 0" line with
# checks > 0. All generated files stay under results/.
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$DIR/results"
BUILD_TIMEOUT=${ISSUE216_BUILD_TIMEOUT:-60}
RUN_TIMEOUT=${ISSUE216_RUN_TIMEOUT:-30}
KILL_AFTER=${ISSUE216_KILL_AFTER:-5}

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

mkdir -p "$OUT" || exit 2
if ! command -v timeout >/dev/null 2>&1; then
  echo "error: 'timeout' utility not found; refusing unbounded tests" >&2
  exit 2
fi
if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler missing: $CPLUS (build ./build/c+ first)" >&2
  exit 2
fi

LIB=""
if [ -f "$ROOT/build/libc/libc/libcplus_hardened.a" ]; then
  LIB="build/libc/libc/libcplus_hardened.a"
elif [ -f "$ROOT/build/libc/libcplus_hardened.a" ]; then
  LIB="build/libc/libcplus_hardened.a"
fi

BIN="$OUT/test_issue216"
BLOG="$OUT/build.log"
SO1="$OUT/run1.stdout"
SE1="$OUT/run1.stderr"
SO2="$OUT/run2.stdout"
SE2="$OUT/run2.stderr"
rm -f "$BIN" "$BLOG" "$SO1" "$SE1" "$SO2" "$SE2"

cd "$ROOT" || exit 2
if [ -n "$LIB" ]; then
  timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$DIR/test_issue216.cp" \
    --default --no-werror --sanitize=none -I. -o "$BIN" -- "$LIB" >"$BLOG" 2>&1
else
  timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$DIR/test_issue216.cp" \
    --default --no-werror --sanitize=none -I. -o "$BIN" >"$BLOG" 2>&1
fi
brc=$?
if [ "$brc" -ne 0 ]; then
  echo "BUILD-FAIL rc=$brc (see $BLOG)"
  tail -n 12 "$BLOG" | sed 's/^/           /'
  exit 1
fi
echo "BUILD ok: $CPLUS build test_issue216.cp --default --no-werror --sanitize=none -I."

run_one() {
  timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$BIN" </dev/null >"$1" 2>"$2"
}

run_one "$SO1" "$SE1"
r1=$?
run_one "$SO2" "$SE2"
r2=$?

fail=0
if [ "$r1" -ne 0 ] || [ "$r2" -ne 0 ]; then
  echo "RUN-FAIL rc1=$r1 rc2=$r2"
  fail=1
fi
if [ -s "$SE1" ] || [ -s "$SE2" ]; then
  echo "STDERR-NONEMPTY"
  head -n 6 "$SE1" "$SE2" | sed 's/^/           /'
  fail=1
fi
if ! cmp -s "$SO1" "$SO2"; then
  echo "NONDETERMINISTIC: run1 stdout != run2 stdout"
  diff -u "$SO1" "$SO2" | head -n 12 | sed 's/^/           /'
  fail=1
fi
if grep -q '^FAIL ' "$SO1"; then
  echo "CHECK-FAIL"
  grep '^FAIL ' "$SO1" | head -n 12 | sed 's/^/           /'
  fail=1
fi

final=$(tail -n 1 "$SO1")
checks=""
failures=""
case "$final" in
  "ISSUE216 "*)
    rest=${final#ISSUE216 }
    checks=${rest%% *}
    failures=${rest##* }
    ;;
esac
numeric() { case "$1" in ''|*[!0-9]*) return 1 ;; *) return 0 ;; esac; }
if ! numeric "$checks" || ! numeric "$failures"; then
  echo "BAD-SUMMARY: '$final'"
  fail=1
else
  if [ "$checks" -le 0 ]; then echo "BAD-SUMMARY: zero checks"; fail=1; fi
  if [ "$failures" -ne 0 ]; then echo "BAD-SUMMARY: failures=$failures"; fail=1; fi
  tag_count=$(grep -c '^ISSUE216 ' "$SO1")
  if [ "$tag_count" -ne 1 ]; then
    echo "BAD-SUMMARY: $tag_count summary lines"
    fail=1
  fi
fi

if [ "$fail" -ne 0 ]; then
  echo "ISSUE216 SUITE FAIL"
  exit 1
fi
echo "RUN ok: $final (stdout byte-identical across 2 runs, stderr empty)"
echo "ISSUE216 SUITE PASS"
exit 0

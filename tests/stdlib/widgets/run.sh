#!/bin/sh
# Headless widget-toolkit core suite (deterministic, no sanitizer).
#
#   sh tests/stdlib/widgets/run.sh
#
# Builds test_widgets.cp with the repo C+ compiler in DEFAULT mode with
# sanitizers disabled, then runs the binary twice and requires:
#   * build and both runs exit 0,
#   * run stderr is empty on both invocations,
#   * stdout is byte-for-byte identical across invocations (determinism),
#   * final stdout line is exactly "WIDGETS <checks> 0" with checks > 0,
#   * no "FAIL " line appears in stdout.
# Every compiler/program invocation is wrapped in `timeout` (SIGKILL escalation)
# so a hung run can never be reported as a pass.
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$DIR/results"
BUILD_TIMEOUT=${WIDGETS_BUILD_TIMEOUT:-60}
RUN_TIMEOUT=${WIDGETS_RUN_TIMEOUT:-30}
KILL_AFTER=${WIDGETS_KILL_AFTER:-5}

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

# The task's archive path is preferred; older trees keep the lib one level up.
# The C+ driver also links its runtime automatically, so a missing archive is
# reported but is not silently treated as a successful link check.
LIB=""
if [ -f "$ROOT/build/libc/libc/libcplus_hardened.a" ]; then
  LIB="build/libc/libc/libcplus_hardened.a"
elif [ -f "$ROOT/build/libc/libcplus_hardened.a" ]; then
  LIB="build/libc/libcplus_hardened.a"
else
  echo "note: libcplus_hardened.a not found; relying on the C+ runtime link" >&2
fi

BIN="$OUT/test_widgets"
BLOG="$OUT/build.log"
SO1="$OUT/run1.stdout"
SE1="$OUT/run1.stderr"
SO2="$OUT/run2.stdout"
SE2="$OUT/run2.stderr"
rm -f "$BIN" "$BLOG" "$SO1" "$SE1" "$SO2" "$SE2"

cd "$ROOT" || exit 2
# -o must precede `--`: after `--` arguments go straight to the C backend.
if [ -n "$LIB" ]; then
  timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$DIR/test_widgets.cp" \
    --default --sanitize=none -I. -o "$BIN" -- "$LIB" >"$BLOG" 2>&1
else
  timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$DIR/test_widgets.cp" \
    --default --sanitize=none -I. -o "$BIN" >"$BLOG" 2>&1
fi
brc=$?
if [ "$brc" -ne 0 ]; then
  echo "BUILD-FAIL rc=$brc (see $BLOG)"
  tail -n 12 "$BLOG" | sed 's/^/           /'
  exit 1
fi
echo "BUILD ok: $CPLUS build test_widgets.cp --default --sanitize=none -I. -- ${LIB:-<runtime only>}"

run_one() {
  _so="$1"; _se="$2"
  timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$BIN" </dev/null >"$_so" 2>"$_se"
  return $?
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
  echo "STDERR-NONEMPTY (C+ safety diagnostic or crash)"
  head -n 6 "$SE1" "$SE2" | sed 's/^/           /'
  fail=1
fi
if ! cmp -s "$SO1" "$SO2"; then
  echo "NONDETERMINISTIC: run1 stdout != run2 stdout"
  diff -u "$SO1" "$SO2" | head -n 12 | sed 's/^/           /'
  fail=1
fi
if grep -q '^FAIL ' "$SO1"; then
  echo "CHECK-FAIL: test program reported failures"
  grep '^FAIL ' "$SO1" | head -n 12 | sed 's/^/           /'
  fail=1
fi

final=$(tail -n 1 "$SO1")
checks=""
failures=""
case "$final" in
  "WIDGETS "*)
    rest=${final#WIDGETS }
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
  tag_count=$(grep -c '^WIDGETS ' "$SO1")
  if [ "$tag_count" -ne 1 ]; then
    echo "BAD-SUMMARY: $tag_count summary lines"
    fail=1
  fi
fi

if [ "$fail" -ne 0 ]; then
  echo "WIDGETS SUITE FAIL"
  exit 1
fi
echo "RUN ok: $final (stdout byte-identical across 2 runs, stderr empty)"
echo "WIDGETS SUITE PASS"
exit 0

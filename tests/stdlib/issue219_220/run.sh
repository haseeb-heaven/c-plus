#!/bin/sh
# Issues #219 / #220 deterministic regression harness (no project build).
#
#   sh tests/stdlib/issue219_220/run.sh
#
# Uses the already-built C+ compiler (CPLUS_BIN overrides ./build/c+).  Each
# test is compiled once, run twice, and must exit 0, print no "FAIL " line, end
# with "ISSUE<NNN> <checks> 0", and produce byte-identical stdout across the two
# runs.  Logs and binaries stay under results/.
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$DIR/results"
BUILD_TIMEOUT=${ISSUE219220_BUILD_TIMEOUT:-60}
RUN_TIMEOUT=${ISSUE219220_RUN_TIMEOUT:-30}
KILL_AFTER=${ISSUE219220_KILL_AFTER:-5}

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

mkdir -p "$OUT" || exit 2
if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler missing: $CPLUS (build ./build/c+ first)" >&2
  exit 2
fi

if command -v timeout >/dev/null 2>&1; then
  TIMEOUT="timeout -k $KILL_AFTER"
else
  TIMEOUT=""
fi

cd "$ROOT" || exit 2

COMMON="--default --no-werror --sanitize=none -I."
AUDIO_LIBS="-Ithird_party/miniaudio -Lbuild/ext -lminiaudio \
  -Wl,-framework,CoreFoundation -Wl,-framework,CoreAudio \
  -Wl,-framework,AudioToolbox"

fail=0

run_case() { # name source extra-args...
  name=$1
  shift
  bin="$OUT/test_issue$name"
  blog="$OUT/test_issue$name.build.log"
  so1="$OUT/test_issue$name.run1.stdout"
  se1="$OUT/test_issue$name.run1.stderr"
  so2="$OUT/test_issue$name.run2.stdout"
  se2="$OUT/test_issue$name.run2.stderr"
  rm -f "$bin" "$blog" "$so1" "$se1" "$so2" "$se2"

  # shellcheck disable=SC2086
  if ! $TIMEOUT "$BUILD_TIMEOUT" "$CPLUS" build "$DIR/test_issue$name.cp" \
      $COMMON "$@" -o "$bin" >"$blog" 2>&1; then
    echo "BUILD-FAIL issue$name (see $blog)"
    tail -n 12 "$blog" | sed 's/^/           /'
    fail=1
    return
  fi
  echo "BUILD ok issue$name"

  $TIMEOUT "$RUN_TIMEOUT" "$bin" </dev/null >"$so1" 2>"$se1"
  r1=$?
  $TIMEOUT "$RUN_TIMEOUT" "$bin" </dev/null >"$so2" 2>"$se2"
  r2=$?
  if [ "$r1" -ne 0 ] || [ "$r2" -ne 0 ]; then
    echo "RUN-FAIL issue$name rc1=$r1 rc2=$r2"
    fail=1
  fi
  if ! cmp -s "$so1" "$so2"; then
    echo "NONDETERMINISTIC issue$name"
    diff -u "$so1" "$so2" | head -n 12 | sed 's/^/           /'
    fail=1
  fi
  if grep -q '^FAIL ' "$so1"; then
    echo "CHECK-FAIL issue$name"
    grep '^FAIL ' "$so1" | head -n 20 | sed 's/^/           /'
    fail=1
  fi
  if grep -q 'error:' "$se1"; then
    echo "STDERR-ERROR issue$name"
    grep 'error:' "$se1" | head -n 6 | sed 's/^/           /'
    fail=1
  fi

  final=$(tail -n 1 "$so1")
  case "$final" in
    "ISSUE$name "*)
      rest=${final#ISSUE$name }
      checks=${rest%% *}
      failures=${rest##* }
      ;;
    *)
      checks=""
      failures=""
      ;;
  esac
  numeric() { case "$1" in ''|*[!0-9]*) return 1 ;; *) return 0 ;; esac; }
  if ! numeric "$checks" || ! numeric "$failures"; then
    echo "BAD-SUMMARY issue$name: '$final'"
    fail=1
  elif [ "$checks" -le 0 ] || [ "$failures" -ne 0 ]; then
    echo "BAD-SUMMARY issue$name: '$final'"
    fail=1
  else
    echo "RUN ok issue$name: $final (2 identical runs)"
  fi
}

run_case 219
# shellcheck disable=SC2086
run_case 220 $AUDIO_LIBS

if [ "$fail" -ne 0 ]; then
  echo "ISSUE219_220 SUITE FAIL"
  exit 1
fi
echo "ISSUE219_220 SUITE PASS"
exit 0

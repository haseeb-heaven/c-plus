#!/bin/sh
# Per-method runtime test runner for the hardened libc / SafeCRT layers.
#
# Builds both static libraries once, then compiles and runs every per-method
# test in tests/methods/ in a fixed group order. Each group is exercised in the
# SafeCRT layer (default: SafeCRT + libc) and, when the source defines a
# -DPLAIN_LIBC path, in the plain libc layer as well. One status line is
# printed per group/variant; stdout and stderr of every run are captured under
# build/methods/.
#
# Usage: sh tests/methods/run_methods.sh
#
# Environment:
#   CPLUS_CLANG  compiler to use (default: /usr/bin/clang when present)
#
# No ASAN_OPTIONS are set or suppressed here: the default build is not
# ASan-instrumented, and sanitizer tuning is deliberately out of scope.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"

# Prefer the SDK-aware system clang; honour CPLUS_CLANG when supplied.
if [ -z "${CPLUS_CLANG:-}" ]; then
  if [ -x /usr/bin/clang ]; then CPLUS_CLANG=/usr/bin/clang; else CPLUS_CLANG=clang; fi
fi
export CPLUS_CLANG
CC=$CPLUS_CLANG

SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
SYSROOT=
if [ -d "$SDK" ]; then SYSROOT="-isysroot $SDK"; fi

OUT="$ROOT/build/methods"
mkdir -p "$OUT"

INC="-I. -Istdlib/safe -Ilibc -Ilibc/install-include -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
LIBC="$ROOT/build/libc/libcplus_hardened.a"
SAFE="$ROOT/build/safe/libcplus_safecrt.a"
COMMON_LIBS="-lm -pthread"

# Fixed execution order, independent of filesystem enumeration.
METHOD_GROUPS="alloc memory string stdio conversion format wchar math_time logging"

PASS=0
FAIL=0

# Portable 30s timeout: use coreutils timeout when available, otherwise a
# background watchdog that kills the child after the requested seconds.
TIMEOUT_BIN=$(command -v timeout 2>/dev/null || command -v gtimeout 2>/dev/null || true)

run_with_timeout() {
  secs=$1
  shift
  if [ -n "$TIMEOUT_BIN" ]; then
    "$TIMEOUT_BIN" "$secs" "$@"
    return $?
  fi
  "$@" &
  child=$!
  ( sleep "$secs"; kill -9 "$child" 2>/dev/null ) 2>/dev/null &
  watcher=$!
  wait "$child"
  rc=$?
  kill "$watcher" 2>/dev/null || true
  wait "$watcher" 2>/dev/null || true
  return $rc
}

pass_line() {
  printf 'ok   %s (%s): %s\n' "$1" "$2" "$3"
  PASS=$((PASS + 1))
}

fail_line() {
  printf 'FAIL %s (%s): %s\n' "$1" "$2" "$3"
  shift 3
  for log in "$@"; do
    [ -f "$log" ] || continue
    printf '       [last 10 lines of %s]\n' "$(basename "$log")"
    tail -n 10 "$log" | sed 's/^/       /'
  done
  FAIL=$((FAIL + 1))
}

summary_line() {
  # Prefer the test's own "methods_<group>:" summary; fall back to last line.
  group=$1
  log=$2
  line=$(grep -E "methods_${group}:" "$log" 2>/dev/null | tail -n 1)
  if [ -z "$line" ]; then line=$(tail -n 1 "$log" 2>/dev/null); fi
  if [ -z "$line" ]; then line="(no output)"; fi
  printf '%s' "$line"
}

run_variant() {
  # $1 group, $2 variant, $3 extra defines, $4 link libraries
  group=$1
  variant=$2
  defs=$3
  libs=$4
  src="tests/methods/methods_${group}.c"
  bin="$OUT/methods_${group}_${variant}"
  build_log="$OUT/methods_${group}_${variant}.build.log"
  run_log="$OUT/methods_${group}_${variant}.run.log"

  # shellcheck disable=SC2086  # deliberate word splitting of flag groups
  if "$CC" $SYSROOT -O2 -std=c99 -Wall -Wextra -pedantic $defs $INC "$src" \
       $libs $COMMON_LIBS -o "$bin" >"$build_log" 2>&1; then
    if run_with_timeout 30 "$bin" >"$run_log" 2>&1; then
      # Do not hide failures behind a zero exit code: require a perfect
      # "<N>/<N> checks ok" summary when the suite prints one.
      summary=$(summary_line "$group" "$run_log")
      counts=$(printf '%s' "$summary" | sed -n 's/.*\([0-9][0-9]*\)\/\([0-9][0-9]*\) checks ok.*/\1 \2/p')
      if [ -n "$counts" ]; then
        got=${counts%% *}; want=${counts##* }
        if [ "$got" != "$want" ]; then
          fail_line "$group" "$variant" "incomplete summary ($got/$want)" "$run_log"
          return 0
        fi
      fi
      pass_line "$group" "$variant" "$summary"
    else
      rc=$?
      if [ "$rc" -eq 124 ]; then
        fail_line "$group" "$variant" "timed out after 30s" "$run_log"
      else
        fail_line "$group" "$variant" "run failed (exit $rc)" "$run_log"
      fi
    fi
  else
    fail_line "$group" "$variant" "compilation failed" "$build_log"
  fi
}

# --- build both libraries --------------------------------------------------
if ! sh tests/libc/build_libc.sh >"$OUT/build_libc.log" 2>&1; then
  echo "FATAL: libc build failed" >&2
  tail -n 12 "$OUT/build_libc.log" >&2 || true
  exit 1
fi
if ! sh scripts/build_safecrt.sh >"$OUT/build_safecrt.log" 2>&1; then
  echo "FATAL: safecrt build failed" >&2
  tail -n 12 "$OUT/build_safecrt.log" >&2 || true
  exit 1
fi

# --- per-group runs --------------------------------------------------------
for group in $METHOD_GROUPS; do
  src="tests/methods/methods_${group}.c"
  if [ ! -f "$src" ]; then
    # Only for incomplete parallel authoring; never counted as a failure.
    printf 'SKIP %s (file not present)\n' "$group"
    continue
  fi

  # SafeCRT layer: default build, links both static libraries.
  run_variant "$group" safecrt "" "$SAFE $LIBC"

  # Plain libc layer: meaningful only when the source defines a PLAIN_LIBC path.
  if grep -q 'PLAIN_LIBC' "$src"; then
    run_variant "$group" plain "-DPLAIN_LIBC" "$LIBC"
  else
    printf 'n/a   %s (plain): no plain variant\n' "$group"
  fi
done

echo
echo "method tests: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]

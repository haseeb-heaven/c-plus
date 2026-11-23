#!/bin/sh
# FUZZ10K / thread_async deterministic fuzz driver.
#
# Builds the harness with the repository C+ driver in --strict mode, then runs
# it under a 600-second process bound.  The harness itself arms a 5-second
# SIGALRM watchdog for each individual scenario, so a stuck join/loop exits
# with status 99 instead of hanging.  No wall-clock assertion is made.
#
# Usage:  sh tests/fuzz10k/thread_async/run.sh
# Output: tests/fuzz10k/thread_async/scenarios.log  (per-scenario evidence)
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT" || exit 2

SRC="$HERE/fuzz10k_thread_async.cp"
BIN="$HERE/fuzz10k_thread_async.bin"
LOG="$HERE/scenarios.log"
OUT="$HERE/run.stdout.log"
ERR="$HERE/run.stderr.log"

# Exact required build invocation.
./build/c+ build "$SRC" --strict --sanitize=none \
    -I/opt/homebrew/include -L/opt/homebrew/lib -luv -I. -o "$BIN"
build_rc=$?
if [ "$build_rc" -ne 0 ]; then
    echo "FUZZ10K: BUILD FAILED rc=$build_rc"
    exit "$build_rc"
fi

TIMEOUT=/usr/bin/timeout
if [ ! -x "$TIMEOUT" ]; then
    TIMEOUT=$(command -v timeout || command -v gtimeout || true)
fi

"$TIMEOUT" 600 "$BIN" "$LOG" >"$OUT" 2>"$ERR"
run_rc=$?

cat "$OUT"
if [ -s "$ERR" ]; then
    cat "$ERR" >&2
fi

case "$run_rc" in
    0)
        exit 0
        ;;
    124|137)
        echo "FUZZ10K thread_async HANG detected (timeout rc=$run_rc)"
        exit 1
        ;;
    99)
        echo "FUZZ10K thread_async HANG detected (per-scenario watchdog rc=99)"
        exit 1
        ;;
    *)
        # A signal (128+N / negative) is a crash; anything else is a harness
        # failure.  Either way the final summary line is absent, so fail.
        echo "FUZZ10K thread_async FAILURE rc=$run_rc (crash or contract failure)"
        exit 1
        ;;
esac

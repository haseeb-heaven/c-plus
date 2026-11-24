#!/usr/bin/env bash
# Run the BLUE20 error-handling / runtime-liveness proof.
# Builds proof.cp with the C+ toolchain, then executes the process-per-case
# harness under a hard 60-second wall-clock bound.  Prints the exact
# `BLUE20 error <cases> <passed> <failed>` summary and propagates its status.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
DIR="$ROOT/tests/redblue20/blue_error"
OUT="$DIR/out"

mkdir -p "$OUT"

if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler not found at $CPLUS (set CPLUS_BIN or build/)" >&2
  exit 2
fi

if ! "$CPLUS" build "$DIR/proof.cp" -o "$OUT/proof" >"$OUT/build.log" 2>&1; then
  echo "error: failed to build proof.cp" >&2
  cat "$OUT/build.log" >&2
  exit 2
fi

# Bounded wall clock: 60 seconds hard cap (per-case watchdog is in proof.cp).
if command -v timeout >/dev/null 2>&1; then
  timeout 60 "$OUT/proof"
elif command -v gtimeout >/dev/null 2>&1; then
  gtimeout 60 "$OUT/proof"
else
  perl -e 'alarm 60; exec @ARGV' "$OUT/proof"
fi
exit $?

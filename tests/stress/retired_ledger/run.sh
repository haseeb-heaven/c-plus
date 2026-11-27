#!/usr/bin/env bash
# Regression runner for the bounded retired-address ledger.
#
# Retires more addresses than the ledger cap allows and checks that the
# eviction path stays linear and keeps double-free detection alive.
#
# Usage:
#   tests/stress/retired_ledger/run.sh
#   CPLUS_LEDGER_SCALE=1200000 tests/stress/retired_ledger/run.sh

set -u

ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS_BIN="${CPLUS_BIN:-$ROOT/build/c+}"
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
OUT="$ROOT/tests/out/stress/retired_ledger"
SCALE="${CPLUS_LEDGER_SCALE:-2400000}"
BUDGET="${CPLUS_LEDGER_BUDGET:-30}"

if [[ ! -x "$CPLUS_BIN" ]]; then
  echo "Compiler not executable: $CPLUS_BIN" >&2
  exit 2
fi

mkdir -p "$OUT"
exe="$OUT/retired_ledger_scale"
if ! "$CPLUS_BIN" build --no-werror "$HERE/retired_ledger_scale.c" \
      -o "$exe" >"$OUT/build.log" 2>&1; then
  echo "FAIL: build failed; see $OUT/build.log" >&2
  exit 1
fi

expected="scale $SCALE
ledger_full 1
last_retired 1
linear 1"

if ! CPLUS_LEDGER_SCALE="$SCALE" CPLUS_LEDGER_BUDGET="$BUDGET" \
     "$exe" >"$OUT/stdout" 2>"$OUT/stderr"; then
  echo "FAIL: retired-ledger regression; see $OUT/stderr" >&2
  cat "$OUT/stderr" >&2
  exit 1
fi

if ! diff -u <(printf '%s\n' "$expected") "$OUT/stdout" \
     >"$OUT/diff" 2>&1; then
  echo "FAIL: unexpected verdicts; see $OUT/diff" >&2
  exit 1
fi

echo "PASS: retired ledger stayed linear at scale $SCALE"
cat "$OUT/stdout"

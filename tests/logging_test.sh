#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$ROOT/tests/out"
mkdir -p "$OUT"

"$CPLUS" build "$ROOT/tests/cases/log_fixture.cp" -o "$OUT/log_off" >/dev/null 2>&1
"$OUT/log_off" >"$OUT/log_off.stdout" 2>"$OUT/log_off.stderr"
if grep -q '\[INFO\]' "$OUT/log_off.stderr"; then
  echo "FAIL: logging emitted without --log"
  exit 1
fi

"$CPLUS" build "$ROOT/tests/cases/log_fixture.cp" --log --log-level debug \
  -o "$OUT/log_on" >"$OUT/log_on.build" 2>&1
grep -q 'c+ driver start' "$OUT/log_on.build"
"$OUT/log_on" >"$OUT/log_on.stdout" 2>"$OUT/log_on.stderr"
grep -q 'String ctor len=3' "$OUT/log_on.stdout"
echo "logging tests: ok"

# Safety diagnostics are logged with structured categories when --log is used.
"$CPLUS" build "$ROOT/tests/safety/uaf_direct.c" --log --log-level warn \
  -o "$OUT/log_uaf" >"$OUT/log_uaf.build" 2>&1 || true
grep -q 'CPLUS-SAFE-LIFETIME' "$OUT/log_uaf.build" || {
  echo "FAIL: safety diagnostic not logged with category"
  exit 1
}
echo "logging categories: ok"

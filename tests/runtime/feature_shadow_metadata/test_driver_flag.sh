#!/bin/sh
# Smoke test for the opt-in driver flag.  No C+ build is performed here: set
# CPLUS_BIN to an already-built c+ executable to run the end-to-end checks.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

CBIN=${CPLUS_BIN:-}
if [ -z "$CBIN" ] || [ ! -x "$CBIN" ]; then
  echo "SKIP: set CPLUS_BIN to a built c+ compiler for the flag smoke test"
  exit 0
fi

OUT=build/runtime
mkdir -p "$OUT"
DIR=tests/runtime/feature_shadow_metadata

# The driver must recognize the flag and must not forward it to the C backend.
"$CBIN" build -fcplus-disjoint-bounds "$DIR/driver_flag_ok.c" \
  -o "$OUT/disjoint_ok" >"$OUT/disjoint_ok.build.log" 2>&1
set +e
"$OUT/disjoint_ok" >/dev/null 2>&1
ok_rc=$?
set -e
[ "$ok_rc" -eq 2 ] || {
  echo "FAIL: in-bounds control returned $ok_rc, expected 2"
  exit 1
}

set +e
"$CBIN" build -fcplus-disjoint-bounds "$DIR/driver_flag_oob.c" \
  -o "$OUT/disjoint_oob" >"$OUT/disjoint_oob.build.log" 2>&1
build_rc=$?
set -e
[ "$build_rc" -eq 0 ] || {
  cat "$OUT/disjoint_oob.build.log"
  exit 1
}
set +e
"$OUT/disjoint_oob" >/dev/null 2>&1
run_rc=$?
set -e
[ "$run_rc" -eq 101 ] || {
  echo "FAIL: expected disjoint-bounds trap exit 101, got $run_rc"
  exit 1
}

# Default (flag absent) keeps the historical behaviour: no injected checks.
"$CBIN" build "$DIR/driver_flag_ok.c" -o "$OUT/disjoint_default" \
  >"$OUT/disjoint_default.build.log" 2>&1

# Issue #184: the opt-in safe-GC flag must be consumed by the driver (never
# forwarded to Clang) and must turn on epoch reclamation at startup.
"$CBIN" build -fcplus-safe-gc "$DIR/driver_flag_gc.c" \
  -o "$OUT/safe_gc_on" >"$OUT/safe_gc_on.build.log" 2>&1
set +e
"$OUT/safe_gc_on" >/dev/null 2>&1
gc_rc=$?
set -e
[ "$gc_rc" -eq 0 ] || {
  echo "FAIL: -fcplus-safe-gc did not enable reclamation (rc=$gc_rc)"
  exit 1
}
"$CBIN" build "$DIR/driver_flag_gc.c" -o "$OUT/safe_gc_default" \
  >"$OUT/safe_gc_default.build.log" 2>&1
set +e
"$OUT/safe_gc_default" >/dev/null 2>&1
gc_default_rc=$?
set -e
[ "$gc_default_rc" -eq 1 ] || {
  echo "FAIL: default build enabled reclamation (rc=$gc_default_rc)"
  exit 1
}
echo "PASS: feature_shadow_metadata driver flag"

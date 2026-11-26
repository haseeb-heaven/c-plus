#!/usr/bin/env bash
# Single ABI-preservation gate. Every suite must pass; no suppression, no skips.
# Invariant: ordinary C pointers, layouts, calling conventions and exported
# application symbols are unchanged by c+ in LEGACY/DEFAULT/STRICT. The only
# permitted additions are documented cplus_* runtime exports.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
export CPLUS_BIN="${CPLUS_BIN:-$ROOT/build/c+}"
OUT="$ROOT/tests/compiler/abi_gate/results"
mkdir -p "$OUT"

status=0
run_suite() {
  name="$1"
  shift
  echo "=== ABI gate: $name ==="
  if "$@" > "$OUT/$name.log" 2>&1; then
    tail -2 "$OUT/$name.log" | sed 's/^/  /'
    echo "  PASS $name"
  else
    rc=$?
    echo "  FAIL $name (rc=$rc)"
    tail -25 "$OUT/$name.log" | sed 's/^/  /'
    status=1
  fi
}

run_suite abi_safety python3 "$ROOT/tests/compiler/abi_safety/run_suite.py"
run_suite abi_mixed python3 "$ROOT/tests/compiler/abi_mixed/run_suite.py"
run_suite abi_real_world python3 "$ROOT/tests/compiler/abi_real_world/run_suite.py"
run_suite cross_compiler python3 "$ROOT/tests/compiler/cross_compiler/run_suite.py"
run_suite source_preservation python3 "$ROOT/tests/compiler/source_preservation/run.py"

if [ "$status" -ne 0 ]; then
  echo "ABI preservation gate: FAIL"
  exit 1
fi
echo "ABI preservation gate: PASS (5 suites)"

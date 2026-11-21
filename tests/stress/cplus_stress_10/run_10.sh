#!/usr/bin/env bash
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS_BIN="${CPLUS_BIN:-$ROOT/build/c+}"
SRC_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
OUT="$ROOT/tests/out/stress/cplus_stress_10"
if [[ ! -x "$CPLUS_BIN" ]]; then
  echo "Compiler not executable: $CPLUS_BIN" >&2
  exit 2
fi

pass=0
fail=0
mkdir -p "$OUT"

check_case() {
  local name="$1" kind="$2" expected="${3:-}" exit_expected="${4:-0}"
  local source="$SRC_DIR/$name.cp" exe="$OUT/$name" log="$OUT/$name.build.log"
  local rc=0
  echo "===== $name ($kind) ====="
  if [[ "$kind" == compile_fail ]]; then
    "$CPLUS_BIN" check "$source" >"$log" 2>&1 && rc=0 || rc=$?
    if [[ "$rc" -ne 0 ]]; then
      echo "PASS: expected rejection (status $rc)"
      ((pass+=1))
    else
      echo "FAIL: unsafe program unexpectedly accepted"
      ((fail+=1))
    fi
    return
  fi
  "$CPLUS_BIN" build "$source" -o "$exe" >"$log" 2>&1 && rc=0 || rc=$?
  if [[ "$rc" -ne 0 ]]; then
    if [[ "$kind" == runtime_fail ]]; then
      echo "PASS: compile-time rejection of unsafe case $name"
      ((pass+=1))
    else
      echo "FAIL: build rejected (exit $rc); $(tail -n 2 "$log" | tr '\n' ' ')"
      ((fail+=1))
    fi
    return
  fi
  local out="$OUT/$name.stdout" err="$OUT/$name.stderr"
  # Avoid generating core dump files from intentional safety traps.
  (ulimit -c 0; "$exe" > "$out" 2> "$err") 2>/dev/null && rc=0 || rc=$?
  if [[ "$kind" == runtime_fail ]]; then
    if [[ "$rc" -eq "$exit_expected" ]]; then
      echo "PASS: expected runtime trap (exit $rc)"
      ((pass+=1))
    else
      echo "FAIL: expected runtime exit $exit_expected; got $rc"
      ((fail+=1))
    fi
    return
  fi
  if [[ "$rc" -ne 0 ]]; then
    echo "FAIL: runtime exit $rc; $(tail -n 2 "$err" | tr '\n' ' ')"
    ((fail+=1))
  elif diff -u <(printf '%s' "$expected") "$out" > "$OUT/$name.diff"; then
    echo "PASS: output matches"
    ((pass+=1))
  else
    echo "FAIL: output mismatch (see .results/$name.diff)"
    ((fail+=1))
  fi
}

check_case 01_class_this_destructor run $'LEDGER 118 236\nDROP 118\n'
check_case 02_resource_early_return run $'CLOSE 2\nCLOSE 1\nRETURN 42\n'
check_case 03_managed_len run $'MANAGED 8 140\n'
check_case 04_stdlib_string_vector run $'STDLIB 10 4 7\n'
check_case 05_range_countof_auto run $'RANGE 6 6 41\n'
check_case 06_real_c_compatibility run $'COMPAT 120 9 3 abc\n'
check_case 07_defer_nested_returns run $'DEFER 2\nDEFER 1\nRESULT 7\nDEFER 2\nDEFER 1\nRESULT 5\n'
check_case 08_reject_resource_copy compile_fail
check_case 09_reject_borrow_escape compile_fail
check_case 10_trap_double_free runtime_fail '' 104

printf '\nSUMMARY: %d passed, %d failed (10 total)\n' "$pass" "$fail"
if [[ "$fail" -ne 0 ]]; then exit 1; fi

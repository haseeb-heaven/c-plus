#!/usr/bin/env bash
set -u
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT="$(CDPATH= cd -- "$HERE/../../.." && pwd)"
CPLUS_BIN="${CPLUS_BIN:-$ROOT/build/c+}"
if [ ! -x "$CPLUS_BIN" ]; then
  echo "Compiler not executable: $CPLUS_BIN" >&2
  echo 'Usage: CPLUS_BIN=/absolute/path/to/c+ bash run.sh' >&2
  exit 2
fi
OUT="$ROOT/tests/out/stress/cplus_breaker_v3"
mkdir -p "$OUT"
PASS=0; FAIL=0
run_case() {
  local name="$1" kind="$2" expected="$3" trap_code="$4"
  local source="$HERE/$name.cp" exe="$OUT/$name" rc=0
  if [ "$kind" = reject ]; then
    "$CPLUS_BIN" check "$source" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr" || rc=$?
    if [ "$rc" -ne 0 ]; then echo "PASS [reject] $name"; PASS=$((PASS+1)); else echo "FAIL [accepted unsafe] $name"; FAIL=$((FAIL+1)); fi
    return
  fi
  "$CPLUS_BIN" build "$source" -o "$exe" >"$OUT/$name.build.stdout" 2>"$OUT/$name.build.stderr" || rc=$?
  if [ "$rc" -ne 0 ]; then
    if [ "$kind" = trap ]; then echo "PASS [reject] $name (compile-time rejection)"; PASS=$((PASS+1));
    else echo "FAIL [compile] $name (see tests/out/stress/cplus_breaker_v3/$name.build.stderr)"; FAIL=$((FAIL+1));
    fi
    return
  fi
  rc=0
  ( cd "$OUT" && "$exe" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr" ) || rc=$?
  if [ "$kind" = trap ]; then
    if [ "$rc" -eq 0 ]; then echo "FAIL [no trap] $name"; FAIL=$((FAIL+1));return;fi
    if [ -n "$trap_code" ] && [ "$rc" -ne "$trap_code" ]; then echo "FAIL [wrong exit $rc, expected $trap_code] $name"; FAIL=$((FAIL+1));return;fi
    if [ -z "$trap_code" ]; then echo "PASS [trapped $rc; inspect stderr] $name"; else echo "PASS [trap $trap_code] $name";fi
    PASS=$((PASS+1));return
  fi
  if [ "$rc" -ne 0 ]; then echo "FAIL [runtime exit $rc] $name";FAIL=$((FAIL+1));return;fi
  if ! diff -u "$HERE/$name.expected" "$OUT/$name.stdout" >"$OUT/$name.diff"; then
    echo "FAIL [output mismatch] $name";FAIL=$((FAIL+1));return
  fi
  echo "PASS [correct output] $name"; PASS=$((PASS+1))
}
while IFS='|' read -r name kind trap_code; do
  [ -n "$name" ] || continue
  run_case "$name" "$kind" "" "$trap_code"
done < "$HERE/cases.list"
printf '\nC+ Breaker V3: %d passed, %d failed\n' "$PASS" "$FAIL"
if [ "$FAIL" -gt 0 ]; then echo 'Investigate .results/*.stderr, *.build.stderr and *.diff';exit 1;fi

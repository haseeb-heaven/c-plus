#!/bin/sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
D=tests/raii_shadowing/generated
OUT=$D/out
mkdir -p "$OUT"
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
pass=0; fail=0; total=0
while IFS= read -r c; do
  [ -z "$c" ] && continue
  total=$((total+1))
  if ! timeout 120 "$CPLUS" build "$D/$c.cp" -I. -o "$OUT/$c" >"$OUT/$c.build.log" 2>&1; then
    printf 'GEN FAIL %s build\n' "$c"
    fail=$((fail+1)); continue
  fi
  timeout 30 "$OUT/$c" >"$OUT/$c.actual" 2>"$OUT/$c.stderr"
  rc=$?
  if [ "$rc" -ne 0 ]; then
    printf 'GEN FAIL %s run_rc=%d\n' "$c" "$rc"; fail=$((fail+1)); continue
  fi
  if diff -u "$D/expected/$c.txt" "$OUT/$c.actual" >"$OUT/$c.diff" 2>&1; then
    pass=$((pass+1))
  else
    printf 'GEN FAIL %s mismatch\n' "$c"; fail=$((fail+1))
  fi
done < "$D/cases.txt"
printf 'RAII_GEN %d %d %d\n' "$total" "$pass" "$fail"
[ "$fail" -eq 0 ]

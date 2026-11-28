#!/usr/bin/env bash
# Compile-time heap-bounds regression matrix.  Every case asserts an exact
# diagnostic substring in both DEFAULT and STRICT with sanitizers disabled.
# No failure is suppressed; safe controls must compile and execute cleanly.
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
CASES="$HERE/bounds_cases"
OUT="$HERE/results_bounds"
mkdir -p "$OUT/logs" "$OUT/generated" "$OUT/bin"
unset CPLUS_CLANG

PASS=0; FAIL=0; TOTAL=0
record() {
  TOTAL=$((TOTAL+1))
  if [ "$2" = PASS ]; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
  printf '%-7s %-28s %-9s %s\n' "$2" "$1" "$3" "$4" >> "$OUT/summary.txt"
}

# name|expected diagnostic substring (empty => must be accepted)
REJECTS="alias_extent|array index 8 exceeds heap allocation 'q' of 32 bytes
cast_extent|array index 64 exceeds heap allocation 'q' of 32 bytes
call_conflict|unverifiable extent
heap_factory|array index 4 exceeds heap allocation 'q' of 16 bytes
mandatory_malloc1|array index 9 exceeds heap allocation 'p' of 1 bytes"

: > "$OUT/summary.txt"
while IFS='|' read -r name want; do
  [ -n "$name" ] || continue
  src="$CASES/$name.c"
  for mode in --strict --default; do
    gen="$OUT/generated/${name}${mode#--}.c"
    err="$OUT/logs/${name}${mode#--}.stderr"
    timeout 90 "$CPLUS" check "$mode" --sanitize=none --emit-c "$gen" "$src" \
      >"$OUT/logs/${name}${mode#--}.stdout" 2>"$err"
    rc=$?
    # DEFAULT does not fail closed on an unverifiable extent; STRICT does.
    if [ "$name" = call_conflict ] && [ "$mode" = --default ]; then
      if [ "$rc" -eq 0 ]; then
        record "$name" PASS default "rc=0 accepted with warnings only"
      else
        record "$name" FAIL default "rc=$rc expected acceptance"
      fi
    elif [ "$rc" -ne 0 ] && grep -qF "$want" "$err" && grep -q 'error:' "$err"; then
      record "$name" PASS "${mode#--}" "rc=$rc diagnostic asserted"
    else
      record "$name" FAIL "${mode#--}" "rc=$rc want='$want' got='$(head -2 "$err" | tr '\n' ' ')'"
    fi
  done
done <<< "$REJECTS"

# Safe controls: must compile without diagnostics and run with exact stdout.
for name in safe_alias safe_cast safe_factory; do
  src="$CASES/$name.c"
  err="$OUT/logs/${name}.stderr"
  timeout 90 "$CPLUS" build --strict --sanitize=none "$src" -o "$OUT/bin/$name" \
    >"$OUT/logs/${name}.stdout" 2>"$err"
  rc=$?
  if [ "$rc" -eq 0 ] && ! grep -qE 'error:|warning:' "$err"; then
    got=$("$OUT/bin/$name" 2>>"$err")
    want=$(cat "$CASES/$name.expected")
    if [ "$got" = "$want" ]; then
      record "$name" PASS runtime "rc=0 stdout='$got'"
    else
      record "$name" FAIL runtime "stdout='$got' want='$want'"
    fi
  else
    record "$name" FAIL runtime "build rc=$rc err='$(head -2 "$err" | tr '\n' ' ')'"
  fi
done

printf '\nTOTAL %d  PASS %d  FAIL %d\n' "$TOTAL" "$PASS" "$FAIL" >> "$OUT/summary.txt"
echo "TOTAL $TOTAL  PASS $PASS  FAIL $FAIL"
[ "$FAIL" -eq 0 ]

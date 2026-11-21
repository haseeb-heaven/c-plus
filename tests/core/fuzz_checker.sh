#!/bin/sh
# Deterministic mutation fuzz of the rewriter/safety checker: generate small
# C-like inputs with randomized array/index/control-flow patterns and assert
# the compiler never crashes (no signal, no sanitizer abort in the compiler).
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CC=${CPLUS_BIN:-$ROOT/build/c+}
OUT="$ROOT/tests/out/core/fuzz"
mkdir -p "$OUT"
N=${FUZZ_ROUNDS:-120}
state=123456789
crashes=0; accepted=0; rejected=0
rand() { state=$(( (state * 1103515245 + 12345) & 0x7fffffff )); echo "$state"; }
for i in $(seq 1 "$N"); do
  n=$(( $(rand) % 8 + 1 ))
  idx=$(( $(rand) % (n + 3) ))
  kind=$(( $(rand) % 4 ))
  case "$kind" in
    0) body="int a[$n]={0}; return a[$idx];" ;;
    1) body="int a[$n]={0}; for(int i=0;i<$idx;i++) a[i]=i; return 0;" ;;
    2) body="int a[$n]={0}; int *p=a+$idx; return *p;" ;;
    *) body="int a[2][$n]={0}; return a[1][$idx];" ;;
  esac
  printf 'int main(void){%s}\n' "$body" > "$OUT/case.c"
  "$CC" check "$OUT/case.c" >"$OUT/case.log" 2>&1
  rc=$?
  if [ "$rc" -ge 128 ]; then
    echo "CRASH rc=$rc input: $body"; cp "$OUT/case.c" "$OUT/crash_$i.c"; crashes=$((crashes+1))
  elif [ "$rc" -eq 0 ]; then accepted=$((accepted+1)); else rejected=$((rejected+1)); fi
done
echo "fuzz checker: $N inputs, $accepted accepted, $rejected rejected, $crashes crashes"
[ "$crashes" -eq 0 ]

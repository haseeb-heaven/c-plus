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
  kind=$(( $(rand) % 8 ))
  case "$kind" in
    0) body="int a[$n]={0}; return a[$idx];" ;;
    1) body="int a[$n]={0}; for(int i=0;i<$idx;i++) a[i]=i; return 0;" ;;
    2) body="int a[$n]={0}; int *p=a+$idx; return *p;" ;;
    3) body="int a[2][$n]={0}; return a[1][$idx];" ;;
    4) body="#include <stdlib.h>
int main(void){int*p=malloc($n*sizeof(int));if(!p)return 1;int*q=p;free(p);return q[$idx];}\n" ;;
    5) body="#include <stdlib.h>
int main(void){int*p=malloc($n);p=realloc(p,$idx);free(p);return 0;}\n" ;;
    6) body="#include <string.h>
int main(void){char d[$n];char s[$idx]={0};memcpy(d,s,$idx);return d[0];}\n" ;;
    7) body="#include \"cplus/cplus.h\"\nint main(void){managed int*p=cplus_new_array(int,$n);if(p.len!=($n))return 1;return p[$idx%${n}] ;}\n" ;;
  esac
  case "$kind" in
    4|5|6|7) printf '%b\n' "$body" > "$OUT/case.c" ;;
    *) printf 'int main(void){%s}\n' "$body" > "$OUT/case.c" ;;
  esac
  "$CC" check "$OUT/case.c" >"$OUT/case.log" 2>&1
  rc=$?
  if [ "$rc" -ge 128 ]; then
    echo "CRASH rc=$rc input: $body"; cp "$OUT/case.c" "$OUT/crash_$i.c"; crashes=$((crashes+1))
  elif [ "$rc" -eq 0 ]; then accepted=$((accepted+1)); else rejected=$((rejected+1)); fi
done
echo "fuzz checker: $N inputs, $accepted accepted, $rejected rejected, $crashes crashes"
[ "$crashes" -eq 0 ]

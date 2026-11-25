#!/bin/sh
# Modern allocator benchmark: 1 warmup + 7 timed runs, report medians.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
export PATH="$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH"
unset SDKROOT || true
BIN=build/bench_modern_allocators
"$ROOT/build/c+" build benchmarks/modern_allocators.cp --default --no-werror --sanitize=none \
    -I. -o "$BIN" -- build/libc/libcplus_hardened.a >/dev/null 2>&1
/tmp/warm_ma 2>/dev/null || true
"$BIN" >/dev/null 2>&1                     # warmup
i=0
: > /tmp/bench_ma_vals
while [ "$i" -lt 7 ]; do
    line=$("$BIN")
    set -- $line
    printf '%s %s %s\n' "$3" "$4" "$5" >> /tmp/bench_ma_vals
    i=$((i+1))
done
awk '{h[NR]=$1;a[NR]=$2;s[NR]=$3} END{
    n=NR; for(i=1;i<=n;i++){for(j=i+1;j<=n;j++){
      if(h[i]>h[j]){t=h[i];h[i]=h[j];h[j]=t}
      if(a[i]>a[j]){t=a[i];a[i]=a[j];a[j]=t}
      if(s[i]>s[j]){t=s[i];s[i]=s[j];s[j]=t}}}
    m=int((n+1)/2);
    printf "BENCH_MEDIAN heap=%s arena=%s scratch=%s (ns/2000 rounds)\n",h[m],a[m],s[m]
    printf "BENCH_SPEEDUP arena=%.2fx scratch=%.2fx\n",h[m]/a[m],h[m]/s[m]
}' /tmp/bench_ma_vals

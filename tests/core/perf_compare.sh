#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/out/core/perf_compare"
mkdir -p "$OUT"
cat > "$OUT/loop.c" <<'C'
#include <stdio.h>
#define N 20000000
int main(void){ long long s=0; for(int i=0;i<N;i++) s+=i; printf("%lld\n",s); return 0; }
C
cat > "$OUT/loop.rs" <<'RS'
fn main() { let mut s: i64 = 0; for i in 0..20_000_000 { s += i; } println!("{}", s); }
RS
"$ROOT/build/c+" build --release "$OUT/loop.c" -o "$OUT/cplus_safe" >/dev/null 2>&1
"$ROOT/build/c+" build --release "$OUT/loop.c" --legacy-c -o "$OUT/cplus_legacy" >/dev/null 2>&1
clang -O2 "$OUT/loop.c" -o "$OUT/clang_o2" >/dev/null 2>&1
rustc -O "$OUT/loop.rs" -o "$OUT/rust_o" >/dev/null 2>&1
for b in clang_o2 cplus_legacy cplus_safe rust_o; do
  printf '%-14s ' "$b"
  /usr/bin/time -p "$OUT/$b" >/dev/null 2>"$OUT/$b.time" || true
  grep real "$OUT/$b.time" | tr -s ' '
done

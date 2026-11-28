#!/bin/sh
# Head-to-head C+ vs Rust rounds on an identical workload. POSIX sh, bounded.
# Prints LANG_ROUNDS <rounds> <cplus_wins> <rust_wins> <ties>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/Users/haseeb-mir/.cargo/bin
unset SDKROOT
unset CPLUS_CLANG
D=benchmarks/language_rounds
OUT=proof/benchmarks/language_rounds.txt
mkdir -p build/lang_rounds
./build/c+ build "$D/bench.cp" --default --no-werror --sanitize=none -o build/lang_rounds/bench_cp >build/lang_rounds/cp.build.log 2>&1 || exit 1
rustc -O "$D/bench.rs" -o build/lang_rounds/bench_rs >build/lang_rounds/rs.build.log 2>&1 || exit 1
: > "$OUT"
printf 'round\tcplus_ns\trust_ns\twinner\n' >> "$OUT"
cw=0; rw=0; ties=0; rounds=5
i=1
while [ "$i" -le "$rounds" ]; do
  c=$(timeout 60 build/lang_rounds/bench_cp | awk '{print $2}')
  r=$(timeout 60 build/lang_rounds/bench_rs | awk '{print $2}')
  if [ -z "$c" ] || [ -z "$r" ]; then echo "LANG_ROUNDS FAIL round $i"; exit 1; fi
  if [ "$c" -lt "$r" ]; then w=C+; cw=$((cw+1));
  elif [ "$r" -lt "$c" ]; then w=Rust; rw=$((rw+1));
  else w=tie; ties=$((ties+1)); fi
  printf '%d\t%s\t%s\t%s\n' "$i" "$c" "$r" "$w" >> "$OUT"
  i=$((i+1))
done
cat "$OUT"
echo "LANG_ROUNDS $rounds $cw $rw $ties"

#!/bin/sh
# Automated Rust-vs-C+ runtime-outcome comparison.
# For each of the 6 method families, run 100 identical scenarios in both
# languages and compare the reported outcome token (ACCEPTED/REFUSED/SKIP).
# A mismatch is a DOCUMENTED DIFFERENCE (different safety mechanism), not a
# test failure. Compile errors and protocol violations ARE failures.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-/usr/bin/clang}
RUSTC=${CPLUS_RUSTC:-rustc}
OUT="$ROOT/build/rust_vs_cplus"
mkdir -p "$OUT"
sh tests/libc/build_libc.sh >/dev/null
sh scripts/build_safecrt.sh >/dev/null

SAFE="$ROOT/build/safe/libcplus_safecrt.a"
LIBC="$ROOT/build/libc/libcplus_hardened.a"
INC="-I$ROOT -I$ROOT/stdlib/safe -I$ROOT/libc -I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"

families="alloc memory stdio conversion math concurrency"
total_match=0; total_diff=0; total_skip=0; fail=0

for fam in $families; do
  csrc="tests/rust_vs_cplus/${fam}_c.c"
  rsrc="tests/rust_vs_cplus/${fam}_rs.rs"
  if [ ! -f "$csrc" ] || [ ! -f "$rsrc" ]; then
    echo "SKIP $fam (source pair not present yet)"
    continue
  fi
  cbin="$OUT/${fam}_c"; rbin="$OUT/${fam}_rs"
  if ! "$CC" -O1 -g -std=c99 -Wall -Wextra -pedantic $INC "$csrc" "$SAFE" "$LIBC" -lm -pthread \
        -o "$cbin" >"$OUT/${fam}_c.build.log" 2>&1; then
    echo "FAIL $fam (C compile)"; tail -8 "$OUT/${fam}_c.build.log"; fail=$((fail+1)); continue
  fi
  if ! "$RUSTC" --edition 2021 "$rsrc" -o "$rbin" >"$OUT/${fam}_rs.build.log" 2>&1; then
    echo "FAIL $fam (Rust compile)"; tail -8 "$OUT/${fam}_rs.build.log"; fail=$((fail+1)); continue
  fi

  match=0; diff=0; skip=0; bad=0
  : > "$OUT/${fam}.csv"
  for i in $(seq 1 100); do
    c_out=$(timeout 10 "$cbin" "$i" 2>/dev/null | grep -m1 '^RESULT ')
    r_out=$(timeout 10 "$rbin" "$i" 2>/dev/null | grep -m1 '^RESULT ')
    c_res=${c_out##* }; r_res=${r_out##* }
    case "$c_res" in ACCEPTED|REFUSED|SKIP) ;; *) bad=$((bad+1));; esac
    case "$r_res" in ACCEPTED|REFUSED|SKIP) ;; *) bad=$((bad+1));; esac
    if [ "$c_res" = "$r_res" ]; then
      if [ "$c_res" = "SKIP" ]; then skip=$((skip+1)); else match=$((match+1)); fi
    else
      diff=$((diff+1))
    fi
    printf '%s,%s,%s\n' "$i" "$c_res" "$r_res" >> "$OUT/${fam}.csv"
  done
  if [ "$bad" -ne 0 ]; then
    echo "FAIL $fam (protocol violations: $bad)"; fail=$((fail+1)); continue
  fi
  total_match=$((total_match+match)); total_diff=$((total_diff+diff)); total_skip=$((total_skip+skip))
  echo "ok   $fam: $match matched, $diff documented differences, $skip both-skip"
done

echo "rust-vs-cplus: $total_match matched, $total_diff documented differences, $total_skip both-skip, $fail failures"
echo "note: outcome equality is evidence of observed behaviour only; it does not prove equivalent safety guarantees."
[ "$fail" -eq 0 ]

#!/bin/sh
# High-volume C bug-class corpus runner for the C+ hardened libc.
# Each test_corpus_*.c generates 600-1000+ deterministic break scenarios for
# one class of C runtime bug. Runs normally and under UBSan.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-clang}
OUT="$ROOT/build/corpus"
mkdir -p "$OUT"
sh tests/libc/build_libc.sh >/dev/null
LIBC="$ROOT/build/libc/libcplus_hardened.a"
SAFE="$ROOT/build/safe/libcplus_safecrt.a"
sh scripts/build_safecrt.sh >/dev/null
INC="-Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
pass=0; fail=0
for t in tests/libc/test_corpus_*.c; do
  [ -f "$t" ] || continue
  name=$(basename "$t" .c)
  for mode in plain ubsan; do
    if [ "$mode" = "ubsan" ]; then
      SAN="-fsanitize=undefined -fno-sanitize-recover=all -fno-sanitize=float-divide-by-zero"
    else
      SAN=""
    fi
    bin="$OUT/${name}_${mode}"
    if "$CC" -O2 -std=c99 -Wall -Wextra -pedantic $SAN $INC "$t" "$LIBC" "$SAFE" -lm -pthread -o "$bin" >"$OUT/${name}_${mode}.build.log" 2>&1 \
       && "$bin" >"$OUT/${name}_${mode}.run.log" 2>&1; then
      echo "ok   $name ($mode): $(tail -1 "$OUT/${name}_${mode}.run.log")"
      pass=$((pass + 1))
    else
      echo "FAIL $name ($mode)"
      tail -12 "$OUT/${name}_${mode}.build.log" 2>/dev/null || true
      tail -12 "$OUT/${name}_${mode}.run.log" 2>/dev/null || true
      fail=$((fail + 1))
    fi
  done
done
echo "bug-class corpora: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

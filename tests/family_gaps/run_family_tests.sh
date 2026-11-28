#!/bin/sh
# Runs every per-family checked-coverage suite one by one, plain + UBSan.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-/usr/bin/clang}
OUT="$ROOT/build/family_gaps"
mkdir -p "$OUT"
sh tests/libc/build_libc.sh >/dev/null
sh scripts/build_safecrt.sh >/dev/null
SAFE="$ROOT/build/safe/libcplus_safecrt.a"
LIBC="$ROOT/build/libc/libcplus_hardened.a"
INC="-I$ROOT -I$ROOT/stdlib/safe -I$ROOT/libc -I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"
pass=0; fail=0

for f in tests/family_gaps/*.c; do
  [ -f "$f" ] || continue
  name=$(basename "$f" .c)
  for mode in plain ubsan; do
    if [ "$mode" = ubsan ]; then
      SAN="-fsanitize=undefined -fno-sanitize-recover=all -fno-sanitize=float-divide-by-zero"
    else SAN=""; fi
    bin="$OUT/${name}_${mode}"
    if "$CC" -O1 -g -std=c99 -Wall -Wextra -pedantic $SAN $INC "$f" "$SAFE" "$LIBC" -lm -pthread \
         -o "$bin" >"$OUT/${name}_${mode}.build.log" 2>&1 \
       && timeout 120 "$bin" >"$OUT/${name}_${mode}.run.log" 2>&1; then
      summary=$(grep -E "${name}: .*checks ok|${name}: .*skipped" "$OUT/${name}_${mode}.run.log" | tail -1)
      findings=$(grep -c 'FINDING' "$OUT/${name}_${mode}.run.log" 2>/dev/null || true)
      if [ "${findings:-0}" -ne 0 ]; then
        echo "FAIL $name ($mode): $findings FINDING line(s)"
        grep 'FINDING' "$OUT/${name}_${mode}.run.log" | head -5
        fail=$((fail+1))
      else
        echo "ok   $name ($mode): $summary"
        pass=$((pass+1))
      fi
    else
      echo "FAIL $name ($mode)"
      tail -10 "$OUT/${name}_${mode}.build.log" 2>/dev/null || true
      tail -10 "$OUT/${name}_${mode}.run.log" 2>/dev/null || true
      fail=$((fail+1))
    fi
  done
done
echo "family suites: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

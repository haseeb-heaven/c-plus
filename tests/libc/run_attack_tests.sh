#!/bin/sh
# Adversarial attack-suite runner for the C+ hardened libc.
# Builds the two libraries, then compiles and runs every test_*_attack.c
# first normally and then under UBSan. Also runs the Python contract-integrity
# attack when present. Failures are reported with the exact failing stage.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-clang}
OUT="$ROOT/build/attack"
mkdir -p "$OUT"

sh tests/libc/build_libc.sh >/dev/null
sh scripts/build_safecrt.sh >/dev/null

LIBC="$ROOT/build/libc/libcplus_hardened.a"
SAFE="$ROOT/build/safe/libcplus_safecrt.a"
INC="-Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
pass=0; fail=0

for t in tests/libc/test_*_attack.c; do
  [ -f "$t" ] || continue
  name=$(basename "$t" .c)
  for mode in plain ubsan; do
    if [ "$mode" = "ubsan" ]; then
      SAN="-fsanitize=undefined -fno-sanitize-recover=all -fno-sanitize=float-divide-by-zero"
    else
      SAN=""
    fi
    bin="$OUT/${name}_${mode}"
    if "$CC" -O1 -g -std=c99 -Wall -Wextra -pedantic $SAN $INC "$t" "$LIBC" "$SAFE" -lm -pthread -o "$bin" >"$OUT/${name}_${mode}.build.log" 2>&1 \
       && "$bin" >"$OUT/${name}_${mode}.run.log" 2>&1; then
      echo "ok   $name ($mode)"
      pass=$((pass + 1))
    else
      echo "FAIL $name ($mode)"
      tail -12 "$OUT/${name}_${mode}.build.log" 2>/dev/null || true
      tail -12 "$OUT/${name}_${mode}.run.log" 2>/dev/null || true
      fail=$((fail + 1))
    fi
  done
done

if [ -f tests/libc/test_contract_integrity.cp ]; then
  if build/native-tools/test_contract_integrity >"$OUT/contract_integrity.run.log" 2>&1; then
    echo "ok   test_contract_integrity.cp"
    pass=$((pass + 1))
  else
    echo "FAIL test_contract_integrity.cp"
    tail -12 "$OUT/contract_integrity.run.log" || true
    fail=$((fail + 1))
  fi
fi

echo "attack suites: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

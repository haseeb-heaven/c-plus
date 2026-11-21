#!/bin/sh
# Run the mass fuzzer across several independent seeds. Each seed generates
# thousands of distinct programs; STRICT-mode cross-checking and differential
# clang/runtime checks are enabled for every case.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
TOTAL=0
SEEDS=${FUZZ_SEEDS:-"0xC0FFEE 0x5EED1234 0xABCDEF01"}
for seed in $SEEDS; do
  echo "== mass fuzz seed $seed =="
  FUZZ_SEED=$seed python3 tests/core/mass_fuzz.py
  TOTAL=$((TOTAL+1))
done
echo "mass fuzz: $TOTAL seeds completed"

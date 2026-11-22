#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
sh scripts/build_safecrt.sh >/dev/null
CC=${CPLUS_CLANG:-clang}
INC="-Istdlib/safe -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
LIB=build/safe/libcplus_safecrt.a
OUT=build/safe/fuzz
mkdir -p "$OUT"
pass=0; fail=0
for t in \
  tests/crt/fuzz/fuzz_memory.c \
  tests/crt/fuzz/fuzz_strings.c \
  tests/crt/fuzz/fuzz_allocator.c \
  tests/crt/fuzz/fuzz_format.c \
  tests/crt/fuzz/fuzz_stdio.c \
  tests/crt/fuzz/fuzz_realloc.c \
  tests/crt/fuzz/fuzz_free.c \
  tests/crt/fuzz/fuzz_parsing.c \
  tests/crt/fuzz/fuzz_error.c \
  tests/crt/fuzz/fuzz_lifecycle.c \
  tests/crt/fuzz/fuzz_ownership.c; do
  b="$OUT/$(basename "$t" .c)_smoke"
  if "$CC" -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -DCPLUS_FUZZ_SMOKE_MAIN -std=c99 $INC "$t" "$LIB" -pthread -o "$b" >/tmp/fuzz.log 2>&1 && "$b" >/tmp/fuzz.out 2>&1; then
    pass=$((pass+1))
  else
    echo "FAIL $(basename "$t")"; tail -20 /tmp/fuzz.log || true; tail -20 /tmp/fuzz.out || true; fail=$((fail+1))
  fi
done
echo "fuzz smoke: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

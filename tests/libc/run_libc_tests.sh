#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
sh tests/libc/build_libc.sh >/dev/null
CC=${CPLUS_CLANG:-clang}
OUT=build/libc
LIB=$OUT/libcplus_hardened.a
pass=0; fail=0
for t in tests/libc/test_*.c; do
  case "$t" in *_attack.c|*/test_corpus_format.c) continue ;; esac # adversarial suite has its own runner
  b="$OUT/$(basename "$t" .c)"
  EXTRA_INC=""; EXTRA_LIB=""
  case "$t" in
    *test_alloc_fault_matrix*) EXTRA_INC="-I."; EXTRA_LIB="build/safe/libcplus_safecrt.a" ;;
  esac
  if "$CC" -O2 -Wall -Wextra -pedantic -std=c99 -Ilibc -Ilibc/include -Ilibc/install-include $EXTRA_INC "$t" "$LIB" $EXTRA_LIB -lm -pthread -o "$b" && "$b" >/tmp/libc_test.out 2>&1; then
    echo "ok $(basename "$t")"; pass=$((pass+1))
  else
    echo "FAIL $(basename "$t")"; tail -20 /tmp/libc_test.out || true; fail=$((fail+1))
  fi
done
echo "libc tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

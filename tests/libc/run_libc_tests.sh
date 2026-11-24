#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
sh tests/libc/build_libc.sh >/dev/null
CC=${CPLUS_CLANG:-clang}
OUT=build/libc
LIB=$OUT/libcplus_hardened.a
# Linux portability (#32): glibc hides strnlen/TIME_UTC/memccpy under strict
# c99; atomic wrappers need libatomic; static archives must be link-ordered so
# a symbol referenced from the safecrt archive can still resolve from libc+.
PLATFORM_CFLAGS=""; PLATFORM_LIBS=""
case "$(uname -s)" in
  Linux) PLATFORM_CFLAGS="-D_GNU_SOURCE"; PLATFORM_LIBS="-latomic" ;;
esac
pass=0; fail=0
for t in tests/libc/test_*.c; do
  case "$t" in *_attack.c|*/test_corpus_format.c) continue ;; esac # adversarial suite has its own runner
  b="$OUT/$(basename "$t" .c)"
  EXTRA_INC=""; EXTRA_LIB=""
  case "$t" in
    *test_alloc_fault_matrix*) EXTRA_INC="-I."; EXTRA_LIB="build/safe/libcplus_safecrt.a" ;;
    *test_vendor_*) EXTRA_INC="-I. -Ithird_party/safeclib/include"; EXTRA_LIB="build/safe/libcplus_safecrt.a" ;;
  esac
  RUN_CMD="$b"
  if command -v timeout >/dev/null 2>&1; then
    RUN_CMD="timeout 60 $b"
  fi
  # safecrt archive before libc+ so undefined refs resolve in one pass on GNU ld.
  if "$CC" -O2 -Wall -Wextra -pedantic -std=c99 $PLATFORM_CFLAGS \
        -Ilibc -Ilibc/include -Ilibc/install-include $EXTRA_INC "$t" \
        $EXTRA_LIB "$LIB" -lm -pthread $PLATFORM_LIBS -o "$b" \
        && $RUN_CMD >/tmp/libc_test.out 2>&1; then
    echo "ok $(basename "$t")"; pass=$((pass+1))
  else
    echo "FAIL $(basename "$t")"; tail -20 /tmp/libc_test.out || true; fail=$((fail+1))
  fi
done
echo "libc tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

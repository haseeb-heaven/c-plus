#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-clang}
COV=${LLVM_COV:-llvm-cov}
PROFDATA=${LLVM_PROFDATA:-llvm-profdata}
if command -v xcrun >/dev/null 2>&1; then
  COV=$(xcrun --find llvm-cov 2>/dev/null || echo "$COV")
  PROFDATA=$(xcrun --find llvm-profdata 2>/dev/null || echo "$PROFDATA")
fi
OUT="$ROOT/build/libc-cov"
rm -rf "$OUT"; mkdir -p "$OUT"
CFLAGS="-O0 -g -fprofile-instr-generate -fcoverage-mapping -Ilibc -Ilibc/include"
SRCS="libc/log/log.c libc/src/errno/errno.c libc/src/stdlib/alloc.c libc/src/stdlib/stdlib_extra.c libc/src/string/memory.c libc/src/string/string.c libc/src/string/extra.c libc/src/string/extra2.c libc/src/stdio/stdio.c libc/src/stdio/stdio2.c libc/src/ctype/ctype.c"
OBJS=""
for s in $SRCS; do o="$OUT/$(basename "$s" .c).o"; $CC $CFLAGS -c "$s" -o "$o"; OBJS="$OBJS $o"; done
ar rcs "$OUT/libcplus_hardened_cov.a" $OBJS
pass=0; fail=0
for t in tests/libc/test_*.c; do
  b="$OUT/$(basename "$t" .c)"
  "$CC" $CFLAGS "$t" "$OUT/libcplus_hardened_cov.a" -o "$b" >/tmp/libc_cov_build.log 2>&1
  LLVM_PROFILE_FILE="$OUT/$(basename "$t" .c).profraw" "$b" >/tmp/libc_cov_run.log 2>&1 || fail=$((fail+1))
  pass=$((pass+1))
done
"$PROFDATA" merge -sparse "$OUT"/*.profraw -o "$OUT/libc.profdata"
# Report per object and total
for o in $OBJS; do
  echo "--- $(basename "$o") ---"
  "$COV" report "$o" -instr-profile="$OUT/libc.profdata" | tail -2
done
echo "libc coverage tests: $pass, failures: $fail"

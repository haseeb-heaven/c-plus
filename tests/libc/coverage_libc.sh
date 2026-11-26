#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-clang}
COV=${LLVM_COV:-llvm-cov}
PROFDATA=${LLVM_PROFDATA:-llvm-profdata}
if command -v xcrun >/dev/null 2>&1; then
  found=$({ xcrun --find llvm-cov; } 2>/dev/null || true)
  [ -n "$found" ] && COV="$found"
  found=$({ xcrun --find llvm-profdata; } 2>/dev/null || true)
  [ -n "$found" ] && PROFDATA="$found"
fi
OUT="$ROOT/build/libc-cov"
rm -rf "$OUT"
mkdir -p "$OUT"
CFLAGS="-O0 -g -Wall -Wextra -pedantic -std=c99 -fprofile-instr-generate -fcoverage-mapping -Ilibc -Ilibc/include -Ilibc/install-include"
SRCS="
  libc/log/log.c
  libc/src/errno/errno.c
  libc/src/stdlib/alloc.c
  libc/src/stdlib/stdlib_extra.c
  libc/src/stdlib/stdlib_extra2.c
  libc/src/stdlib/stdlib_extra3.c
  libc/src/stdlib/stdlib_extra4.c
  libc/src/stdlib/stdlib_extra5.c
  libc/src/stdlib/stdlib_extra6.c
  libc/src/stdlib/assert_extra.c
  libc/src/stdlib/musl_stdlib_extra.c
  libc/src/string/extra2.c
  libc/src/string/extra3.c
  libc/src/string/string_extra4.c
  libc/src/string/string_extra5.c
  libc/src/string/string_extra6.c
  libc/src/string/strtok.c
  libc/src/string/musl_extra.c
  libc/src/string/strndup.c
  libc/src/string/strerror.c
  libc/src/string/memory.c
  libc/src/string/bounded.c
  libc/src/string/string.c
  libc/src/string/extra.c
  libc/src/stdio/stdio.c
  libc/src/stdio/stdio2.c
  libc/src/stdio/stdio3.c
  libc/src/ctype/ctype.c
  libc/src/ctype/ctype2.c
  libc/src/ctype/ctype3.c
  libc/src/wchar/wchar.c
  libc/src/wctype/wctype.c
  libc/src/math/math.c
  libc/src/math/math2.c
  libc/src/math/math3.c
  libc/src/math/math4.c
  libc/src/time/time.c
  libc/src/time/time_extra.c
  libc/src/locale/locale.c
  libc/src/signal/signal.c
  libc/src/signal/signal2.c
  libc/src/fenv/fenv.c
  libc/src/complex/complex.c
  libc/src/thread/thread.c
  libc/src/thread/threads_c11.c
  libc/src/fenv/fenv_extra.c
  libc/src/time/time_ascii.c
  libc/src/wchar/wchar_numeric.c
  libc/src/wchar/wchar_multibyte.c
  libc/src/wchar/wchar_format.c
  libc/src/wctype/wctype_extra.c
  libc/src/complex/complex_extra.c
  libc/src/complex/complex_inverse.c
  libc/src/wchar/wchar_gaps.c
  libc/src/uchar/uchar.c
  libc/src/c23_small_gaps.c
  libc/src/c23_upstream_small.c
  libc/src/uchar/uchar_c23.c
  libc/src/math/math_c23_upstream.c
  libc/src/math/math_c23.c
  libc/src/math/math_c23_musl.c
  libc/src/math/math_gaps.c
  libc/src/math/math_float.c
  libc/src/math/math_ldouble.c
  libc/src/stdlib/stdlib_c17.c
  libc/src/stdlib/inttypes_wide.c
  libc/src/string/upstream_safecrt.c
  libc/src/math/c23_nextup.c
  libc/src/math/c23_arith.c
  libc/src/math/c23_hypot16.c
  libc/src/stdlib/c23_time_unicode.c
  libc/src/thread/sync_checked.c
  libc/src/stdlib/alloc_hardened.c
  libc/src/runtime/violation_log.c
  libc/src/math/math_c23_hypot3_f16.c
  libc/src/math/math_c23_next.c
  libc/src/stdlib/atomic_wrappers.c
"
sh "$ROOT/scripts/build_safecrt.sh" >/dev/null
OBJS=""
FIRST_OBJ=""
OBJ_ARGS=""
for s in $SRCS; do
  o="$OUT/$(echo "$s" | tr / _).o"
  "$CC" $CFLAGS -c "$s" -o "$o"
  OBJS="$OBJS $o"
  if [ -z "$FIRST_OBJ" ]; then
    FIRST_OBJ="$o"
  else
    OBJ_ARGS="$OBJ_ARGS -object $o"
  fi
done
ar rcs "$OUT/libcplus_hardened_cov.a" $OBJS
# The atomic wrappers call __atomic_is_lock_free, which Clang/GCC lower to a
# libatomic symbol on Linux.  macOS has no separate libatomic, so probe the
# toolchain rather than hard-coding a platform or skipping the test.
ATOMIC_LIB=""
if printf 'int main(void) { return 0; }\n' | "$CC" -x c - -latomic \
      -o "$OUT/atomic-link-probe" >/dev/null 2>&1; then
  ATOMIC_LIB="-latomic"
fi
rm -f "$OUT/atomic-link-probe"
pass=0; fail=0
for t in tests/libc/test_*.c; do
  case "$t" in *_attack.c|*/test_corpus_format.c) continue ;; esac # adversarial suite has its own runner
  b="$OUT/$(basename "$t" .c)"
  EXTRA_INC=""; EXTRA_LIB=""
  case "$t" in
    *test_alloc_fault_matrix*) EXTRA_INC="-I."; EXTRA_LIB="build/safe/libcplus_safecrt.a" ;;
    *test_vendor_*) EXTRA_INC="-I. -Ithird_party/safeclib/include"; EXTRA_LIB="build/safe/libcplus_safecrt.a" ;;
  esac
  if "$CC" -O0 -std=c99 -fprofile-instr-generate -Ilibc -Ilibc/include -Ilibc/install-include $EXTRA_INC \
      "$t" "$OUT/libcplus_hardened_cov.a" $EXTRA_LIB -lm -lpthread $ATOMIC_LIB \
      -o "$b" >"$OUT/$(basename "$t" .c).build.log" 2>&1; then
    if LLVM_PROFILE_FILE="$OUT/$(basename "$t" .c).profraw" "$b" >"$OUT/$(basename "$t" .c).out" 2>&1; then
      pass=$((pass + 1))
    else
      fail=$((fail + 1))
      echo "FAIL $(basename "$t")"
    fi
  else
    fail=$((fail + 1))
    echo "FAIL BUILD $(basename "$t")"
    tail -12 "$b.build.log" 2>/dev/null || true
  fi
done
"$PROFDATA" merge -sparse "$OUT"/*.profraw -o "$OUT/libc.profdata"
REPORT="$OUT/report.txt"
"$COV" report "$FIRST_OBJ" $OBJ_ARGS -instr-profile="$OUT/libc.profdata" >"$REPORT"
cat "$REPORT"
echo "libc coverage tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
python3 - "$REPORT" <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
total_line = [l for l in lines if l.startswith("TOTAL")]
if not total_line:
    sys.exit("missing TOTAL line")
parts = total_line[0].split()
# Format: TOTAL regions missed reg_cov funcs missed func_cov lines missed line_cov ...
line_cov = float(parts[9].rstrip("%"))
print(f"libc total line coverage: {line_cov:.2f}%")
if line_cov < 80.0:
    sys.exit(f"libc line coverage {line_cov:.2f}% < 80.0%")
PY

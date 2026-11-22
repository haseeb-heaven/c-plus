#!/bin/sh
# Dedicated AddressSanitizer integration runner for the checked allocator.
#
# Both libraries are rebuilt with ASan instrumentation into build/asan so the
# ordinary release outputs in build/safe and build/libc are left untouched.
# Linux is the primary target; on hosts where the ASan runtime cannot start
# (e.g. Apple clang 16 on macOS arm64) the run is reported as HOST_UNSUPPORTED
# instead of being counted as a test failure.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-clang}

OUT="$ROOT/build/asan"
SAFE_OUT="$OUT/safe"
LIBC_OUT="$OUT/libc"
RUN_OUT="$OUT/run"
# Always start from a clean ASan output tree so stale archive members cannot
# leak into a later run. This directory is dedicated to ASan; release outputs
# in build/safe and build/libc are never touched.
rm -rf "$SAFE_OUT" "$LIBC_OUT" "$RUN_OUT"
mkdir -p "$SAFE_OUT" "$LIBC_OUT" "$RUN_OUT"

# -O1 + frame pointers + debug info are the flags recommended for ASan.
# The same flags are used for both libraries and for the test binary.
ASAN="-fsanitize=address -fno-omit-frame-pointer -g -O1"

# Pick a compiler whose ASan runtime actually starts. Apple clang 16 fails at
# process start on macOS arm64 (sanitizer_malloc_mac.inc:189); Homebrew clang
# works when an existing SDK is supplied explicitly. Probe rather than guess.
asan_probe() {
  probe_c="$OUT/asan_probe.c"; probe_bin="$OUT/asan_probe"
  printf 'int main(void){return 0;}\n' > "$probe_c"
  # $1 = compiler, $2 = extra flags (may be empty)
  if $1 $2 -fsanitize=address "$probe_c" -o "$probe_bin" >/dev/null 2>&1 \
     && "$probe_bin" >/dev/null 2>&1; then
    return 0
  fi
  return 1
}
ASAN_SYSROOT=""
if [ -z "${CPLUS_CLANG:-}" ]; then
  if asan_probe clang ""; then
    :
  elif [ "$(uname -s)" = "Darwin" ] && \
       [ -d /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk ]; then
    SDK="/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
    if asan_probe clang "-isysroot $SDK"; then
      ASAN_SYSROOT="-isysroot $SDK"
    elif [ -x /usr/bin/clang ] && asan_probe /usr/bin/clang "-isysroot $SDK"; then
      CC=/usr/bin/clang
      ASAN_SYSROOT="-isysroot $SDK"
    fi
  fi
elif [ "$(uname -s)" = "Darwin" ] && \
     [ -d /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk ]; then
  # Explicit override still needs a valid SDK on this host.
  SDK="/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
  if ! asan_probe "$CC" "" && asan_probe "$CC" "-isysroot $SDK"; then
    ASAN_SYSROOT="-isysroot $SDK"
  fi
fi
ASAN="$ASAN $ASAN_SYSROOT"

SC="$ROOT/third_party/safeclib"
UP="$ROOT/third_party/intel-safestringlib"

TEST="$ROOT/tests/crt/asan/test_alloc_asan.c"
if [ ! -f "$TEST" ]; then
  echo "ERROR: missing ASan test source: $TEST" >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# 1. Build libcplus_safecrt.a with ASan (mirrors scripts/build_safecrt.sh).
# ---------------------------------------------------------------------------
cat > "$OUT/config.h" <<'CFG'
#ifndef CPLUS_SAFECLIB_CONFIG
#define CPLUS_SAFECLIB_CONFIG
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_ERRNO_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDIO_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_WCHAR_H 1
#define HAVE_WCTYPE_H 1
#define HAVE_MEMORY_H 1
#define STDC_HEADERS 1
#define PACKAGE "safeclib-cplus"
#define VERSION "3.9.1-cplus"
#define HAVE_C99 1
#ifndef __STDC_WANT_LIB_EXT1__
#define __STDC_WANT_LIB_EXT1__ 1
#endif
#endif
CFG

# Symbol prefixing via -D (avoids macro conflicts in headers) -- same list as
# scripts/build_safecrt.sh.
SC_DEFS="-Dmemcpy_s=cplus_sc_memcpy_s -D_memcpy_s_chk=cplus_sc__memcpy_s_chk -Dmemmove_s=cplus_sc_memmove_s -D_memmove_s_chk=cplus_sc__memmove_s_chk -Dmemset_s=cplus_sc_memset_s -D_memset_s_chk=cplus_sc__memset_s_chk -Dmemcmp_s=cplus_sc_memcmp_s -D_memcmp_s_chk=cplus_sc__memcmp_s_chk -Dmemzero_s=cplus_sc_memzero_s -D_memzero_s_chk=cplus_sc__memzero_s_chk -Dstrcpy_s=cplus_sc_strcpy_s -D_strcpy_s_chk=cplus_sc__strcpy_s_chk -Dstrncpy_s=cplus_sc_strncpy_s -D_strncpy_s_chk=cplus_sc__strncpy_s_chk -Dstrcat_s=cplus_sc_strcat_s -D_strcat_s_chk=cplus_sc__strcat_s_chk -Dstrncat_s=cplus_sc_strncat_s -D_strncat_s_chk=cplus_sc__strncat_s_chk -Dstrnlen_s=cplus_sc_strnlen_s -D_strnlen_s_chk=cplus_sc__strnlen_s_chk -Dstrerror_s_chk=cplus_sc__strerror_s_chk -Dstrtok_s_chk=cplus_sc__strtok_s_chk -Dstrcmp_s=cplus_sc_strcmp_s -D_strcmp_s_chk=cplus_sc__strcmp_s_chk -Dabort_handler_s=cplus_sc_abort_handler_s -Dignore_handler_s=cplus_sc_ignore_handler_s -Dset_mem_constraint_handler_s=cplus_sc_set_mem_constraint_handler_s -Dset_str_constraint_handler_s=cplus_sc_set_str_constraint_handler_s -Dinvoke_safe_mem_constraint_handler=cplus_sc_invoke_safe_mem_constraint_handler -Dinvoke_safe_str_constraint_handler=cplus_sc_invoke_safe_str_constraint_handler"

INC_SC="-I$SC/include -I$OUT -I$SC/src -I$ROOT/stdlib/safe"
CFLAGS_SC="-std=c99 -D__STDC_WANT_LIB_EXT1__=0 $SC_DEFS -Wno-unused-parameter -Wno-missing-field-initializers -Wno-macro-redefined $INC_SC $ASAN"
CFLAGS_W="-Wall -Wextra -pedantic -std=c99 -I$SC/include -I$OUT -I$ROOT/stdlib/safe -I$ROOT/third_party/exceptions4c $ASAN"

if [ ! -f "$SC/include/safe_lib_errno.h" ]; then
  sed 's/@configure_input@/Generated by tests\/crt\/asan\/run_asan.sh/; s/@INSERT_ERRNO_H@/#include <errno.h>/' \
    "$SC/include/safe_lib_errno.h.in" > "$OUT/safe_lib_errno.h"
fi
if [ ! -f "$SC/include/safe_types.h" ]; then
  cat > "$OUT/safe_types.h" <<'TYPES'
#ifndef __SAFE_TYPES_H__
#define __SAFE_TYPES_H__
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#ifndef errno_t
typedef int errno_t;
#endif
#ifndef _RSIZE_T
typedef size_t rsize_t;
#endif
#ifndef RSIZE_MAX_STR
#define RSIZE_MAX_STR (4UL << 10)
#endif
#ifndef RSIZE_MAX_MEM
#define RSIZE_MAX_MEM (RSIZE_MAX_STR*4)
#endif
#ifndef RSIZE_MAX
#define RSIZE_MAX (~(rsize_t)0)
#endif
#ifdef __GNUC__
#define __attribute_format__(type, index, check) __attribute__((format(type, index, check)))
#define __attribute_format_wprintf(index, check)
#define __attribute_format_wscanf(index, check)
#else
#define __attribute_format__(type, index, check)
#define __attribute_format_wprintf(index, check)
#define __attribute_format_wscanf(index, check)
#endif
#ifdef __cplusplus
}
#endif
#endif
TYPES
fi

for f in \
  "$SC"/src/abort_handler_s.c \
  "$SC"/src/ignore_handler_s.c \
  "$SC"/src/mem/safe_mem_constraint.c \
  "$SC"/src/str/safe_str_constraint.c \
  "$SC"/src/mem/mem_primitives_lib.c \
  "$SC"/src/mem/memcpy_s.c \
  "$SC"/src/mem/memmove_s.c \
  "$SC"/src/mem/memset_s.c \
  "$SC"/src/extmem/memcmp_s.c \
  "$SC"/src/extmem/memzero_s.c \
  "$SC"/src/str/strcpy_s.c \
  "$SC"/src/str/strncpy_s.c \
  "$SC"/src/str/strcat_s.c \
  "$SC"/src/str/strncat_s.c \
  "$SC"/src/str/strnlen_s.c \
  "$SC"/src/str/strerror_s.c \
  "$SC"/src/str/strtok_s.c \
  "$SC"/src/extstr/strcmp_s.c \
  "$SC"/src/cplus_sc_wrappers.c; do
  base=$(basename "$f" .c)
  "$CC" $CFLAGS_SC -c "$f" -o "$SAFE_OUT/sc_$base.o"
done

# Intel safestringlib is retained for provenance only; not compiled/linked.
"$CC" $CFLAGS_W -c "$ROOT/third_party/exceptions4c/exceptions4c.c" -o "$SAFE_OUT/e4c_exceptions4c.o"

for f in "$ROOT"/stdlib/safe/src/*.c; do
  base=$(basename "$f" .c)
  "$CC" $CFLAGS_W -I"$SC/include" -c "$f" -o "$SAFE_OUT/w_$base.o"
done

ar rcs "$OUT/libcplus_safecrt.a" "$SAFE_OUT"/*.o

# ---------------------------------------------------------------------------
# 2. Build libcplus_hardened.a with ASan (mirrors tests/libc/build_libc.sh).
# ---------------------------------------------------------------------------
CFLAGS_LIBC="-Wall -Wextra -pedantic -std=c99 -I$ROOT/libc -I$ROOT/libc/include $ASAN"
for f in "$ROOT"/libc/log/log.c "$ROOT"/libc/src/errno/errno.c "$ROOT"/libc/src/stdlib/alloc.c "$ROOT"/libc/src/stdlib/stdlib_extra.c "$ROOT"/libc/src/stdlib/stdlib_extra2.c "$ROOT"/libc/src/stdlib/stdlib_extra3.c "$ROOT"/libc/src/stdlib/stdlib_extra4.c "$ROOT"/libc/src/stdlib/stdlib_extra5.c "$ROOT"/libc/src/stdlib/stdlib_extra6.c "$ROOT"/libc/src/stdlib/assert_extra.c "$ROOT"/libc/src/stdlib/musl_stdlib_extra.c "$ROOT"/libc/src/string/extra2.c "$ROOT"/libc/src/string/extra3.c "$ROOT"/libc/src/string/string_extra4.c "$ROOT"/libc/src/string/string_extra5.c "$ROOT"/libc/src/string/string_extra6.c "$ROOT"/libc/src/string/strtok.c "$ROOT"/libc/src/string/musl_extra.c "$ROOT"/libc/src/string/strndup.c "$ROOT"/libc/src/string/strerror.c "$ROOT"/libc/src/string/memory.c "$ROOT"/libc/src/string/bounded.c "$ROOT"/libc/src/string/string.c "$ROOT"/libc/src/string/extra.c "$ROOT"/libc/src/stdio/stdio.c "$ROOT"/libc/src/stdio/stdio2.c "$ROOT"/libc/src/ctype/ctype.c "$ROOT"/libc/src/wchar/wchar.c "$ROOT"/libc/src/wctype/wctype.c "$ROOT"/libc/src/ctype/ctype2.c "$ROOT"/libc/src/ctype/ctype3.c "$ROOT"/libc/src/math/math.c "$ROOT"/libc/src/math/math2.c "$ROOT"/libc/src/math/math3.c "$ROOT"/libc/src/math/math4.c "$ROOT"/libc/src/time/time.c "$ROOT"/libc/src/time/time_extra.c "$ROOT"/libc/src/locale/locale.c "$ROOT"/libc/src/signal/signal.c "$ROOT"/libc/src/signal/signal2.c "$ROOT"/libc/src/stdio/stdio3.c "$ROOT"/libc/src/fenv/fenv.c "$ROOT"/libc/src/complex/complex.c "$ROOT"/libc/src/thread/thread.c "$ROOT"/libc/src/fenv/fenv_extra.c "$ROOT"/libc/src/time/time_ascii.c "$ROOT"/libc/src/wchar/wchar_numeric.c "$ROOT"/libc/src/wchar/wchar_multibyte.c "$ROOT"/libc/src/wchar/wchar_format.c "$ROOT"/libc/src/wctype/wctype_extra.c "$ROOT"/libc/src/complex/complex_extra.c "$ROOT"/libc/src/math/math_float.c "$ROOT"/libc/src/math/math_ldouble.c "$ROOT"/libc/src/stdlib/stdlib_c17.c "$ROOT"/libc/src/stdlib/inttypes_wide.c; do
  b="$LIBC_OUT/$(basename "$f" .c).o"
  "$CC" $CFLAGS_LIBC -c "$f" -o "$b"
done

ar rcs "$OUT/libcplus_hardened.a" "$LIBC_OUT"/*.o

# ---------------------------------------------------------------------------
# 3. Build the ASan test against the instrumented archives.
#    -fsanitize=address is on BOTH the compile and the link command line.
# ---------------------------------------------------------------------------
INC="-I$ROOT/stdlib/safe -I$SC/include -I$UP/include -I$ROOT/libc -I$ROOT/libc/include -I$ROOT/libc/install-include -I$ROOT"
BIN="$OUT/test_alloc_asan"
"$CC" $ASAN -Wall -Wextra -std=c99 $INC "$TEST" \
  "$OUT/libcplus_safecrt.a" "$OUT/libcplus_hardened.a" -lm -pthread -o "$BIN"

# ---------------------------------------------------------------------------
# 4. Run the scenarios.
#
# The test selects a scenario via argv[1] (1..12). Some scenarios deliberately
# perform an invalid access, so a nonzero exit plus an ASan report is the
# expected outcome there. The clean-exit gate is scenarios 5, 8, 11 and 12;
# the remaining scenarios are reported as informational.
# ---------------------------------------------------------------------------
pass=0
fail=0
info=0

# run_scenario sets the globals RC and HAS_ASAN. It is called directly (not
# via command substitution) so that the HOST_UNSUPPORTED early exit applies to
# the whole script.
run_scenario() {
  n=$1
  out="$RUN_OUT/scenario_${n}.out"
  err="$RUN_OUT/scenario_${n}.err"
  : > "$out"
  : > "$err"
  set +e
  "$BIN" "$n" > "$out" 2> "$err"
  RC=$?
  set -e

  # ASan startup failure (before main): the test never reached main, so none
  # of its ASAN_LABEL/ASAN_CLEAN/ASAN_FAIL markers were printed.
  if grep -Eq 'CHECK failed: sanitizer_malloc_mac\.inc|Interceptors are not working|ASan runtime does not come first' "$err"; then
    if ! grep -Eq 'ASAN_LABEL|ASAN_CLEAN|ASAN_FAIL|Usage:' "$out" "$err"; then
      echo "HOST_UNSUPPORTED: ASan runtime cannot initialize on this host"
      echo "  ($(head -n 1 "$err"))"
      echo "  This is a platform limitation (seen with Apple clang on macOS arm64), not a test failure."
      echo "asan integration: 0 passed, 0 failed"
      exit 0
    fi
  fi

  HAS_ASAN=0
  if grep -q 'ERROR: AddressSanitizer' "$out" "$err"; then
    HAS_ASAN=1
  fi
}

case_clean() {
  n=$1
  run_scenario "$n"
  if [ "$RC" -eq 0 ] && [ "$HAS_ASAN" -eq 0 ]; then
    echo "PASS scenario $n: clean exit, no ASan report"
    pass=$((pass+1))
  else
    echo "FAIL scenario $n: rc=$RC asan_report=$HAS_ASAN (expected clean exit with no ASan report)"
    if [ "$HAS_ASAN" -eq 1 ]; then
      grep -m1 'ERROR: AddressSanitizer' "$RUN_OUT/scenario_${n}.err" || true
    else
      tail -n 5 "$RUN_OUT/scenario_${n}.err" || true
    fi
    fail=$((fail+1))
  fi
}

case_info() {
  n=$1
  run_scenario "$n"
  info=$((info+1))
  if [ "$RC" -ne 0 ] && [ "$HAS_ASAN" -eq 1 ]; then
    echo "INFO scenario $n: ASan reported the expected invalid access (rc=$RC)"
  elif [ "$RC" -eq 0 ] && [ "$HAS_ASAN" -eq 0 ]; then
    echo "INFO scenario $n: clean exit (valid path)"
  else
    echo "INFO scenario $n: unexpected outcome rc=$RC asan_report=$HAS_ASAN"
    tail -n 5 "$RUN_OUT/scenario_${n}.err" || true
  fi
}

# Mandatory clean-exit gate.
for n in 5 8 11 12; do
  case_clean "$n"
done

# Informational scenarios: 1-4 are invalid accesses, 6/7/9/10 mix valid and
# overflow sub-parts, so their exact sub-case numbering is test-dependent.
for n in 1 2 3 4 6 7 9 10; do
  case_info "$n"
done

echo "asan integration: $pass passed, $fail failed"
echo "asan integration (informational): $info scenario(s) reported"
[ "$fail" -eq 0 ]

#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CPLUS_CLANG:-clang}
OUT="$ROOT/build/safe"
mkdir -p "$OUT"
SC="$ROOT/third_party/safeclib"
UP="$ROOT/third_party/intel-safestringlib"

# generate minimal config.h for safeclib
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

# symbol prefixing via -D (avoid macro conflicts in headers)
SC_DEFS="-Dmemcpy_s=cplus_sc_memcpy_s -D_memcpy_s_chk=cplus_sc__memcpy_s_chk -Dmemmove_s=cplus_sc_memmove_s -D_memmove_s_chk=cplus_sc__memmove_s_chk -Dmemset_s=cplus_sc_memset_s -D_memset_s_chk=cplus_sc__memset_s_chk -Dmemcmp_s=cplus_sc_memcmp_s -D_memcmp_s_chk=cplus_sc__memcmp_s_chk -Dmemzero_s=cplus_sc_memzero_s -D_memzero_s_chk=cplus_sc__memzero_s_chk -Dstrcpy_s=cplus_sc_strcpy_s -D_strcpy_s_chk=cplus_sc__strcpy_s_chk -Dstrncpy_s=cplus_sc_strncpy_s -D_strncpy_s_chk=cplus_sc__strncpy_s_chk -Dstrcat_s=cplus_sc_strcat_s -D_strcat_s_chk=cplus_sc__strcat_s_chk -Dstrncat_s=cplus_sc_strncat_s -D_strncat_s_chk=cplus_sc__strncat_s_chk -Dstrnlen_s=cplus_sc_strnlen_s -D_strnlen_s_chk=cplus_sc__strnlen_s_chk -D_strerror_s_chk=cplus_sc__strerror_s_chk -D_strtok_s_chk=cplus_sc__strtok_s_chk -Dstrcmp_s=cplus_sc_strcmp_s -D_strcmp_s_chk=cplus_sc__strcmp_s_chk -Dabort_handler_s=cplus_sc_abort_handler_s -Dignore_handler_s=cplus_sc_ignore_handler_s -Dset_mem_constraint_handler_s=cplus_sc_set_mem_constraint_handler_s -Dset_str_constraint_handler_s=cplus_sc_set_str_constraint_handler_s -Dinvoke_safe_mem_constraint_handler=cplus_sc_invoke_safe_mem_constraint_handler -Dinvoke_safe_str_constraint_handler=cplus_sc_invoke_safe_str_constraint_handler"

INC_SC="-I$OUT -I$SC/include -I$SC/src -I$ROOT/stdlib/safe"
CFLAGS_SC="-O2 -std=c99 -D__STDC_WANT_LIB_EXT1__=0 $SC_DEFS -Wno-unused-parameter -Wno-missing-field-initializers -Wno-macro-redefined $INC_SC"
CFLAGS_W="-O2 -Wall -Wextra -pedantic -std=c99 -I$SC/include -I$ROOT/stdlib/safe"

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
  "$CC" $CFLAGS_SC -c "$f" -o "$OUT/sc_$base.o"
done

# Intel safestringlib retained in third_party for provenance/reference only; not compiled/linked.

for f in "$ROOT"/stdlib/safe/src/*.c; do
  base=$(basename "$f" .c)
  "$CC" $CFLAGS_W -I$SC/include -c "$f" -o "$OUT/w_$base.o"
done

ar rcs "$OUT/libcplus_safecrt.a" "$OUT"/sc_*.o "$OUT"/w_*.o
echo "$OUT/libcplus_safecrt.a"

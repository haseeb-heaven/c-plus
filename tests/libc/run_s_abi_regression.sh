#!/bin/sh
# Issue #189 regression: hardened libc must not define or shadow any standard
# C symbol, and every Annex K _s symbol it does expose must be additive
# (cplus_* prefixed).  Also builds/runs the typed-ABI C suite.
#
# Usage:
#   sh tests/libc/run_s_abi_regression.sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CC=${CPLUS_CLANG:-clang}
LIB="$ROOT/build/libc/libcplus_hardened.a"

sh "$ROOT/tests/libc/build_libc.sh" >/dev/null

TMP=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMP/cplus-s-abi.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

fail=0

# Portable "defined global symbols" extraction (GNU nm vs BSD/LLVM nm).
if nm -g --defined-only "$LIB" >"$WORK/defined.raw" 2>/dev/null; then :; else
  nm -gU "$LIB" >"$WORK/defined.raw"
fi
awk 'NF >= 2 { print $NF }' "$WORK/defined.raw" |
  sed 's/^_//' | sort -u >"$WORK/defined.txt"

# (1) Non-ABI-preserving Annex K spellings must NEVER be exported unprefixed.
for sym in strcpy_s strncpy_s strcat_s strncat_s memcpy_s memmove_s memset_s \
           sprintf_s snprintf_s scanf_s fopen_s getenv_s gmtime_s localtime_s \
           qsort_s bsearch_s strtok_s strerror_s; do
  if grep -qx "$sym" "$WORK/defined.txt"; then
    echo "FAIL: hardened libc defines unprefixed Annex K symbol '$sym'"
    fail=1
  fi
done

# (2) Standard C ABI symbols must remain undefined here (provided by the CRT).
for sym in strcpy strncpy strcat strncat memcpy memmove memset fopen fclose \
           qsort bsearch malloc calloc realloc free snprintf getenv strerror; do
  if grep -qx "$sym" "$WORK/defined.txt"; then
    echo "FAIL: hardened libc redefines standard C symbol '$sym'"
    fail=1
  fi
done

# (3) The additive, ABI-incompatible _s variants libc DOES ship are namespaced.
for sym in cplus_sc_upstream_bsearch_s cplus_sc_upstream_memrchr_s; do
  if ! grep -qx "$sym" "$WORK/defined.txt"; then
    echo "FAIL: missing additive namespaced symbol '$sym'"
    fail=1
  fi
done

# (4) Typed C ABI suite + link against a host-C object (no duplicate symbols).
cat >"$WORK/precompiled_c.c" <<'EOF'
#include <stdlib.h>
#include <string.h>
int cplus_s_abi_precompiled_probe(void) {
    char buf[8];
    void *p = malloc(8u);
    if (p == NULL) return 1;
    memset(p, 0, 8u);
    strcpy(buf, "ok");
    free(p);
    return buf[0] == 'o' ? 0 : 1;
}
EOF
if "$CC" -O2 -std=c99 -c "$WORK/precompiled_c.c" -o "$WORK/precompiled_c.o"; then
  if "$CC" -O2 -Wall -Wextra -pedantic -std=c99 \
        -I"$ROOT/libc" -I"$ROOT/libc/include" -I"$ROOT/libc/install-include" \
        "$ROOT/tests/libc/test_s_abi_preservation.c" "$WORK/precompiled_c.o" \
        "$LIB" -lm -pthread -o "$WORK/s_abi_test"; then
    if ! "$WORK/s_abi_test"; then
      echo "FAIL: test_s_abi_preservation did not pass"
      fail=1
    fi
  else
    echo "FAIL: typed ABI suite did not build/link"
    fail=1
  fi
else
  echo "FAIL: precompiled C object did not build"
  fail=1
fi

if [ "$fail" -eq 0 ]; then
  echo "s_abi_regression: ok"
fi
exit "$fail"

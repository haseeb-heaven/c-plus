#!/bin/sh
# Gate for the cve_like SAFE variants only.
#
# The original crashing fixtures case_13.c / case_15.c are BASELINE-ONLY
# evidence (see NOTES_5.md). They are deliberately NOT built or run here.
#
# Each *_safe.c in this directory is built in both layers:
#   safecrt : stdlib/safe/safe_c.h  + libcplus_safecrt.a + libcplus_hardened.a
#   plain   : -DPLAIN_LIBC, cplus_libc_safe.h + libcplus_hardened.a
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

# Prefer the SDK-aware system clang; honour CPLUS_CLANG when supplied.
if [ -z "${CPLUS_CLANG:-}" ]; then
  if [ -x /usr/bin/clang ]; then CPLUS_CLANG=/usr/bin/clang; else CPLUS_CLANG=clang; fi
fi
export CPLUS_CLANG
CC=$CPLUS_CLANG

SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
SYSROOT=
if [ -d "$SDK" ]; then SYSROOT="-isysroot $SDK"; fi

OUT="$ROOT/build/cve_like"
mkdir -p "$OUT"

if ! sh tests/libc/build_libc.sh >"$OUT/build_libc.log" 2>&1; then
  echo "FATAL: libc build failed"
  tail -12 "$OUT/build_libc.log" 2>/dev/null || true
  exit 1
fi
if ! sh scripts/build_safecrt.sh >"$OUT/build_safecrt.log" 2>&1; then
  echo "FATAL: safecrt build failed"
  tail -12 "$OUT/build_safecrt.log" 2>/dev/null || true
  exit 1
fi

LIBC="$ROOT/build/libc/libcplus_hardened.a"
SAFE="$ROOT/build/safe/libcplus_safecrt.a"
INC="-I. -Istdlib/safe -Ilibc -Ilibc/install-include -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"

pass=0; fail=0

for t in tests/scenarios/cve_like/*_safe.c; do
  [ -f "$t" ] || continue
  name=$(basename "$t" .c)
  for layer in safecrt plain; do
    if [ "$layer" = plain ]; then
      DEFS="-DPLAIN_LIBC"
      LIBS="$LIBC -lm -pthread"
    else
      DEFS=""
      LIBS="$SAFE $LIBC -lm -pthread"
    fi
    bin="$OUT/${name}_${layer}"
    if "$CC" $SYSROOT -O2 -std=c99 -Wall -Wextra -pedantic $DEFS $INC "$t" $LIBS \
         -o "$bin" >"$OUT/${name}_${layer}.build.log" 2>&1 \
       && printf '1\n' | "$bin" >"$OUT/${name}_${layer}.run.log" 2>&1; then
      echo "ok   $name ($layer): $(tail -1 "$OUT/${name}_${layer}.run.log")"
      pass=$((pass + 1))
    else
      echo "FAIL $name ($layer)"
      tail -12 "$OUT/${name}_${layer}.build.log" 2>/dev/null || true
      tail -12 "$OUT/${name}_${layer}.run.log" 2>/dev/null || true
      fail=$((fail + 1))
    fi
  done
done

echo "cve-like safe variants: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

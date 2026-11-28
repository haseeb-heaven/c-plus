#!/bin/sh
# compare.sh -- driver for the per-method field/first-last/classify memory-
# safety comparison (C+ checked APIs vs safe Rust equivalents).
#
# Availability is NEVER assumed: every method is probed with `nm -g` on
# build/safe/libcplus_safecrt.a. A method whose name does not appear in that
# archive is reported BLOCKED (with the exact grep evidence on stderr) and is
# compiled out of both harnesses -- no tokens are fabricated for it.
#
# Output: METHOD_FIELD <cases> <matched> <mismatched> <blocked>
#   <cases>      number of case lines emitted by the C harness (6 per method)
#   <matched>    case lines whose token matches the Rust harness line-for-line
#   <mismatched> case lines that differ (or are missing/extra)
#   <blocked>    number of methods absent from the archive
# Exit status: 0 iff mismatched == 0 and both harnesses built and ran.

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../../.." && pwd)
ARCHIVE="$ROOT/build/safe/libcplus_safecrt.a"
CLANG=${CLANG:-/usr/bin/clang}
RUSTC=${RUSTC:-rustc}
TIMEOUT=${TIMEOUT:-timeout}

METHODS="strcmpfld_s strcpyfld_s strcoll_s strfirstchar_s strlastchar_s strfirstdiff_s strlastdiff_s strisalphanumeric_s strisascii_s strisdigit_s strishex_s"

have_define() {
  case "$1" in
    strcmpfld_s)          echo HAVE_STRCMPFLD_S ;;
    strcpyfld_s)          echo HAVE_STRCPYFLD_S ;;
    strcoll_s)            echo HAVE_STRCOLL_S ;;
    strfirstchar_s)       echo HAVE_STRFIRSTCHAR_S ;;
    strlastchar_s)        echo HAVE_STRLASTCHAR_S ;;
    strfirstdiff_s)       echo HAVE_STRFIRSTDIFF_S ;;
    strlastdiff_s)        echo HAVE_STRLASTDIFF_S ;;
    strisalphanumeric_s)  echo HAVE_STRISALPHANUMERIC_S ;;
    strisascii_s)         echo HAVE_STRISASCII_S ;;
    strisdigit_s)         echo HAVE_STRISDIGIT_S ;;
    strishex_s)           echo HAVE_STRISHEX_S ;;
  esac
}

# ---- 1. availability probe (nm on the exact archive) --------------------
if [ -f "$ARCHIVE" ]; then
  NM_OUT=$("$TIMEOUT" 30 nm -g "$ARCHIVE" 2>/dev/null || true)
else
  NM_OUT=""
  echo "ARCHIVE-MISSING $ARCHIVE" >&2
fi

DEFS=""
AVAIL=""
BLOCKED=0
for m in $METHODS; do
  if printf '%s\n' "$NM_OUT" | grep -q "$m"; then
    DEFS="$DEFS -D$(have_define "$m")"
    AVAIL="$AVAIL,$m"
  else
    BLOCKED=$((BLOCKED + 1))
    printf 'BLOCKED %s: nm -g %s | grep %s -> no match (absent from archive)\n' \
      "$m" "$ARCHIVE" "$m" >&2
  fi
done
AVAIL=${AVAIL#,}

# ---- 2. build + run the C harness (clang -std=c99, timeout 30) ----------
C_STATUS=0
"$TIMEOUT" 30 "$CLANG" -std=c99 \
  -I"$ROOT/libc/install-include" \
  -I"$ROOT/third_party/safeclib/include" \
  $DEFS "$HERE/field_harness.c" "$ARCHIVE" \
  -o "$HERE/field_harness" 2>"$HERE/c_compile.err" || C_STATUS=$?

C_RUN=0
if [ "$C_STATUS" -eq 0 ]; then
  "$TIMEOUT" 30 "$HERE/field_harness" >"$HERE/c.out" 2>"$HERE/c.err" || C_RUN=$?
else
  : >"$HERE/c.out"
fi

# ---- 3. build + run the Rust harness (rustc -O, no unsafe) --------------
R_STATUS=0
"$TIMEOUT" 30 "$RUSTC" -O "$HERE/field.rs" \
  -o "$HERE/field" 2>"$HERE/rust_compile.err" || R_STATUS=$?

R_RUN=0
if [ "$R_STATUS" -eq 0 ]; then
  "$TIMEOUT" 30 "$HERE/field" "$AVAIL" \
    >"$HERE/rust.out" 2>"$HERE/rust.err" || R_RUN=$?
else
  : >"$HERE/rust.out"
fi

# ---- 4. token-by-token comparison ---------------------------------------
COUNT=$(awk '
  NR == FNR { c[FNR] = $0; nc = FNR; next }
  { r[FNR] = $0; nr = FNR }
  END {
    n = (nc > nr) ? nc : nr
    m = 0; x = 0
    for (i = 1; i <= n; i++) {
      if (c[i] == r[i]) m++; else x++;
    }
    printf "%d %d", m, x
  }
' "$HERE/c.out" "$HERE/rust.out")
MATCHED=$(printf '%s\n' "$COUNT" | awk '{print $1}')
MISMATCHED=$(printf '%s\n' "$COUNT" | awk '{print $2}')
CASES=$(wc -l <"$HERE/c.out" | tr -d ' ')

if [ -s "$HERE/c.err" ]; then
  echo "C harness stderr (first lines):" >&2
  head -5 "$HERE/c.err" >&2
fi

if ! diff -u "$HERE/c.out" "$HERE/rust.out" >"$HERE/token.diff" 2>&1; then
  echo "=== token mismatch (c.out vs rust.out) ===" >&2
  head -40 "$HERE/token.diff" >&2
fi

echo "METHOD_FIELD $CASES $MATCHED $MISMATCHED $BLOCKED"

if [ "$C_STATUS" -ne 0 ] || [ "$C_RUN" -ne 0 ] || \
   [ "$R_STATUS" -ne 0 ] || [ "$R_RUN" -ne 0 ] || \
   [ "$MISMATCHED" -ne 0 ]; then
  echo "compare.sh: FAIL (c_build=$C_STATUS c_run=$C_RUN " \
       "rust_build=$R_STATUS rust_run=$R_RUN mismatched=$MISMATCHED)" >&2
  exit 1
fi
exit 0

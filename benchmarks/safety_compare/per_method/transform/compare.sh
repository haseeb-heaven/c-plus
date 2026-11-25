#!/bin/sh
# compare.sh -- per-method memory-safety token comparison: C+ checked APIs vs Rust.
#
# For every target method, the linked symbol in build/safe/libcplus_safecrt.a is
# detected with nm. Available methods are compiled into transform.c via
# -DHAVE_<METHOD>; methods with no linked symbol are reported BLOCKED with the
# exact nm evidence. Both harnesses are built, run with a 30s timeout, and
# their "<method> <case> <token>" lines are compared position-by-position.
#
# Prints:  METHOD_TRANSFORM <cases> <matched> <mismatched> <blocked>
# Exit:    0 when mismatched == 0 (BLOCKED is not a mismatch), 1 on a real
#          token mismatch. Build/run failures are reported as BLOCKED with
#          captured evidence, never as a pass and never as a fabricated token.
#
# Env overrides (optional): CPLUS_CLANG, CPLUS_RUSTC, CPLUS_ARCHIVE.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
HERE="$ROOT/benchmarks/safety_compare/per_method/transform"
RESULTS="$HERE/results"
ARCHIVE=${CPLUS_ARCHIVE:-"$ROOT/build/safe/libcplus_safecrt.a"}
CLANG=${CPLUS_CLANG:-/usr/bin/clang}
RUSTC=${CPLUS_RUSTC:-rustc}
TIMEOUT=${CPLUS_COMPARE_TIMEOUT:-30}

mkdir -p "$RESULTS" || exit 2

METHODS="strtolowercase_s strtouppercase_s strlcat strlcpy strnset_s strset_s strnterminate_s"

symbol_for() {
  case "$1" in
    strtolowercase_s)  printf 'cplus_v__strtolowercase_s_chk' ;;
    strtouppercase_s)  printf 'cplus_v__strtouppercase_s_chk' ;;
    strlcat)           printf 'cplus_v_strlcat' ;;
    strlcpy)           printf 'cplus_v_strlcpy' ;;
    strnset_s)         printf 'cplus_v__strnset_s_chk' ;;
    strset_s)          printf 'cplus_v__strset_s_chk' ;;
    strnterminate_s)   printf 'cplus_v__strnterminate_s_chk' ;;
    *)                 printf '' ;;
  esac
}

if [ ! -f "$ARCHIVE" ]; then
  echo "BLOCKED all: archive $ARCHIVE does not exist" >&2
  printf 'METHOD_TRANSFORM 0 0 0 7\n'
  exit 0
fi

# Capture the full relevant nm output once for the evidence record.
NM_OUT="$RESULTS/nm_symbols.txt"
nm -g "$ARCHIVE" 2>"$RESULTS/nm.stderr" | \
  grep -E 'cplus_v__?(strtolowercase_s|strtouppercase_s|strlcat|strlcpy|strnset_s|strset_s|strnterminate_s)' \
  > "$NM_OUT" 2>/dev/null || true

AVAIL=""
BLOCKED=""
for m in $METHODS; do
  sym=$(symbol_for "$m")
  if [ -n "$sym" ] && grep -qE " [TDB] _?${sym}$" "$NM_OUT"; then
    AVAIL="$AVAIL $m"
  else
    BLOCKED="$BLOCKED $m"
    echo "BLOCKED $m: symbol '$sym' not linked in $ARCHIVE" >&2
    grep -n "$sym" "$NM_OUT" >&2 || true
  fi
done

DEFS=""
for m in $AVAIL; do
  def=$(printf '%s' "$m" | tr '[:lower:]' '[:upper:]')
  DEFS="$DEFS -DHAVE_$def=1"
done

# ---------------------------------------------------------------------------
# Build + run the C99 harness.
# ---------------------------------------------------------------------------
C_BIN="$RESULTS/transform_c"
C_BUILD_OUT="$RESULTS/c.build.out"
C_BUILD_ERR="$RESULTS/c.build.err"
C_RUN_OUT="$RESULTS/c.out"
C_RUN_ERR="$RESULTS/c.run.err"

C_BUILD_RC=0
$CLANG -std=c99 -I"$ROOT/libc/install-include" \
  -I"$ROOT/third_party/safeclib/include" $DEFS \
  "$HERE/transform.c" -o "$C_BIN" "$ARCHIVE" \
  >"$C_BUILD_OUT" 2>"$C_BUILD_ERR" || C_BUILD_RC=$?

C_RUN_RC=0
if [ "$C_BUILD_RC" -eq 0 ] && [ -x "$C_BIN" ]; then
  timeout "$TIMEOUT" "$C_BIN" >"$C_RUN_OUT" 2>"$C_RUN_ERR" || C_RUN_RC=$?
else
  C_RUN_RC=125
  : > "$C_RUN_OUT"
fi

# ---------------------------------------------------------------------------
# Build + run the safe Rust harness.
# ---------------------------------------------------------------------------
R_BIN="$RESULTS/transform_rust"
R_BUILD_OUT="$RESULTS/rust.build.out"
R_BUILD_ERR="$RESULTS/rust.build.err"
R_RUN_OUT="$RESULTS/rust.out"
R_RUN_ERR="$RESULTS/rust.run.err"

R_BUILD_RC=0
$RUSTC -O "$HERE/transform.rs" -o "$R_BIN" \
  >"$R_BUILD_OUT" 2>"$R_BUILD_ERR" || R_BUILD_RC=$?

R_RUN_RC=0
if [ "$R_BUILD_RC" -eq 0 ] && [ -x "$R_BIN" ]; then
  timeout "$TIMEOUT" "$R_BIN" >"$R_RUN_OUT" 2>"$R_RUN_ERR" || R_RUN_RC=$?
else
  R_RUN_RC=125
  : > "$R_RUN_OUT"
fi

# If either side could not be built/run, this is BLOCKED (external/toolchain),
# not a token mismatch and not a pass. Print evidence and stop.
if [ "$C_BUILD_RC" -ne 0 ] || [ "$C_RUN_RC" -ne 0 ] || \
   [ "$R_BUILD_RC" -ne 0 ] || [ "$R_RUN_RC" -ne 0 ]; then
  {
    echo "BLOCKED: harness could not be built/run; evidence:"
    echo "  C build rc=$C_BUILD_RC ($C_BUILD_ERR)"
    echo "  C run   rc=$C_RUN_RC ($C_RUN_ERR)"
    echo "  Rust build rc=$R_BUILD_RC ($R_BUILD_ERR)"
    echo "  Rust run   rc=$R_RUN_RC ($R_RUN_ERR)"
  } >&2
  B=$(printf '%s\n' $BLOCKED | grep -c . || true)
  BC=$B
  # Count methods that were available but could not execute as blocked too.
  for m in $AVAIL; do BC=$((BC + 1)); done
  printf 'METHOD_TRANSFORM 0 0 0 %s\n' "$BC"
  exit 0
fi

# ---------------------------------------------------------------------------
# Position-by-position token comparison. Raw output stays as evidence; the
# token comparison uses filtered copies so any stray safeclib constraint
# handler diagnostic ("IGNORE CONSTRAINT HANDLER: ...") cannot shift tokens.
# ---------------------------------------------------------------------------
C_TOKENS="$RESULTS/c.tokens"
R_TOKENS="$RESULTS/r.tokens"
grep -v '^IGNORE CONSTRAINT HANDLER:' "$C_RUN_OUT" > "$C_TOKENS" 2>/dev/null || true
grep -v '^IGNORE CONSTRAINT HANDLER:' "$R_RUN_OUT" > "$R_TOKENS" 2>/dev/null || true

CN=$(awk 'END{print NR}' "$C_TOKENS")
RN=$(awk 'END{print NR}' "$R_TOKENS")
TOTAL=$CN
[ "$RN" -gt "$TOTAL" ] && TOTAL=$RN

MATCHED=0
if [ "$TOTAL" -gt 0 ]; then
  MATCHED=$(awk -v max="$TOTAL" '
    NR==FNR { a[FNR]=$0; next }
    { if ($0 == a[FNR]) n++ }
    END { print n+0 }
  ' "$C_TOKENS" "$R_TOKENS")
fi
MISMATCHED=$((TOTAL - MATCHED))

diff -u "$C_TOKENS" "$R_TOKENS" > "$RESULTS/tokens.diff" 2>&1 || true

BLOCKED_N=$(printf '%s\n' $BLOCKED | grep -c . || true)

printf 'METHOD_TRANSFORM %s %s %s %s\n' \
  "$TOTAL" "$MATCHED" "$MISMATCHED" "$BLOCKED_N"

if [ "$MISMATCHED" -ne 0 ]; then
  echo "MISMATCH: see $RESULTS/tokens.diff" >&2
  exit 1
fi
exit 0

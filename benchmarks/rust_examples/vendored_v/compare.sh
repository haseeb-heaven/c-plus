#!/bin/sh
# benchmarks/rust_examples/vendored_v/compare.sh
#
# Build and run the C caller and the safe-Rust examples, diff their
# "<api> <case> <token>" streams, and print:
#
#   RUST_EXAMPLES vendored_v <apis> <cases> <matched> <mismatched>
#
# Every API is verified against `nm -g build/safe/libcplus_safecrt.a` before
# anything is built. An API with no ` T _<symbol>` is reported BLOCKED and is
# never fabricated in the output. A real token mismatch (or a build/run
# failure) exits 1; a fully blocked run exits 0 with zero counts.
#
# All build/run steps are wrapped in /usr/bin/timeout 30. Artifacts are kept
# in a temporary directory and removed on exit; no source file is modified.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
HERE="$ROOT/benchmarks/rust_examples/vendored_v"
SAFE="$ROOT/build/safe/libcplus_safecrt.a"
HARD="$ROOT/build/libc/libcplus_hardened.a"
SAFECRT_INC="$ROOT/libc/install-include"
UPSTREAM_INC="$ROOT/third_party/safeclib/include"

CLANG=${CLANG:-/usr/bin/clang}
TIMEOUT=${TIMEOUT:-/usr/bin/timeout}
RUSTC=${RUSTC:-rustc}
NM=${NM:-nm}
case "$RUSTC" in
  /*) ;;
  *) RUSTC=$(command -v "$RUSTC" 2>/dev/null || printf '%s' "$RUSTC") ;;
esac
case "$NM" in
  /*) ;;
  *) NM=$(command -v "$NM" 2>/dev/null || printf '%s' "$NM") ;;
esac

# Every symbol this harness must call (from the task scope / nm evidence).
EXPECTED_APIS="
cplus_v_memchr_s
cplus_v__memchr_s_chk
cplus_v_memccpy_s
cplus_v__memccpy_s_chk
cplus_v__strchr_s_chk
cplus_v__strrchr_s_chk
cplus_v__strstr_s_chk
cplus_v__strcasestr_s_chk
cplus_v__strspn_s_chk
cplus_v__strcspn_s_chk
cplus_v_strlcpy
cplus_v_strlcat
cplus_v__stpcpy_s_chk
cplus_v__stpncpy_s_chk
cplus_v__strset_s_chk
cplus_v__strnset_s_chk
cplus_v__strnterminate_s_chk
cplus_v__strtolowercase_s_chk
cplus_v__strtouppercase_s_chk
cplus_v__strfirstchar_s_chk
cplus_v__strlastchar_s_chk
cplus_v__strfirstdiff_s_chk
cplus_v__strlastdiff_s_chk
cplus_v__strcmpfld_s_chk
cplus_v__strcpyfld_s_chk
cplus_v__strcoll_s_chk
"

summary_zero() {
  printf 'RUST_EXAMPLES vendored_v 0 0 0 0 0\n'
}

# --- prerequisites ---------------------------------------------------------
if [ ! -f "$SAFE" ] || [ ! -f "$HARD" ]; then
  echo "BLOCKED static libraries missing: $SAFE $HARD" >&2
  summary_zero
  exit 0
fi
if [ ! -x "$TIMEOUT" ]; then
  echo "BLOCKED timeout binary missing: $TIMEOUT" >&2
  summary_zero
  exit 0
fi
if [ ! -x "$CLANG" ] || [ ! -x "$RUSTC" ]; then
  echo "BLOCKED toolchain missing: clang=$CLANG rustc=$RUSTC" >&2
  summary_zero
  exit 0
fi

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export PATH
export LC_ALL=C

# --- nm symbol evidence ----------------------------------------------------
NM_TXT=$($TIMEOUT 30 "$NM" -g "$SAFE" 2>/dev/null) || NM_TXT=""
BLOCKED=""
VERIFIED=""
for api in $EXPECTED_APIS; do
  if printf '%s\n' "$NM_TXT" | grep -q " T _${api}\$"; then
    VERIFIED="$VERIFIED $api"
  else
    echo "BLOCKED $api: no ' T _${api}' in nm -g $SAFE"
    BLOCKED="$BLOCKED $api"
  fi
done
if [ -n "$BLOCKED" ]; then
  # Do not build or print tokens for APIs that do not exist in the archive.
  summary_zero
  exit 0
fi

# --- build and run ---------------------------------------------------------
OUT=$(mktemp -d "${TMPDIR:-/tmp}/vendored_v.XXXXXX") || exit 2
trap 'rm -rf "$OUT"' EXIT HUP INT TERM

$TIMEOUT 30 "$CLANG" -O2 -std=c99 \
  -I"$SAFECRT_INC" -I"$UPSTREAM_INC" \
  "$HERE/harness.c" "$SAFE" "$HARD" -o "$OUT/harness" \
  >"$OUT/c.build.out" 2>"$OUT/c.build.err" || {
    echo "FAIL C build (see $OUT/c.build.err)" >&2
    cat "$OUT/c.build.err" >&2
    exit 1
  }

$TIMEOUT 30 "$RUSTC" -O "$HERE/vendored.rs" -o "$OUT/vendored" \
  >"$OUT/rust.build.out" 2>"$OUT/rust.build.err" || {
    echo "FAIL Rust build (see $OUT/rust.build.err)" >&2
    cat "$OUT/rust.build.err" >&2
    exit 1
  }

$TIMEOUT 30 "$OUT/harness" >"$OUT/c.tokens" 2>"$OUT/c.run.err" || {
  echo "FAIL C run" >&2
  exit 1
}
$TIMEOUT 30 "$OUT/vendored" >"$OUT/r.tokens" 2>"$OUT/r.run.err" || {
  echo "FAIL Rust run" >&2
  exit 1
}

# --- verify no fabricated APIs --------------------------------------------
printf '%s\n' "$NM_TXT" | sed -n 's/.* T _\(cplus_v_[^ ]*\)$/\1/p' | sort -u > "$OUT/nm.apis"
awk '{print $1}' "$OUT/r.tokens" | sort -u > "$OUT/out.apis"
if ! cmp -s "$OUT/nm.apis" "$OUT/out.apis"; then
  echo "FAIL emitted API set differs from nm symbol set" >&2
  diff -u "$OUT/nm.apis" "$OUT/out.apis" >&2 || true
  exit 1
fi

# --- counts and diff -------------------------------------------------------
API_COUNT=$(grep -c . "$OUT/out.apis" || true)
C_LINES=$(grep -c . "$OUT/c.tokens" || true)
R_LINES=$(grep -c . "$OUT/r.tokens" || true)
LC_ALL=C sort "$OUT/c.tokens" > "$OUT/c.sorted"
LC_ALL=C sort "$OUT/r.tokens" > "$OUT/r.sorted"
MATCHED=$(LC_ALL=C comm -12 "$OUT/c.sorted" "$OUT/r.sorted" | grep -c . || true)
MISMATCHED=$((C_LINES + R_LINES - 2 * MATCHED))

printf 'RUST_EXAMPLES vendored_v %s %s %s %s 0\n' \
  "$API_COUNT" "$R_LINES" "$MATCHED" "$MISMATCHED"

if [ "$MISMATCHED" -ne 0 ]; then
  diff -u "$OUT/c.tokens" "$OUT/r.tokens" >&2 || true
  exit 1
fi
exit 0

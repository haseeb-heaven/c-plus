#!/bin/sh
# benchmarks/safety_compare/per_method/search/compare.sh
#
# Per-method memory-safety outcome comparison: C+ checked search APIs vs safe
# Rust.  Deterministic, no timing, no nondeterministic input.
#
# Method coverage is determined by nm on the archives: only methods with a
# real linkable symbol are exercised.  At the measured commit:
#   bsearch_s  -> cplus_sc_upstream_bsearch_s  (libcplus_hardened.a, PRESENT)
#   memrchr_s  -> cplus_sc_upstream_memrchr_s  (libcplus_hardened.a, PRESENT)
#   memchr_s   -> cplus_v_memchr_s             (libcplus_safecrt.a,  PRESENT)
#   qsort_s    -> no symbol, only a macro in safeclib's safe_lib.h (BLOCKED)
#
# stdout: informational SYMBOL lines then the required summary line
#   METHOD_SEARCH <cases> <matched> <mismatched> <blocked>
# where <blocked> is the number of methods with no linkable symbol.
# Exit status: 0 when every exercised case matches (BLOCKED is not a failure);
# nonzero on any token mismatch, build failure, run failure or timeout.
#
# Environment overrides:
#   METHOD_SEARCH_CLANG, METHOD_SEARCH_RUSTC, METHOD_SEARCH_ARCHIVES,
#   METHOD_SEARCH_TIMEOUT, METHOD_SEARCH_OUT

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../../.." && pwd)

CLANG=${METHOD_SEARCH_CLANG:-/usr/bin/clang}
RUSTC=${METHOD_SEARCH_RUSTC:-rustc}
ARCHIVES=${METHOD_SEARCH_ARCHIVES:-"$ROOT/build/libc/libcplus_hardened.a $ROOT/build/safe/libcplus_safecrt.a"}
TMO=${METHOD_SEARCH_TIMEOUT:-30}
OUT=${METHOD_SEARCH_OUT:-"$HERE/out"}

C_BIN="$OUT/harness_c"
RS_BIN="$OUT/harness_rs"
C_OUT="$OUT/c.out"
RS_OUT="$OUT/rs.out"
C_ERR="$OUT/c.err"
RS_ERR="$OUT/rs.err"

mkdir -p "$OUT" || exit 1

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

# Report whether any of the given symbol spellings is defined in the archive.
# nm prints "0000000000000000 T _symbol", so match the final field with an
# optional leading underscore.
has_symbol() {
    syms=$1
    # $ARCHIVES is intentionally unquoted: it is a list of archive paths.
    nm -g --defined-only $ARCHIVES 2>/dev/null | awk -v syms="$syms" '
        BEGIN { n = split(syms, a, " ") }
        { name = $NF; sub(/^_/, "", name)
          for (i = 1; i <= n; i++) if (name == a[i]) found = 1 }
        END { exit(found ? 0 : 1) }
    '
}

for a in $ARCHIVES; do
    [ -f "$a" ] || die "archive not found: $a"
done
command -v "$CLANG" >/dev/null 2>&1 || die "clang not found: $CLANG"
command -v "$RUSTC" >/dev/null 2>&1 || die "rustc not found: $RUSTC"
command -v timeout >/dev/null 2>&1 || die "timeout utility not found"

blocked=0
for spec in \
    'bsearch_s|cplus_sc_upstream_bsearch_s bsearch_s' \
    'memrchr_s|cplus_sc_upstream_memrchr_s memrchr_s' \
    'memchr_s|cplus_v_memchr_s cplus_upstream_memchr_s cplus_sc_upstream_memchr_s cplus_sc_memchr_s memchr_s' \
    'qsort_s|cplus_upstream_qsort_s cplus_sc_upstream_qsort_s cplus_sc_qsort_s cplus_qsort_s qsort_s'
do
    method=${spec%%|*}
    candidates=${spec#*|}
    if has_symbol "$candidates"; then
        printf 'SYMBOL %s PRESENT (%s)\n' "$method" "$ARCHIVES"
        case "$method" in
            bsearch_s|memrchr_s|memchr_s) ;;
            *) printf 'NOTE %s has a symbol but harness.c has no case for it; not counted\n' \
                   "$method" >&2 ;;
        esac
    else
        printf 'SYMBOL %s ABSENT (BLOCKED, no linkable symbol)\n' "$method"
        blocked=$((blocked + 1))
    fi
done

# The C harness references the PRESENT symbols listed above.
rm -f "$C_BIN" "$RS_BIN" "$C_OUT" "$RS_OUT" "$C_ERR" "$RS_ERR"

timeout "$TMO" "$CLANG" -std=c99 \
    -I"$ROOT/libc/install-include" \
    -I"$ROOT/third_party/safeclib/include" \
    "$HERE/harness.c" $ARCHIVES -o "$C_BIN" \
    >"$OUT/c_build.log" 2>&1 \
    || die "C harness build failed (see $OUT/c_build.log)"

timeout "$TMO" "$RUSTC" -O "$HERE/search.rs" -o "$RS_BIN" \
    >"$OUT/rs_build.log" 2>&1 \
    || die "Rust harness build failed (see $OUT/rs_build.log)"

timeout "$TMO" "$C_BIN" >"$C_OUT" 2>"$C_ERR" \
    || die "C harness run failed/timed out (see $C_ERR)"
timeout "$TMO" "$RS_BIN" >"$RS_OUT" 2>"$RS_ERR" \
    || die "Rust harness run failed/timed out (see $RS_ERR)"

stats=$(awk '
    NR == FNR { c[++nc] = $0; next }
    { r[++nr] = $0 }
    END {
        n = (nc > nr) ? nc : nr
        m = 0; mm = 0
        for (i = 1; i <= n; i++) {
            if (i <= nc && i <= nr && c[i] == r[i]) m++
            else mm++
        }
        printf "%d %d %d", n, m, mm
    }
' "$C_OUT" "$RS_OUT") || die "token comparison failed"

set -- $stats
cases=$1
matched=$2
mismatched=$3

if [ "$mismatched" -ne 0 ]; then
    printf '--- token mismatch (C vs Rust) ---\n' >&2
    diff -u "$C_OUT" "$RS_OUT" >&2 || true
fi

printf 'METHOD_SEARCH %s %s %s %s\n' "$cases" "$matched" "$mismatched" "$blocked"

[ "$mismatched" -eq 0 ] || exit 1
exit 0

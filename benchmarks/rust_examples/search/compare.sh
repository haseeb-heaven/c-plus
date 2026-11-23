#!/bin/sh
# benchmarks/rust_examples/search/compare.sh
#
# Deterministic outcome comparison: C99 harness calling the checked search
# APIs (both archives on the link line) vs safe-Rust examples. No timing, no
# nondeterministic input.
#
# Method coverage is decided by `nm` on the archives. A method with no real
# defined symbol is reported BLOCKED and contributes to <blocked>, never to
# <cases>. qsort_s is BLOCKED: it exists only as a macro in safeclib's
# safe_lib.h (no linkable _qsort_s_chk / qsort_s in either archive).
#
# stdout: informational SYMBOL lines (nm evidence), then exactly one summary:
#   RUST_EXAMPLES search <apis> <cases> <matched> <mismatched> <blocked>
# Exit 0 when all cases match; nonzero on any real token mismatch, build
# failure, run failure or timeout. BLOCKED alone is NOT a failure.
#
# Environment overrides:
#   RUST_EXAMPLES_CLANG, RUST_EXAMPLES_RUSTC, RUST_EXAMPLES_ARCHIVES,
#   RUST_EXAMPLES_TIMEOUT, RUST_EXAMPLES_OUT, RUST_EXAMPLES_NM

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)

CLANG=${RUST_EXAMPLES_CLANG:-/usr/bin/clang}
RUSTC=${RUST_EXAMPLES_RUSTC:-rustc}
NM=${RUST_EXAMPLES_NM:-nm}
HARDENED=${RUST_EXAMPLES_HARDENED:-"$ROOT/build/libc/libcplus_hardened.a"}
SAFECRT=${RUST_EXAMPLES_SAFECRT:-"$ROOT/build/safe/libcplus_safecrt.a"}
ARCHIVES=${RUST_EXAMPLES_ARCHIVES:-"$HARDENED $SAFECRT"}
TMO=${RUST_EXAMPLES_TIMEOUT:-30}
OUT=${RUST_EXAMPLES_OUT:-"$HERE/out"}

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

# True iff any candidate spelling is a DEFINED global symbol in the archives.
# Apple nm prints "000... T _symbol": strip the optional leading underscore.
has_symbol() {
    syms=$1
    # $ARCHIVES is intentionally unquoted: it is a list of archive paths.
    $NM -g --defined-only $ARCHIVES 2>/dev/null | awk -v syms="$syms" '
        BEGIN { n = split(syms, a, ",") }
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
command -v "$NM" >/dev/null 2>&1 || die "nm not found: $NM"
command -v timeout >/dev/null 2>&1 || die "timeout utility not found"

# api|defined-symbol candidates (first is the one the harness calls)
API_SPECS='
bsearch_s|cplus_sc_upstream_bsearch_s
memrchr_s|cplus_sc_upstream_memrchr_s
memchr_s|cplus_v_memchr_s
strchr_s|cplus_v__strchr_s_chk
strrchr_s|cplus_v__strrchr_s_chk
strstr_s|cplus_v__strstr_s_chk
qsort_s|cplus_sc_upstream_qsort_s,cplus_v_qsort_s,cplus_qsort_s,qsort_s,_qsort_s_chk
'

apis=0
blocked=0
for spec in $API_SPECS; do
    api=${spec%%|*}
    candidates=${spec#*|}
    primary=${candidates%%,*}
    apis=$((apis + 1))
    if has_symbol "$candidates"; then
        printf 'SYMBOL %s PRESENT %s (nm -g --defined-only on: %s)\n' \
            "$api" "$primary" "$ARCHIVES"
    else
        printf 'SYMBOL %s BLOCKED no defined symbol for %s (nm -g --defined-only on: %s)\n' \
            "$api" "$candidates" "$ARCHIVES"
        blocked=$((blocked + 1))
    fi
done

# qsort_s nm evidence: only a macro, no symbol. Show that explicitly.
QSORT_MACRO="$ROOT/third_party/safeclib/include/safe_lib.h"
if [ -f "$QSORT_MACRO" ]; then
    printf 'EVIDENCE qsort_s only macro, no symbol: %s\n' \
        "$(grep -n 'define qsort_s' "$QSORT_MACRO" | head -1)"
else
    printf 'EVIDENCE qsort_s: no defined symbol found in archives\n'
fi

# Rebuild from scratch so stale artifacts cannot mask a failure.
rm -f "$C_BIN" "$RS_BIN" "$C_OUT" "$RS_OUT" "$C_ERR" "$RS_ERR" \
      "$OUT/c_build.log" "$OUT/rs_build.log"

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
    printf '%s\n' '--- token mismatch (C vs Rust) ---' >&2
    diff -u "$C_OUT" "$RS_OUT" >&2 || true
fi

printf 'RUST_EXAMPLES search %s %s %s %s %s\n' \
    "$apis" "$cases" "$matched" "$mismatched" "$blocked"

[ "$mismatched" -eq 0 ] || exit 1
exit 0

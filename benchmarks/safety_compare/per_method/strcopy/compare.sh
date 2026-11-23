#!/bin/bash
# Per-method safety-token comparison: C+ checked string-copy APIs vs safe Rust.
#
# Runs one method at a time in both harnesses (deterministic C99 / safe Rust),
# diffs `<method> <case> <token>` records per method/case, and prints:
#   METHOD_STRCOPY <cases> <matched> <mismatched>
#   METHOD_STRCOPY_BLOCKED <blocked>
# plus a per-method line for each of the six methods. Exits nonzero on a real
# mismatch (different tokens, a missing record, or a compile/run failure).
# BLOCKED records are counted separately and never counted as passes.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../../.." && pwd)
BIN="$HERE/bin"
OUT="$HERE/results"
C_BIN="$BIN/strcopy_harness"
R_BIN="$BIN/strcopy_rust"
C_BUILD_LOG="$OUT/c_build.log"
R_BUILD_LOG="$OUT/rs_build.log"
DETAILS="$OUT/mismatch_details.txt"

mkdir -p "$BIN" "$OUT"
: > "$DETAILS"

CC=/usr/bin/clang
CFLAGS="-std=c99 -O2 -I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include"
SAFECRT="$ROOT/build/safe/libcplus_safecrt.a"
HARDENED="$ROOT/build/libc/libcplus_hardened.a"

echo "# C build (timeout 30): $CC $CFLAGS $HERE/strcopy_harness.c $SAFECRT $HARDENED -lm -pthread" > "$C_BUILD_LOG"
if ! timeout 30 $CC $CFLAGS "$HERE/strcopy_harness.c" \
        "$SAFECRT" "$HARDENED" -lm -pthread \
        -o "$C_BIN" >> "$C_BUILD_LOG" 2>&1; then
    echo "compare.sh: C harness build FAILED (see $C_BUILD_LOG)" >&2
    exit 2
fi

echo "# Rust build (timeout 30): rustc -O $HERE/strcopy.rs -o $R_BIN" > "$R_BUILD_LOG"
if ! timeout 30 rustc -O "$HERE/strcopy.rs" -o "$R_BIN" >> "$R_BUILD_LOG" 2>&1; then
    echo "compare.sh: Rust harness build FAILED (see $R_BUILD_LOG)" >&2
    exit 2
fi

METHODS="strcpy_s strncpy_s strcat_s strncat_s stpcpy_s stpncpy_s"
TOTAL_N=0
TOTAL_OK=0
TOTAL_BAD=0
TOTAL_BLK=0
TOTAL_MISSING=0

for m in $METHODS; do
    c_out="$OUT/${m}.c.out"
    r_out="$OUT/${m}.rs.out"
    c_err="$OUT/${m}.c.err"
    r_err="$OUT/${m}.rs.err"
    counts="$OUT/${m}.counts"

    if ! timeout 30 "$C_BIN" "$m" > "$c_out" 2> "$c_err"; then
        echo "compare.sh: C harness failed/timed out for $m (see $c_err)" >&2
        exit 3
    fi
    if ! timeout 30 "$R_BIN" "$m" > "$r_out" 2> "$r_err"; then
        echo "compare.sh: Rust harness failed/timed out for $m (see $r_err)" >&2
        exit 3
    fi

    # Compare the two record sets by (method, case). Counts are written to a
    # separate file so the sorted detail lines stay independent of them.
    awk -v CF="$counts" '
        FNR == NR { c[$1 " " $2] = $3; next }
        { r[$1 " " $2] = $3 }
        END {
            n = 0; ok = 0; bad = 0; blk = 0; missing = 0;
            for (k in c) {
                if (!(k in r)) { print "MISSING_RUST " k; missing++; continue }
                n++;
                if (c[k] == "BLOCKED" || r[k] == "BLOCKED") {
                    if (c[k] == r[k]) blk++;
                    else { bad++; print "MISMATCH " k " " c[k] " " r[k] }
                } else if (c[k] == r[k]) {
                    ok++;
                } else {
                    bad++; print "MISMATCH " k " " c[k] " " r[k]
                }
            }
            for (k in r) if (!(k in c)) { print "MISSING_C " k; missing++ }
            printf "%d %d %d %d %d\n", n, ok, bad, blk, missing > CF
        }
    ' "$c_out" "$r_out" | sort >> "$DETAILS"

    read -r n ok bad blk missing < "$counts"

    uc=$(echo "$m" | tr '[:lower:]' '[:upper:]')
    if [ "$blk" -gt 0 ]; then
        printf 'METHOD_STRCOPY_%s %d %d %d BLOCKED=%d\n' "$uc" "$n" "$ok" "$bad" "$blk"
    else
        printf 'METHOD_STRCOPY_%s %d %d %d\n' "$uc" "$n" "$ok" "$bad"
    fi

    TOTAL_N=$((TOTAL_N + n))
    TOTAL_OK=$((TOTAL_OK + ok))
    TOTAL_BAD=$((TOTAL_BAD + bad))
    TOTAL_BLK=$((TOTAL_BLK + blk))
    TOTAL_MISSING=$((TOTAL_MISSING + missing))
done

# Surface all real mismatches / missing records before the summary lines.
if [ -s "$DETAILS" ]; then
    echo "# per-record differences (sorted):"
    cat "$DETAILS"
fi

echo "METHOD_STRCOPY $TOTAL_N $TOTAL_OK $TOTAL_BAD"
echo "METHOD_STRCOPY_BLOCKED $TOTAL_BLK"

if [ "$TOTAL_BAD" -gt 0 ] || [ "$TOTAL_MISSING" -gt 0 ]; then
    exit 1
fi
exit 0

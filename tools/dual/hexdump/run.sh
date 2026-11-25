#!/bin/sh
# Dual hexdump proof driver. Builds the pure-C tool under every C standard
# spelling the installed clang accepts, builds the modern safe-C+ tool,
# compares stdout+stderr+exit on deterministic valid fixtures, exercises the
# hostile cases, then hands off to the safe-Rust comparison which prints the
# single final DUAL line. All artifacts stay under this directory's out/.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
OUT="$HERE/out"
STDS=${DUAL_STDS:-"c89 c90 c94 c99 c11 c17 c2x"}
CLANG=${CLANG:-/usr/bin/clang}
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
COMPARE="$ROOT/benchmarks/example_rust_compare/dual/compare.sh"

mkdir -p "$OUT"

# ---- 1. Deterministic binary fixtures (generated, never checked in) --------
python3 - "$OUT" <<'PY'
import os, sys
out = sys.argv[1]
def w(name, data):
    with open(os.path.join(out, name), 'wb') as f:
        f.write(data)
w('fixture_valid_small.bin', b'Hello, world!\n' + bytes([0, 1, 2, 127, 128, 255, 65]))
w('fixture_valid_boundary.bin', bytes(range(256)))          # exactly HD_CAP
w('fixture_oversized.bin', bytes(range(256)) + b'X')        # HD_CAP + 1
PY

# ---- 2. Pure C: compile matrix over every requested std spelling -----------
STDS_OK=0
MATCHED=0
REFUSED=0
FAILED=0
: >"$OUT/compile_matrix.log"
printf '%-6s %-4s %s\n' STD RESULT DETAIL
for std in $STDS; do
    "$CLANG" -std="$std" -pedantic-errors -Wall -Wextra -O2 \
        -o "$OUT/hexdump_c_$std" "$HERE/hexdump.c" \
        >"$OUT/compile_$std.log" 2>&1
    crc=$?
    if [ "$crc" -eq 0 ]; then
        STDS_OK=$((STDS_OK + 1))
        printf '%-6s %-4s rc=0\n' "$std" OK
        printf '%s OK rc=0\n' "$std" >>"$OUT/compile_matrix.log"
    else
        diag=$(sed -n '1p' "$OUT/compile_$std.log")
        printf '%-6s %-4s rc=%s diag=%s\n' "$std" FAIL "$crc" "$diag"
        printf '%s FAIL rc=%s diag=%s\n' "$std" "$crc" "$diag" >>"$OUT/compile_matrix.log"
    fi
done

# ---- 3. Modern safe C+ binary ---------------------------------------------
(
    cd "$ROOT" || exit 1
    "$CPLUS" build "$HERE/hexdump.cp" --strict --sanitize=none \
        -I. -Istdlib/safe -Ilibc/install-include \
        -o "$OUT/hexdump_cp" -- \
        build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
        -lm -pthread
) >"$OUT/cp_build.log" 2>&1
cp_build_rc=$?
printf 'CP_BUILD %s rc=%s\n' "$( [ "$cp_build_rc" -eq 0 ] && echo OK || echo FAIL )" "$cp_build_rc"
if [ "$cp_build_rc" -ne 0 ]; then
    printf 'DUAL hexdump %s %s %s %s\n' "$STDS_OK" "$MATCHED" "$REFUSED" "$((FAILED + 1))"
    exit 0
fi

# ---- 4. Valid input: byte-identical stdout + stderr + exit -----------------
: >"$OUT/valid_matrix.log"
for fixture in valid_small valid_boundary; do
    "$OUT/hexdump_cp" "$OUT/fixture_$fixture.bin" \
        >"$OUT/cp_${fixture}.stdout" 2>"$OUT/cp_${fixture}.stderr"
    cp_rc=$?
    for std in $STDS; do
        [ -x "$OUT/hexdump_c_$std" ] || continue
        "$OUT/hexdump_c_$std" "$OUT/fixture_$fixture.bin" \
            >"$OUT/c_${std}_${fixture}.stdout" 2>"$OUT/c_${std}_${fixture}.stderr"
        c_rc=$?
        if cmp -s "$OUT/c_${std}_${fixture}.stdout" "$OUT/cp_${fixture}.stdout" &&
           cmp -s "$OUT/c_${std}_${fixture}.stderr" "$OUT/cp_${fixture}.stderr" &&
           [ "$c_rc" -eq "$cp_rc" ]; then
            MATCHED=$((MATCHED + 1))
            result=MATCH
        else
            FAILED=$((FAILED + 1))
            result=DIFFER
        fi
        printf '%-6s %-15s %s rc=%s/%s\n' "$std" "$fixture" "$result" "$c_rc" "$cp_rc"
        printf '%s %s %s rc=%s/%s\n' "$std" "$fixture" "$result" "$c_rc" "$cp_rc" >>"$OUT/valid_matrix.log"
    done
done

# ---- 5. Hostile input: C+ refuses; pure C has no such check ----------------
: >"$OUT/hostile_matrix.log"
# null / no argument (stdin supplied from /dev/null for both)
"$OUT/hexdump_cp" </dev/null >"$OUT/cp_null.stdout" 2>"$OUT/cp_null.stderr"
cp_null_rc=$?
"$OUT/hexdump_c_c89" </dev/null >"$OUT/c_null.stdout" 2>"$OUT/c_null.stderr"
c_null_rc=$?
# missing path
"$OUT/hexdump_cp" "$OUT/no_such_file.bin" >"$OUT/cp_missing.stdout" 2>"$OUT/cp_missing.stderr"
cp_missing_rc=$?
"$OUT/hexdump_c_c89" "$OUT/no_such_file.bin" </dev/null >"$OUT/c_missing.stdout" 2>"$OUT/c_missing.stderr"
c_missing_rc=$?
# oversized input
"$OUT/hexdump_cp" "$OUT/fixture_oversized.bin" >"$OUT/cp_oversized.stdout" 2>"$OUT/cp_oversized.stderr"
cp_oversized_rc=$?
"$OUT/hexdump_c_c89" "$OUT/fixture_oversized.bin" >"$OUT/c_oversized.stdout" 2>"$OUT/c_oversized.stderr"
c_oversized_rc=$?
for label in null missing oversized; do
    eval "crc=\$cp_${label}_rc"
    eval "ccrc=\$c_${label}_rc"
    cpline=$(sed -n '1p' "$OUT/cp_${label}.stdout")
    ccline=$(sed -n '1p' "$OUT/c_${label}.stdout")
    if [ "$crc" -ne 0 ]; then
        REFUSED=$((REFUSED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
    printf '%-10s CP rc=%s "%s" | PURE-C rc=%s "%s"\n' \
        "$label" "$crc" "$cpline" "$ccrc" "$ccline"
    printf '%s CP rc=%s %s | PURE-C rc=%s\n' "$label" "$crc" "$cpline" "$ccrc" >>"$OUT/hostile_matrix.log"
done

# ---- 6. Hand counts to the Rust comparator (prints the final DUAL line) ----
{
    printf 'STDS_OK=%s\n' "$STDS_OK"
    printf 'MATCHED=%s\n' "$MATCHED"
    printf 'REFUSED=%s\n' "$REFUSED"
    printf 'FAILED=%s\n' "$FAILED"
} >"$OUT/c_summary.env"

sh "$COMPARE" "$OUT"

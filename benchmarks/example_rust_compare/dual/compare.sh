#!/bin/sh
# Rust half of the dual-hexdump proof.
#
# Reads the C/C+ counts written by tools/dual/hexdump/run.sh into
#   <OUT>/c_summary.env  (STDS_OK, MATCHED, REFUSED, FAILED)
# then builds the safe-Rust model, verifies it against the already-built C+
# binary on the same deterministic fixtures, and prints exactly one line:
#   DUAL hexdump <stds> <matched> <refused> <failed>
#
# Usage: compare.sh [OUT_DIR]   (default: tools/dual/hexdump/out)
# Override the Rust compiler with RUSTC and the C+ binary with HD_CP.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
OUT=${1:-"$ROOT/tools/dual/hexdump/out"}
RUSTC=${RUSTC:-rustc}
HD_CP=${HD_CP:-"$OUT/hexdump_cp"}

STDS_OK=0; MATCHED=0; REFUSED=0; FAILED=0
if [ -f "$OUT/c_summary.env" ]; then
    . "$OUT/c_summary.env"
fi

mkdir -p "$OUT"
"$RUSTC" -O "$HERE/hexdump.rs" -o "$OUT/hexdump_rust" \
    >"$OUT/rust.build.log" 2>&1
if [ $? -ne 0 ]; then
    FAILED=$((FAILED + 1))
    printf 'DUAL hexdump %s %s %s %s\n' "$STDS_OK" "$MATCHED" "$REFUSED" "$FAILED"
    exit 0
fi

# Valid input: Rust stdout+exit must match the C+ binary byte for byte.
for fixture in valid_small valid_boundary; do
    "$HD_CP" "$OUT/fixture_$fixture.bin" >"$OUT/cp_${fixture}.stdout" 2>/dev/null
    cp_rc=$?
    "$OUT/hexdump_rust" "$OUT/fixture_$fixture.bin" >"$OUT/rust_${fixture}.stdout" 2>/dev/null
    rust_rc=$?
    if cmp -s "$OUT/cp_${fixture}.stdout" "$OUT/rust_${fixture}.stdout" &&
       [ "$cp_rc" -eq "$rust_rc" ]; then
        MATCHED=$((MATCHED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
done

# Hostile input: Rust must refuse with the same exit codes as C+.
check_refusal() {
    label=$1; shift
    "$OUT/hexdump_rust" "$@" >"$OUT/rust_${label}.stdout" 2>/dev/null
    rrc=$?
    "$HD_CP" "$@" >"$OUT/cp_${label}.stdout" 2>/dev/null
    crc=$?
    if cmp -s "$OUT/rust_${label}.stdout" "$OUT/cp_${label}.stdout" &&
       [ "$rrc" -eq "$crc" ] && [ "$rrc" -ne 0 ]; then
        REFUSED=$((REFUSED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
}

check_refusal null
check_refusal missing "$OUT/no_such_file.bin"
check_refusal oversized "$OUT/fixture_oversized.bin"

printf 'DUAL hexdump %s %s %s %s\n' "$STDS_OK" "$MATCHED" "$REFUSED" "$FAILED"

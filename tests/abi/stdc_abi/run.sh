#!/bin/sh
# run.sh -- prove the ordinary C runtime ABI is unchanged by the safety layer.
#
# Compiles ONE ordinary C source (original_c.c) three ways with
# -fno-builtin -O0, then uses nm -gU to compare the exported symbol sets:
#   1. PLAIN    : plain /usr/bin/clang
#   2. SAFE     : + -include stdlib/safe/safe_c.h + both archives
#   3. DISABLED : + -include stdlib/safe/safe_c.h -DCPLUS_SAFE_C_DISABLE
#
# Checks performed:
#   * ordinary C function/data symbols are byte-identical in all three builds
#     (and equal EXPECTED_SYMBOLS.txt) -- no C++ mangling introduced;
#   * sizeof(void*)=8 and abi_sample layout/offsets are compile-time asserted
#     in every build and verified again from each program's stdout;
#   * the SAFE build adds ONLY internal cplus_* helper symbols and removes
#     no ordinary symbol -- i.e. caller-side macro remapping, not interposition.
#
# Every compiler/link/program step is wrapped in `timeout 60`.
# Usage: sh tests/abi/stdc_abi/run.sh

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
cd "$ROOT" || exit 2

CLANG=${CPLUS_STDABI_CLANG:-/usr/bin/clang}
NM=${CPLUS_STDABI_NM:-/usr/bin/nm}
OUT="$SCRIPT_DIR/out"
mkdir -p "$OUT" || exit 2

SRC="tests/abi/stdc_abi/original_c.c"
EXPECTED="$SCRIPT_DIR/EXPECTED_SYMBOLS.txt"
ARCHIVES="build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a"
INC="-I. -Ilibc/install-include -Ilibc/include -Ilibc -Istdlib/safe -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
BASE="-fno-builtin -O0 -std=c11 -Wall"

FAILED=0
pass() { printf 'PASS %s\n' "$*"; }
fail() { printf 'FAIL %s\n' "$*"; FAILED=$((FAILED + 1)); }

printf 'stdc_abi: root=%s clang=%s nm=%s\n' "$ROOT" "$CLANG" "$NM"
printf 'stdc_abi: bash=%s\n' "${BASH_VERSION:-sh}"

# ---------------------------------------------------------------------------
# 1. Compile the one source three ways.
# ---------------------------------------------------------------------------
printf 'CMD  %s -fno-builtin -O0 -std=c11 -Wall %s -lm -pthread -o %s/plain\n' "$CLANG" "$SRC" "$OUT"
# shellcheck disable=SC2086
timeout 60 $CLANG $BASE "$SRC" -lm -pthread -o "$OUT/plain" \
    >"$OUT/plain.compile.log" 2>&1 || { printf 'FAIL build_plain\n'; cat "$OUT/plain.compile.log"; exit 2; }
pass build_plain

printf 'CMD  %s -fno-builtin -O0 -std=c11 -Wall %s -include stdlib/safe/safe_c.h %s %s -lm -pthread -o %s/safe\n' "$CLANG" "$INC" "$SRC" "$ARCHIVES" "$OUT"
# shellcheck disable=SC2086
timeout 60 $CLANG $BASE $INC -include stdlib/safe/safe_c.h "$SRC" $ARCHIVES -lm -pthread -o "$OUT/safe" \
    >"$OUT/safe.compile.log" 2>&1 || { printf 'FAIL build_safe\n'; cat "$OUT/safe.compile.log"; exit 2; }
pass build_safe

printf 'CMD  %s -fno-builtin -O0 -std=c11 -Wall %s -include stdlib/safe/safe_c.h -DCPLUS_SAFE_C_DISABLE %s %s -lm -pthread -o %s/disabled\n' "$CLANG" "$INC" "$SRC" "$ARCHIVES" "$OUT"
# shellcheck disable=SC2086
timeout 60 $CLANG $BASE $INC -include stdlib/safe/safe_c.h -DCPLUS_SAFE_C_DISABLE "$SRC" $ARCHIVES -lm -pthread -o "$OUT/disabled" \
    >"$OUT/disabled.compile.log" 2>&1 || { printf 'FAIL build_disabled\n'; cat "$OUT/disabled.compile.log"; exit 2; }
pass build_disabled

# ---------------------------------------------------------------------------
# 2. Symbol extraction helpers.
# ---------------------------------------------------------------------------
# Full defined-global symbol set, Mach-O leading underscore stripped so the
# comparison is about the C identifiers, not the Mach-O underscore convention.
all_syms() {
    $NM -gU "$1" 2>/dev/null \
      | awk 'NF >= 3 && $2 ~ /^[TDBR]$/ {print $3}' \
      | sed 's/^_//' \
      | sort -u
}
# Only the ordinary C API defined by original_c.c (abi_* plus main).
ord_syms() {
    all_syms "$1" | grep -E '^(abi_[A-Za-z0-9_]*|main)$' | sort -u
}

for b in plain safe disabled; do
    all_syms "$OUT/$b" > "$OUT/$b.all"
    ord_syms  "$OUT/$b" > "$OUT/$b.ord"
    printf 'INFO %s: %s defined-global symbols, %s ordinary C symbols\n' \
        "$b" "$(wc -l < "$OUT/$b.all" | tr -d ' ')" \
             "$(wc -l < "$OUT/$b.ord" | tr -d ' ')"
done

# EXPECTED_SYMBOLS.txt is already sorted.
sort -u "$EXPECTED" > "$OUT/expected.sorted"

# ---------------------------------------------------------------------------
# 3. Ordinary C names/signatures identical (no mangling).
# ---------------------------------------------------------------------------
for b in plain safe disabled; do
    if diff -u "$OUT/expected.sorted" "$OUT/$b.ord" > "$OUT/$b.ord.diff" 2>&1; then
        pass "T1_ordinary_symbols:$b"
    else
        fail "T1_ordinary_symbols:$b"
        cat "$OUT/$b.ord.diff"
    fi
done

for b in plain safe disabled; do
    if $NM -gU "$OUT/$b" 2>/dev/null | grep -E '_Z' > "$OUT/$b.mangled" 2>&1; then
        fail "T2_no_cxx_mangling:$b"
        cat "$OUT/$b.mangled"
    else
        pass "T2_no_cxx_mangling:$b"
    fi
done

# ---------------------------------------------------------------------------
# 4. SAFE build: only internal helpers added, none of the ordinary API lost.
# ---------------------------------------------------------------------------
comm -13 "$OUT/plain.all" "$OUT/safe.all" > "$OUT/safe.added"
comm -23 "$OUT/plain.all" "$OUT/safe.all" > "$OUT/safe.removed"

printf 'INFO safe-only added symbols (%s):\n' "$(wc -l < "$OUT/safe.added" | tr -d ' ')"
sed 's/^/  + /' "$OUT/safe.added"

if [ -s "$OUT/safe.removed" ]; then
    fail T3_no_ordinary_symbol_removed
    sed 's/^/  - /' "$OUT/safe.removed"
else
    pass T3_no_ordinary_symbol_removed
fi

# The added set must be entirely accounted for by the two shipped archives:
# these are internal implementation helpers pulled in by the remapped calls,
# never a new or redefined ordinary C API symbol.
# shellcheck disable=SC2086
$NM -gU $ARCHIVES 2>/dev/null \
  | awk 'NF >= 3 && $2 ~ /^[TDBR]$/ {print $3}' \
  | sed 's/^_//' \
  | sort -u > "$OUT/archive.all"
printf 'INFO archives define %s global symbols\n' \
    "$(wc -l < "$OUT/archive.all" | tr -d ' ')"

comm -13 "$OUT/archive.all" "$OUT/safe.added" > "$OUT/safe.added.external" || true
if [ -s "$OUT/safe.added.external" ]; then
    fail T4_added_symbols_are_internal_helpers
    sed 's/^/  unexpected /' "$OUT/safe.added.external"
else
    pass T4_added_symbols_are_internal_helpers
fi

if grep -E '^(abi_[A-Za-z0-9_]*|main)$' "$OUT/safe.added" > "$OUT/safe.added.ordinary" 2>&1; then
    fail T4b_no_ordinary_symbol_added
    cat "$OUT/safe.added.ordinary"
else
    pass T4b_no_ordinary_symbol_added
fi

# ---------------------------------------------------------------------------
# 5. Runtime layout values identical in all three builds.
# ---------------------------------------------------------------------------
for b in plain safe disabled; do
    printf 'CMD  timeout 60 %s/%s\n' "$OUT" "$b"
    timeout 60 "$OUT/$b" > "$OUT/$b.run.out" 2> "$OUT/$b.run.err" || {
        fail "T5_run:$b"; exit 2;
    }
    pass "T5_run:$b"
done
printf 'INFO program stdout (plain build):\n'
sed 's/^/  | /' "$OUT/plain.run.out"

for b in safe disabled; do
    if diff -u "$OUT/plain.run.out" "$OUT/$b.run.out" > "$OUT/$b.run.diff" 2>&1; then
        pass "T6_runtime_layout_identical:$b"
    else
        fail "T6_runtime_layout_identical:$b"
        cat "$OUT/$b.run.diff"
    fi
done

if grep -q '^sizeof(void\*)=8$' "$OUT/plain.run.out"; then
    pass T7_pointer_is_8_bytes
else
    fail T7_pointer_is_8_bytes
fi
if grep -q '^offsets tag=0 count=4 value=8 ptr=16 name=24 big=32$' "$OUT/plain.run.out"; then
    pass T8_abi_sample_offsets
else
    fail T8_abi_sample_offsets
fi

# ---------------------------------------------------------------------------
# 6. Byte-for-byte machine code for functions whose bodies contain NO
#    macro-remapped library call (direct AAPCS64 calling-convention evidence).
# ---------------------------------------------------------------------------
OBJDUMP=${CPLUS_STDABI_OBJDUMP:-/usr/bin/objdump}
if [ -x "$OBJDUMP" ]; then
    for f in _abi_add _abi_sum _abi_fill; do
        for b in plain safe disabled; do
            # shellcheck disable=SC2086
            "$OBJDUMP" -d --disassemble-symbols=$f "$OUT/$b" 2>/dev/null \
              | awk '/^[[:space:]]*[0-9a-f]+:/ {printf "%s ", $2}' \
              > "$OUT/$b.$f.bytes"
        done
        if diff -u "$OUT/plain.$f.bytes" "$OUT/safe.$f.bytes" > "$OUT/$f.safe.diff" 2>&1 \
           && diff -u "$OUT/plain.$f.bytes" "$OUT/disabled.$f.bytes" > "$OUT/$f.disabled.diff" 2>&1; then
            pass "T9_machine_code_byte_identical:$f"
        else
            fail "T9_machine_code_byte_identical:$f"
            cat "$OUT/$f.safe.diff" "$OUT/$f.disabled.diff"
        fi
    done
else
    printf 'INFO objdump unavailable at %s; skipping machine-code check\n' "$OBJDUMP"
fi

printf 'stdc_abi: %s failed\n' "$FAILED"
if [ "$FAILED" -eq 0 ]; then
    printf 'stdc_abi: RESULT PASS -- ordinary C ABI unchanged by the safety layer\n'
    exit 0
fi
printf 'stdc_abi: RESULT FAIL\n'
exit 1

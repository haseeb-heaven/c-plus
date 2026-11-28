#!/bin/sh
# prove original legacy C source compiles unchanged with and without the
# checked layer, and produces byte-identical stdout + exit 0.
#
# Run from anywhere:  sh tests/abi/c_source_compat/run.sh
# All generated artifacts stay under tests/abi/c_source_compat/out/.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

SRC=tests/abi/c_source_compat
OUT=$SRC/out
mkdir -p "$OUT"

CC=/usr/bin/clang
INC="-I. -Istdlib/safe -Ilibc -Ilibc/install-include -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
SAFE=build/safe/libcplus_safecrt.a
LIBC=build/libc/libcplus_hardened.a
LIBS="$SAFE $LIBC -lm -pthread"

RESULTS=$OUT/RESULTS.txt
: > "$RESULTS"

# emit to both the results file and the console
both() { printf '%s\n' "$1" >> "$RESULTS"; printf '%s\n' "$1"; }
note() { printf '%s\n' "$1" >> "$RESULTS"; }
dump() { note "  --- $1 ---"; cat "$2" >> "$RESULTS" 2>/dev/null; }
sha()  { shasum -a 256 "$1" | awk '{print $1}'; }

PASS=0
FAIL=0

compile_plain() { # name
    n=$1
    cmd="$CC -std=c99 $SRC/$n.c -o $OUT/$n.plain"
    both "+ $cmd"
    if $CC -std=c99 "$SRC/$n.c" -o "$OUT/$n.plain" \
        >"$OUT/$n.plain.build.log" 2>&1; then
        note "  compile rc=0"
    else
        note "  compile rc=$? BUILD FAILED"
        return 1
    fi
}

compile_checked() { # name
    n=$1
    cmd="$CC -std=c99 -include stdlib/safe/safe_c.h $INC $SRC/$n.c $LIBS -o $OUT/$n.checked"
    both "+ $cmd"
    if $CC -std=c99 -include stdlib/safe/safe_c.h $INC "$SRC/$n.c" $LIBS \
        -o "$OUT/$n.checked" >"$OUT/$n.checked.build.log" 2>&1; then
        note "  compile rc=0"
    else
        note "  compile rc=$? BUILD FAILED"
        return 1
    fi
}

compile_disabled() { # name
    n=$1
    cmd="$CC -std=c99 -include stdlib/safe/safe_c.h -DCPLUS_SAFE_C_DISABLE $INC $SRC/$n.c $LIBS -o $OUT/$n.disabled"
    both "+ $cmd"
    if $CC -std=c99 -include stdlib/safe/safe_c.h -DCPLUS_SAFE_C_DISABLE $INC \
        "$SRC/$n.c" $LIBS -o "$OUT/$n.disabled" \
        >"$OUT/$n.disabled.build.log" 2>&1; then
        note "  compile rc=0"
    else
        note "  compile rc=$? BUILD FAILED"
        return 1
    fi
}

run_bin() { # binary tag
    bin=$1; t=$2
    "$bin" >"$OUT/$t.stdout" 2>"$OUT/$t.stderr"
    rc=$?
    note "  + $bin > $OUT/$t.stdout 2> $OUT/$t.stderr => rc=$rc"
    return $rc
}

both "=== c_source_compat verification (toolchain: $($CC --version | head -1)) ==="
both "Repository root: $ROOT"
both "Archives: $SAFE , $LIBC"

# ---- 10 stdout/exit parity sources ----------------------------------------
PARITY="01_record_pack 02_name_builder 03_token_table 04_stream_roundtrip \
05_matrix_copy 06_string_table 07_packet_build 08_inventory 09_config_parse \
10_report"

for n in $PARITY; do
    both ""
    both "== $n (stdout/exit parity: plain vs checked vs declarations-only) =="
    if ! compile_plain "$n" || ! compile_checked "$n" || ! compile_disabled "$n"; then
        note "RESULT: FAIL ($n did not compile in all three modes)"
        FAIL=$((FAIL + 1))
        continue
    fi

    run_bin "$OUT/$n.plain"    "$n.plain";    prc=$?
    run_bin "$OUT/$n.checked"  "$n.checked";  crc=$?
    run_bin "$OUT/$n.disabled" "$n.disabled"; drc=$?

    note "  sha256 stdout plain    = $(sha "$OUT/$n.plain.stdout")"
    note "  sha256 stdout checked  = $(sha "$OUT/$n.checked.stdout")"
    note "  sha256 stdout disabled = $(sha "$OUT/$n.disabled.stdout")"
    dump "$n stdout (checked)" "$OUT/$n.checked.stdout"

    ok=1
    [ "$prc" -eq 0 ] || { note "  plain exit != 0 ($prc)"; ok=0; }
    [ "$crc" -eq 0 ] || { note "  checked exit != 0 ($crc)"; ok=0; }
    [ "$drc" -eq 0 ] || { note "  disabled exit != 0 ($drc)"; ok=0; }
    cmp -s "$OUT/$n.plain.stdout" "$OUT/$n.checked.stdout" \
        || { note "  plain vs checked stdout DIFFERS"; ok=0; }
    cmp -s "$OUT/$n.plain.stdout" "$OUT/$n.disabled.stdout" \
        || { note "  plain vs disabled stdout DIFFERS"; ok=0; }
    for t in plain checked disabled; do
        [ ! -s "$OUT/$n.$t.stderr" ] \
            || { note "  $t stderr not empty (see $OUT/$n.$t.stderr)"; ok=0; }
    done

    if [ "$ok" -eq 1 ]; then
        note "RESULT: PASS ($n: all 3 exit 0, stdout byte-identical, stderr empty)"
        both "PASS $n"
        PASS=$((PASS + 1))
    else
        note "RESULT: FAIL ($n)"
        both "FAIL $n"
        FAIL=$((FAIL + 1))
    fi
done

# ---- deliberate local-buffer overflow: checked refusal in a subprocess ----
both ""
both "== 11_overflow_refusal (deliberate overflow; checked refusal demo) =="
n=11_overflow_refusal
compile_plain "$n"    || { note "RESULT: FAIL ($n plain build)"; FAIL=$((FAIL+1)); }
compile_checked "$n"  || { note "RESULT: FAIL ($n checked build)"; FAIL=$((FAIL+1)); }
compile_disabled "$n" || { note "RESULT: FAIL ($n disabled build)"; FAIL=$((FAIL+1)); }

run_bin "$OUT/$n.plain"    "$n.plain"
prc=$?
run_bin "$OUT/$n.checked"  "$n.checked"
crc=$?
run_bin "$OUT/$n.disabled" "$n.disabled"
drc=$?

dump "$n checked stdout" "$OUT/$n.checked.stdout"
dump "$n checked stderr" "$OUT/$n.checked.stderr"
dump "$n plain stdout"   "$OUT/$n.plain.stdout"
dump "$n plain stderr"   "$OUT/$n.plain.stderr"
dump "$n disabled stdout" "$OUT/$n.disabled.stdout"
dump "$n disabled stderr" "$OUT/$n.disabled.stderr"

EXPECTED='[CPLUS-SAFE] memcpy_s: buffer overflow refused (requested 40 > capacity 8, ESNOSPC 406)'
note "  expected checked stderr line: $EXPECTED"
if [ "$crc" -eq 70 ] && grep -Fxq "$EXPECTED" "$OUT/$n.checked.stderr"; then
    note "RESULT: PASS ($n: checked subprocess refused with rc=70 and exact ESNOSPC 406 diagnostic)"
    note "  plain rc=$prc (silent guard corruption: UB, no refusal)"
    note "  declarations-only rc=$drc (no macros -> same raw overflow as plain)"
    both "PASS $n (checked refusal rc=70; plain rc=$prc)"
    PASS=$((PASS + 1))
else
    note "RESULT: FAIL ($n: expected rc=70 + exact diagnostic; got rc=$crc)"
    dump "$n actual checked stderr" "$OUT/$n.checked.stderr"
    both "FAIL $n (refusal)"
    FAIL=$((FAIL + 1))
fi

both ""
both "=== SUMMARY: PASS=$PASS FAIL=$FAIL ==="
[ "$FAIL" -eq 0 ]

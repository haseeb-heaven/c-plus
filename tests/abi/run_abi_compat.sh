#!/bin/sh
# Comprehensive ABI-compatibility suite: real C compiler + real linker + real
# archives.  No source inspection, no `|| true` masking, bounded runtimes.
#
# Ownership: only writes build/abi/compat/* (generated artifacts).  It does not
# touch tests/abi/abi_layouts.c or tests/abi/run_abi_layouts.sh.
set -u

# Restricted, auditable tool PATH.  The C+ driver is always invoked by absolute
# path via $ROOT/build/c+; its backend resolves /usr/bin/clang from this PATH.
export PATH=/usr/bin:/bin
export CPLUS_CLANG=/usr/bin/clang
CC=/usr/bin/clang

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
OUT=build/abi/compat
mkdir -p "$OUT"
LOG="$OUT/results.log"
: > "$LOG"

PASS=0
FAIL=0
LAST_RC=0

log() { printf '%s\n' "$*" >> "$LOG"; }

log "=== ABI compatibility run ==="
log "date_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "PATH=$PATH"
log "CPLUS_CLANG=$CPLUS_CLANG"
log "arch=$(uname -m) os=$(uname -s)"
log "--- clang --version ---"
"$CC" --version >> "$LOG" 2>&1
log "--- c+ --version ---"
./build/c+ --version >> "$LOG" 2>&1
log "========================"

# step <label> <stdout-file|-> <timeout-seconds> <command...>
step() {
    label=$1
    out=$2
    tmo=$3
    shift 3
    if [ "$out" = "-" ]; then
        log "CMD[$label] timeout=${tmo}s: $*"
        timeout "$tmo" "$@" >> "$LOG" 2>&1
    else
        log "CMD[$label] timeout=${tmo}s: $* > $out"
        timeout "$tmo" "$@" > "$out" 2>> "$LOG"
    fi
    LAST_RC=$?
    log "RC[$label]=$LAST_RC"
    if [ "$LAST_RC" -eq 0 ]; then
        PASS=$((PASS + 1))
        printf 'PASS %s\n' "$label"
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL %s (rc=%s)\n' "$label" "$LAST_RC"
    fi
}

# check_line <label> <file> <fixed-string>: prove the program's own final line.
check_line() {
    label=$1
    file=$2
    needle=$3
    log "CHECK[$label]: grep -F '$needle' $file"
    if grep -Fq "$needle" "$file"; then
        PASS=$((PASS + 1))
        printf 'PASS %s\n' "$label"
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL %s (final summary line missing)\n' "$label"
        log "MISSING in $file: $needle"
    fi
}

# check_symbols <label> <nm-output> <expected-symbol-file>
check_symbols() {
    label=$1
    nmfile=$2
    symfile=$3
    while IFS= read -r sym; do
        [ -n "$sym" ] || continue
        if grep -Eq "^[0-9A-Fa-f]+ T _${sym}$" "$nmfile"; then
            PASS=$((PASS + 1))
            printf 'PASS %s:%s\n' "$label" "$sym"
        else
            FAIL=$((FAIL + 1))
            printf 'FAIL %s:%s (symbol absent/renamed)\n' "$label" "$sym"
            log "MISSING SYMBOL $sym in $nmfile"
        fi
    done < "$symfile"
}

INC="-I$ROOT/libc -I$ROOT/libc/include -I$ROOT/libc/install-include \
-I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include \
-I$ROOT/third_party/intel-safestringlib/include -I$ROOT/tests/abi"

# ---------------------------------------------------------------------------
# Prerequisites: the two real static libraries whose exported names we audit.
# ---------------------------------------------------------------------------
step build_libc - 300 sh tests/libc/build_libc.sh
step build_safecrt - 300 sh scripts/build_safecrt.sh

# ---------------------------------------------------------------------------
# Test 1: pointer sizes and byte-representation equality.
# ---------------------------------------------------------------------------
step T1_pointers_compile - 120 "$CC" -O1 -std=c99 -Wall -Wextra -pedantic \
    tests/abi/abi_compat_pointers.c -o "$OUT/pointers"
step T1_pointers_run "$OUT/pointers.out" 60 "$OUT/pointers"
check_line T1_pointers_summary "$OUT/pointers.out" "pointer ABI checks match"

# ---------------------------------------------------------------------------
# Test 2: captured layout constants for every public C-compatible type.
# ---------------------------------------------------------------------------
step T2_layouts_compile - 180 "$CC" -O1 -std=c99 -Wall -Wextra -pedantic $INC \
    tests/abi/abi_compat_layouts.c build/libc/libcplus_hardened.a \
    build/safe/libcplus_safecrt.a -lm -pthread -o "$OUT/layouts"
step T2_layouts_run "$OUT/layouts.out" 60 "$OUT/layouts"
check_line T2_layouts_summary "$OUT/layouts.out" "layouts match"

# ---------------------------------------------------------------------------
# Tests 3a + 4a: ordinary C calls functions emitted by the C+ driver.
# ---------------------------------------------------------------------------
step T3a_cplus_lib_compile - 180 ./build/c+ build --sanitize=none \
    tests/abi/abi_compat_cplus_lib.cp -c -o "$OUT/abi_compat_cplus_lib.o" \
    -- "-I$ROOT/libc/install-include"
step T3a_cplus_lib_archive - 60 /usr/bin/ar rcs \
    "$OUT/libabi_compat_cplus.a" "$OUT/abi_compat_cplus_lib.o"
step T3a_c_driver_compile - 180 "$CC" -O1 -std=c99 -Wall -Wextra -pedantic $INC \
    tests/abi/abi_compat_c_driver.c "$OUT/libabi_compat_cplus.a" \
    build/libc/libcplus_hardened.a -lm -pthread -o "$OUT/c_driver"
step T3a_c_driver_run "$OUT/c_driver.out" 60 "$OUT/c_driver"
check_line T3a_c_driver_summary "$OUT/c_driver.out" "C->C+ boundary checks match"

# ---------------------------------------------------------------------------
# Tests 3b + 4b: C+ links and calls a precompiled C archive from /usr/bin/clang.
# The driver forwards -- args to both the backend compile and the link step;
# -Wno-unused-command-line-argument silences the benign "linker input unused"
# warning emitted by the compile step (the link step uses the .a correctly).
# ---------------------------------------------------------------------------
step T3b_c_part_compile - 120 "$CC" -O2 -std=c99 -Wall -Wextra -pedantic \
    -I"$ROOT/tests/abi" -c tests/abi/abi_compat_c_part.c \
    -o "$OUT/abi_compat_c_part.o"
step T3b_c_part_archive - 60 /usr/bin/ar rcs \
    "$OUT/libabi_compat_cpart.a" "$OUT/abi_compat_c_part.o"
# Preflight probe: prove the driver links the .a DIRECTLY (no manual linker
# workaround).  Without the suppression flag the backend compile step prints a
# benign "linker input unused" warning; the link step still consumes the .a.
# The exact warning is preserved in results.log for the record.
step T3b_direct_link_probe - 180 ./build/c+ build --sanitize=none \
    tests/abi/abi_compat_cplus_caller.cp -o "$OUT/cplus_caller_direct" \
    -- "$OUT/libabi_compat_cpart.a"
step T3b_cplus_caller_build - 180 ./build/c+ build --sanitize=none \
    tests/abi/abi_compat_cplus_caller.cp -o "$OUT/cplus_caller" \
    -- "$OUT/libabi_compat_cpart.a" -Wno-unused-command-line-argument
step T3b_cplus_caller_run "$OUT/cplus_caller.out" 60 "$OUT/cplus_caller"
check_line T3b_cplus_caller_summary "$OUT/cplus_caller.out" "C+->C boundary checks match"

# ---------------------------------------------------------------------------
# Test 5: exported symbol names (demangled, no C++ name mangling).
# ---------------------------------------------------------------------------
step T5_nm_libc - 60 /usr/bin/nm -gU build/libc/libcplus_hardened.a
/usr/bin/nm -gU build/libc/libcplus_hardened.a > "$OUT/nm_libc.txt" 2>> "$LOG"
step T5_nm_safecrt - 60 /usr/bin/nm -gU build/safe/libcplus_safecrt.a
/usr/bin/nm -gU build/safe/libcplus_safecrt.a > "$OUT/nm_safecrt.txt" 2>> "$LOG"
/usr/bin/nm -gU "$OUT/libabi_compat_cplus.a" "$OUT/libabi_compat_cpart.a" \
    > "$OUT/nm_compat.txt" 2>> "$LOG"
check_symbols T5_libc_symbols "$OUT/nm_libc.txt" \
    tests/abi/abi_compat_expected_libc_symbols.txt
check_symbols T5_safecrt_symbols "$OUT/nm_safecrt.txt" \
    tests/abi/abi_compat_expected_safecrt_symbols.txt
check_symbols T5_compat_symbols "$OUT/nm_compat.txt" \
    tests/abi/abi_compat_expected_compat_symbols.txt

for nmfile in "$OUT/nm_libc.txt" "$OUT/nm_safecrt.txt" "$OUT/nm_compat.txt"; do
    label="T5_no_cxx_mangling:$(basename "$nmfile")"
    log "CHECK[$label]: no ' T __Z' in $nmfile"
    if grep -Eq ' T __Z' "$nmfile"; then
        FAIL=$((FAIL + 1))
        printf 'FAIL %s\n' "$label"
    else
        PASS=$((PASS + 1))
        printf 'PASS %s\n' "$label"
    fi
done

# ---------------------------------------------------------------------------
# Test 6: metadata/public headers do not perturb ordinary C layouts.
# ---------------------------------------------------------------------------
step T6_neutral_plain_compile - 120 "$CC" -O1 -std=gnu11 -Wall -Wextra \
    tests/abi/abi_compat_header_neutrality.c -o "$OUT/neutral_plain"
step T6_neutral_cplus_compile - 120 "$CC" -O1 -std=gnu11 -Wall -Wextra \
    -include cplus_libc.h -include cplus_libc_safe.h \
    -I"$ROOT/libc/install-include" \
    tests/abi/abi_compat_header_neutrality.c -o "$OUT/neutral_cplus"
step T6_neutral_plain_run "$OUT/neutral_plain.out" 60 "$OUT/neutral_plain"
step T6_neutral_cplus_run "$OUT/neutral_cplus.out" 60 "$OUT/neutral_cplus"
step T6_neutral_diff - 60 /usr/bin/diff -u \
    "$OUT/neutral_plain.out" "$OUT/neutral_cplus.out"
check_line T6_neutral_line_count "$OUT/neutral_plain.out" "LAYOUT void *"

# ---------------------------------------------------------------------------
printf 'abi_compat: %d passed, %d failed\n' "$PASS" "$FAIL"
log "RESULT: abi_compat: $PASS passed, $FAIL failed"
log "Full command/exit-code log: $LOG"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
exit 0

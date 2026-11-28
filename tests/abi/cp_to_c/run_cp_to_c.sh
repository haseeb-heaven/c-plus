#!/bin/sh
# C+ -> C interoperability lab: a real C+ source class-lowered by ./build/c+,
# archived with /usr/bin/ar, linked into an ordinary /usr/bin/clang C program.
# Records the exact commands, nm output and exit codes in artifacts/results.log.
#
# File ownership: writes ONLY tests/abi/cp_to_c/artifacts/* (generated files).
# It requires the repo's prebuilt build/libc/libcplus_hardened.a; it does NOT
# rebuild or modify the compiler, runtime, stdlib or any file outside this dir.
set -u

export PATH=/usr/bin:/bin
export CPLUS_CLANG=/usr/bin/clang
CC=/usr/bin/clang
NM=/usr/bin/nm
AR=/usr/bin/ar

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT"
OUT=tests/abi/cp_to_c/artifacts
mkdir -p "$OUT"
LOG="$OUT/results.log"
: > "$LOG"

PASS=0
FAIL=0
LAST_RC=0

log() { printf '%s\n' "$*" >> "$LOG"; }

log "=== C+ -> C interoperability lab ==="
log "date_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "PATH=$PATH"
log "CPLUS_CLANG=$CPLUS_CLANG"
log "arch=$(uname -m) os=$(uname -s)"
log "ROOT=$ROOT"
log "--- clang --version ---"
"$CC" --version >> "$LOG" 2>&1
log "--- c+ --version ---"
./build/c+ --version >> "$LOG" 2>&1
log "================================"

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

# check_line <label> <file> <fixed-string>
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
        printf 'FAIL %s (summary line missing)\n' "$label"
        log "MISSING in $file: $needle"
    fi
}

# check_symbols <label> <nm-output> <expected-file>
check_symbols() {
    label=$1
    nmfile=$2
    symfile=$3
    while IFS= read -r sym; do
        case "$sym" in
            ''|'#'*) continue ;;
        esac
        log "CHECK[$label:$sym]: grep -Eq '^[0-9A-Fa-f]+ T _${sym}$' $nmfile"
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

INC="-I$ROOT/libc/install-include -I$ROOT/tests/abi/cp_to_c"

# Prerequisite: the repo's prebuilt hardened C runtime (not built here).
if [ ! -f build/libc/libcplus_hardened.a ]; then
    printf 'PREREQUISITE MISSING: build/libc/libcplus_hardened.a\n'
    printf 'Build it with the repo standard step before running this lab.\n'
    log "PREREQUISITE MISSING: build/libc/libcplus_hardened.a"
    exit 2
fi
log "PREREQUISITE OK: $(ls -l build/libc/libcplus_hardened.a)"

# ---------------------------------------------------------------------------
# 1. Lower a real C+ source (strict mode) to an object, emitting the lowered C
#    so class lowering is auditable.  Class methods become <Class>_<method>.
# ---------------------------------------------------------------------------
step T1_cplus_lower_compile - 180 ./build/c+ build --strict --sanitize=none \
    tests/abi/cp_to_c/cp_to_c_lib.cp \
    --emit-c "$OUT/cp_to_c_lib.lowered.c" \
    -c -o "$OUT/cp_to_c_lib.o" -- $INC
step T1_ar_archive - 60 "$AR" rcs "$OUT/libcp_to_c.a" "$OUT/cp_to_c_lib.o"

# 2. Compile the ordinary C driver with /usr/bin/clang and link the C+ archive.
step T2_c_driver_link - 180 "$CC" -O1 -std=c99 -Wall -Wextra -pedantic \
    $INC tests/abi/cp_to_c/cp_to_c_driver.c \
    "$OUT/libcp_to_c.a" build/libc/libcplus_hardened.a \
    -lm -pthread -o "$OUT/cp_to_c_driver"
step T3_driver_run "$OUT/driver.out" 60 "$OUT/cp_to_c_driver"
check_line T3_driver_summary "$OUT/driver.out" "boundary checks match"

# 3. Symbol audit: exact names plus no Itanium/C++ mangling.
step T4_nm_archive - 60 "$NM" -gU "$OUT/libcp_to_c.a"
"$NM" -gU "$OUT/libcp_to_c.a" > "$OUT/nm_cp_to_c.txt" 2>> "$LOG"
log "--- nm -gU $OUT/libcp_to_c.a (full) ---"
cat "$OUT/nm_cp_to_c.txt" >> "$LOG"
log "--- end nm ---"
check_symbols T4_exact_symbols "$OUT/nm_cp_to_c.txt" \
    tests/abi/cp_to_c/expected_symbols.txt
log "CHECK[T4_no_cxx_mangling]: no ' T __Z' in nm_cp_to_c.txt"
if grep -Eq ' T __Z' "$OUT/nm_cp_to_c.txt"; then
    FAIL=$((FAIL + 1))
    printf 'FAIL T4_no_cxx_mangling\n'
else
    PASS=$((PASS + 1))
    printf 'PASS T4_no_cxx_mangling\n'
fi

# 4. Prove class lowering in the emitted C source (struct + plain function).
for needle in 'struct CpAdder' 'int32_t CpAdder_apply' \
              'struct CpRecordValidator' 'int CpRecordValidator_check' \
              'struct CpScaler' 'double CpScaler_apply' \
              'struct CpBuffer' 'int CpBuffer_write' \
              'struct CpLayoutProbe' 'int CpLayoutProbe_fill'; do
    log "CHECK[T5_lowering:$needle]: grep -F '$needle' $OUT/cp_to_c_lib.lowered.c"
    if grep -Fq "$needle" "$OUT/cp_to_c_lib.lowered.c"; then
        PASS=$((PASS + 1))
        printf 'PASS T5_lowering:%s\n' "$needle"
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL T5_lowering:%s\n' "$needle"
    fi
done

# 5. Mach-O object type / architecture of the archive's object.
step T6_file_object - 60 /usr/bin/file "$OUT/cp_to_c_lib.o"

printf 'cp_to_c lab: %d passed, %d failed\n' "$PASS" "$FAIL"
log "RESULT: cp_to_c lab: $PASS passed, $FAIL failed"
log "Full command/exit-code log: $LOG"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
exit 0

#!/bin/sh
# C-to-C+ interoperability lab runner.
#
# Builds a real C object/archive with /usr/bin/clang, links it from a C+ program
# with ./build/c+ via the supported `-- <archive>` path, runs both directions of
# the boundary, and records exact commands / symbol names / exit codes /
# stdout / stderr.  No source inspection, no `|| true` masking, bounded timeouts.
#
# Ownership: writes ONLY under tests/abi/c_to_cp/artifacts/.  It does not touch
# compiler, runtime, stdlib or any other test file.
set -u

export PATH=/usr/bin:/bin
export CPLUS_CLANG=/usr/bin/clang
CC=/usr/bin/clang
AR=/usr/bin/ar
NM=/usr/bin/nm
TIMEOUT=/usr/bin/timeout

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LAB=tests/abi/c_to_cp
OUT=$ROOT/$LAB/artifacts
mkdir -p "$OUT"

LOG=$OUT/results.log
: > "$LOG"
# Never let a stale artifact turn a failed build into a passing run.
rm -f "$OUT/c_to_cp_lib.o" "$OUT/libc_to_cp.a" \
      "$OUT/caller_direct" "$OUT/c_to_cp_caller" \
      "$OUT/caller.stdout" "$OUT/caller.stderr"
PASS=0
FAIL=0
LAST_RC=0

log() { printf '%s\n' "$*" >> "$LOG"; }

log "=== C-to-C+ interoperability lab ==="
log "date_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "arch=$(uname -m) os=$(uname -s)"
log "PATH=$PATH CPLUS_CLANG=$CPLUS_CLANG"
log "--- clang --version ---"
"$CC" --version >> "$LOG" 2>&1
log "--- c+ --version ---"
"$ROOT/build/c+" --version >> "$LOG" 2>&1
log "================================"

# step <label> <stdout-file|-> <stderr-file|-> <timeout> <command...>
step() {
    label=$1
    out=$2
    err=$3
    tmo=$4
    shift 4
    log "CMD[$label] timeout=${tmo}s: $*"
    if [ "$out" = "-" ]; then
        $TIMEOUT "$tmo" "$@" >> "$LOG" 2>&1
    else
        $TIMEOUT "$tmo" "$@" > "$out" 2> "$err"
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
        PASS=$((PASS + 1)); printf 'PASS %s\n' "$label"
    else
        FAIL=$((FAIL + 1)); printf 'FAIL %s (line missing)\n' "$label"
        log "MISSING in $file: $needle"
    fi
}

# check_absent <label> <file> <fixed-string>
check_absent() {
    label=$1
    file=$2
    needle=$3
    log "CHECK[$label]: grep -Fq '$needle' $file must be false"
    if grep -Fq "$needle" "$file"; then
        FAIL=$((FAIL + 1)); printf 'FAIL %s\n' "$label"
    else
        PASS=$((PASS + 1)); printf 'PASS %s\n' "$label"
    fi
}

cd "$ROOT" || exit 1

# ---------------------------------------------------------------------------
# 1. Compile the C library to a real object, then a real archive (clang + ar).
# ---------------------------------------------------------------------------
step C_compile - - 120 "$CC" -O2 -std=c99 -Wall -Wextra -pedantic \
    -I"$ROOT/$LAB" -c "$LAB/c_to_cp_lib.c" -o "$OUT/c_to_cp_lib.o"
step C_archive - - 60 "$AR" rcs "$OUT/libc_to_cp.a" "$OUT/c_to_cp_lib.o"

# ---------------------------------------------------------------------------
# 2. Export symbol names (demangled, unmangled C names) from the archive.
# ---------------------------------------------------------------------------
$NM -gU "$OUT/libc_to_cp.a" > "$OUT/nm_libc_to_cp.txt" 2>> "$LOG"
log "CMD[NM]: $NM -gU $OUT/libc_to_cp.a > $OUT/nm_libc_to_cp.txt"

for sym in \
    cto_point_translate cto_rect_from_points cto_rect_area \
    cto_sum_ints cto_scale_in_place \
    cto_register_callback cto_fire_callback cto_clear_callback \
    cto_apply_binary \
    cto_store_create cto_store_put cto_store_get cto_store_size cto_store_destroy \
    cto_make_int_buffer cto_consume_and_free_int_buffer cto_vsum; do
    if grep -Eq "^[0-9A-Fa-f]+ T _${sym}$" "$OUT/nm_libc_to_cp.txt"; then
        PASS=$((PASS + 1)); printf 'PASS symbol:%s\n' "$sym"
    else
        FAIL=$((FAIL + 1)); printf 'FAIL symbol:%s (absent/renamed)\n' "$sym"
        log "MISSING SYMBOL $sym in $OUT/nm_libc_to_cp.txt"
    fi
done
check_absent no_cxx_mangling "$OUT/nm_libc_to_cp.txt" ' T __Z'

# ---------------------------------------------------------------------------
# 3. Direct-link probe: prove the STRICT C+ driver consumes the precompiled C
#    archive.  `-- <bare .a path>` is rejected by the STRICT pass-through
#    allow-list (bare inputs are an unbounded injection surface), so the archive
#    is passed through the allow-listed `-L <dir> -l <name>` spelling.  No
#    warning-suppression flag is used: -Wno-* is deliberately rejected in
#    STRICT, and the expected compile-step "linker input unused" diagnostic is
#    a backend warning, not a C+ safety diagnostic.
# ---------------------------------------------------------------------------
step Cplus_probe_link - - 180 "$ROOT/build/c+" build --strict --sanitize=none \
    "$LAB/c_to_cp_caller.cp" -o "$OUT/caller_direct" -- \
    -L"$OUT" -lc_to_cp

# ---------------------------------------------------------------------------
# 4. Production C+ build (second STRICT configuration: -O2) and run.
# ---------------------------------------------------------------------------
step Cplus_build - - 180 "$ROOT/build/c+" build --strict --sanitize=none -O2 \
    "$LAB/c_to_cp_caller.cp" -o "$OUT/c_to_cp_caller" -- \
    -L"$OUT" -lc_to_cp
step Cplus_run "$OUT/caller.stdout" "$OUT/caller.stderr" 60 "$OUT/c_to_cp_caller"
check_line final_summary "$OUT/caller.stdout" "calls match"

# ---------------------------------------------------------------------------
printf 'c_to_cp: %d passed, %d failed\n' "$PASS" "$FAIL"
log "RESULT: c_to_cp: $PASS passed, $FAIL failed"
log "Full command/exit-code log: $LOG"
log "Caller stdout: $OUT/caller.stdout"
log "Caller stderr: $OUT/caller.stderr"
log "Archive symbols: $OUT/nm_libc_to_cp.txt"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
exit 0

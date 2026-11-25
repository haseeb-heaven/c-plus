#!/bin/sh
# capture.sh -- reproducible terminal "screenshots" of real runtime refusals.
#
# Builds the existing safety demos with their documented flags (no source
# edits), runs each resulting binary under /usr/bin/timeout 30, and writes the
# verbatim stdout+stderr transcripts into apps/legacy_examples/screenshots/captures/.
#
# Strict scope: this script ONLY writes under apps/legacy_examples/screenshots/.  Build
# outputs and logs land in apps/legacy_examples/screenshots/captures/tmp/, and TMPDIR is
# redirected there too.  No checked-in demo binary, test output, or source
# file is modified.
#
# These transcripts are evidence of runtime refusal under the tested inputs,
# NOT a proof of full memory/lifetime/thread safety.  See MANIFEST.md.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd) || exit 2
CAP="$HERE/captures"
TMP="$CAP/tmp"

cd "$ROOT" || exit 2
mkdir -p "$CAP" "$TMP" || exit 2
export TMPDIR="$TMP"

CPLUS=./build/c+
CC=/usr/bin/clang
TIMEOUT=/usr/bin/timeout
SAFECRT=build/safe/libcplus_safecrt.a
HARDENED=build/libc/libcplus_hardened.a

for prereq in "$CPLUS" "$CC" "$TIMEOUT" "$SAFECRT" "$HARDENED"; do
    if [ ! -e "$prereq" ]; then
        printf 'MISSING PREREQUISITE: %s\n' "$prereq"
        exit 2
    fi
done

CPLUS_VER=$("$CPLUS" --version 2>&1 | head -n 1)
CLANG_VER=$("$CC" --version 2>&1 | head -n 1)
UNAME_S=$(uname -m)-$(uname -s)
TOOLCHAIN="$CPLUS_VER ; $CLANG_VER ; $UNAME_S"
DATE_UTC=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
printf 'CAPTURE_TOOLCHAIN: %s\nCAPTURE_DATE_UTC: %s\n' "$TOOLCHAIN" "$DATE_UTC"

CAP_FAIL=0
BUILDS=0
RUNS=0

# build_one <name> <binary> <build command...>
build_one() {
    bname=$1; bbin=$2; shift 2
    BUILDS=$((BUILDS + 1))
    rm -f "$bbin"
    if ! "$@" >"$TMP/$bname.build.log" 2>&1; then
        printf 'BUILD_FAIL %s (log: %s)\n' "$bname" "$TMP/$bname.build.log"
        cat "$TMP/$bname.build.log"
        CAP_FAIL=1
        return 1
    fi
    # The C+ driver can exit 0 even when its linker step failed; require a binary.
    if [ ! -x "$bbin" ]; then
        printf 'BUILD_FAIL %s no executable produced (log: %s)\n' "$bname" "$TMP/$bname.build.log"
        CAP_FAIL=1
        return 1
    fi
    printf 'BUILD_OK %s\n' "$bname"
}

# cap_start <file> <title>
cap_start() {
    sf=$1; stitle=$2
    : > "$CAP/$sf"
    printf '# CAPTURED TERMINAL TRANSCRIPT: %s\n' "$sf" >> "$CAP/$sf"
    printf '# TITLE: %s\n' "$stitle" >> "$CAP/$sf"
    printf '# CAPTURED_AT_UTC: %s\n' "$DATE_UTC" >> "$CAP/$sf"
    printf '# TOOLCHAIN: %s\n' "$TOOLCHAIN" >> "$CAP/$sf"
    printf '# CWD: %s\n' "$ROOT" >> "$CAP/$sf"
    printf '# NOTE: everything between BEGIN/END markers is verbatim stdout+stderr\n' >> "$CAP/$sf"
    printf '#       of a real binary executed under /usr/bin/timeout 30.\n' >> "$CAP/$sf"
}

# cap_build_note <file> <literal build command>
cap_build_note() {
    printf '# BUILD CMD: %s\n' "$2" >> "$CAP/$1"
}

# cap_note <file> <free-form annotation>
cap_note() {
    printf '# NOTE: %s\n' "$2" >> "$CAP/$1"
}

# cap_run <file> <literal run command> <binary> [args...]
cap_run() {
    rf=$1; rdisplay=$2; rbin=$3; shift 3
    printf '# RUN CMD: %s\n' "$rdisplay" >> "$CAP/$rf"
    printf '# --- BEGIN VERBATIM OUTPUT ---\n' >> "$CAP/$rf"
    "$TIMEOUT" 30 "$rbin" "$@" >> "$CAP/$rf" 2>&1
    rrc=$?
    printf '# --- END VERBATIM OUTPUT ---\n' >> "$CAP/$rf"
    printf '# EXIT_CODE=%d\n' "$rrc" >> "$CAP/$rf"
    RUNS=$((RUNS + 1))
    if [ "$rrc" -ne 0 ]; then
        printf 'RUN_FAIL %s exit=%d\n' "$rf" "$rrc"
        CAP_FAIL=1
    fi
}

# cap_end <file> <pipe-separated expected tokens>
cap_end() {
    ef=$1; etokens=$2
    oIFS=$IFS; IFS='|'
    missing=''
    for tok in $etokens; do
        if grep -qF -- "$tok" "$CAP/$ef"; then
            printf '# TOKEN_OK: %s\n' "$tok" >> "$CAP/$ef"
        else
            printf '# TOKEN_MISSING: %s\n' "$tok" >> "$CAP/$ef"
            missing="$missing [$tok]"
            CAP_FAIL=1
        fi
    done
    IFS=$oIFS
    if [ -z "$missing" ]; then
        printf '# VERDICT: PASS -- every expected refusal token was found in real captured output\n' >> "$CAP/$ef"
    else
        printf '# VERDICT: FAIL -- missing tokens:%s\n' "$missing" >> "$CAP/$ef"
    fi
    printf 'TOKENS %s %s\n' "$ef" "$( [ -z "$missing" ] && printf PASS || printf FAIL )"
}

# ---------------------------------------------------------------------------
# Build with the exact documented flags.  Binaries live under captures/tmp/.
# ---------------------------------------------------------------------------
BIN_MEMCPY="$TMP/memcpy_s_demo"
BIN_BOUNDS="$TMP/bounds_demo"
BIN_LIFETIME="$TMP/lifetime_demo"
BIN_SYNC="$TMP/sync_demo"
BIN_DB="$TMP/database_safety_demo"
BIN_HOSTILE="$TMP/hostile_checked"

CMD_MEMCPY_BUILD="$CPLUS build apps/legacy_examples/expanded/safety/memcpy_s_demo.cp --strict --sanitize=none -I. -o \"$BIN_MEMCPY\" -- $SAFECRT $HARDENED"
CMD_BOUNDS_BUILD="$CPLUS build apps/legacy_examples/expanded/safety/bounds_demo.cp --strict --sanitize=none -I. -o \"$BIN_BOUNDS\""
CMD_LIFETIME_BUILD="$CPLUS build apps/legacy_examples/expanded/safety/lifetime_demo.cp --strict --sanitize=none -I. -o \"$BIN_LIFETIME\""
CMD_SYNC_BUILD="$CC -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -I. -Ilibc/install-include apps/legacy_examples/expanded/safety/sync_demo.c $HARDENED -pthread -o \"$BIN_SYNC\""
CMD_DB_BUILD="$CPLUS build apps/legacy_examples/expanded/database/safety_demo.cp --strict --sanitize=none -I. -o \"$BIN_DB\""
INC="-I. -Istdlib/safe -Ilibc -Ilibc/install-include -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
CFLAGS="-O0 -fno-builtin -std=c99 -Wall -Wextra -Wno-format-nonliteral -Wno-fortify-source -Wno-builtin-memcpy-chk-size"
CMD_HOSTILE_BUILD="$CC $CFLAGS $INC -include stdlib/safe/safe_c.h tests/abi/stdc_safety/hostile.c $SAFECRT $HARDENED -lm -pthread -o \"$BIN_HOSTILE\""

CMD_MEMCPY_RUN="$TIMEOUT 30 $BIN_MEMCPY"
CMD_BOUNDS_RUN="$TIMEOUT 30 $BIN_BOUNDS"
CMD_LIFETIME_RUN="$TIMEOUT 30 $BIN_LIFETIME"
CMD_SYNC_RUN="$TIMEOUT 30 $BIN_SYNC"
CMD_DB_RUN="$TIMEOUT 30 $BIN_DB"
CMD_HOSTILE_RUN="$TIMEOUT 30 $BIN_HOSTILE $TMP/hostile-out"

build_one memcpy_s_demo "$BIN_MEMCPY" \
    "$CPLUS" build apps/legacy_examples/expanded/safety/memcpy_s_demo.cp \
    --strict --sanitize=none -I. -o "$BIN_MEMCPY" \
    -- "$SAFECRT" "$HARDENED"

build_one bounds_demo "$BIN_BOUNDS" \
    "$CPLUS" build apps/legacy_examples/expanded/safety/bounds_demo.cp \
    --strict --sanitize=none -I. -o "$BIN_BOUNDS"

build_one lifetime_demo "$BIN_LIFETIME" \
    "$CPLUS" build apps/legacy_examples/expanded/safety/lifetime_demo.cp \
    --strict --sanitize=none -I. -o "$BIN_LIFETIME"

build_one sync_demo "$BIN_SYNC" \
    "$CC" -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra \
    -I. -Ilibc/install-include \
    apps/legacy_examples/expanded/safety/sync_demo.c "$HARDENED" -pthread -o "$BIN_SYNC"

build_one database_safety_demo "$BIN_DB" \
    "$CPLUS" build apps/legacy_examples/expanded/database/safety_demo.cp \
    --strict --sanitize=none -I. -o "$BIN_DB"

mkdir -p "$TMP/hostile-out"
build_one hostile_checked "$BIN_HOSTILE" \
    "$CC" $CFLAGS $INC -include stdlib/safe/safe_c.h \
    tests/abi/stdc_safety/hostile.c "$SAFECRT" "$HARDENED" \
    -lm -pthread -o "$BIN_HOSTILE"

# ---------------------------------------------------------------------------
# Captured transcripts.
# ---------------------------------------------------------------------------
cap_start unsafe_vs_safe_memcpy.txt \
    'Before/after memcpy: forked raw one-over corrupts a canary; cplus_memcpy_s refuses the same one-over with safeclib ESNOSPC=406'
cap_build_note unsafe_vs_safe_memcpy.txt "$CMD_MEMCPY_BUILD"
cap_run unsafe_vs_safe_memcpy.txt "$CMD_MEMCPY_RUN" "$BIN_MEMCPY"
cap_end unsafe_vs_safe_memcpy.txt \
    'memcpy_s ordinary_one_over OK canary=0x2a corrupted=1|memcpy_s checked_one_over REFUSED rc=406|IGNORE CONSTRAINT HANDLER: (406)|memcpy_s parent_canary OK canary=0xa5'

cap_start bounds_query.txt \
    'Bounds query: stdlib_mem_span_ok/stdlib_mem_query refuse a one-over span and a stale-generation key (STDLIB_CATEGORY_BOUNDS / LIFECYCLE)'
cap_build_note bounds_query.txt "$CMD_BOUNDS_BUILD"
cap_run bounds_query.txt "$CMD_BOUNDS_RUN" "$BIN_BOUNDS"
cap_end bounds_query.txt \
    'bounds exact_fit OK cap=16|bounds one_over REFUSED span_1over=0 span_tail=0 query=-1 bounds_cat=1|bounds interior_pointer OK interior_has_metadata=0 contains_24=1 contains_25=0|bounds stale_generation REFUSED moved=1 old_key_current=0 old_query=-1 lifecycle_cat=1'

cap_start lifetime_refusal.txt \
    'Lifetime: stale token and double release are refused from metadata; bounds_demo is re-run to show the lifecycle-category refusal line as well'
cap_build_note lifetime_refusal.txt "$CMD_LIFETIME_BUILD"
cap_run lifetime_refusal.txt "$CMD_LIFETIME_RUN" "$BIN_LIFETIME"
cap_note lifetime_refusal.txt 'The next command re-runs the already-built bounds_demo binary solely to capture its lifecycle-category refusal line.'
cap_build_note lifetime_refusal.txt "$CMD_BOUNDS_BUILD"
cap_run lifetime_refusal.txt "$CMD_BOUNDS_RUN" "$BIN_BOUNDS"
cap_end lifetime_refusal.txt \
    'lifetime stale_token_refused REFUSED released=1 retired=1 not_live=1 read=-1 cat=1|lifetime double_release_refused REFUSED init=1 first=1 second=-1 double_free_cat=1|bounds stale_generation REFUSED moved=1 old_key_current=0 old_query=-1 lifecycle_cat=1'

cap_start sync_double_destroy.txt \
    'Synchronization: second destroy/join of a mutex, condition variable, and thread handle is refused with EINVAL=22; NULL handles are refused too'
cap_build_note sync_double_destroy.txt "$CMD_SYNC_BUILD"
cap_run sync_double_destroy.txt "$CMD_SYNC_RUN" "$BIN_SYNC"
cap_end sync_double_destroy.txt \
    'sync mtx_destroy REFUSED init_destroy=0 double=22 (EINVAL=22)|sync cnd_destroy REFUSED init_destroy=0 double=22 (EINVAL=22)|sync thrd_join REFUSED join=0 result=42 double=22 (EINVAL=22)|sync null_handles REFUSED mtx_NULL=22 cnd_NULL=22 thrd_zero=22 (EINVAL=22)'

cap_start database_safety_refusals.txt \
    'Database demo: oversized key, null value, overflowing sum, duplicate key and delete-missing are all refused; canary and store stay intact'
cap_build_note database_safety_refusals.txt "$CMD_DB_BUILD"
cap_run database_safety_refusals.txt "$CMD_DB_RUN" "$BIN_DB"
cap_end database_safety_refusals.txt \
    'SAFETY oversized_key REFUSED 101|SAFETY null_value REFUSED 108|SAFETY overflowing_sum REFUSED 101|SAFETY duplicate_key REFUSED 108|SAFETY delete_missing REFUSED 108|SAFETY FINAL_CANARY ok|SAFETY_SUMMARY 5 5 5 0'

cap_start stdc_hostile_refusals.txt \
    'Hostile ordinary-C inputs: force-included checked layer refuses null/small/huge cases across APIs with all canaries intact'
cap_build_note stdc_hostile_refusals.txt "$CMD_HOSTILE_BUILD"
cap_run stdc_hostile_refusals.txt "$CMD_HOSTILE_RUN" "$BIN_HOSTILE" "$TMP/hostile-out"
cap_end stdc_hostile_refusals.txt \
    '[CPLUS-SAFE] memcpy_s: buffer overflow refused (requested 17 > capacity 16, ESNOSPC 406)|stdc_safety memcpy null REFUSED 1|stdc_safety strcpy huge REFUSED 1|stdc_safety free null REFUSED 1|STDC_SAFETY 64 48 64 0'

FILES=$(find "$CAP" -maxdepth 1 -name '*.txt' -type f | wc -l | tr -d ' ')
printf 'CAPTURE_SUMMARY files=%s builds=%d runs=%d failures=%d\n' "$FILES" "$BUILDS" "$RUNS" "$CAP_FAIL"
[ "$CAP_FAIL" -eq 0 ] || exit 1
exit 0

#!/bin/sh
# Build TWO callers from ONE source against the SAME saved precompiled archive:
#   (A) app_plain  - no safe_c.h
#   (B) app_safe   - -include stdlib/safe/safe_c.h  (caller-side only)
# and a NEGATIVE control compiled with the same force-include.  Run all three,
# capture rc/stdout/stderr, compare (A) vs (B) stdout byte-for-byte, and verify
# the archive sha256 is unchanged before and after every build.
#
# Ownership: reads sources in tests/abi/precompiled_safe_layer/ and prebuilt
# support archives in build/; writes ONLY to tests/abi/precompiled_safe_layer/
# artifacts/.  No compiler, runtime, or stdlib file is modified.
set -u

export PATH=/usr/bin:/bin
CC=/usr/bin/clang
SHA=/usr/bin/shasum

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
HERE="$ROOT/tests/abi/precompiled_safe_layer"
OUT="$HERE/artifacts"
mkdir -p "$OUT"
LOG="$OUT/caller_build.log"
RESULTS="$OUT/results.log"
ARCHIVE="$OUT/libsafe_layer.a"
SAFECRT="$ROOT/build/safe/libcplus_safecrt.a"
HARDENED="$ROOT/build/libc/libcplus_hardened.a"

if [ ! -f "$ARCHIVE" ] || [ ! -f "$OUT/lib.sha256" ]; then
    printf 'FATAL: run build_lib_once.sh first\n' >&2; exit 2
fi
for lib in "$SAFECRT" "$HARDENED"; do
    if [ ! -f "$lib" ]; then
        printf 'FATAL: required prebuilt support archive missing: %s\n' "$lib" >&2; exit 2
    fi
done

sha256_of() { "$SHA" -a 256 "$1" | /usr/bin/awk '{print $1}'; }
baseline=$(/usr/bin/awk '$2=="libsafe_layer.a"{print $1}' "$OUT/lib.sha256")

# Include paths needed by safe_c.h and its transitive headers (cplus_libc.h,
# safeclib headers).  $ROOT makes "-include stdlib/safe/safe_c.h" resolvable.
INC="-I$ROOT -I$ROOT/stdlib/safe -I$ROOT/libc -I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"
CFLAGS="-O2 -std=c11 -Wall -Wextra -pedantic"

: > "$LOG"
log() { printf '%s\n' "$*" >> "$LOG"; }
run() {
    label=$1; outfile=$2; shift 2
    log "CMD[$label]: $*"
    "$@" > "$outfile" 2> "$OUT/_err"
    rc=$?
    log "RC[$label]=$rc"
    log "--- stdout[$label] ---"; /bin/cat "$outfile" >> "$LOG"
    log "--- stderr[$label] ---"; /bin/cat "$OUT/_err" >> "$LOG"
    return "$rc"
}

log "=== caller builds + run against the SAME saved archive ==="
log "date_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "baseline_sha256(libsafe_layer.a)=$baseline"
log "safecrt=$SAFECRT sha256=$(sha256_of "$SAFECRT")"
log "hardened=$HARDENED sha256=$(sha256_of "$HARDENED")"
log "========================================="

fails=0

# ---- prove the saved archive is unchanged BEFORE building callers ----------
now=$(sha256_of "$ARCHIVE")
log "PRE-BUILD sha256(libsafe_layer.a)=$now"
[ "$now" = "$baseline" ] || { log "FAIL: archive changed before build"; fails=$((fails+1)); }

# ---- (A) plain caller: links only the saved archive ------------------------
run build_plain /dev/null \
    "$CC" $CFLAGS -I"$HERE" "$HERE/safe_layer_app.c" "$ARCHIVE" -o "$OUT/app_plain"
rc=$?; [ $rc -eq 0 ] || { printf 'FAIL build_plain rc=%d\n' "$rc" >&2; fails=$((fails+1)); }

# ---- (B) safe caller: force-included header, same archive + support libs ---
run build_safe /dev/null \
    "$CC" $CFLAGS -I"$HERE" $INC -include stdlib/safe/safe_c.h \
    "$HERE/safe_layer_app.c" "$ARCHIVE" "$SAFECRT" "$HARDENED" \
    -lm -pthread -o "$OUT/app_safe"
rc=$?; [ $rc -eq 0 ] || { printf 'FAIL build_safe rc=%d\n' "$rc" >&2; fails=$((fails+1)); }

# ---- negative control: same force-include, overflow on a local buffer ------
run build_negative /dev/null \
    "$CC" $CFLAGS -I"$HERE" $INC -include stdlib/safe/safe_c.h \
    "$HERE/safe_layer_negative.c" "$ARCHIVE" "$SAFECRT" "$HARDENED" \
    -lm -pthread -o "$OUT/negative_safe"
rc=$?; [ $rc -eq 0 ] || { printf 'FAIL build_negative rc=%d\n' "$rc" >&2; fails=$((fails+1)); }

# ---- prove the saved archive is unchanged AFTER building callers -----------
now=$(sha256_of "$ARCHIVE")
log "POST-BUILD sha256(libsafe_layer.a)=$now"
[ "$now" = "$baseline" ] || { log "FAIL: archive changed after build"; fails=$((fails+1)); }

# ---- run the three programs, capturing rc/stdout/stderr --------------------
run_program() {
    name=$1; bin=$2
    "$bin" > "$OUT/$name.stdout" 2> "$OUT/$name.stderr"
    rc=$?
    printf '%d\n' "$rc" > "$OUT/$name.rc"
    log "RUN[$name] rc=$rc"
    log "--- stdout[$name] ---"; /bin/cat "$OUT/$name.stdout" >> "$LOG"
    log "--- stderr[$name] ---"; /bin/cat "$OUT/$name.stderr" >> "$LOG"
    return "$rc"
}

run_program app_plain "$OUT/app_plain"; plain_rc=$?
run_program app_safe "$OUT/app_safe"; safe_rc=$?
run_program negative_safe "$OUT/negative_safe"; neg_rc=$?

# ---- verification ----------------------------------------------------------
: > "$RESULTS"
res() { printf '%s\n' "$*" >> "$RESULTS"; /bin/cat "$RESULTS" > /dev/null; }
check() { # label ok(0/1)
    if [ "$2" -eq 0 ]; then res "PASS $1"; else res "FAIL $1"; fails=$((fails+1)); fi
}

check "app_plain exit 0" $([ "$plain_rc" -eq 0 ] && printf 0 || printf 1)
check "app_safe exit 0"  $([ "$safe_rc" -eq 0 ] && printf 0 || printf 1)

if /usr/bin/cmp -s "$OUT/app_plain.stdout" "$OUT/app_safe.stdout"; then
    res "PASS stdout byte-identical (plain vs safe, cmp -s)"
    res "stdout sha256 plain=$(sha256_of "$OUT/app_plain.stdout")"
    res "stdout sha256 safe =$(sha256_of "$OUT/app_safe.stdout")"
else
    res "FAIL stdout byte-identical (plain vs safe, cmp -s)"
    fails=$((fails+1))
fi

check "archive sha256 unchanged after caller builds" \
    $([ "$(sha256_of "$ARCHIVE")" = "$baseline" ] && printf 0 || printf 1)

# Negative control: must refuse overflow -> exit 70, diagnostic on stderr, no
# "UNEXPECTED" line, and stdout still shows the unchanged C library was called.
check "negative_safe exit 70 (refusal)" $([ "$neg_rc" -eq 70 ] && printf 0 || printf 1)
if /usr/bin/grep -q '\[CPLUS-SAFE\] memset_s: buffer overflow refused' "$OUT/negative_safe.stderr"; then
    res "PASS negative stderr reports ESNOSPC refusal"
else
    res "FAIL negative stderr reports ESNOSPC refusal"; fails=$((fails+1))
fi
if /usr/bin/grep -q 'lib_version=3 lib_mul=42' "$OUT/negative_safe.stdout"; then
    res "PASS negative stdout shows unchanged C library was called"
else
    res "FAIL negative stdout shows unchanged C library was called"; fails=$((fails+1))
fi
if /usr/bin/grep -q 'UNEXPECTED' "$OUT/negative_safe.stdout"; then
    res "FAIL negative overflow was NOT refused (UNEXPECTED line present)"; fails=$((fails+1))
else
    res "PASS negative overflow refused before UNEXPECTED line"
fi

res "----"
res "exit codes: app_plain=$plain_rc app_safe=$safe_rc negative_safe=$neg_rc"
res "failures=$fails"
res "CONCLUSION: caller-side -include safe_c.h needs NO rebuild of the saved C archive."
/bin/cat "$LOG" >> "$RESULTS"

if [ "$fails" -eq 0 ]; then
    printf 'caller lab: ALL CHECKS PASSED\n'
else
    printf 'caller lab: %d failure(s) (see %s)\n' "$fails" "$RESULTS" >&2
fi
/bin/cat "$RESULTS"
exit "$fails"

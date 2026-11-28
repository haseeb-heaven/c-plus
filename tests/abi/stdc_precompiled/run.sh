#!/bin/sh
# Prove a precompiled standard-C library links UNCHANGED with and without the
# caller-side safety layer (stdlib/safe/safe_c.h).
#
#   sh tests/abi/stdc_precompiled/run.sh
#
# Stages:
#   1. Compile legacy.c EXACTLY ONCE (/usr/bin/clang -O2 -std=c11 -c), archive
#      to libstdc_legacy.a, record its SHA-256 baseline.
#   2. Build TWO callers from the SAME caller.c against the SAME saved archive:
#        app_plain - no safe_c.h, links only the saved .a
#        app_safe  - -include stdlib/safe/safe_c.h + both C+ archives + saved .a
#      and a NEGATIVE caller (safe include, 64-byte copy into char[8]).
#   3. Re-hash the archive before and after every link: it must be unchanged.
#   4. Run both valid callers with IDENTICAL argv inputs; require exit 0 and
#      byte-identical stdout.  Require the negative caller to refuse (70),
#      report the ESNOSPC refusal, still call the unchanged library, and never
#      print its UNEXPECTED line.
#
# Ownership: reads only files in tests/abi/stdc_precompiled/ and prebuilt
# support archives in build/.  Writes ONLY under .../stdc_precompiled/artifacts/.
# No compiler, header, source, or archive outside this directory is modified.
# Whole run is bounded by a portable 60-second watchdog (macOS has no `timeout`).
set -u

# ---- portable 60s watchdog -------------------------------------------------
( sleep 60; kill -TERM $$ 2>/dev/null ) >/dev/null 2>&1 </dev/null & WATCHDOG=$!
trap 'kill -TERM "$WATCHDOG" 2>/dev/null' EXIT INT TERM

export PATH=/usr/bin:/bin
CC=/usr/bin/clang
AR=/usr/bin/ar
SHA=/usr/bin/shasum
CMP=/usr/bin/cmp
GREP=/usr/bin/grep

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
HERE="$ROOT/tests/abi/stdc_precompiled"
OUT="$HERE/artifacts"
mkdir -p "$OUT"

ARCHIVE="$OUT/libstdc_legacy.a"
OBJ="$OUT/stdc_legacy.o"
BASELINE="$OUT/lib.sha256"
LOG="$OUT/results.log"

SAFECRT="$ROOT/build/safe/libcplus_safecrt.a"
HARDENED="$ROOT/build/libc/libcplus_hardened.a"

# Include paths required by safe_c.h and its transitive headers.  $ROOT makes
# "-include stdlib/safe/safe_c.h" resolvable from the repository root.
INC="-I$ROOT -I$ROOT/stdlib/safe -I$ROOT/libc -I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"
CFLAGS="-O2 -std=c11 -Wall -Wextra -pedantic"

# Identical valid inputs passed to BOTH valid callers.
IN_A=20
IN_B=22

# ---- clean only our own generated files ------------------------------------
rm -f "$OBJ" "$ARCHIVE" "$BASELINE" "$LOG" "$OUT/_err" \
      "$OUT/app_plain" "$OUT/app_safe" "$OUT/negative_safe" \
      "$OUT"/*.rc "$OUT"/*.stdout "$OUT"/*.stderr "$OUT"/*.log

: > "$LOG"
log() { printf '%s\n' "$*" >> "$LOG"; }
run() {
    label=$1; shift
    log "CMD[$label]: $*"
    "$@" > "$OUT/_out" 2> "$OUT/_err"
    rc=$?
    log "RC[$label]=$rc"
    log "--- stdout[$label] ---"; /bin/cat "$OUT/_out" >> "$LOG"
    log "--- stderr[$label] ---"; /bin/cat "$OUT/_err" >> "$LOG"
    return "$rc"
}
sha256_of() { "$SHA" -a 256 "$1" | /usr/bin/awk '{print $1}'; }

log "=== stdc_precompiled: precompiled standard-C library ABI proof ==="
log "date_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "arch=$(uname -m) os=$(uname -s)"
log "PATH=$PATH"
"$CC" --version >> "$LOG" 2>&1
log "support: SAFECRT=$SAFECRT sha256=$(sha256_of "$SAFECRT")"
log "support: HARDENED=$HARDENED sha256=$(sha256_of "$HARDENED")"
log "=========================================================="

fails=0
check() { # label ok(0/1)
    if [ "$2" -eq 0 ]; then log "PASS $1"; else log "FAIL $1"; fails=$((fails+1)); fi
}

for lib in "$SAFECRT" "$HARDENED"; do
    if [ ! -f "$lib" ]; then
        printf 'FATAL: required prebuilt support archive missing: %s\n' "$lib" >&2
        log "FATAL: missing support archive $lib"
        exit 2
    fi
done

# ---- stage 1: build the C library ONCE -------------------------------------
log "-- stage 1: compile legacy.c exactly once and archive --"
run lib_compile "$CC" -O2 -std=c11 -Wall -Wextra -pedantic \
    -c "$HERE/legacy.c" -o "$OBJ" || { log "FAIL lib_compile"; exit 1; }

run lib_archive "$AR" rcs "$ARCHIVE" "$OBJ" || { log "FAIL lib_archive"; exit 1; }

obj_sha=$(sha256_of "$OBJ")
archive_sha=$(sha256_of "$ARCHIVE")
{
    printf '%s  %s\n' "$obj_sha" "stdc_legacy.o"
    printf '%s  %s\n' "$archive_sha" "libstdc_legacy.a"
} > "$BASELINE"
log "SHA256[stdc_legacy.o]=${obj_sha}  <-- bit-reproducible object baseline"
log "SHA256[libstdc_legacy.a]=${archive_sha}  <-- baseline (ar embeds a timestamp)"
baseline=$archive_sha
obj_baseline=$obj_sha

# ---- stage 2: build callers against the SAME saved archive ------------------
log "-- stage 2: build callers against the SAME saved archive --"

now=$(sha256_of "$ARCHIVE")
now_o=$(sha256_of "$OBJ")
log "PRE-LINK sha256(libstdc_legacy.a)=$now"
log "PRE-LINK sha256(stdc_legacy.o)=$now_o"
check "archive unchanged before links" $([ "$now" = "$baseline" ] && printf 0 || printf 1)
check "object unchanged before links" $([ "$now_o" = "$obj_baseline" ] && printf 0 || printf 1)

# (A) plain caller: no safe_c.h, links ONLY the saved archive.
run build_plain "$CC" $CFLAGS -I"$HERE" "$HERE/caller.c" "$ARCHIVE" \
    -o "$OUT/app_plain" || { log "FAIL build_plain"; fails=$((fails+1)); }

# (B) safe caller: caller-side force-include, links SAME archive + support libs.
run build_safe "$CC" $CFLAGS -I"$HERE" $INC -include stdlib/safe/safe_c.h \
    "$HERE/caller.c" "$ARCHIVE" "$SAFECRT" "$HARDENED" \
    -lm -pthread -o "$OUT/app_safe" || { log "FAIL build_safe"; fails=$((fails+1)); }

# Negative caller: same force-include, 64-byte copy into char[8].
run build_negative "$CC" $CFLAGS -I"$HERE" $INC -include stdlib/safe/safe_c.h \
    "$HERE/negative.c" "$ARCHIVE" "$SAFECRT" "$HARDENED" \
    -lm -pthread -o "$OUT/negative_safe" || { log "FAIL build_negative"; fails=$((fails+1)); }

# Re-hash AFTER every link: must be bit-for-bit identical to the baseline.
now=$(sha256_of "$ARCHIVE")
now_o=$(sha256_of "$OBJ")
log "POST-LINK sha256(libstdc_legacy.a)=$now"
log "POST-LINK sha256(stdc_legacy.o)=$now_o"
check "archive unchanged after all links" $([ "$now" = "$baseline" ] && printf 0 || printf 1)
check "object unchanged after all links" $([ "$now_o" = "$obj_baseline" ] && printf 0 || printf 1)

# ---- stage 3: run with identical valid inputs -------------------------------
log "-- stage 3: run both valid callers with identical inputs ($IN_A $IN_B) --"
run_program() {
    name=$1; bin=$2; shift 2
    "$bin" "$@" > "$OUT/$name.stdout" 2> "$OUT/$name.stderr"
    rc=$?
    printf '%d\n' "$rc" > "$OUT/$name.rc"
    log "RUN[$name] rc=$rc argv=[$*]"
    log "--- stdout[$name] ---"; /bin/cat "$OUT/$name.stdout" >> "$LOG"
    log "--- stderr[$name] ---"; /bin/cat "$OUT/$name.stderr" >> "$LOG"
    return "$rc"
}

run_program app_plain "$OUT/app_plain" "$IN_A" "$IN_B"; plain_rc=$?
run_program app_safe "$OUT/app_safe" "$IN_A" "$IN_B"; safe_rc=$?
run_program negative_safe "$OUT/negative_safe"; neg_rc=$?

# ---- verification ----------------------------------------------------------
check "app_plain exit 0" $([ "$plain_rc" -eq 0 ] && printf 0 || printf 1)
check "app_safe exit 0"  $([ "$safe_rc" -eq 0 ] && printf 0 || printf 1)

if "$CMP" -s "$OUT/app_plain.stdout" "$OUT/app_safe.stdout"; then
    log "PASS stdout byte-identical (plain vs safe, cmp -s)"
    log "stdout sha256 plain=$(sha256_of "$OUT/app_plain.stdout")"
    log "stdout sha256 safe =$(sha256_of "$OUT/app_safe.stdout")"
else
    log "FAIL stdout byte-identical (plain vs safe, cmp -s)"
    fails=$((fails+1))
fi

# Negative: refusal -> exit 70, ESNOSPC diagnostic, saved lib still called,
# and the UNEXPECTED line must not appear.
check "negative_safe exit 70 (refusal)" $([ "$neg_rc" -eq 70 ] && printf 0 || printf 1)
if "$GREP" -q '\[CPLUS-SAFE\] memcpy_s: buffer overflow refused' "$OUT/negative_safe.stderr"; then
    log "PASS negative stderr reports ESNOSPC refusal"
else
    log "FAIL negative stderr reports ESNOSPC refusal"; fails=$((fails+1))
fi
if "$GREP" -q 'ESNOSPC 406' "$OUT/negative_safe.stderr"; then
    log "PASS negative stderr names ESNOSPC 406"
else
    log "FAIL negative stderr names ESNOSPC 406"; fails=$((fails+1))
fi
if "$GREP" -q 'negative: lib_version=11 lib_mul=42' "$OUT/negative_safe.stdout"; then
    log "PASS negative stdout shows unchanged C library was called"
else
    log "FAIL negative stdout shows unchanged C library was called"; fails=$((fails+1))
fi
if "$GREP" -q 'UNEXPECTED' "$OUT/negative_safe.stdout"; then
    log "FAIL negative overflow was NOT refused (UNEXPECTED present)"; fails=$((fails+1))
else
    log "PASS negative overflow refused before UNEXPECTED line"
fi

log "----"
log "exit codes: app_plain=$plain_rc app_safe=$safe_rc negative_safe=$neg_rc"
log "failures=$fails"
log "CONCLUSION: caller-side -include safe_c.h is compile-time macro mapping;"
log "            NO rebuild of the precompiled C library is needed; ABI unchanged."
/bin/cat "$LOG"

if [ "$fails" -eq 0 ]; then
    printf 'stdc_precompiled: ALL CHECKS PASSED\n'
else
    printf 'stdc_precompiled: %d failure(s) (see %s)\n' "$fails" "$LOG" >&2
fi
exit "$fails"

#!/bin/sh
# Build the small plain-C library ONCE with /usr/bin/clang and record its
# sha256.  A second invocation (without --force) reuses the saved bytes and
# refuses to recompile: the archive is the compatibility baseline that the two
# caller variants must both link, unchanged.
#
# Ownership: reads only the C source in tests/abi/precompiled_safe_layer/ and
# writes only to tests/abi/precompiled_safe_layer/artifacts/.
set -u

export PATH=/usr/bin:/bin
CC=/usr/bin/clang
AR=/usr/bin/ar
SHA=/usr/bin/shasum

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
HERE="$ROOT/tests/abi/precompiled_safe_layer"
OUT="$HERE/artifacts"
mkdir -p "$OUT"
LOG="$OUT/lib_build.log"

FORCE=0
for arg in "$@"; do
    case "$arg" in
        --force) FORCE=1 ;;
        *) printf 'build_lib_once.sh: unknown argument: %s\n' "$arg" >&2; exit 2 ;;
    esac
done

sha256_of() {
    [ -f "$1" ] && "$SHA" -a 256 "$1" | /usr/bin/awk '{print $1}' || printf 'MISSING'
}

if [ "$FORCE" -eq 0 ] && [ -f "$OUT/libsafe_layer.o" ] && \
   [ -f "$OUT/libsafe_layer.a" ] && [ -f "$OUT/lib.sha256" ]; then
    printf 'precompiled library present; REUSING saved archive (no compile).\n'
    printf 'libsafe_layer.a sha256=%s\n' "$(sha256_of "$OUT/libsafe_layer.a")"
    printf 'baseline recorded in %s\n' "$OUT/lib.sha256"
    exit 0
fi

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

log "=== precompiled C library build (ONCE) ==="
log "date_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "PATH=$PATH"
log "arch=$(uname -m) os=$(uname -s)"
log "--- clang --version ---"; "$CC" --version >> "$LOG" 2>&1
log "========================================="

if ! run lib_compile /dev/null \
     "$CC" -O2 -std=c11 -Wall -Wextra -pedantic -I"$HERE" \
     -c "$HERE/safe_layer_lib.c" -o "$OUT/libsafe_layer.o"; then
    printf 'FAIL lib_compile (see %s)\n' "$LOG" >&2; exit 1
fi

if ! run lib_archive /dev/null \
     "$AR" rcs "$OUT/libsafe_layer.a" "$OUT/libsafe_layer.o"; then
    printf 'FAIL lib_archive (see %s)\n' "$LOG" >&2; exit 1
fi

: > "$OUT/lib.sha256"
for bin in libsafe_layer.o libsafe_layer.a; do
    digest=$(sha256_of "$OUT/$bin")
    printf '%s  %s\n' "$digest" "$bin" >> "$OUT/lib.sha256"
    log "SHA256[$bin]=$digest"
done
log "RESULT: C library built once; baseline in $OUT/lib.sha256"
printf 'precompiled library built once; baseline in %s\n' "$OUT/lib.sha256"
/bin/cat "$OUT/lib.sha256"
exit 0

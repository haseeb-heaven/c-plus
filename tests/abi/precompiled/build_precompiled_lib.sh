#!/bin/sh
# Build the small C compatibility library ONCE with /usr/bin/clang.
#
# Ownership: writes ONLY to build/abi-precompiled/ (generated, not committed).
# The single object precomp_lib.o is reused to create BOTH the static archive
# and the shared dylib, so the C source is compiled exactly once.  A subsequent
# invocation without --force reuses the saved binaries and must not rebuild.
set -u

# Restricted, auditable tool PATH.  (No C+ driver is used in this script.)
export PATH=/usr/bin:/bin
export CPLUS_CLANG=/usr/bin/clang
CC=/usr/bin/clang
AR=/usr/bin/ar

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT="$ROOT/build/abi-precompiled"
mkdir -p "$OUT"
LOG="$OUT/lib_build.log"
FORCE=0
for precomp_arg in "$@"; do
    case "$precomp_arg" in
        --force) FORCE=1 ;;
        *) printf 'build_precompiled_lib.sh: unknown argument: %s\n' "$precomp_arg" >&2
           exit 2 ;;
    esac
done

sha256_of() {
    if [ -f "$1" ]; then
        /usr/bin/shasum -a 256 "$1" | /usr/bin/awk '{print $1}'
    else
        printf 'MISSING'
    fi
}

if [ "$FORCE" -eq 0 ] && [ -f "$OUT/precomp_lib.o" ] && \
   [ -f "$OUT/libprecomp.a" ] && [ -f "$OUT/libprecomp.dylib" ] && \
   [ -f "$OUT/lib.sha256" ]; then
    printf 'precompiled library already present; REUSING saved binaries (no compile).\n'
    printf 'libprecomp.a    sha256=%s\n' "$(sha256_of "$OUT/libprecomp.a")"
    printf 'libprecomp.dylib sha256=%s\n' "$(sha256_of "$OUT/libprecomp.dylib")"
    printf 'recorded baselines in %s\n' "$OUT/lib.sha256"
    exit 0
fi

: > "$LOG"
log() { printf '%s\n' "$*" >> "$LOG"; }

LOG_STDOUT="$OUT/_lib_current.stdout"
LOG_STDERR="$OUT/_lib_current.stderr"
run() {
    precomp_label=$1
    precomp_outfile=$2
    shift 2
    log "CMD[$precomp_label]: $*"
    "$@" > "$precomp_outfile" 2> "$LOG_STDERR"
    precomp_rc=$?
    log "RC[$precomp_label]=$precomp_rc"
    log "--- stdout[$precomp_label] ---"
    /bin/cat "$precomp_outfile" >> "$LOG"
    log "--- stderr[$precomp_label] ---"
    /bin/cat "$LOG_STDERR" >> "$LOG"
    return "$precomp_rc"
}

log "=== precompiled C library build (ONCE) ==="
log "date_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "PATH=$PATH"
log "arch=$(uname -m) os=$(uname -s)"
log "--- clang --version ---"
"$CC" --version >> "$LOG" 2>&1
log "========================================="

PRECOMP_INC="-I$ROOT/tests/abi/precompiled"

if ! run lib_compile /dev/null \
     "$CC" -O2 -std=c11 -Wall -Wextra -pedantic $PRECOMP_INC \
     -c "$ROOT/tests/abi/precompiled/precomp_lib.c" -o "$OUT/precomp_lib.o"; then
    printf 'FAIL lib_compile (see %s)\n' "$LOG" >&2
    exit 1
fi

if ! run lib_archive /dev/null \
     "$AR" rcs "$OUT/libprecomp.a" "$OUT/precomp_lib.o"; then
    printf 'FAIL lib_archive (see %s)\n' "$LOG" >&2
    exit 1
fi

if ! run lib_dylib /dev/null \
     "$CC" -dynamiclib -install_name @rpath/libprecomp.dylib \
     "$OUT/precomp_lib.o" -o "$OUT/libprecomp.dylib"; then
    printf 'FAIL lib_dylib (see %s)\n' "$LOG" >&2
    exit 1
fi

# Record exact sha256 of every saved binary (the compatibility baseline).
: > "$OUT/lib.sha256"
for precomp_bin in precomp_lib.o libprecomp.a libprecomp.dylib; do
    precomp_digest=$(sha256_of "$OUT/$precomp_bin")
    printf '%s  %s\n' "$precomp_digest" "$precomp_bin" >> "$OUT/lib.sha256"
    log "SHA256[$precomp_bin]=$precomp_digest"
done

log "RESULT: precompiled library built once; baselines in $OUT/lib.sha256"
printf 'precompiled library built once; baselines in %s\n' "$OUT/lib.sha256"
/bin/cat "$OUT/lib.sha256"
exit 0

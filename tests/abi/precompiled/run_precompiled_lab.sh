#!/bin/sh
# Top-level driver for the precompiled-library compatibility lab.
#
# Sequence (the library is compiled ONCE, then only the C+ app is rebuilt):
#   1. sh build_precompiled_lib.sh            -> build/abi-precompiled/libprecomp.a + .dylib
#   2. sh build_app_against_precompiled.sh     -> static + shared C+ apps, linked to saved binaries
#   3. sh rebuild_app_after_stdlib_change.sh   -> rebuild ONLY the C+ app; library sha256 unchanged
#
# Ownership: writes ONLY to build/abi-precompiled/ (generated, not committed).
# No source outside tests/abi/precompiled/ is created or modified.
set -u

export PATH=/usr/bin:/bin
export CPLUS_CLANG=/usr/bin/clang

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
OUT="$ROOT/build/abi-precompiled"
mkdir -p "$OUT"
LOG="$OUT/results.log"
: > "$LOG"

log() { printf '%s\n' "$*" >> "$LOG"; }

log "=== precompiled library compatibility lab ==="
log "date_utc=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "PATH=$PATH"
log "CPLUS_CLANG=$CPLUS_CLANG"
log "arch=$(uname -m) os=$(uname -s)"
log "--- clang --version ---"
/usr/bin/clang --version >> "$LOG" 2>&1
log "--- c+ --version ---"
./build/c+ --version >> "$LOG" 2>&1
log "==============================================="

PASS=0
FAIL=0
step() {
    precomp_label=$1
    shift
    log "CMD[$precomp_label]: $*"
    "$@" >> "$LOG" 2>&1
    precomp_rc=$?
    log "RC[$precomp_label]=$precomp_rc"
    if [ "$precomp_rc" -eq 0 ]; then
        PASS=$((PASS + 1))
        printf 'PASS %s\n' "$precomp_label"
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL %s (rc=%d)\n' "$precomp_label" "$precomp_rc"
    fi
}

step build_library_once sh tests/abi/precompiled/build_precompiled_lib.sh
step build_and_test_app sh tests/abi/precompiled/build_app_against_precompiled.sh --nonce 0 --mode all
step rebuild_only_app_after_stdlib_change sh tests/abi/precompiled/rebuild_app_after_stdlib_change.sh

log "--- saved binary sha256 baseline ($OUT/lib.sha256) ---"
/bin/cat "$OUT/lib.sha256" >> "$LOG"
log "--- per-stage logs ---"
log "library: $OUT/lib_build.log"
log "app:     $OUT/app_build.log"
log "rebuild: $OUT/rebuild.log"
log "RESULT: precompiled_lab: $PASS passed, $FAIL failed"
printf 'precompiled_lab: %d passed, %d failed\n' "$PASS" "$FAIL"
printf 'Full command/exit-code transcript: %s\n' "$LOG"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
exit 0

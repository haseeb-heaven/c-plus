#!/bin/sh
# Build and run the C+ application against the ALREADY-BUILT precompiled C
# library.  This script NEVER compiles precomp_lib.c and NEVER re-creates the
# archive/dylib: it only invokes the C+ driver on precomp_app.cp and links the
# saved binary.  It refuses to run if the saved binaries do not match the
# sha256 baseline recorded by build_precompiled_lib.sh.
#
# Usage: build_app_against_precompiled.sh [--nonce N] [--mode static|shared|all]
set -u

export PATH=/usr/bin:/bin
export CPLUS_CLANG=/usr/bin/clang

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS="$ROOT/build/c+"
OUT="$ROOT/build/abi-precompiled"
LOG="$OUT/app_build.log"
NONCE=0
MODE=all
while [ "$#" -gt 0 ]; do
    case "$1" in
        --nonce)
            shift
            [ "$#" -gt 0 ] || { printf 'missing nonce\n' >&2; exit 2; }
            NONCE=$1
            ;;
        --mode)
            shift
            [ "$#" -gt 0 ] || { printf 'missing mode\n' >&2; exit 2; }
            MODE=$1
            ;;
        *)
            printf 'build_app_against_precompiled.sh: unknown argument: %s\n' "$1" >&2
            exit 2
            ;;
    esac
    shift
done
case "$MODE" in
    static|shared|all) ;;
    *) printf 'invalid --mode: %s\n' "$MODE" >&2; exit 2 ;;
esac

if [ ! -x "$CPLUS" ]; then
    printf 'C+ driver missing: %s\n' "$CPLUS" >&2
    exit 2
fi
if [ ! -f "$OUT/libprecomp.a" ] || [ ! -f "$OUT/libprecomp.dylib" ] || \
   [ ! -f "$OUT/lib.sha256" ]; then
    printf 'precompiled library missing; run build_precompiled_lib.sh first\n' >&2
    exit 2
fi

sha256_of() {
    /usr/bin/shasum -a 256 "$1" | /usr/bin/awk '{print $1}'
}
baseline_a=$(/usr/bin/awk '$2=="libprecomp.a"{print $1}' "$OUT/lib.sha256")
baseline_d=$(/usr/bin/awk '$2=="libprecomp.dylib"{print $1}' "$OUT/lib.sha256")
current_a=$(sha256_of "$OUT/libprecomp.a")
current_d=$(sha256_of "$OUT/libprecomp.dylib")
if [ "$baseline_a" != "$current_a" ] || [ "$baseline_d" != "$current_d" ]; then
    printf 'PRECOMPILED LIBRARY SHA256 MISMATCH; refusing to pretend it is unchanged\n' >&2
    printf '  libprecomp.a baseline=%s current=%s\n' "$baseline_a" "$current_a" >&2
    printf '  libprecomp.dylib baseline=%s current=%s\n' "$baseline_d" "$current_d" >&2
    exit 3
fi

mkdir -p "$OUT"
{
    printf '\n=== C+ app build against precompiled library ===\n'
    printf 'date_utc=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf 'nonce=%s mode=%s\n' "$NONCE" "$MODE"
    printf 'baseline libprecomp.a=%s\n' "$baseline_a"
    printf 'baseline libprecomp.dylib=%s\n' "$baseline_d"
} >> "$LOG"

log() { printf '%s\n' "$*" >> "$LOG"; }

run() {
    precomp_label=$1
    precomp_outfile=$2
    shift 2
    log "CMD[$precomp_label]: $*"
    "$@" > "$precomp_outfile" 2> "$OUT/_app_current.stderr"
    precomp_rc=$?
    log "RC[$precomp_label]=$precomp_rc"
    log "--- stdout[$precomp_label] -> $precomp_outfile ---"
    /bin/cat "$precomp_outfile" >> "$LOG"
    log "--- stderr[$precomp_label] ---"
    /bin/cat "$OUT/_app_current.stderr" >> "$LOG"
    return "$precomp_rc"
}

PASS=0
FAIL=0
check_final() {
    precomp_label=$1
    precomp_file=$2
    log "CHECK[$precomp_label]: grep -F 'PRECOMP_APP ' $precomp_file"
    if /usr/bin/grep -Fq "PRECOMP_APP " "$precomp_file" && \
       /usr/bin/grep -Eq '^PRECOMP_APP [0-9]+ 0$' "$precomp_file"; then
        PASS=$((PASS + 1))
        printf 'PASS %s\n' "$precomp_label"
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL %s (final PRECOMP_APP line missing or nonzero failures)\n' "$precomp_label"
    fi
}

INC_FLAG="-I$ROOT/tests/abi/precompiled"
NONCE_FLAG="-DPRECOMP_REBUILD_NONCE=$NONCE"
SUPPRESS="-Wno-unused-command-line-argument"

if [ "$MODE" = static ] || [ "$MODE" = all ]; then
    if ! run app_static_build /dev/null \
         "$CPLUS" build --sanitize=none \
         "$ROOT/tests/abi/precompiled/precomp_app.cp" -o "$OUT/app_static" -- \
         "$OUT/libprecomp.a" "$INC_FLAG" "$NONCE_FLAG" "$SUPPRESS"; then
        printf 'FAIL app_static_build (see %s)\n' "$LOG" >&2
        FAIL=$((FAIL + 1))
    else
        PASS=$((PASS + 1))
        printf 'PASS app_static_build\n'
    fi
    if [ -x "$OUT/app_static" ]; then
        if run app_static_run "$OUT/app_static.stdout" "$OUT/app_static"; then
            PASS=$((PASS + 1))
            printf 'PASS app_static_run (rc=0)\n'
        else
            FAIL=$((FAIL + 1))
            printf 'FAIL app_static_run (nonzero rc)\n'
        fi
        check_final app_static_summary "$OUT/app_static.stdout"
    fi
fi

if [ "$MODE" = shared ] || [ "$MODE" = all ]; then
    if ! run app_shared_build /dev/null \
         "$CPLUS" build --sanitize=none \
         "$ROOT/tests/abi/precompiled/precomp_app.cp" -o "$OUT/app_shared" -- \
         "$OUT/libprecomp.dylib" -Wl,-rpath,@loader_path \
         "$INC_FLAG" "$NONCE_FLAG" "$SUPPRESS"; then
        printf 'FAIL app_shared_build (see %s)\n' "$LOG" >&2
        FAIL=$((FAIL + 1))
    else
        PASS=$((PASS + 1))
        printf 'PASS app_shared_build\n'
    fi
    if [ -x "$OUT/app_shared" ]; then
        if run app_shared_run "$OUT/app_shared.stdout" "$OUT/app_shared"; then
            PASS=$((PASS + 1))
            printf 'PASS app_shared_run (rc=0)\n'
        else
            FAIL=$((FAIL + 1))
            printf 'FAIL app_shared_run (nonzero rc)\n'
        fi
        check_final app_shared_summary "$OUT/app_shared.stdout"
        log "--- otool -L app_shared (linked dylib proof) ---"
        /usr/bin/otool -L "$OUT/app_shared" >> "$LOG" 2>&1
    fi
fi

log "RESULT: app build/run: $PASS passed, $FAIL failed"
printf 'app build/run: %d passed, %d failed\n' "$PASS" "$FAIL"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
exit 0

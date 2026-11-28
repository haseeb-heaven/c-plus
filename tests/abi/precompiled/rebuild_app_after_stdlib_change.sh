#!/bin/sh
# Simulate the workflow after a C+ stdlib source change: rebuild ONLY the C+
# application and keep using the SAVED precompiled C library binary.
#
# This script deliberately does NOT call build_precompiled_lib.sh and does NOT
# invoke /usr/bin/clang on precomp_lib.c or /usr/bin/ar.  It proves that by
# (a) checking the library sha256 against the recorded baseline before and
# after the rebuild and (b) passing a fresh nonce into the C+ build so the new
# app binary/stdout is observably different while the library is byte-identical.
#
# Usage: rebuild_app_after_stdlib_change.sh [--nonce N]
set -u

export PATH=/usr/bin:/bin
export CPLUS_CLANG=/usr/bin/clang

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT="$ROOT/build/abi-precompiled"
LOG="$OUT/rebuild.log"
APP_SCRIPT="$ROOT/tests/abi/precompiled/build_app_against_precompiled.sh"

if [ ! -f "$OUT/lib.sha256" ]; then
    printf 'no sha256 baseline; run build_precompiled_lib.sh first\n' >&2
    exit 2
fi

NONCE_FILE="$OUT/last_nonce"
if [ "$#" -gt 0 ]; then
    if [ "$1" != "--nonce" ] || [ "$#" -ne 2 ]; then
        printf 'usage: rebuild_app_after_stdlib_change.sh [--nonce N]\n' >&2
        exit 2
    fi
    NONCE=$2
elif [ -f "$NONCE_FILE" ]; then
    precomp_prev=$(/bin/cat "$NONCE_FILE")
    case "$precomp_prev" in
        ''|*[!0-9]*) NONCE=1 ;;
        *) NONCE=$((precomp_prev + 1)) ;;
    esac
else
    NONCE=1
fi
printf '%s\n' "$NONCE" > "$NONCE_FILE"

sha256_of() {
    /usr/bin/shasum -a 256 "$1" | /usr/bin/awk '{print $1}'
}
baseline_a=$(/usr/bin/awk '$2=="libprecomp.a"{print $1}' "$OUT/lib.sha256")
baseline_d=$(/usr/bin/awk '$2=="libprecomp.dylib"{print $1}' "$OUT/lib.sha256")
before_a=$(sha256_of "$OUT/libprecomp.a")
before_d=$(sha256_of "$OUT/libprecomp.dylib")

{
    printf '\n=== rebuild after stdlib source change (nonce=%s) ===\n' "$NONCE"
    printf 'date_utc=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf 'SKIPPED (library NOT rebuilt): /usr/bin/clang -O2 -c precomp_lib.c ...\n'
    printf 'SKIPPED (library NOT rebuilt): /usr/bin/ar rcs libprecomp.a ...\n'
    printf 'SKIPPED (library NOT rebuilt): /usr/bin/clang -dynamiclib ...\n'
    printf 'ONLY build step: sh %s --nonce %s --mode all\n' "$APP_SCRIPT" "$NONCE"
    printf 'sha256 libprecomp.a before=%s (baseline=%s)\n' "$before_a" "$baseline_a"
    printf 'sha256 libprecomp.dylib before=%s (baseline=%s)\n' "$before_d" "$baseline_d"
} >> "$LOG"

PASS=0
FAIL=0
if [ "$before_a" = "$baseline_a" ] && [ "$before_d" = "$baseline_d" ]; then
    PASS=$((PASS + 1))
    printf 'PASS library_matches_baseline_before\n'
else
    FAIL=$((FAIL + 1))
    printf 'FAIL library_matches_baseline_before\n'
fi

sh "$APP_SCRIPT" --nonce "$NONCE" --mode all
app_rc=$?
printf 'app rebuild rc=%d\n' "$app_rc" >> "$LOG"
if [ "$app_rc" -eq 0 ]; then
    PASS=$((PASS + 1))
    printf 'PASS app_rebuild_rc0\n'
else
    FAIL=$((FAIL + 1))
    printf 'FAIL app_rebuild_rc0 (rc=%d)\n' "$app_rc"
fi

after_a=$(sha256_of "$OUT/libprecomp.a")
after_d=$(sha256_of "$OUT/libprecomp.dylib")
printf 'sha256 libprecomp.a after=%s\n' "$after_a" >> "$LOG"
printf 'sha256 libprecomp.dylib after=%s\n' "$after_d" >> "$LOG"
if [ "$after_a" = "$baseline_a" ] && [ "$after_d" = "$baseline_d" ] && \
   [ "$after_a" = "$before_a" ] && [ "$after_d" = "$before_d" ]; then
    PASS=$((PASS + 1))
    printf 'PASS library_byte_identical_after_rebuild\n'
else
    FAIL=$((FAIL + 1))
    printf 'FAIL library_byte_identical_after_rebuild\n'
fi

for precomp_mode in static shared; do
    precomp_out="$OUT/app_${precomp_mode}.stdout"
    if /usr/bin/grep -Fq "precomp_app: nonce=$NONCE" "$precomp_out" && \
       /usr/bin/grep -Eq '^PRECOMP_APP [0-9]+ 0$' "$precomp_out"; then
        PASS=$((PASS + 1))
        printf 'PASS fresh_nonce_and_summary_%s\n' "$precomp_mode"
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL fresh_nonce_and_summary_%s\n' "$precomp_mode"
    fi
done

printf 'RESULT: rebuild: %d passed, %d failed\n' "$PASS" "$FAIL" >> "$LOG"
printf 'rebuild: %d passed, %d failed\n' "$PASS" "$FAIL"
printf 'Rebuild log: %s\n' "$LOG"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
exit 0

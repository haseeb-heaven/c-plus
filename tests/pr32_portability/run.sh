#!/bin/sh
# PR32 libc+ portability runner (POSIX sh only; no interpreter or success-fallback shorthands).
# Runs the local probes, continues after any failure, and always prints:
#   PR32_LIBC <checks> <pass> <fail> <not_tested>
# Bounded by a global 260s timeout when the timeout utility is available.

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
OUT="$HERE/out"
# Pin the host system compiler for reproducibility: an ambient Homebrew clang
# (23.x here) changes SDK/sysroot and duplicate-symbol behaviour. Override with
# CPLUS_CLANG if you deliberately want to test another toolchain.
: "${CPLUS_CLANG:=/usr/bin/clang}"
export CPLUS_CLANG
CHECKS="toolchain install_smoke sdk_consumer link_static link_shared_sources link_shared_consumer asan ubsan tsan mingw_compile"

# ---- global bound: re-exec once under `timeout 60` -----------------------
if [ -z "${PR32_LIBC_WRAPPED:-}" ]; then
    if TMO=$(command -v timeout 2>/dev/null); then
        PR32_LIBC_WRAPPED=1
        export PR32_LIBC_WRAPPED
        printf 'PR32_LIBC_BOUND: global 260s via %s\n' "$TMO" >&2
        exec "$TMO" 260 sh "$0" "$@"
    fi
    printf 'PR32_LIBC_BOUND: WARNING: no timeout utility; 260s global bound NOT enforced\n' >&2
fi

mkdir -p "$OUT"
: > "$OUT/run.log"
for c in $CHECKS; do rm -f "$OUT/$c.status"; done

checks=0
pass=0
fail=0
not_tested=0
summary_done=0

finish() {
    [ "$summary_done" -eq 1 ] && return 0
    summary_done=1
    for c in $CHECKS; do
        sf="$OUT/$c.status"
        checks=$((checks + 1))
        if [ -f "$sf" ]; then
            line=$(sed -n '1p' "$sf")
            state=${line%% *}
            reason=${line#* }
            if [ "$line" = "$state" ]; then reason=""; fi
        else
            state=FAIL
            reason='NO STATUS FILE (probe did not complete)'
        fi
        case "$state" in
            PASS)       pass=$((pass + 1)) ;;
            NOT_TESTED) not_tested=$((not_tested + 1)) ;;
            *)          fail=$((fail + 1)) ;;
        esac
        printf 'PR32_CHECK %s %s %s\n' "$c" "$state" "$reason" | tee -a "$OUT/run.log"
    done
    printf 'PR32_LIBC %d %d %d %d\n' "$checks" "$pass" "$fail" "$not_tested" | tee -a "$OUT/run.log"
}

trap 'finish' 0
trap 'exit 124' 15

run_probe() {
    label=$1
    script=$2
    shift 2
    printf '=== probe %s: $ sh tests/pr32_portability/%s %s ===\n' "$label" "$script" "$*" >> "$OUT/run.log"
    if TMO=$(command -v timeout 2>/dev/null); then
        "$TMO" 150 sh "$HERE/$script" "$@" >> "$OUT/run.log" 2>&1
        rc=$?
    else
        sh "$HERE/$script" "$@" >> "$OUT/run.log" 2>&1
        rc=$?
    fi
    printf 'probe %s exit=%d\n' "$label" "$rc" >> "$OUT/run.log"
}

# Sanitizers first: toolchain.txt summarises their recorded statuses.
run_probe sanitizers    probe_sanitizers.sh
run_probe toolchain     probe_toolchain.sh
run_probe install_smoke probe_smoke.sh install_smoke tests/libc/install_smoke.sh
run_probe sdk_consumer  probe_smoke.sh sdk_consumer  tests/sdk/run_sdk_consumer.sh
run_probe link          probe_link.sh

finish
# Real failures must fail the runner; NOT_TESTED (documented blockers) do not.
exit "$fail"

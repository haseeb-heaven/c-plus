#!/bin/sh
# Aggregate runner for C-standard support testing.
#
# Discovers every immediate subdirectory of tests/c_standards/ that contains a
# run.sh, executes it under a 300-second timeout, and captures/prints the final
# lines of each run.  Pure POSIX shell only: no scripting-language helpers and
# no failure-suppressing no-op fallbacks.
#
# By default full per-suite logs go to a temporary directory that is removed on
# exit.  Set C_STANDARDS_LOG_DIR to a writable path to retain them as evidence.
#
# Usage:
#   sh tests/c_standards/run_all.sh
#   C_STANDARDS_LOG_DIR=tests/c_standards/.logs sh tests/c_standards/run_all.sh
#
# Exit status is 0 when every discovered suite passes, 1 otherwise (including
# when a suite is still PENDING because its run.sh has not landed yet).

set -u

cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)" || exit 1

LOG_DIR=${C_STANDARDS_LOG_DIR:-}
if [ -z "$LOG_DIR" ]; then
    LOG_DIR=$(mktemp -d "${TMPDIR:-/tmp}/c_standards_run.XXXXXX") || exit 1
    CLEAN_LOGS=1
else
    mkdir -p "$LOG_DIR" || exit 1
    CLEAN_LOGS=0
fi
if [ "$CLEAN_LOGS" -eq 1 ]; then
    trap 'rm -rf "$LOG_DIR"' EXIT HUP INT TERM
fi

total=0
passed=0
failed=0
pending=0

for dir in */; do
    dir=${dir%/}
    script="$dir/run.sh"
    log="$LOG_DIR/$dir.log"

    if [ ! -f "$script" ]; then
        printf '%-14s %-8s %s\n' "$dir" PENDING \
            "no $script -- reproduction: sh tests/c_standards/$dir/run.sh"
        pending=$((pending + 1))
        continue
    fi

    total=$((total + 1))
    # Honour each suite's shebang rather than forcing one shell: some suite
    # scripts use bash, and not every one carries the executable bit.  We must
    # not modify sibling files to make this aggregate runner work.
    first_line=$(sed -n '1p' "$script")
    case "$first_line" in
        *bash*) runner=bash ;;
        *)      runner=sh ;;
    esac
    timeout 300 "$runner" "$script" >"$log" 2>&1
    rc=$?

    case "$rc" in
        0)   verdict=PASS ;;
        124) verdict=TIMEOUT ;;
        137) verdict=TIMEOUT ;;
        *)   verdict=FAIL ;;
    esac

    if [ "$verdict" = PASS ]; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi

    printf '\n=== %s [%s] (exit %s, log: %s) ===\n' "$dir" "$verdict" "$rc" "$log"
    if [ -s "$log" ]; then
        tail -n 3 "$log" | sed 's/^/  | /'
    else
        printf '  | (no output)\n'
    fi
done

printf '\n'
printf 'c_standards aggregate: %s suites run, %s passed, %s failed, %s pending\n' \
    "$total" "$passed" "$failed" "$pending"

if [ "$failed" -ne 0 ] || [ "$pending" -ne 0 ]; then
    exit 1
fi
exit 0

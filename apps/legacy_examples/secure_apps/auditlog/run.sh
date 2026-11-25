#!/bin/sh
# Build and run the secure audit-log example once, under a 30-second bound.
#
# The single run exercises the whole deterministic transcript: the fixed
# event sequence, every refusal, the append/re-open path and the read-back.
#
# Final line: SECURE_AUDIT <scenarios> <passed> <failed> <refused>
#   scenarios = total expectations checked by auditlog.c
#   passed    = expectations that held
#   failed    = expectations that did not hold (0 means all good)
#   refused   = subset of passed that were correctly refused hostile inputs
#
# Exit status is 0 only when the process exited 0 and every expectation held.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
cd "$ROOT" || exit 1

DIR=apps/legacy_examples/secure_apps/auditlog
WORK=$(mktemp -d "${TMPDIR:-/tmp}/secure_audit.XXXXXX") || exit 1
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
BIN=$WORK/auditlog
LOG=$WORK/audit.log

if ! /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -pthread \
        -Ilibc/install-include \
        "$DIR/auditlog.c" build/libc/libcplus_hardened.a \
        -o "$BIN" >"$WORK/build.log" 2>&1; then
    echo "BUILD FAIL (compiler output follows)"
    cat "$WORK/build.log"
    exit 1
fi
echo "BUILD ok auditlog"

timeout 30 "$BIN" "$LOG" >"$WORK/run.out" 2>&1
rc=$?
cat "$WORK/run.out"

summary=$(awk '$1 == "RESULT" { print $2, $3, $4, $5; found = 1 }
               END { if (!found) exit 1 }' "$WORK/run.out")
if [ -z "$summary" ]; then
    echo "RUN FAIL: no RESULT line (rc=$rc)"
    exit 1
fi

# shellcheck disable=SC2086
set -- $summary
scenarios=$1
passed=$2
failed=$3
refused=$4

printf 'SECURE_AUDIT %s %s %s %s\n' "$scenarios" "$passed" "$failed" "$refused"

[ "$rc" -eq 0 ] && [ "$failed" -eq 0 ] && [ "$passed" -eq "$scenarios" ] &&
    [ "$refused" -eq 7 ]

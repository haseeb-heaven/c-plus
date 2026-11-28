#!/bin/sh
# Aggregate runner for the modern 3D / GUI / CLI standard-library additions.
#
# This is the single entry point behind docs/stdlib/MODERN_LIBS_3D_GUI_CLI.md.
# It runs all nine backing suites from the repository root:
#
#   5 library suites : tests/stdlib/{math3d,scene3d,mesh3d,widgets,cli}/run.sh
#   3 demo apps      : apps/legacy_examples/{games3d,gui_app,cli_app}/run.sh
#   1 cross suite    : tests/modern_libs_cross/run.sh
#
# Every runner is invoked as:
#
#   timeout 900 sh <path>/run.sh
#
# with PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin,
# CPLUS_CLANG=/usr/bin/clang and LC_ALL=C.  Each runner's stdout and stderr go
# to build/modern_libs/<label>.log.  A missing runner is reported as PENDING
# together with the exact command that could not be run; no result is invented.
#
# Exit status: 0 = all nine runners PASSED.
#              1 = at least one runner FAILed, TIMED OUT or is PENDING.
#              2 = setup error ('timeout' missing, cannot create log directory).
# Execution continues after a failure so the table always covers all nine
# suites.  The runner uses only POSIX shell utilities and never swallows a
# runner's non-zero status.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
cd "$ROOT" || exit 2

export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin
export CPLUS_CLANG=/usr/bin/clang
export LC_ALL=C

LIMIT=900
LOGDIR=build/modern_libs

if ! command -v timeout >/dev/null 2>&1; then
    echo "error: 'timeout' not found in restricted PATH; refusing unbounded runners" >&2
    exit 2
fi
if ! mkdir -p "$LOGDIR"; then
    echo "error: cannot create $LOGDIR" >&2
    exit 2
fi

pass=0; fail=0; timedout=0; pending=0; total=0
bad_labels=

hdr_fmt='%-16s %-44s %-9s %5s  %s\n'
printf "$hdr_fmt" SUITE RUNNER STATUS RC LOG
printf "$hdr_fmt" ---------------- -------------------------------------------- --------- ----- ---

# The here-doc keeps the loop (and its counters) in this shell, not a sub-shell.
# Runner stdin is /dev/null so a sibling script cannot consume the here-doc.
while IFS='|' read -r label rel; do
    [ -n "$label" ] || continue
    total=$((total + 1))
    log="$LOGDIR/$label.log"

    if [ ! -f "$rel" ]; then
        pending=$((pending + 1))
        printf "$hdr_fmt" "$label" "$rel" PENDING - "MISSING (no log)"
        printf '  PENDING command: timeout %s sh %s\n' "$LIMIT" "$rel"
        continue
    fi

    if timeout "$LIMIT" sh "$rel" </dev/null >"$log" 2>&1; then
        rc=0
        status=PASS
        pass=$((pass + 1))
    else
        rc=$?
        # GNU timeout: 124 = timed out, 137 = killed after --kill-after.
        bad_labels="$bad_labels $label"
        if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
            status=TIMEOUT
            timedout=$((timedout + 1))
        else
            status=FAIL
            fail=$((fail + 1))
        fi
    fi
    printf "$hdr_fmt" "$label" "$rel" "$status" "$rc" "$log"
done <<EOF
math3d|tests/stdlib/math3d/run.sh
scene3d|tests/stdlib/scene3d/run.sh
mesh3d|tests/stdlib/mesh3d/run.sh
widgets|tests/stdlib/widgets/run.sh
cli|tests/stdlib/cli/run.sh
demo_games3d|apps/games3d/run.sh
demo_gui_app|apps/gui_app/run.sh
demo_cli_app|apps/cli_app/run.sh
cross|tests/modern_libs_cross/run.sh
EOF

printf "$hdr_fmt" TOTALS - - - "total=$total pass=$pass fail=$fail timeout=$timedout pending=$pending"
echo "TOTALS: total=$total pass=$pass fail=$fail timeout=$timedout pending=$pending"
if [ "$pass" -eq "$total" ] && [ "$total" -gt 0 ]; then
    echo "modern_libs aggregate: PASS"
else
    echo "modern_libs aggregate: INCOMPLETE (PENDING counts as not verified)"
fi

# Show the tail of each non-PASS log so a single table line is never the only
# evidence for a failure.  Logs are plain text; no post-processing tool needed.
if [ -n "$bad_labels" ]; then
    echo
    echo "FAILURE / TIMEOUT DETAIL (last 8 lines per log):"
    for label in $bad_labels; do
        log="$LOGDIR/$label.log"
        [ -f "$log" ] || continue
        echo "--- $label ($log) ---"
        tail -n 8 "$log"
    done
fi

if [ "$pass" -eq "$total" ] && [ "$total" -gt 0 ]; then
    exit 0
fi
exit 1

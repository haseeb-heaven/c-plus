#!/bin/sh
# ADV1000 / thread_async deterministic runner.
# Builds with the exact prescribed command, runs under `timeout 120`, writes
# the 1000 scenario lines plus digest/summary to results/scenarios.log, and
# records every exact command and result in results/commands.txt.
# No network, no wall-clock assertions, no live-game or injected-DLL claims.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
HERE="$ROOT/tests/adversarial_1000/thread_async"
LOGDIR="$HERE/results"
mkdir -p "$LOGDIR"

BUILD_CMD="./build/c+ build tests/adversarial_1000/thread_async/adv1000_thread_async.cp --strict --sanitize=none -I/opt/homebrew/include -L/opt/homebrew/lib -luv -o tests/adversarial_1000/thread_async/adv1000_thread_async.bin"
RUN_CMD="timeout 120 ./tests/adversarial_1000/thread_async/adv1000_thread_async.bin"

echo "+ $BUILD_CMD" > "$LOGDIR/commands.txt"
cd "$ROOT" || exit 1

$BUILD_CMD > "$LOGDIR/build.stdout" 2> "$LOGDIR/build.stderr"
BR=$?
echo "build_rc=$BR" >> "$LOGDIR/commands.txt"
if [ "$BR" -ne 0 ]; then
    echo "ADV1000 thread_async: BUILD FAILED rc=$BR" >&2
    cat "$LOGDIR/build.stderr" >&2
    exit "$BR"
fi

echo "+ $RUN_CMD" >> "$LOGDIR/commands.txt"
$RUN_CMD > "$LOGDIR/scenarios.log" 2> "$LOGDIR/run.stderr"
RR=$?
echo "run_rc=$RR" >> "$LOGDIR/commands.txt"

# sha256 digest of the full machine-readable scenario log.
shasum -a 256 "$LOGDIR/scenarios.log" > "$LOGDIR/scenarios.sha256"
DIGEST=$(awk '{print $1}' "$LOGDIR/scenarios.sha256")
echo "scenarios_sha256=$DIGEST" >> "$LOGDIR/commands.txt"

SCEN=$(awk '/^S /{n++} END{print n+0}' "$LOGDIR/scenarios.log")
FINAL=$(grep '^ADV1000 thread_async ' "$LOGDIR/scenarios.log" | tail -1)
FNV=$(grep '^ADV1000_DIGEST ' "$LOGDIR/scenarios.log" | tail -1 | awk '{print $2}')
echo "scenario_lines=$SCEN" >> "$LOGDIR/commands.txt"
echo "final=$FINAL" >> "$LOGDIR/commands.txt"
echo "fnv_digest=$FNV" >> "$LOGDIR/commands.txt"

echo "ADV1000 thread_async: build_rc=$BR run_rc=$RR scenario_lines=$SCEN"
echo "$FINAL"
echo "FNV=$FNV sha256=$DIGEST"
exit "$RR"

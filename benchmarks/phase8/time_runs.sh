#!/bin/sh
# phase8 wall-clock harness: 1 untimed warmup per binary, then N interleaved
# measured rounds (round-robin) using `date +%s%N` nanosecond timestamps.
# stdout is CSV: round,label,elapsed_ns,exit,stdout_first_line
# All captured stdout is also kept under benchmarks/phase8/logs/.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
HERE="$ROOT/benchmarks/phase8"
RUNS=${1:?usage: time_runs.sh RUNS label=path [label=path ...]}
shift
mkdir -p "$HERE/logs"
case "$RUNS" in ''|*[!0-9]*) echo 'error: RUNS must be an integer' >&2; exit 2 ;; esac
[ "$RUNS" -ge 7 ] || { echo 'error: RUNS must be >= 7' >&2; exit 2; }

# Validate specs and warm up each binary once (untimed).
for spec in "$@"; do
    label=${spec%%=*}
    bin=${spec#*=}
    [ "$label" != "$spec" ] || { echo "error: bad spec '$spec'" >&2; exit 2; }
    [ -x "$bin" ] || { echo "error: not executable: $bin" >&2; exit 2; }
    if ! "$bin" > "$HERE/logs/${label}.warm.out" 2> "$HERE/logs/${label}.warm.err"; then
        echo "error: warmup failed for $label (rc=$?)" >&2
        cat "$HERE/logs/${label}.warm.err" >&2
        exit 1
    fi
done

round=1
while [ "$round" -le "$RUNS" ]; do
    for spec in "$@"; do
        label=${spec%%=*}
        bin=${spec#*=}
        out="$HERE/logs/${label}.run.$(printf '%02d' "$round").out"
        err="$HERE/logs/${label}.run.$(printf '%02d' "$round").err"
        t0=$(date +%s%N)
        if "$bin" > "$out" 2> "$err"; then rc=0; else rc=$?; fi
        t1=$(date +%s%N)
        first=$(head -n 1 "$out")
        printf '%s,%s,%s,%s,%s\n' "$round" "$label" "$((t1 - t0))" "$rc" "$first"
    done
    round=$((round + 1))
done

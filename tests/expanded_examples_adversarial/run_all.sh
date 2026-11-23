#!/bin/sh
# Master adversarial driver for all examples/expanded categories.
#
# Every individual build and every individual program invocation is bounded by
# `timeout 120` (see common.sh).  This script aggregates the per-category
# output, recomputes the totals from the per-case lines (never from a
# self-reported summary), and fails if any case crashed or was unexpectedly
# accepted, or if any example could not be built.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
mkdir -p "$HERE/logs" || exit 1
# Per-invocation scratch directory. Two concurrent run_all.sh invocations used
# to share logs/adv_all.log and logs/<cat>.driver.out, so one would truncate the
# other's output mid-run: a run could report a partial TOTAL (observed: 59 of
# 110 cases) and still exit 0. Keep all mutable aggregation state private to
# this PID; refresh the canonical evidence snapshots only at the end.
RUNID=$$
RUNDIR="$HERE/logs/.run.$RUNID"
mkdir -p "$RUNDIR" || exit 1
ADV_BIN="$RUNDIR/bin"
ADV_LOGS="$RUNDIR/logs"
ADV_FIX="$RUNDIR/fixtures"
export ADV_BIN ADV_LOGS ADV_FIX
ALL="$RUNDIR/adv_all.log"
: > "$ALL"

overall=0
for cat in cli games database engine safety; do
    catlog="$RUNDIR/$cat.driver.out"
    sh "$HERE/$cat.sh" >"$catlog" 2>&1
    crc=$?
    cat "$catlog" >> "$ALL"
    if [ "$crc" -ne 0 ]; then
        overall=1
        echo "ADV_CATEGORY_FAIL $cat rc=$crc" >&2
    fi
done

# Echo the complete, ordered per-case evidence.
cat "$ALL"

totals=$(awk '
    /^ADV_EXPANDED / {
        cases++
        if ($4 == "REFUSED")       refused++
        else if ($4 == "CRASHED")  crashed++
        else if ($4 == "UNEXPECTED_OK") unexpected++
    }
    END { printf "%d %d %d %d", cases+0, refused+0, crashed+0, unexpected+0 }
' "$ALL")
set -- $totals
cases=$1; refused=$2; crashed=$3; unexpected=$4

printf 'ADV_EXPANDED_TOTAL %s %s %s %s\n' \
    "$cases" "$refused" "$crashed" "$unexpected"

if grep -q '^ADV_BUILD_FAIL ' "$ALL"; then
    overall=1
fi
if [ "$crashed" -ne 0 ] || [ "$unexpected" -ne 0 ]; then
    overall=1
fi
if [ "$cases" -eq 0 ]; then
    echo "ADV_NO_CASES zero cases executed is never a pass" >&2
    overall=1
fi

# Refresh canonical evidence snapshots (this script's own stdout and the
# caller's captured runN.out remain the authoritative per-run record).
cat "$ALL" > "$HERE/logs/adv_all.log" 2>/dev/null || :
for cat in cli games database engine safety; do
    cat "$RUNDIR/$cat.driver.out" > "$HERE/logs/$cat.driver.out" 2>/dev/null || :
done
# Copy raw per-case outputs and build logs back to the canonical logs/ dir.
cp -R "$ADV_LOGS/." "$HERE/logs/" 2>/dev/null || :
rm -rf "$RUNDIR" 2>/dev/null || :

exit "$overall"

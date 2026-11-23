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
ALL="$HERE/logs/adv_all.log"
: > "$ALL"

overall=0
for cat in cli games database engine safety; do
    catlog="$HERE/logs/$cat.driver.out"
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

exit "$overall"

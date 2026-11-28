#!/bin/sh
# Verify every committed screenshot and per-app page exists and is non-empty.
# Prints: SCREENSHOTS <cases> <pass> <fail>.
set -u
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
pass=0; fail=0; cases=0
for f in "$D"/real/*.png "$D"/cli_transcripts/*.svg; do
    cases=$((cases + 1))
    if [ -s "$f" ] && grep -q '<svg' "$f" || file "$f" | grep -q 'PNG image data'; then pass=$((pass + 1));
    else fail=$((fail + 1)); echo "SCREENSHOTS FAIL $f"; fi
done
for f in "$D"/*.md; do
    cases=$((cases + 1))
    if [ -s "$f" ]; then pass=$((pass + 1));
    else fail=$((fail + 1)); echo "SCREENSHOTS FAIL $f"; fi
done
echo "SCREENSHOTS $cases $pass $fail"
[ "$fail" -eq 0 ]

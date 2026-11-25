#!/bin/sh
# Build and run the 1000-scenario net/http adversarial harness.
# Usage: sh tests/adversarial_1000/net_http/run.sh
# All generated files live in tests/adversarial_1000/net_http/results/ (gitignored).

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
RES=tests/adversarial_1000/net_http/results
BIN="$RES/adv1000_net_http"
SRC=tests/adversarial_1000/net_http/adv1000_net_http.cp
mkdir -p "$RES"

echo "+ timeout 120 ./build/c+ build $SRC --default --no-werror --sanitize=none -lcurl -o $BIN"
timeout 120 ./build/c+ build "$SRC" --default --no-werror --sanitize=none -lcurl -o "$BIN" \
    > "$RES/build.log" 2>&1
brc=$?
echo "  build rc=$brc"
if [ "$brc" -ne 0 ]; then
    tail -n 20 "$RES/build.log"
    exit "$brc"
fi

echo "+ timeout 120 $BIN"
timeout 120 "$BIN" > "$RES/stdout.log" 2> "$RES/stderr.log"
rrc=$?
echo "  run rc=$rrc"

shasum -a 256 "$RES/scenarios.log" > "$RES/digest.sha256"
echo "+ shasum -a 256 $RES/scenarios.log"
cat "$RES/digest.sha256"
grep -E '^(ADV1000|ADV1000_DIGEST)' "$RES/stdout.log"
echo "  scenarios.log lines: $(wc -l < "$RES/scenarios.log" | tr -d ' ')"
echo "  stderr lines: $(wc -l < "$RES/stderr.log" | tr -d ' ')"
exit "$rrc"

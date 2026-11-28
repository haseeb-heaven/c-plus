#!/bin/sh
# Build and verify the vendored olive.c still-frame demo.
# Uses the C+ compiler when available (CPLUS_BIN override), else plain clang.
# Prints: OLIVE_DEMO PASS compiler=<c+|clang> bytes=<n> sha256=<hex>
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$D"
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
OUT="$D/out"
mkdir -p "$OUT"
ROOT=$(CDPATH= cd -- "$D/../../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
BIN="$OUT/triangle_demo"
rm -f "$BIN" "$OUT/frame.ppm" "$OUT/run.out" "$OUT/run.err"

if [ -x "$CPLUS" ]; then
    COMPILER=c+
    "$CPLUS" build "$D/triangle_demo.c" --legacy-c --sanitize=none -std=c11 \
        -I"$D" -o "$BIN" >"$OUT/build.log" 2>&1 || {
        echo "OLIVE_DEMO FAIL build (c+)"; cat "$OUT/build.log"; exit 1; }
else
    COMPILER=clang
    CC=${CC:-/usr/bin/clang}
    if [ ! -x "$CC" ]; then CC=clang; fi
    "$CC" -std=c11 -O2 -Wall -Wextra -Werror -I"$D" "$D/triangle_demo.c" \
        -o "$BIN" >"$OUT/build.log" 2>&1 || {
        echo "OLIVE_DEMO FAIL build (clang)"; cat "$OUT/build.log"; exit 1; }
fi
[ -x "$BIN" ] || { echo "OLIVE_DEMO FAIL no binary"; exit 1; }

"$BIN" >"$OUT/run.out" 2>"$OUT/run.err" || {
    echo "OLIVE_DEMO FAIL run"; cat "$OUT/run.err"; exit 1; }
[ ! -s "$OUT/run.err" ] || { echo "OLIVE_DEMO FAIL stderr"; cat "$OUT/run.err"; exit 1; }

EXPECTED_BYTES=230415
EXPECTED_SHA=8c7784763ad0558d1783830f7116e01ad7f2036fa394f11018069ad0d001977d
EXPECTED_LINE='OLIVE_DEMO bytes=230415 fnv1a64=878c7e60fc33e272 ppm=out/frame.ppm'

bytes=$(wc -c < "$OUT/frame.ppm" | tr -d '[:space:]')
[ "$bytes" = "$EXPECTED_BYTES" ] || {
    echo "OLIVE_DEMO FAIL bytes=$bytes expected=$EXPECTED_BYTES"; exit 1; }
[ "$(head -c 2 "$OUT/frame.ppm")" = "P6" ] || {
    echo "OLIVE_DEMO FAIL ppm magic"; exit 1; }
if command -v shasum >/dev/null 2>&1; then
    sha=$(shasum -a 256 "$OUT/frame.ppm" | awk '{print $1}')
else
    sha=$(sha256sum "$OUT/frame.ppm" | awk '{print $1}')
fi
[ "$sha" = "$EXPECTED_SHA" ] || {
    echo "OLIVE_DEMO FAIL sha256=$sha expected=$EXPECTED_SHA"; exit 1; }
line=$(cat "$OUT/run.out")
[ "$line" = "$EXPECTED_LINE" ] || {
    echo "OLIVE_DEMO FAIL stdout=$line expected=$EXPECTED_LINE"; exit 1; }

printf 'OLIVE_DEMO PASS compiler=%s bytes=%s sha256=%s
' "$COMPILER" "$bytes" "$sha"

#!/bin/sh
# Build and verify the interactive olive.c + SDL3 demo in deterministic
# proof mode (dummy video driver, fixed 4-frame sequence).
# Prints: OLIVE_SDL_TEST <cases> <pass> <fail>
set -u
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$D" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
OUT="$D/out"
mkdir -p "$OUT"
ROOT=$(CDPATH= cd -- "$D/../../.." && pwd)
CPLUS_BIN_OVERRIDE=${CPLUS_BIN:-"$ROOT/build/c+"}
BIN="$OUT/olive_sdl_demo"
BMP="$OUT/olive_sdl_real.bmp"
PNG="$OUT/olive_sdl_real.png"
rm -f "$BIN" "$BMP" "$PNG" "$OUT/sdl_build.log" "$OUT/sdl_run.log"

if [ -x "$CPLUS_BIN_OVERRIDE" ]; then
    COMPILER=c+
    "$CPLUS_BIN_OVERRIDE" build "$D/olive_sdl_demo.c" --legacy-c --sanitize=none \
        -std=c11 -I"$D" -I/opt/homebrew/include -o "$BIN" \
        -- -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib -lSDL3 \
        >"$OUT/sdl_build.log" 2>&1 || {
        echo "OLIVE_SDL_TEST FAIL build (c+)"; tail -10 "$OUT/sdl_build.log"; exit 1; }
else
    COMPILER=clang
    CC=${CC:-/usr/bin/clang}
    "$CC" -std=c11 -O2 -Wall -Wextra -Werror -I"$D" -I/opt/homebrew/include \
        "$D/olive_sdl_demo.c" -o "$BIN" -L/opt/homebrew/lib \
        -Wl,-rpath,/opt/homebrew/lib -lSDL3 >"$OUT/sdl_build.log" 2>&1 || {
        echo "OLIVE_SDL_TEST FAIL build (clang)"; tail -10 "$OUT/sdl_build.log"; exit 1; }
fi
[ -x "$BIN" ] || { echo "OLIVE_SDL_TEST FAIL no binary"; exit 1; }

SDL_VIDEODRIVER=dummy OLIVE_SDL_PROOF=1 timeout 30 "$BIN" \
    >"$OUT/sdl_run.log" 2>&1 || {
    echo "OLIVE_SDL_TEST FAIL run"; cat "$OUT/sdl_run.log"; exit 1; }

pass=0; fail=0; cases=0
check() {
    cases=$((cases + 1))
    if [ "$1" = "ok" ]; then pass=$((pass + 1));
    else fail=$((fail + 1)); echo "OLIVE_SDL_TEST FAIL $2"; fi
}
LINE=$(cat "$OUT/sdl_run.log")
EXPECTED="OLIVE_SDL frames=4 circle=160,120 colors=2 triangle=1 mouse=0 keys=0 clicks=0"
if [ "$LINE" = "$EXPECTED" ]; then check ok marker; else check no marker; fi

[ -s "$BMP" ] && check ok bmp || check no bmp
if [ "$(head -c 2 "$BMP")" = "BM" ]; then check ok bmp-magic; else check no bmp-magic; fi
BMP_BYTES=$(wc -c < "$BMP" | tr -d "[:space:]")
if [ "$BMP_BYTES" = "921654" ]; then check ok bmp-bytes; else check no bmp-bytes; fi

if command -v sips >/dev/null 2>&1; then
    sips -s format png -o "$PNG" "$BMP" >/dev/null 2>&1 && check ok png || check no png
elif command -v python3 >/dev/null 2>&1 && python3 -c "import PIL" >/dev/null 2>&1; then
    python3 -c "from PIL import Image; import sys; Image.open(sys.argv[1]).save(sys.argv[2])"         "$BMP" "$PNG" >/dev/null 2>&1 && check ok png || check no png
else
    check no png
fi

if [ -s "$PNG" ]; then
    SHA=$(shasum -a 256 "$PNG" | awk "{print \$1}")
    if [ "$SHA" = "5278ee77fe3cdc4c659d95b64d3d8e4dacb71edf7653eee01f9db5a756cd2e9b" ]; then
        check ok sha256; else check no sha256; fi
else
    check no sha256
fi

echo "OLIVE_SDL_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]

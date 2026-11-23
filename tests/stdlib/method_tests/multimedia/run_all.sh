#!/bin/sh
# Build and run every multimedia/window method test.
# Exact build form (per task):
#   ./build/c+ build <test>.cp --strict --sanitize=none <link flags> -o <bin>
# Each binary is executed under `timeout 30` with SDL's dummy video driver.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../../.." && pwd)
C="$ROOT/build/c+"
OUT="$HERE/out"
mkdir -p "$OUT"

SDL="$(pkg-config --cflags --libs sdl3 2>/dev/null)"
FF="$(pkg-config --cflags --libs libavformat libavcodec libavutil 2>/dev/null)"
STB="-I$ROOT/third_party/stb -L$ROOT/build/ext -lstb"
NUKLEAR="-I$ROOT/third_party/nuklear -L$ROOT/build/ext -lnuklear"
MINIAUDIO="-I$ROOT/third_party/miniaudio -L$ROOT/build/ext -lminiaudio -Wl,-framework,CoreFoundation -Wl,-framework,CoreAudio -Wl,-framework,AudioToolbox"

cd "$ROOT" || exit 1

TOTAL=0
PASSED=0
FAILED=0
SKIPPED=0
BUILDERR=0

run_one() { # $1=name  $2=extra flags...
    name=$1
    shift
    src="$HERE/$name.cp"
    bin="$OUT/$name"
    total_before=$TOTAL
    if "$C" build "$src" --strict --sanitize=none "$@" -o "$bin" \
        >"$OUT/$name.build.log" 2>&1; then
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 30 "$bin" \
            >"$OUT/$name.out" 2>"$OUT/$name.err"
        rc=$?
        if [ "$rc" -ne 0 ]; then
            echo "RUNNER $name FAIL (exit $rc; see $OUT/$name.err)" >&2
            BUILDERR=1
        fi
        cat "$OUT/$name.out"
    else
        echo "RUNNER $name FAIL (build failed; see $OUT/$name.build.log)" >&2
        BUILDERR=1
    fi
    # Recount from this binary's output (the authoritative per-method result).
    passed=$(grep -c ' PASS$' "$OUT/$name.out" 2>/dev/null || true)
    failed=$(grep -c ' FAIL$' "$OUT/$name.out" 2>/dev/null || true)
    TOTAL=$((TOTAL + passed + failed))
    PASSED=$((PASSED + passed))
    FAILED=$((FAILED + failed))
    _unused=$total_before
}

run_one window_methods $SDL
run_one graphics_methods $SDL
run_one input_methods $SDL
run_one image_methods $STB
run_one audio_methods $MINIAUDIO
run_one gui_methods $SDL $NUKLEAR
run_one video_methods $FF

echo "METHOD_TEST multimedia $TOTAL $PASSED $FAILED $SKIPPED"
[ "$BUILDERR" -eq 0 ] && [ "$FAILED" -eq 0 ] && [ "$TOTAL" -gt 0 ]

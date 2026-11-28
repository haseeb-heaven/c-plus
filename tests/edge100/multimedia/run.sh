#!/bin/sh
# EDGE100 multimedia edge-case driver.
#
# Builds harness.cp with the exact flags from
# tests/stdlib/method_tests/multimedia/run_all.sh, then runs every case under
# SDL/Audio dummy drivers.  Each case is forked by the harness and bounded to
# 60 seconds; run.sh additionally wraps each harness invocation in `timeout 60`.
#
# Prints the final line:
#   EDGE100 multimedia <apis> <cases> <passed> <failed>
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
C="$ROOT/build/c+"
OUT="$HERE/out"
mkdir -p "$OUT"

# Flags copied verbatim from tests/stdlib/method_tests/multimedia/run_all.sh.
SDL="$(pkg-config --cflags --libs sdl3 2>/dev/null)"
FF="$(pkg-config --cflags --libs libavformat libavcodec libavutil 2>/dev/null)"
STB="-I$ROOT/third_party/stb -L$ROOT/build/ext -lstb"
NUKLEAR="-I$ROOT/third_party/nuklear -L$ROOT/build/ext -lnuklear"
MINIAUDIO="-I$ROOT/third_party/miniaudio -L$ROOT/build/ext -lminiaudio -Wl,-framework,CoreFoundation -Wl,-framework,CoreAudio -Wl,-framework,AudioToolbox"

cd "$ROOT" || exit 1

BIN="$OUT/harness"
# SDL3/ffmpeg/miniaudio headers contain C-only patterns the strict C+ checker
# rejects, so use --legacy-c like the method-test suite does.
if ! "$C" build "$HERE/harness.cp" --legacy-c --sanitize=none \
        $SDL $FF $STB $NUKLEAR $MINIAUDIO -o "$BIN" \
        >"$OUT/build.log" 2>&1; then
    echo "EDGE100 multimedia BUILD FAILED (see $OUT/build.log)" >&2
    cat "$OUT/build.log" >&2
    exit 1
fi

N=$("$BIN" list | wc -l | tr -d ' ')
APIS=$("$BIN" apis | tr -d ' ')
PASSED=0
FAILED=0
: >"$OUT/results.log"

i=0
while [ "$i" -lt "$N" ]; do
    line=$(SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 60 "$BIN" "$i" 2>&1)
    printf '%s\n' "$line" >>"$OUT/results.log"
    case "$line" in
        *" PASS") PASSED=$((PASSED + 1)) ;;
        *)        FAILED=$((FAILED + 1)) ;;
    esac
    i=$((i + 1))
done

cat "$OUT/results.log"
echo "EDGE100 multimedia $APIS $N $PASSED $FAILED"
[ "$FAILED" -eq 0 ]

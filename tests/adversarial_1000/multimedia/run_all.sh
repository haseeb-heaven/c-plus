#!/bin/sh
# ADV1000 / multimedia -- build + run the 1000 deterministic adversarial cases.
#
# Link flags are the same set used by
#   tests/stdlib/method_tests/multimedia/run_all.sh
# (sdl3, stb, miniaudio + Apple frameworks, nuklear, ffmpeg), all in one binary.
# SDL runs on the dummy video/audio driver and miniaudio uses its null backend,
# so the run is fully headless.  Both build and run are bounded by `timeout 120`.
#
# Emits the 1000 canonical "S ..." lines, a sha256 digest of scenarios.log,
# and finally the authoritative "ADV1000 multimedia 1000 ..." line.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
C="$ROOT/build/c+"
OUT="$HERE/results"
mkdir -p "$OUT" || exit 1

SDL="$(pkg-config --cflags --libs sdl3 2>/dev/null)"
FF="$(pkg-config --cflags --libs libavformat libavcodec libavutil 2>/dev/null)"
STB="-I$ROOT/third_party/stb -L$ROOT/build/ext -lstb"
NUKLEAR="-I$ROOT/third_party/nuklear -L$ROOT/build/ext -lnuklear"
MINIAUDIO="-I$ROOT/third_party/miniaudio -L$ROOT/build/ext -lminiaudio -Wl,-framework,CoreFoundation -Wl,-framework,CoreAudio -Wl,-framework,AudioToolbox"

cd "$ROOT" || exit 1

BIN="$OUT/adv1000_multimedia"
LOG="$OUT/scenarios.log"
STDOUT="$OUT/scenarios.out"
STDERR="$OUT/scenarios.err"
BUILDLOG="$OUT/build.log"

CPLUS_CLANG=${CPLUS_CLANG:-/usr/bin/clang}
export CPLUS_CLANG

if ! timeout 120 "$C" build "$HERE/adv1000_multimedia.cp" --strict \
        --sanitize=none $SDL $STB $MINIAUDIO $NUKLEAR $FF -o "$BIN" \
        >"$BUILDLOG" 2>&1; then
    echo "ADV1000 multimedia build failed (see $BUILDLOG)" >&2
    tail -n 20 "$BUILDLOG" >&2
    echo "ADV1000 multimedia 1000 0 sentinel_ok 1"
    exit 1
fi

SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 120 "$BIN" "$LOG" \
    "build/stdlib-tests/clip.mp4" >"$STDOUT" 2>"$STDERR"
rc=$?

# Digest of the 1000-line scenario log (never the aggregate final line).
if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$LOG" >"$OUT/scenarios.sha256" 2>/dev/null
else
    shasum "$LOG" >"$OUT/scenarios.sha256" 2>/dev/null
fi
printf 'DIGEST sha256 %s\n' "$(awk '{print $1}' "$OUT/scenarios.sha256" 2>/dev/null)"

# The binary's final ADV1000 line is the authoritative last stdout line.
cat "$STDOUT"
if [ "$rc" -ne 0 ] && ! grep -q '^ADV1000 multimedia 1000 ' "$STDOUT"; then
    echo "ADV1000 multimedia 1000 0 sentinel_ok 1" >&2
fi
exit "$rc"

#!/bin/sh
# BLUE-TEAM agent 19 -- build + run the multimedia header safety PROOF.
#
# 2400 fixed-seed cases, fork-per-case, each child guarded by alarm(60) and a
# 60s parent-side monotonic deadline.  SDL runs on the dummy video/audio driver
# and miniaudio uses its null backend, so the proof is fully headless.
#
# Link flags match tests/stdlib/method_tests/multimedia/run_all.sh
# (sdl3, stb, miniaudio + Apple frameworks, nuklear, ffmpeg).  Build and run
# output stay inside this directory.  The authoritative result is the final
# "BLUE20 multimedia <cases> <passed> <failed>" line on stdout.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
C="$ROOT/build/c+"
OUT="$HERE/out"
mkdir -p "$OUT" || exit 1

SDL="$(pkg-config --cflags --libs sdl3 2>/dev/null)"
FF="$(pkg-config --cflags --libs libavformat libavcodec libavutil 2>/dev/null)"
STB="-I$ROOT/third_party/stb -L$ROOT/build/ext -lstb"
NUKLEAR="-I$ROOT/third_party/nuklear -L$ROOT/build/ext -lnuklear"
MINIAUDIO="-I$ROOT/third_party/miniaudio -L$ROOT/build/ext -lminiaudio -Wl,-framework,CoreFoundation -Wl,-framework,CoreAudio -Wl,-framework,AudioToolbox"

cd "$ROOT" || exit 1

BIN="$OUT/proof"
LOG="$OUT/results.log"
STDOUT="$OUT/proof.out"
STDERR="$OUT/proof.err"
BUILDLOG="$OUT/build.log"

CPLUS_CLANG=${CPLUS_CLANG:-/usr/bin/clang}
export CPLUS_CLANG

# The seven multimedia headers wrap C APIs whose third-party headers contain
# C-only patterns, and this proof deliberately exercises anti-patterns (double
# drop, null handles) that the C+ safety AST rejects statically.  Use the real
# C backend, exactly as tests/stdlib/method_tests/multimedia/run_all.sh does.
if ! timeout 120 "$C" build "$HERE/proof.cp" --legacy-c \
        --sanitize=none $SDL $STB $MINIAUDIO $NUKLEAR $FF -o "$BIN" \
        >"$BUILDLOG" 2>&1; then
    echo "BLUE20 multimedia build failed (see $BUILDLOG)" >&2
    tail -n 25 "$BUILDLOG" >&2
    echo "BLUE20 multimedia 2400 0 2400"
    exit 1
fi

SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 60 "$BIN" "$LOG" \
    "build/stdlib-tests/clip.mp4" >"$STDOUT" 2>"$STDERR"
rc=$?

if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$LOG" >"$OUT/results.sha256" 2>/dev/null
fi

cat "$STDOUT"
if ! grep -q '^BLUE20 multimedia 2400 ' "$STDOUT"; then
    echo "BLUE20 multimedia 2400 0 2400" >&2
    exit 1
fi
exit "$rc"

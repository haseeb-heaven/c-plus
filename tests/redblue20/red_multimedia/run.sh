#!/bin/sh
# RED20 / red_multimedia -- reproducible build + run of the fork-per-case
# multimedia attack harness.  Build flags are copied verbatim from
# tests/stdlib/method_tests/multimedia/run_all.sh (--legacy-c, --sanitize=none,
# SDL3 + stb + miniaudio(+Apple frameworks) + Nuklear + FFmpeg).
#
# All artifacts stay under this directory.  No source/header edits, no git
# add/commit.  Emits the authoritative RED20 line last on stdout.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
C="$ROOT/build/c+"
D="$HERE"
R="$D/results"
mkdir -p "$R"

SDL="$(pkg-config --cflags --libs sdl3 2>/dev/null)"
FF="$(pkg-config --cflags --libs libavformat libavcodec libavutil 2>/dev/null)"
STB="-I$ROOT/third_party/stb -L$ROOT/build/ext -lstb"
NUKLEAR="-I$ROOT/third_party/nuklear -L$ROOT/build/ext -lnuklear"
MINIAUDIO="-I$ROOT/third_party/miniaudio -L$ROOT/build/ext -lminiaudio -Wl,-framework,CoreFoundation -Wl,-framework,CoreAudio -Wl,-framework,AudioToolbox"

cd "$ROOT" || exit 1

echo "== [1/4] deterministic fixtures ==" 
CLIP="$ROOT/build/stdlib-tests/clip.mp4"
TRUNC="$R/trunc.mp4"
GARBAGE="$R/garbage.bin"
if [ ! -f "$CLIP" ]; then
    echo "MISSING clip fixture: $CLIP" >&2
    exit 70
fi
dd if="$CLIP" of="$TRUNC" bs=1 count=64 2>/dev/null
awk 'BEGIN { for (i=0;i<256;i++) printf "%c", 165 }' > "$GARBAGE"
ls -l "$CLIP" "$TRUNC" "$GARBAGE"

echo "== [2/4] build harness (method-test flags) =="
if ! "$C" build "$D/harness.cp" --legacy-c --sanitize=none \
        $SDL $STB $MINIAUDIO $NUKLEAR $FF -o "$R/harness" \
        >"$R/build.log" 2>&1; then
    echo "BUILD FAILED; see $R/build.log" >&2
    tail -40 "$R/build.log" >&2
    exit 71
fi
echo "build ok; see $R/build.log"

echo "== [3/4] run 2520 cases, fork-per-case, 60s bound =="
ulimit -c 0 2>/dev/null || true
set +e
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 60 \
    "$R/harness" "$CLIP" "$TRUNC" "$GARBAGE" "$R/scenarios.log" \
    >"$R/harness.out" 2>"$R/harness.err"
HRC=$?
set -e
echo "harness exit=$HRC"
echo "-- stdout (authoritative) --"
cat "$R/harness.out"
echo "-- findings (stderr, first 60) --"
head -60 "$R/harness.err"
echo "-- finding count --"
grep -c '^FIND ' "$R/harness.err" 2>/dev/null || true
echo "-- case log lines --"
wc -l < "$R/scenarios.log"

echo "== [4/4] digest =="
shasum -a 256 "$R/scenarios.log" | tee "$R/digests.sha256"
exit "$HRC"

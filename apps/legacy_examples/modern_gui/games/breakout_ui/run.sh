#!/bin/sh
# Build and run the modern Nuklear+SDL3 breakout example with a deterministic
# headless transcript.  No interactive input, no wall clock, no network.
#
# A scenario passes only when:
#   * the --strict --sanitize=none build succeeds,
#   * valid runs exit 0 under `timeout 30` and emit a well-formed transcript
#     (real frames, Nuklear HUD lines, FINAL and GFXBREAKOUT-END),
#   * the default transcript is byte-identical across two runs and matches the
#     compact-token / per-character-argv runs,
#   * hostile runs exit exactly 2 with the expected REFUSED line, never a
#     timeout/signal/crash.
#
# Final line: GFX_BREAKOUT <scenarios> <passed> <failed> <refused>
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../../" && pwd)
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
# The C+ driver uses Apple clang; Homebrew clang may lack the macOS SDK here.
if [ -n "${CPLUS_CLANG:-}" ]; then
  CXX=$CPLUS_CLANG
elif [ -x /usr/bin/clang ]; then
  CXX=/usr/bin/clang
else
  CXX=clang
fi
DIR=apps/legacy_examples/modern_gui/games/breakout_ui
BIN=$DIR/bin
SRC=$DIR/breakout_ui.cp
mkdir -p "$BIN"

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists sdl3; then
  echo "error: SDL3 development files (pkg-config sdl3) are required" >&2
  exit 1
fi
SDL_CFLAGS=$(pkg-config --cflags sdl3)
SDL_LIBS=$(pkg-config --libs sdl3)
NK_INC=$ROOT/third_party/nuklear

# Build the Nuklear implementation once, inside our own directory only.
if [ ! -f "$BIN/libnuklear.a" ] || \
   [ "$ROOT/third_party/nuklear/nuklear_impl.c" -nt "$BIN/libnuklear.a" ]; then
  if ! "$CXX" -c "$ROOT/third_party/nuklear/nuklear_impl.c" \
        -o "$BIN/nuklear.o" -O2 -I"$NK_INC" $SDL_CFLAGS \
        >"$BIN/nuklear.build.log" 2>&1; then
    echo "BUILD FAIL nuklear (see $BIN/nuklear.build.log)" >&2
    exit 1
  fi
  ar rcs "$BIN/libnuklear.a" "$BIN/nuklear.o"
fi

# Exact build flags: strict C+ lowering, SDL3 via pkg-config, vendored Nuklear.
timeout 30 "$CPLUS" build "$SRC" --strict --sanitize=none -I. \
  $SDL_CFLAGS $SDL_LIBS -I"$NK_INC" -L"$BIN" -lnuklear \
  -o "$BIN/breakout_ui" >"$BIN/breakout_ui.build.log" 2>&1
if [ $? -ne 0 ] || [ ! -x "$BIN/breakout_ui" ]; then
  echo "BUILD FAIL breakout_ui (see $BIN/breakout_ui.build.log)" >&2
  exit 1
fi
echo "BUILD ok breakout_ui"

scenarios=0
passed=0
failed=0
refused=0

# Run one case under a 30-second bound; write stdout/stderr to separate files.
run_case() {
  rc_out=$1
  rc_err=$2
  shift 2
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    timeout 30 "$@" >"$rc_out" 2>"$rc_err"
  echo $?
}

# Verify the transcript invariants of a successful headless run.
check_transcript() {
  ct_file=$1
  grep -q '^GFXBREAKOUT seed=' "$ct_file" || return 1
  grep -q '^INIT ball=' "$ct_file" || return 1
  grep -q '^FRAME 000 draw=OK hud=OK shot=OK painted=1$' "$ct_file" || return 1
  grep -q '^FINAL outcome=' "$ct_file" || return 1
  grep -q '^GFXBREAKOUT-END frames=' "$ct_file" || return 1
  if grep -q '=FAIL' "$ct_file"; then
    return 1
  fi
  ct_frames=$(sed -n 's/^GFXBREAKOUT-END frames=\([0-9][0-9]*\).*/\1/p' "$ct_file")
  ct_hud=$(grep -c '^HUD ' "$ct_file")
  [ "$ct_hud" -eq $((ct_frames * 4)) ] || return 1
  return 0
}

valid_case() {
  vc_tag=$1
  shift
  scenarios=$((scenarios + 1))
  vc_out=$BIN/valid.$vc_tag.out
  vc_err=$BIN/valid.$vc_tag.err
  vc_rc=$(run_case "$vc_out" "$vc_err" "$BIN/breakout_ui" "$@")
  if [ "$vc_rc" -ne 0 ]; then
    echo "RUN FAIL valid/$vc_tag rc=$vc_rc"
    failed=$((failed + 1))
    return 1
  fi
  if ! check_transcript "$vc_out"; then
    echo "RUN FAIL valid/$vc_tag malformed transcript"
    failed=$((failed + 1))
    return 1
  fi
  echo "RUN ok valid/$vc_tag rc=$vc_rc frames=$(sed -n 's/^GFXBREAKOUT-END frames=\([0-9]*\).*/\1/p' "$vc_out")"
  passed=$((passed + 1))
  return 0
}

hostile_case() {
  hc_tag=$1
  hc_msg=$2
  shift 2
  scenarios=$((scenarios + 1))
  hc_out=$BIN/hostile.$hc_tag.out
  hc_err=$BIN/hostile.$hc_tag.err
  hc_rc=$(run_case "$hc_out" "$hc_err" "$BIN/breakout_ui" "$@")
  if [ "$hc_rc" -ne 2 ]; then
    echo "REFUSE FAIL hostile/$hc_tag expected rc=2 got $hc_rc"
    failed=$((failed + 1))
    return 1
  fi
  if ! grep -q "$hc_msg" "$hc_err"; then
    echo "REFUSE FAIL hostile/$hc_tag missing message: $hc_msg"
    failed=$((failed + 1))
    return 1
  fi
  echo "REFUSE ok hostile/$hc_tag rc=$hc_rc"
  passed=$((passed + 1))
  refused=$((refused + 1))
  return 0
}

CMDS=RLSRRLSSLLRRRLSRSSLLRRRLLSRRRLS

# --- valid scenarios --------------------------------------------------------

valid_case default || true

# Determinism: a second default run must be byte-identical.
scenarios=$((scenarios + 1))
det_rc=$(run_case "$BIN/determinism.2.out" "$BIN/determinism.2.err" \
  "$BIN/breakout_ui")
if [ "$det_rc" -eq 0 ] && cmp -s "$BIN/valid.default.out" "$BIN/determinism.2.out"; then
  echo "RUN ok determinism/default byte-identical"
  passed=$((passed + 1))
else
  echo "RUN FAIL determinism/default rc=$det_rc"
  failed=$((failed + 1))
fi

valid_case compact "$CMDS" || true

# Per-character argv tokens must match the compact-token transcript byte-for-byte.
scenarios=$((scenarios + 1))
set --
argv_i=1
argv_len=${#CMDS}
while [ "$argv_i" -le "$argv_len" ]; do
  argv_ch=$(printf '%s' "$CMDS" | cut -c "$argv_i")
  set -- "$@" "$argv_ch"
  argv_i=$((argv_i + 1))
done
argv_rc=$(run_case "$BIN/valid.argv.out" "$BIN/valid.argv.err" \
  "$BIN/breakout_ui" "$@")
if [ "$argv_rc" -eq 0 ] && check_transcript "$BIN/valid.argv.out" && \
   cmp -s "$BIN/valid.compact.out" "$BIN/valid.argv.out"; then
  echo "RUN ok valid/argv identical-to-compact"
  passed=$((passed + 1))
else
  echo "RUN FAIL valid/argv rc=$argv_rc"
  failed=$((failed + 1))
fi

valid_case grid --grid 12x4 RLSR || true

# --- hostile scenarios ------------------------------------------------------

hostile_case badcmd "out-of-range paddle command" X
hostile_case oversized "oversized brick dimensions" --grid 9999x9999

# Null-handle probe: require both REFUSED guard lines and the guard summary.
scenarios=$((scenarios + 1))
null_rc=$(run_case "$BIN/hostile.nullhandles.out" "$BIN/hostile.nullhandles.err" \
  "$BIN/breakout_ui" --probe-null)
if [ "$null_rc" -eq 2 ] && \
   grep -q "REFUSED null-surface" "$BIN/hostile.nullhandles.err" && \
   grep -q "REFUSED null-context" "$BIN/hostile.nullhandles.err" && \
   grep -q "GUARD-END refused=2" "$BIN/hostile.nullhandles.out"; then
  echo "REFUSE ok hostile/nullhandles rc=$null_rc"
  passed=$((passed + 1))
  refused=$((refused + 1))
else
  echo "REFUSE FAIL hostile/nullhandles rc=$null_rc"
  failed=$((failed + 1))
fi

echo "GFX_BREAKOUT $scenarios $passed $failed $refused"
[ "$failed" -eq 0 ] && [ "$refused" -eq 3 ]

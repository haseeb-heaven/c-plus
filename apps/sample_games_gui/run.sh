#!/bin/sh
# Real SDL3 GUI proof for the game sample workloads. Prints SAMPLE_GAMES_GUI_TEST.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=apps/sample_games_gui
timeout 300 ./build/c+ build "$D/app.cp" --legacy-c --sanitize=none -I. \
  -I/opt/homebrew/include -o "$D/app" -- -L/opt/homebrew/lib \
  -Wl,-rpath,/opt/homebrew/lib -lSDL3 -lm build/libc/libcplus_hardened.a \
  >"$D/build.log" 2>&1 || { echo "SAMPLE_GAMES_GUI_TEST FAIL build"; exit 1; }
SDL_VIDEODRIVER=dummy timeout 60 "$D/app" >"$D/out" 2>&1 || {
  echo "SAMPLE_GAMES_GUI_TEST FAIL run"; cat "$D/out"; exit 1; }
pass=0; fail=0; cases=0
for marker in "SAMPLE_GAMES_GUI modes=3 frames=6"; do
  cases=$((cases+1))
  if grep -qF "$marker" "$D/out"; then pass=$((pass+1));
  else fail=$((fail+1)); echo "SAMPLE_GAMES_GUI_TEST FAIL $marker"; fi
done
cases=$((cases+1))
if [ -s proof/games_gui_nuklear/screenshots/sample_games.png ]; then pass=$((pass+1));
else fail=$((fail+1)); echo "SAMPLE_GAMES_GUI_TEST FAIL screenshot"; fi
echo "SAMPLE_GAMES_GUI_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]

#!/bin/sh
# Real SDL3 3D framebuffer proof. Prints: GAMES3D_REAL_TEST <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=apps/games3d_real
timeout 300 ./build/c+ build "$D/app.cp" --legacy-c --sanitize=none -I. \
  -I/opt/homebrew/include -o "$D/app" -- -L/opt/homebrew/lib \
  -Wl,-rpath,/opt/homebrew/lib -lSDL3 build/libc/libcplus_hardened.a \
  >"$D/build.log" 2>&1 || { echo "GAMES3D_REAL_TEST FAIL build"; exit 1; }
SDL_VIDEODRIVER=dummy timeout 60 "$D/app" >"$D/out" 2>&1 || {
  echo "GAMES3D_REAL_TEST FAIL run"; cat "$D/out"; exit 1; }
pass=0; fail=0; cases=0
for marker in "GAMES3D_REAL frames=4 edges=12"; do
  cases=$((cases+1))
  if grep -qF "$marker" "$D/out"; then pass=$((pass+1));
  else fail=$((fail+1)); echo "GAMES3D_REAL_TEST FAIL $marker"; fi
done
cases=$((cases+1))
if [ -s proof/games_gui_nuklear/screenshots/games3d_real.png ]; then pass=$((pass+1));
else fail=$((fail+1)); echo "GAMES3D_REAL_TEST FAIL screenshot"; fi
echo "GAMES3D_REAL_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]

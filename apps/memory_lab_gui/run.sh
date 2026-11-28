#!/bin/sh
# Real interactive SDL3 GUI app proof with scoped data-stack state.
# Prints: MEMORY_LAB_TEST <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=apps/memory_lab_gui
timeout 300 ./build/c+ build "$D/app.cp" --legacy-c --sanitize=none -I. \
  -I/opt/homebrew/include -o "$D/app" -- -L/opt/homebrew/lib \
  -Wl,-rpath,/opt/homebrew/lib -lSDL3 build/libc/libcplus_hardened.a \
  >"$D/build.log" 2>&1 || { echo "MEMORY_LAB_TEST FAIL build"; exit 1; }
SDL_VIDEODRIVER=dummy timeout 60 "$D/app" demo >"$D/out" 2>&1 || {
  echo "MEMORY_LAB_TEST FAIL run"; cat "$D/out"; exit 1; }
pass=0; fail=0; cases=0
for marker in "MEMORY_LAB frames=4 clicks=1 selected=1"; do
  cases=$((cases + 1))
  if grep -qF "$marker" "$D/out"; then pass=$((pass + 1));
  else fail=$((fail + 1)); echo "MEMORY_LAB_TEST FAIL $marker"; fi
done
cases=$((cases + 1))
if [ -s proof/games_gui_nuklear/screenshots/memory_lab.png ]; then pass=$((pass + 1));
else fail=$((fail + 1)); echo "MEMORY_LAB_TEST FAIL screenshot"; fi
echo "MEMORY_LAB_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]

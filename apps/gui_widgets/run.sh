#!/bin/sh
# Real SDL3 windowed GUI proof (bounded POSIX sh).
# Builds the C+ GFX app, runs it under SDL_VIDEODRIVER=dummy so CI is headless,
# and verifies the framebuffer screenshot and widget markers.
# Prints: GFX_APP_TEST <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=apps/gui_widgets
SDL_FLAGS=$(pkg-config --cflags --libs sdl3 2>/dev/null) || { echo "GFX_APP_TEST FAIL sdl3"; exit 1; }
if ! timeout 300 ./build/c+ build "$D/gui_widgets.cp" --legacy-c --sanitize=none \
      -I. -o "$D/gui_widgets" -- $SDL_FLAGS >"$D/build.log" 2>&1; then
    echo "GFX_APP_TEST FAIL build"; tail -8 "$D/build.log"; exit 1
fi
if ! SDL_VIDEODRIVER=dummy timeout 60 "$D/gui_widgets" >"$D/out" 2>&1; then
    echo "GFX_APP_TEST FAIL run"; cat "$D/out"; exit 1
fi
pass=0; fail=0; cases=0
for marker in "GFX_APP screenshot saved" "GFX_APP_OK widgets=textbox,dropdown,list,button"; do
    cases=$((cases + 1))
    if grep -qF "$marker" "$D/out"; then pass=$((pass + 1));
    else fail=$((fail + 1)); echo "GFX_APP_TEST FAIL $marker"; fi
done
cases=$((cases + 1))
if [ -s proof/games_gui_nuklear/screenshots/gui_widgets.png ]; then pass=$((pass + 1));
else fail=$((fail + 1)); echo "GFX_APP_TEST FAIL screenshot"; fi
echo "GFX_APP_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]

#!/bin/sh
# Build and verify the real microui calculator + notes GUI.
# Prints: MICROUI_TEST <cases> <pass> <fail>
set -u
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$D" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export CPLUS_CLANG=/usr/bin/clang
unset SDKROOT
OUT="$D/out"; mkdir -p "$OUT"
R=$(CDPATH= cd -- "$D/../.." && pwd)
C=${CPLUS_BIN:-"$R/build/c+"}
pass=0; fail=0; cases=0
check() { cases=$((cases+1)); if [ "$1" = ok ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "MICROUI_TEST FAIL $2"; fi; }

/usr/bin/clang -std=c11 -O2 -I. -I/opt/homebrew/include -c microui.c -o "$OUT/microui.o" >"$OUT/mu.log" 2>&1 && check ok mu || { check no mu; tail -6 "$OUT/mu.log"; }
/usr/bin/clang -std=c11 -O2 -I. -I/opt/homebrew/include -c microui_backend.c -o "$OUT/microui_backend.o" >"$OUT/backend.log" 2>&1 && check ok backend || { check no backend; tail -6 "$OUT/backend.log"; }

if [ -x "$C" ] && "$C" build microui_app.cp --default --sanitize=none -I"$R" -I. -o "$OUT/microui_app" -- "$OUT/microui.o" "$OUT/microui_backend.o" -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib -lSDL3 -framework Cocoa -framework OpenGL -framework GLUT -framework CoreVideo -framework IOKit -framework QuartzCore -Wno-unused-command-line-argument >"$OUT/app.log" 2>&1; then check ok build; else check no build; tail -8 "$OUT/app.log"; fi
rm -f "$OUT/microui.bmp"
UI_PROOF=1 timeout 30 "$OUT/microui_app" >"$OUT/run.log" 2>&1 && check ok run || { check no run; tail -6 "$OUT/run.log"; }
grep -qF "MICROUI frames=" "$OUT/run.log" && check ok marker || check no marker
[ -s "$OUT/microui.bmp" ] && [ "$(head -c 2 "$OUT/microui.bmp")" = "BM" ] && check ok framebuffer || check no framebuffer

echo "MICROUI_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]

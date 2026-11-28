#!/bin/sh
# Build and verify three REAL raylib demos written in modern C+.
# Prints: RAYLIB_DEMOS <cases> <pass> <fail>
set -u
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$D" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export CPLUS_CLANG=/usr/bin/clang
unset SDKROOT
OUT="$D/out"
mkdir -p "$OUT"
ROOT=$(CDPATH= cd -- "$D/../.." && pwd)
C=${CPLUS_BIN:-"$ROOT/build/c+"}
RL="$ROOT/examples/raylib"
RP=$(cd "$RL/lib" && pwd)
LIBS="-L$RL/lib -Wl,-rpath,$RP -lraylib -framework CoreVideo -framework IOKit -framework Cocoa -framework QuartzCore -framework GLUT -framework OpenGL -Wno-unused-command-line-argument"
pass=0; fail=0; cases=0
check() { cases=$((cases+1)); if [ "$1" = ok ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "RAYLIB_DEMOS FAIL $2"; fi; }

for n in shapes2d text2d cube3d; do
  BIN="$OUT/$n"
  PNG="$OUT/$n.png"
  rm -f "$BIN" "$PNG"
  if [ -x "$C" ] && "$C" build "$D/$n.cp" --default --sanitize=none -I"$D" -I"$RL/include" -o "$BIN" -- $LIBS >"$OUT/$n.build.log" 2>&1; then
    check ok "build-$n"
  else
    check no "build-$n"; tail -6 "$OUT/$n.build.log" 2>/dev/null; continue
  fi
  RAYLIB_PROOF=1 timeout 30 "$BIN" >"$OUT/$n.run.log" 2>&1 && check ok "run-$n" || { check no "run-$n"; tail -4 "$OUT/$n.run.log"; continue; }
  [ -s "$PNG" ] && check ok "png-$n" || check no "png-$n"
  [ "$(head -c 4 "$PNG" | od -An -tx1 | tr -d " " | tr -d "
")" = "89504e47" ] && check ok "pngmagic-$n" || check no "pngmagic-$n"
done

echo "RAYLIB_DEMOS $cases $pass $fail"
[ "$fail" -eq 0 ]

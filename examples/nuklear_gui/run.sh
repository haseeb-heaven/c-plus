#!/bin/sh
# Build and verify the real Nuklear GUI app (modern C+ via the C+ stdlib
# facade stdlib/gfx_nuklear.hp, backed by stdlib/nuklear + third_party/nuklear).
# Prints: NUKLEAR_GUI_TEST <cases> <pass> <fail>
set -u
# Resolve CPLUS_BIN to an absolute path before changing directory.
if [ -n "${CPLUS_BIN:-}" ]; then
  case "$CPLUS_BIN" in
    /*) ;;
    *) CPLUS_BIN=$(CDPATH= cd -- "$(dirname -- "$CPLUS_BIN")" && pwd)/$(basename -- "$CPLUS_BIN") ;;
  esac
fi
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$D" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export CPLUS_CLANG=/usr/bin/clang
unset SDKROOT
OUT="$D/out"
mkdir -p "$OUT"
R=$(CDPATH= cd -- "$D/../.." && pwd)
C=${CPLUS_BIN:-"$R/build/c+"}
pass=0; fail=0; cases=0
check() { cases=$((cases+1)); if [ "$1" = ok ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "NUKLEAR_GUI_TEST FAIL $2"; fi; }

# The stdlib facade keeps SDL3/Nuklear headers out of the C+ TU: both sources
# are real third-party C, compiled once here and linked through a plain C ABI.
/usr/bin/clang -std=c11 -O2 -I"$R/third_party/nuklear" -I/opt/homebrew/include \
  -c "$R/third_party/nuklear/nuklear_impl.c" -o "$OUT/nuklear_impl.o" >"$OUT/impl.log" 2>&1 && check ok impl || { check no impl; tail -6 "$OUT/impl.log"; }
/usr/bin/clang -std=c11 -O2 -I"$R" -I"$R/third_party/nuklear" -I/opt/homebrew/include \
  -c "$R/stdlib/nuklear/nuklear_gui_backend.c" -o "$OUT/nuklear_backend.o" >"$OUT/backend.log" 2>&1 && check ok backend || { check no backend; tail -6 "$OUT/backend.log"; }

if [ -x "$C" ] && (cd "$R" && "$C" build "$D/nuklear_gui.cp" --default --sanitize=none \
    -I. -Istdlib/nuklear -o "$OUT/nuklear_gui" -- \
    "$OUT/nuklear_impl.o" "$OUT/nuklear_backend.o" \
    -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib -lSDL3 \
    -framework Cocoa -framework OpenGL -framework GLUT -framework CoreVideo \
    -framework IOKit -framework QuartzCore -Wno-unused-command-line-argument \
    >"$OUT/app.log" 2>&1); then check ok build; else check no build; tail -8 "$OUT/app.log"; fi

rm -f "$OUT/nuklear_gui.bmp"
GUI_PROOF=1 timeout 30 "$OUT/nuklear_gui" >"$OUT/run.log" 2>&1 && check ok run || { check no run; tail -6 "$OUT/run.log"; }
grep -qF "NUKLEAR_GUI frames=" "$OUT/run.log" && check ok marker || check no marker
[ -s "$OUT/nuklear_gui.bmp" ] && check ok bmp || check no bmp
[ "$(head -c 2 "$OUT/nuklear_gui.bmp")" = "BM" ] && check ok bmp-magic || check no bmp-magic

echo "NUKLEAR_GUI_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]

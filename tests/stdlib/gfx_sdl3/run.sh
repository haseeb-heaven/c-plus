#!/bin/sh
# Issue #79: deterministic SDL3 lifecycle proof using a recording stub.
# Prints: GFX_SDL3_TEST <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
D=tests/stdlib/gfx_sdl3
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
if [ ! -x "$CPLUS" ]; then
    echo "GFX_SDL3_TEST FAIL compiler"; exit 1
fi
# The test deliberately passes NULL handles/items to prove the guards fail
# closed; those arguments trigger C+ lifetime warnings, so do not promote
# warnings to errors for this adversarial fixture.
if ! timeout 300 "$CPLUS" build "$D/test.cp" --no-werror -I. -I"$D/stub" -o "$D/test" \
      >"$D/build.log" 2>&1; then
    echo "GFX_SDL3_TEST FAIL build"; tail -6 "$D/build.log"; exit 1
fi
timeout 30 "$D/test" >"$D/out" 2>"$D/err" || { echo "GFX_SDL3_TEST FAIL run"; cat "$D/out"; exit 1; }
if grep -qF 'GFX_SDL3 0' "$D/out"; then
    echo "GFX_SDL3_TEST 1 1 0"
else
    echo "GFX_SDL3_TEST 1 0 1"; cat "$D/out"; exit 1
fi

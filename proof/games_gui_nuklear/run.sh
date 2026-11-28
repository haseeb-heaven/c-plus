#!/bin/sh
# Proof: rebuild game/app suite from source, run it, diff stdout and all three
# generated SVG screenshots byte-for-byte. POSIX sh, bounded, no Python.
# Prints: GAME_SUITE <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=proof/games_gui_nuklear
pass=0; fail=0; cases=0
# The vendored Nuklear archive is a build artifact (gitignored); rebuild it
# from the MIT source if a fresh worktree lacks it.
if [ ! -f build/ext/libnuklear.a ]; then
    mkdir -p build/ext
    # Build core Nuklear only; nuklear_impl.c needs an SDL3 backend we do not ship.
    printf '#define NK_IMPLEMENTATION\n#include "nuklear.h"\n' \
        > build/ext/nuklear_core.c
    ${CPLUS_CC:-clang} -O2 -std=c11 -Ithird_party/nuklear -c \
        build/ext/nuklear_core.c -o build/ext/nuklear_impl.o || exit 1
    /usr/bin/ar rcs build/ext/libnuklear.a build/ext/nuklear_impl.o || exit 1
fi
if ! timeout 300 ./build/c+ build "$D/game_suite.cp" --legacy-c --no-werror --sanitize=none \
      -I. -Ithird_party/nuklear -o "$D/bin/game_suite" \
      -- build/ext/libnuklear.a build/libc/libcplus_hardened.a \
      >"$D/bin/build.log" 2>&1; then
    echo "GAME_SUITE FAIL build"; tail -5 "$D/bin/build.log"; exit 1
fi
timeout 30 "$D/bin/game_suite" >"$D/bin/stdout.txt" 2>"$D/bin/stderr.txt" || {
    echo "GAME_SUITE FAIL run"; exit 1; }
[ -s "$D/bin/stderr.txt" ] && { echo "GAME_SUITE FAIL stderr"; fail=$((fail + 1)); }

for pair in "bin/stdout.txt expected/stdout.txt" \
            "screenshots/snake.svg expected/snake.svg" \
            "screenshots/maze.svg expected/maze.svg" \
            "screenshots/nuklear_ui.svg expected/nuklear_ui.svg"; do
    set -- $pair
    cases=$((cases + 1))
    if diff -u "$D/$2" "$D/$1" >/dev/null 2>&1; then pass=$((pass + 1));
    else fail=$((fail + 1)); echo "GAME_SUITE FAIL diff $2"; fi
done
echo "GAME_SUITE $cases $pass $fail"
[ "$fail" -eq 0 ]

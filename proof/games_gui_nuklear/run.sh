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
if ! timeout 300 ./build/c+ build "$D/game_suite.cp" --legacy-c --sanitize=none \
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

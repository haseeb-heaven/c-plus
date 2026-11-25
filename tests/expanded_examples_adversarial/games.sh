#!/bin/sh
# Adversarial driver: deterministic game cores under examples/expanded/games/.
set -u
. "$(dirname -- "$0")/common.sh" || exit 1
make_fixtures

BIG=$(cat "$FIX/oversized_10k.txt")
SIZEMAX=18446744073709551615

for g in game2048 maze tictactoe; do
    build_one "$g" "examples/expanded/games/cores/$g.cp" || true
done
build_one snake_core examples/expanded/games/snake/snake_core.cp || true
build_one snake_cli  examples/expanded/games/snake/snake_cli.cp  || true

# Move-token games reject bad input with stderr "name: ..." and exit 2.
for g in game2048 maze; do
    if [ -x "$BIN/$g" ]; then
        run_case "$g" oversized_direction REFUSE "$BIN/$g" "$BIG"
        run_case "$g" invalid_command REFUSE "$BIN/$g" X
        run_case "$g" empty_arg REFUSE "$BIN/$g" ""
        run_case "$g" size_max_number REFUSE "$BIN/$g" "$SIZEMAX"
        run_case "$g" whitespace_arg REFUSE "$BIN/$g" "   "
        run_case "$g" stdin_dash_with_extra REFUSE "$BIN/$g" - R
    fi
done

if [ -x "$BIN/tictactoe" ]; then
    run_case tictactoe oversized_move REFUSE "$BIN/tictactoe" "$BIG"
    run_case tictactoe invalid_command REFUSE "$BIN/tictactoe" 9
    run_case tictactoe size_max_number REFUSE "$BIN/tictactoe" "$SIZEMAX"
    run_case tictactoe empty_arg REFUSE "$BIN/tictactoe" ""
    run_case tictactoe whitespace_arg REFUSE "$BIN/tictactoe" "   "
    # Self-collision: the same cell is claimed twice -> illegal move, exit 2.
    run_case tictactoe self_collision REFUSE "$BIN/tictactoe" 0 0
    # Both argv and stdin forms must refuse the same hostile token.
    ADV_STDIN_FILE="$FIX/oversized_10k.txt" \
        run_case tictactoe oversized_stdin REFUSE "$BIN/tictactoe" -
fi

if [ -x "$BIN/snake_core" ]; then
    # Self-contained core self-test already drives the self-collision and
    # reverse-into-body refusals. Hostile argv is ignored; it must not crash.
    ADV_SURVIVE_TOKEN='SNAKE_CORE_SELFTEST' \
        run_case snake_core hostile_argv_self_collision SURVIVE "$BIN/snake_core" "$BIG"
    ADV_SURVIVE_TOKEN='SNAKE_CORE_SELFTEST' \
        run_case snake_core size_max_argv SURVIVE "$BIN/snake_core" "$SIZEMAX"
fi

if [ -x "$BIN/snake_cli" ]; then
    run_case snake_cli oversized_scenario REFUSE "$BIN/snake_cli" "$BIG"
    run_case snake_cli invalid_command REFUSE "$BIN/snake_cli" frobnicate
    run_case snake_cli empty_scenario REFUSE "$BIN/snake_cli" ""
    run_case snake_cli size_max_scenario REFUSE "$BIN/snake_cli" "$SIZEMAX"
    # A pathological 10KB direction stream is bounded to 256 steps; the core
    # detects the self collision. Contract here is containment, not a token.
    ADV_SURVIVE_TOKEN='SCENARIO self' \
        run_case snake_cli self_collision_stream SURVIVE "$BIN/snake_cli" self "$BIG"
fi

subtotal games

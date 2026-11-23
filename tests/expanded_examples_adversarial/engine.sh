#!/bin/sh
# Adversarial driver: examples/expanded/engine/{engine_loop,ecs}.cp
set -u
. "$(dirname -- "$0")/common.sh" || exit 1
make_fixtures

BIG=$(cat "$FIX/oversized_10k.txt")
SIZEMAX=18446744073709551615

build_one engine_loop examples/expanded/engine/engine_loop.cp || true
build_one ecs          examples/expanded/engine/ecs.cp          || true

if [ -x "$BIN/engine_loop" ]; then
    # Bad token -> "ERROR ... code=" on stdout and exit 2.
    run_case engine_loop oversized_token REFUSE "$BIN/engine_loop" "$BIG"
    run_case engine_loop empty_token REFUSE "$BIN/engine_loop" ""
    run_case engine_loop invalid_command REFUSE "$BIN/engine_loop" X
    run_case engine_loop size_max_token REFUSE "$BIN/engine_loop" "$SIZEMAX"
    run_case engine_loop whitespace_token REFUSE "$BIN/engine_loop" " "
    ADV_STDIN_FILE="$FIX/empty.txt" \
        run_case engine_loop null_like_no_args REFUSE "$BIN/engine_loop"
fi

if [ -x "$BIN/ecs" ]; then
    # ecs.cp takes no arguments; it deliberately performs three failing
    # lookups. Hostile argv must be ignored without a crash, and the world
    # checksum/summary must still be printed (state sentinel).
    ADV_SURVIVE_TOKEN='REFUSE unknown-entity' ADV_REQUIRE_TOKEN='SUMMARY survivors=' \
        run_case ecs oversized_argv SURVIVE "$BIN/ecs" "$BIG"
    ADV_SURVIVE_TOKEN='REFUSE unknown-entity' ADV_REQUIRE_TOKEN='SUMMARY survivors=' \
        run_case ecs size_max_argv SURVIVE "$BIN/ecs" "$SIZEMAX"
    ADV_SURVIVE_TOKEN='REFUSE unknown-entity' ADV_REQUIRE_TOKEN='SUMMARY survivors=' \
        run_case ecs null_like_argv SURVIVE "$BIN/ecs" ""
fi

subtotal engine

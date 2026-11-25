#!/bin/sh
# Build and run all three deterministic game cores with fixed inputs.
# No terminal graphics, no wall clock, no network.  A game passes only when
# its build succeeds, its argv run and stdin run agree byte-for-byte and exit
# 0, and an invalid input exits nonzero.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../.." && pwd)
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
DIR=apps/legacy_examples/expanded/games/cores
BIN=$DIR/bin
mkdir -p "$BIN"

passed=0
failed=0

build_one() {
    name=$1
    src=$2
    log=$BIN/$name.build.log
    if "$CPLUS" build "$src" --strict --sanitize=none -I. -o "$BIN/$name" >"$log" 2>&1; then
        echo "BUILD ok $name"
        return 0
    fi
    echo "BUILD FAIL $name (see $log)"
    return 1
}

# Run one case under a 30-second bound and echo its exit code.
run_case() {
    out=$1
    shift
    timeout 30 "$@" >"$out" 2>&1
    echo $?
}

check_game() {
    name=$1
    src=$2
    stdin_moves=$3
    invalid=$4
    shift 4

    if ! build_one "$name" "$src"; then
        failed=$((failed + 1))
        return
    fi

    game_ok=1

    rc=$(run_case "$BIN/$name.argv.out" "$BIN/$name" "$@")
    if [ "$rc" -ne 0 ]; then
        echo "RUN FAIL $name argv run exited $rc"
        game_ok=0
    fi

    printf '%s\n' "$stdin_moves" | timeout 30 "$BIN/$name" - \
        >"$BIN/$name.stdin.out" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "RUN FAIL $name stdin run exited $rc"
        game_ok=0
    fi

    if [ "$game_ok" -eq 1 ] && ! cmp -s "$BIN/$name.argv.out" "$BIN/$name.stdin.out"; then
        echo "RUN FAIL $name argv/stdin output differs"
        game_ok=0
    fi

    timeout 30 "$BIN/$name" "$invalid" >"$BIN/$name.invalid.out" 2>&1
    rc=$?
    if [ "$rc" -eq 0 ]; then
        echo "RUN FAIL $name accepted invalid input '$invalid'"
        game_ok=0
    fi

    if [ "$game_ok" -eq 1 ]; then
        echo "RUN ok $name"
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
}

check_game game2048 "$DIR/game2048.cp" "RDLURD" "X" R D L U R D
check_game maze "$DIR/maze.cp" "UDDDDRRRRR" "X" U D D D D R R R R R
check_game tictactoe "$DIR/tictactoe.cp" "01458" "9" 0 1 4 5 8

echo "GAME_CORES 3 $passed $failed"
[ "$failed" -eq 0 ]

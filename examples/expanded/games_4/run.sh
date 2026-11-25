#!/bin/sh
# Build and run the three deterministic board-game cores in games_4, plus
# hostile-input refusal cases.  No terminal UI, no wall-clock-dependent input,
# no network.  A game passes only when its strict build succeeds, its argv
# run and stdin run agree byte-for-byte (and a repeat run is byte-identical),
# and every hostile case is refused with exit 2 and the expected REFUSE code.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
DIR=examples/expanded/games_4
BIN=$DIR/bin
mkdir -p "$BIN" || exit 1

games=0
passed=0
failed=0
refused_count=0
current=""

run_case() {
    out=$1
    shift
    timeout 30 "$@" >"$out" 2>&1
    echo $?
}

build_one() {
    name=$1
    src=$2
    log=$BIN/$name.build.log
    if "$CPLUS" build "$src" --strict --sanitize=none -I. -o "$BIN/$name" \
            >"$log" 2>&1; then
        echo "BUILD ok $name"
        return 0
    fi
    echo "BUILD FAIL $name (see $log)"
    return 1
}

# Verify one hostile move sequence is refused with the expected stable code.
# Usage: check_refusal <label> <code> <name> <argv...>
check_refusal() {
    label=$1
    code=$2
    code_name=$3
    shift 3
    out=$BIN/$current.refuse.$label.out
    rc=$(run_case "$out" "$BIN/$current" "$@")
    if [ "$rc" -ne 2 ]; then
        echo "REFUSE FAIL $current/$label exit=$rc (expected 2)"
        game_ok=0
        return 1
    fi
    if ! grep -q "REFUSE step=.* code=$code name=$code_name errcode=" "$out"; then
        echo "REFUSE FAIL $current/$label missing code=$code name=$code_name"
        game_ok=0
        return 1
    fi
    echo "REFUSE ok $current/$label code=$code name=$code_name"
    refused_count=$((refused_count + 1))
    return 0
}

# Usage: run_game <name> <src> <stdin_line> <expected_result_line> <argv moves...>
run_game() {
    name=$1
    src=$2
    stdin_line=$3
    expected=$4
    shift 4

    current=$name
    games=$((games + 1))
    game_ok=1
    if ! build_one "$name" "$src"; then
        failed=$((failed + 1))
        return 1
    fi

    rc=$(run_case "$BIN/$name.argv.out" "$BIN/$name" "$@")
    if [ "$rc" -ne 0 ]; then
        echo "RUN FAIL $name argv exit=$rc"
        game_ok=0
    fi
    rc=$(run_case "$BIN/$name.argv2.out" "$BIN/$name" "$@")
    if [ "$rc" -ne 0 ]; then
        echo "RUN FAIL $name repeat exit=$rc"
        game_ok=0
    fi
    if [ "$game_ok" -eq 1 ] && ! cmp -s "$BIN/$name.argv.out" "$BIN/$name.argv2.out"; then
        echo "RUN FAIL $name repeat output differs"
        game_ok=0
    fi

    printf '%s\n' "$stdin_line" | timeout 30 "$BIN/$name" - \
        >"$BIN/$name.stdin.out" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "RUN FAIL $name stdin exit=$rc"
        game_ok=0
    fi
    if [ "$game_ok" -eq 1 ] && ! cmp -s "$BIN/$name.argv.out" "$BIN/$name.stdin.out"; then
        echo "RUN FAIL $name argv/stdin output differs"
        game_ok=0
    fi
    if [ "$game_ok" -eq 1 ] && ! grep -qF "$expected" "$BIN/$name.argv.out"; then
        echo "RUN FAIL $name missing '$expected'"
        game_ok=0
    fi

    # Hostile cases are supplied by the per-game functions below.
    check_hostile_$name

    if [ "$game_ok" -eq 1 ]; then
        echo "RUN ok $name"
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
    return 0
}

check_hostile_connect4() {
    check_refusal col7 2 E_RANGE 3 2 7 2 3 2 3
    check_refusal colminus1 2 E_RANGE -1
    check_refusal oversize 1 E_OVERSIZE 99999999999999999999
    check_refusal fullcol 3 E_ILLEGAL 3 3 3 3 3 3 3
}

check_hostile_checkers() {
    check_refusal coord8 2 E_RANGE 2188
    check_refusal oversize 1 E_OVERSIZE 213045
    check_refusal illegal 3 E_ILLEGAL 2131
    check_refusal badchar 2 E_RANGE 99zz
}

check_hostile_chesslite() {
    check_refusal coord8 2 E_RANGE 0008
    check_refusal oversize 1 E_OVERSIZE 000000000
    check_refusal blockedrook 3 E_ILLEGAL 0002
    check_refusal badchar 2 E_RANGE 99zz
}

run_game connect4 "$DIR/connect4.cp" \
    "3 2 3 2 3 2 3" "RESULT win detail=vertical step=7" \
    3 2 3 2 3 2 3

run_game checkers "$DIR/checkers.cp" \
    "2130 5041 1021 4132 2334 3223 0110 2301" \
    "STEP 8 side=white from=(2,3) to=(0,1) capture=1 promote=1" \
    2130 5041 1021 4132 2334 3223 0110 2301

run_game chesslite "$DIR/chesslite.cp" \
    "0120 6343 1323 6151 0625 6757 2544 6656 1434 4334" \
    "STEP 10 side=black piece=p from=(4,3) to=(3,4) capture=1" \
    0120 6343 1323 6151 0625 6757 2544 6656 1434 4334

echo "GAMES_4 $games $passed $failed $refused_count"
[ "$failed" -eq 0 ]

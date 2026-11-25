#!/bin/sh
# Build and run all three deterministic puzzle cores plus their hostile cases.
#
# A game passes only when:
#   * its .cp builds in --strict --sanitize=none mode from the repo root,
#   * its built-in (no-argv) run exits 0 and contains its expected marker,
#   * an explicit argv run exits 0 with status=OK,
#   * two identical built-in runs produce byte-identical stdout.
# A hostile case passes only when it exits nonzero AND prints a REFUSE line
# carrying the expected symbolic error code.  Every run is bounded by 30s.
#
# Final line: GAMES_3 <games> <passed> <failed> <refused>
#   games   = 3 (fixed)
#   passed  = number of games that passed all checks
#   failed  = failed game checks + failed hostile checks (0 means all good)
#   refused = number of hostile cases that were correctly REFUSED
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
DIR=apps/legacy_examples/expanded/games_3
BIN=$DIR/bin
mkdir -p "$BIN"

passed=0
game_failed=0
hostile_failed=0
refused=0
hcase=0

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

# Run a command under the 30-second bound; stdout+stderr go to $1; echo rc.
run_to() {
    out=$1
    shift
    timeout 30 "$@" >"$out" 2>&1
    echo $?
}

check_game() {
    name=$1
    src=$2
    marker=$3
    shift 3
    ok=1

    if ! build_one "$name" "$src"; then
        ok=0
    fi

    if [ "$ok" -eq 1 ]; then
        rc=$(run_to "$BIN/$name.default.out" "$BIN/$name")
        if [ "$rc" -ne 0 ]; then
            echo "RUN FAIL $name default exited $rc"
            ok=0
        elif ! grep -q "$marker" "$BIN/$name.default.out"; then
            echo "RUN FAIL $name default missing marker '$marker'"
            ok=0
        fi
    fi

    if [ "$ok" -eq 1 ]; then
        rc=$(run_to "$BIN/$name.argv.out" "$BIN/$name" "$@")
        if [ "$rc" -ne 0 ]; then
            echo "RUN FAIL $name argv exited $rc"
            ok=0
        elif ! grep -q "status=OK" "$BIN/$name.argv.out"; then
            echo "RUN FAIL $name argv did not report status=OK"
            ok=0
        fi
    fi

    if [ "$ok" -eq 1 ]; then
        rc=$(run_to "$BIN/$name.repeat.out" "$BIN/$name")
        if [ "$rc" -ne 0 ] || ! cmp -s "$BIN/$name.default.out" "$BIN/$name.repeat.out"; then
            echo "RUN FAIL $name output is not deterministic"
            ok=0
        fi
    fi

    if [ "$ok" -eq 1 ]; then
        echo "GAME ok $name"
        passed=$((passed + 1))
    else
        echo "GAME FAIL $name"
        game_failed=$((game_failed + 1))
    fi
}

# check_refusal <game> <expected_code> <args...>
check_refusal() {
    name=$1
    want=$2
    shift 2
    hcase=$((hcase + 1))
    out=$BIN/$name.hostile.$hcase.out

    rc=$(run_to "$out" "$BIN/$name" "$@")
    if [ "$rc" -eq 0 ]; then
        echo "REFUSE FAIL $name case=$hcase accepted hostile input: $*"
        hostile_failed=$((hostile_failed + 1))
        return
    fi
    if ! grep -q "REFUSE .*code=$want" "$out"; then
        echo "REFUSE FAIL $name case=$hcase expected code=$want (rc=$rc)"
        hostile_failed=$((hostile_failed + 1))
        return
    fi
    if ! grep -q "status=REFUSED" "$out"; then
        echo "REFUSE FAIL $name case=$hcase missing status=REFUSED (rc=$rc)"
        hostile_failed=$((hostile_failed + 1))
        return
    fi
    echo "REFUSE ok $name case=$hcase code=$want rc=$rc args=$*"
    refused=$((refused + 1))
}

# ---- games ---------------------------------------------------------------
check_game sudoku "$DIR/sudoku.cp" "SUDOKU_DONE status=OK solved=1" \
    P 0 2 4 V
check_game minesweeper "$DIR/minesweeper.cp" "MINESWEEPER_DONE status=OK state=win" \
    R 4 4 V
check_game nim "$DIR/nim.cp" "NIM_DONE status=OK winner=A" \
    O T 0 1 O T 1 1 S

# ---- hostile cases: out-of-range index, oversized input, invalid move ----
# sudoku
check_refusal sudoku E_BOUNDS   P 9 0 5
check_refusal sudoku E_BOUNDS   P -1 0 5
check_refusal sudoku E_PARSE    P 100000000 0 5
check_refusal sudoku E_GIVEN    P 0 0 4
check_refusal sudoku E_CONFLICT P 0 2 5
check_refusal sudoku E_GIVEN    R 0 0
# minesweeper
check_refusal minesweeper E_BOUNDS   R 9 0
check_refusal minesweeper E_BOUNDS   R -1 0
check_refusal minesweeper E_BOUNDS   F 3 9
check_refusal minesweeper E_PARSE    R 0 999999999
check_refusal minesweeper E_REVEALED R 4 4 F 4 4
check_refusal minesweeper E_FLAGGED  F 4 4 R 4 4
# nim
check_refusal nim E_BOUNDS  T 3 1
check_refusal nim E_BOUNDS  T -1 1
check_refusal nim E_AMOUNT  T 0 99
check_refusal nim E_AMOUNT  T 0 0
check_refusal nim E_PARSE   T 0 100000000000
check_refusal nim E_AMOUNT  T 0 1 T 0 1 T 0 1 T 0 1

failed=$((game_failed + hostile_failed))
echo "GAMES_3 3 $passed $failed $refused"
[ "$passed" -eq 3 ] && [ "$failed" -eq 0 ]

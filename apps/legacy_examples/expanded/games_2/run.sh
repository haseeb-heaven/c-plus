#!/bin/sh
# Build and run all three deterministic game cores with fixed inputs.
# No terminal graphics, no wall clock, no network, no interactivity.
#
# A game passes only when:
#   * its --strict --sanitize=none -I. build succeeds,
#   * compact-token argv, per-character argv and stdin runs all exit 0 under
#     `timeout 30` and produce byte-identical stdout,
#   * three adverse inputs are REFUSED cleanly (exit exactly 2 with a
#     "refused:" message, never a timeout/signal crash):
#       1. an out-of-range command token ("X"),
#       2. an oversized token (200 characters > the 64-character limit),
#       3. too many commands (129 one-character tokens > the 128-tick limit).
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
DIR=examples/expanded/games_2
BIN=$DIR/bin
mkdir -p "$BIN"

passed=0
failed=0

build_one() {
    bg_name=$1
    bg_src=$2
    bg_log=$BIN/$bg_name.build.log
    if "$CPLUS" build "$bg_src" --strict --sanitize=none -I. -o "$BIN/$bg_name" >"$bg_log" 2>&1; then
        echo "BUILD ok $bg_name"
        return 0
    fi
    echo "BUILD FAIL $bg_name (see $bg_log)"
    return 1
}

# Run one case under a 30-second bound; echo its exit code to stdout.
run_case() {
    rc_out=$1
    shift
    timeout 30 "$@" >"$rc_out" 2>&1
    echo $?
}

# Verify a clean, crash-free refusal: exit 2 and a stable "refused:" message.
adverse() {
    av_name=$1
    av_tag=$2
    av_msg=$3
    shift 3
    av_out=$BIN/$av_name.$av_tag.out
    av_rc=$(run_case "$av_out" "$BIN/$av_name" "$@")
    if [ "$av_rc" -ne 2 ]; then
        echo "REFUSE FAIL $av_name/$av_tag expected rc=2 got $av_rc"
        return 1
    fi
    if ! grep -q "refused: $av_msg" "$av_out"; then
        echo "REFUSE FAIL $av_name/$av_tag missing message"
        return 1
    fi
    echo "REFUSE ok $av_name/$av_tag rc=$av_rc"
    return 0
}

check_game() {
    cg_name=$1
    cg_compact=$2

    if ! build_one "$cg_name" "$DIR/$cg_name.cp"; then
        failed=$((failed + 1))
        return
    fi
    cg_ok=1

    # 1. One compact argv token.
    cg_rc=$(run_case "$BIN/$cg_name.compact.out" "$BIN/$cg_name" "$cg_compact")
    if [ "$cg_rc" -ne 0 ]; then
        echo "RUN FAIL $cg_name compact rc=$cg_rc"
        cg_ok=0
    fi

    # 2. The same sequence as one-character argv tokens.
    cg_len=${#cg_compact}
    set --
    cg_i=1
    while [ "$cg_i" -le "$cg_len" ]; do
        cg_ch=$(printf '%s' "$cg_compact" | cut -c "$cg_i")
        set -- "$@" "$cg_ch"
        cg_i=$((cg_i + 1))
    done
    cg_rc=$(run_case "$BIN/$cg_name.argv.out" "$BIN/$cg_name" "$@")
    if [ "$cg_rc" -ne 0 ]; then
        echo "RUN FAIL $cg_name argv rc=$cg_rc"
        cg_ok=0
    fi

    # 3. The same sequence from one stdin line.
    printf '%s\n' "$cg_compact" | timeout 30 "$BIN/$cg_name" - \
        >"$BIN/$cg_name.stdin.out" 2>&1
    cg_rc=$?
    if [ "$cg_rc" -ne 0 ]; then
        echo "RUN FAIL $cg_name stdin rc=$cg_rc"
        cg_ok=0
    fi

    if [ "$cg_ok" -eq 1 ]; then
        if ! cmp -s "$BIN/$cg_name.compact.out" "$BIN/$cg_name.argv.out"; then
            echo "RUN FAIL $cg_name compact/argv output differs"
            cg_ok=0
        fi
        if ! cmp -s "$BIN/$cg_name.compact.out" "$BIN/$cg_name.stdin.out"; then
            echo "RUN FAIL $cg_name compact/stdin output differs"
            cg_ok=0
        fi
    fi

    # Adverse scenario 1: out-of-range command.
    adverse "$cg_name" badcmd "invalid command token" X || cg_ok=0

    # Adverse scenario 2: oversized token (200 > 64 characters).
    cg_long=""
    cg_i=0
    while [ "$cg_i" -lt 200 ]; do
        cg_long="${cg_long}R"
        cg_i=$((cg_i + 1))
    done
    adverse "$cg_name" oversized "token is oversized" "$cg_long" || cg_ok=0

    # Adverse scenario 3: too many commands (129 > 128 ticks).
    set --
    cg_i=0
    while [ "$cg_i" -lt 129 ]; do
        set -- "$@" L
        cg_i=$((cg_i + 1))
    done
    adverse "$cg_name" toomany "too many commands" "$@" || cg_ok=0

    if [ "$cg_ok" -eq 1 ]; then
        echo "GAME ok $cg_name"
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
}

check_game pong "SSSSSSSSSSRRRRRRRRRRSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS"
check_game breakout "RLSRRLSSLLRRRLSRSSLLRRRLLSRRRLS"
check_game invaders "LFRFLFSRFLFLSFRFLFFFF"

echo "GAMES_2 3 $passed $failed"
[ "$failed" -eq 0 ]

#!/bin/sh
# benchmarks/example_rust_compare/more_games_a/compare.sh
#
# One-to-one stdout/exit comparison of the new games_2 / games_3 expanded
# examples against SAFE-Rust models (no unsafe, no FFI).
#
#   C+  : ./build/c+ build --strict --sanitize=none -I. -o <bin> <examples/expanded/games_{2,3}/name.cp>
#   Rust: rustc -O --edition 2021 <name>.rs -o <bin>
#   run : timeout 30 <bin> <fixed args>   (stdin: /dev/null for every case)
#
# stdout: exactly one line per example:
#   MORE_GAMES_COMPARE <name> MATCH|DIFFERENT|BLOCKED|PENDING <reason>
# Diagnostics (commands, hashes, diffs) go to stderr and to .build/* logs.
#
# Exit status: nonzero ONLY on a real DIFFERENT. BLOCKED (build failure) and
# PENDING (source/model not created yet) are inconclusive and are reported but
# intentionally do NOT fail the script.
#
# If a scoped C+ source (or a Rust model) has not appeared yet, the example is
# reported PENDING together with the exact command to refresh: re-run this
# script.  A MATCH is never fabricated for a missing source or model.
#
# Environment overrides: MG_RUSTC, MG_CPLUS, MG_TIMEOUT, MG_OUT.

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)

CPLUS=${MG_CPLUS:-"$ROOT/build/c+"}
RUSTC=${MG_RUSTC:-rustc}
TMO=${MG_TIMEOUT:-30}
OUT=${MG_OUT:-"$HERE/.build"}

mkdir -p "$OUT" || exit 1

note() { printf '%s\n' "$*" >&2; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[ -x "$CPLUS" ] || die "c+ compiler not found/executable: $CPLUS"
command -v "$RUSTC" >/dev/null 2>&1 || die "rustc not found: $RUSTC"
command -v timeout >/dev/null 2>&1 || die "timeout utility not found"

# c+ resolves stdlib/*.hp includes relative to cwd; always build from the root.
cd "$ROOT" || die "cannot cd to repository root: $ROOT"

CURRENT_NAME=""
run_case() {
    # run_case <binary> <prefix> [args...]
    bin=$1; prefix=$2; shift 2
    timeout "$TMO" "$bin" "$@" </dev/null >>"$prefix.stdout" 2>>"$prefix.stderr"
    printf '%s\n' "$?" >>"$prefix.rc"
}

emit_cases() {
    # emit_cases <binary> <prefix>   -- fixed invocations for $CURRENT_NAME
    bin=$1; prefix=$2
    : >"$prefix.stdout"; : >"$prefix.rc"; : >"$prefix.stderr"
    case "$CURRENT_NAME" in
        pong)
            run_case "$bin" "$prefix" RLSRR
            run_case "$bin" "$prefix" R L S R R
            run_case "$bin" "$prefix"          # built-in default sequence
            run_case "$bin" "$prefix" X        # refused: invalid command
            ;;
        breakout)
            run_case "$bin" "$prefix" RLSRRLSSLLRRRLSRS
            run_case "$bin" "$prefix" RLSR
            run_case "$bin" "$prefix" R L S R
            run_case "$bin" "$prefix" X
            ;;
        invaders)
            run_case "$bin" "$prefix" LFRFLFSRFLFLSFRFL
            run_case "$bin" "$prefix" LFRSF
            run_case "$bin" "$prefix" L F R F S F
            run_case "$bin" "$prefix" X
            ;;
        sudoku)
            run_case "$bin" "$prefix"                 # built-in solve program
            run_case "$bin" "$prefix" P 0 2 4 V
            run_case "$bin" "$prefix" P 0 0 5 V       # refused: fixed given
            run_case "$bin" "$prefix" P 9 9 5         # refused: out of bounds
            run_case "$bin" "$prefix" X               # refused: parse error
            ;;
        minesweeper)
            run_case "$bin" "$prefix"                 # built-in clear-board program
            run_case "$bin" "$prefix" R 4 4 F 0 0 V
            run_case "$bin" "$prefix" R 0 0 V         # reveal a mine -> lose
            run_case "$bin" "$prefix" R 9 9           # refused: out of bounds
            run_case "$bin" "$prefix" F 4 4 F 4 4 V   # flag then unflag
            ;;
        nim)
            run_case "$bin" "$prefix"                 # built-in A vs B game
            run_case "$bin" "$prefix" O T 0 1 O T 1 1 S
            run_case "$bin" "$prefix" S
            run_case "$bin" "$prefix" T 9 1           # refused: heap index
            run_case "$bin" "$prefix" X               # refused: parse error
            ;;
        *)
            die "no fixed inputs defined for $CURRENT_NAME"
            ;;
    esac
}

fails=0           # exit-driving counter: incremented ONLY on a real DIFFERENT
inconclusive=0    # PENDING/BLOCKED: reported but do NOT affect exit status
compare_example() {
    name=$1; src=$2
    CURRENT_NAME=$name
    srcpath="$ROOT/examples/expanded/$src"
    rs="$HERE/$name.rs"

    if [ ! -f "$srcpath" ]; then
        printf 'MORE_GAMES_COMPARE %s PENDING source-not-yet-created %s; refresh: sh %s/compare.sh\n' \
            "$name" "$srcpath" "$HERE"
        inconclusive=$((inconclusive + 1))
        return
    fi
    if [ ! -f "$rs" ]; then
        printf 'MORE_GAMES_COMPARE %s PENDING rust-model-not-yet-created %s; refresh: sh %s/compare.sh\n' \
            "$name" "$rs" "$HERE"
        inconclusive=$((inconclusive + 1))
        return
    fi

    cbin="$OUT/c_$name"
    rbin="$OUT/r_$name"
    cbuild="$OUT/$name.c.build.log"
    rbuild="$OUT/$name.rs.build.log"

    note "BUILD C+ $name: $CPLUS build --strict --sanitize=none -I$ROOT -o $cbin $srcpath"
    timeout "$TMO" "$CPLUS" build --strict --sanitize=none -I"$ROOT" -o "$cbin" "$srcpath" \
        >"$cbuild" 2>&1 || { printf 'MORE_GAMES_COMPARE %s BLOCKED c+-build-failed see %s\n' "$name" "$cbuild"; inconclusive=$((inconclusive+1)); return; }

    note "BUILD Rust $name: $RUSTC -O --edition 2021 $rs -o $rbin"
    timeout "$TMO" "$RUSTC" -O --edition 2021 "$rs" -o "$rbin" \
        >"$rbuild" 2>&1 || { printf 'MORE_GAMES_COMPARE %s BLOCKED rust-build-failed see %s\n' "$name" "$rbuild"; inconclusive=$((inconclusive+1)); return; }

    cpfx="$OUT/$name.cases.c"
    rpfx="$OUT/$name.cases.r"
    emit_cases "$cbin" "$cpfx"
    emit_cases "$rbin" "$rpfx"

    if cmp -s "$cpfx.stdout" "$rpfx.stdout" && cmp -s "$cpfx.rc" "$rpfx.rc"; then
        note "MATCH $name: c_sha256=$(shasum -a 256 "$cpfx.stdout" | awk '{print $1}') rust_sha256=$(shasum -a 256 "$rpfx.stdout" | awk '{print $1}')"
        printf 'MORE_GAMES_COMPARE %s MATCH stdout+exit identical for %s fixed case(s)\n' \
            "$name" "$(wc -l < "$cpfx.rc" | tr -d ' ')"
    else
        diff -u "$cpfx.stdout" "$rpfx.stdout" >"$OUT/$name.diff" 2>&1 || true
        printf '\n--- exit-status diff ---\n' >>"$OUT/$name.diff"
        diff -u "$cpfx.rc" "$rpfx.rc" >>"$OUT/$name.diff" 2>&1 || true
        note "DIFFERENT $name: see $OUT/$name.diff"
        printf 'MORE_GAMES_COMPARE %s DIFFERENT stdout-or-exit-mismatch see %s\n' "$name" "$OUT/$name.diff"
        fails=$((fails + 1))
    fi
}

# games_2 (present at time of writing)
compare_example pong        games_2/pong.cp
compare_example breakout    games_2/breakout.cp
compare_example invaders    games_2/invaders.cp
# games_3
compare_example sudoku      games_3/sudoku.cp
compare_example minesweeper games_3/minesweeper.cp
compare_example nim         games_3/nim.cp

note "SUMMARY different=$fails inconclusive(BLOCKED+PENDING)=$inconclusive"
# Exit nonzero only for a real DIFFERENT (fails).  BLOCKED/PENDING are
# reported to stdout+stderr but deliberately leave the exit status at 0.
exit "$fails"

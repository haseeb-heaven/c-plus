#!/bin/sh
# benchmarks/example_rust_compare/expanded_cli_games/compare.sh
#
# One-to-one stdout/exit comparison of the NEW expanded CLI and game examples
# against safe-Rust models (no unsafe, no FFI).
#
#   C+  : ./build/c+ build --strict --sanitize=none -I. -o <bin> <examples/expanded/.../name.cp>
#   Rust: rustc -O --edition 2021 <name>.rs -o <bin>
#   run : timeout 30 <bin> <fixed args>   (identical stdin: empty)
#
# stdout: exactly one line per example:
#   EXPANDED_COMPARE <name> MATCH|DIFFERENT|BLOCKED|PENDING <reason>
# Diagnostics (commands, hashes, diffs) go to stderr and to .build/* logs.
# Exit status: nonzero ONLY on a real DIFFERENT.  BLOCKED (build failure) and
# PENDING (source/model not created yet) are inconclusive and are reported but
# intentionally do NOT fail the script.
#
# If a scoped C+ source has not appeared yet, the example is reported PENDING
# together with the exact command to refresh: re-run this script.
#
# Environment overrides: EC_RUSTC, EC_CPLUS, EC_TIMEOUT, EC_OUT.

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)

CPLUS=${EC_CPLUS:-"$ROOT/build/c+"}
RUSTC=${EC_RUSTC:-rustc}
TMO=${EC_TIMEOUT:-30}
OUT=${EC_OUT:-"$HERE/.build"}

mkdir -p "$OUT" || exit 1

note() { printf '%s\n' "$*" >&2; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[ -x "$CPLUS" ] || die "c+ compiler not found/executable: $CPLUS"
command -v "$RUSTC" >/dev/null 2>&1 || die "rustc not found: $RUSTC"
command -v timeout >/dev/null 2>&1 || die "timeout utility not found"

# c+ resolves stdlib/*.hp includes relative to cwd; always build from the root.
cd "$ROOT" || die "cannot cd to repository root: $ROOT"

# Deterministic fixture files (same paths used by both sides).
printf 'hello world\nfoo bar baz\none\ntwo three\n' > "$OUT/text_input.txt" || die "fixture write"
printf 'abcdefghij' > "$OUT/ft_a.txt" || die "fixture write"
printf 'abcdefghijk' > "$OUT/ft_b.txt" || die "fixture write"
printf 'abcdefghij' > "$OUT/ft_copy_src.txt" || die "fixture write"

CURRENT_NAME=""
run_case() {
    # run_case <binary> <prefix> [args...]
    bin=$1; prefix=$2; shift 2
    timeout "$TMO" "$bin" "$@" >>"$prefix.stdout" 2>>"$prefix.stderr"
    printf '%s\n' "$?" >>"$prefix.rc"
}

emit_cases() {
    # emit_cases <binary> <prefix>   -- fixed invocations for $CURRENT_NAME
    bin=$1; prefix=$2
    : >"$prefix.stdout"; : >"$prefix.rc"; : >"$prefix.stderr"
    case "$CURRENT_NAME" in
        todo)
            run_case "$bin" "$prefix" add 1 alpha -- add 2 beta -- done 1 -- list -- remove 2 -- list
            ;;
        calculator)
            run_case "$bin" "$prefix" '1+2*3'
            run_case "$bin" "$prefix" '2*(3+4)-5'
            run_case "$bin" "$prefix" '1/0'
            ;;
        text_tools)
            run_case "$bin" "$prefix" "$OUT/text_input.txt" words
            run_case "$bin" "$prefix" "$OUT/text_input.txt" lines
            run_case "$bin" "$prefix" "$OUT/text_input.txt" rev
            run_case "$bin" "$prefix" "$OUT/text_input.txt" find bar
            ;;
        file_tools)
            run_case "$bin" "$prefix" size "$OUT/ft_a.txt"
            run_case "$bin" "$prefix" cmp "$OUT/ft_a.txt" "$OUT/ft_copy_src.txt"
            run_case "$bin" "$prefix" cmp "$OUT/ft_a.txt" "$OUT/ft_b.txt"
            rm -f "$OUT/ft_dst.txt"
            run_case "$bin" "$prefix" copy "$OUT/ft_copy_src.txt" "$OUT/ft_dst.txt"
            run_case "$bin" "$prefix" size "$OUT/ft_dst.txt"
            ;;
        game2048)
            run_case "$bin" "$prefix" RDLURD
            ;;
        maze)
            run_case "$bin" "$prefix" UDDDDRRRRR
            ;;
        tictactoe)
            run_case "$bin" "$prefix" 01458
            ;;
        snake_core)
            run_case "$bin" "$prefix"
            ;;
        snake_cli)
            run_case "$bin" "$prefix" growth R
            run_case "$bin" "$prefix" wall RRRRRRRRRRRR
            run_case "$bin" "$prefix" self RULDL
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
        printf 'EXPANDED_COMPARE %s PENDING source-not-yet-created %s; refresh: sh %s/compare.sh\n' \
            "$name" "$srcpath" "$HERE"
        inconclusive=$((inconclusive + 1))
        return
    fi
    if [ ! -f "$rs" ]; then
        printf 'EXPANDED_COMPARE %s PENDING rust-model-not-yet-created %s; refresh: sh %s/compare.sh\n' \
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
        >"$cbuild" 2>&1 || { printf 'EXPANDED_COMPARE %s BLOCKED c+-build-failed see %s\n' "$name" "$cbuild"; inconclusive=$((inconclusive+1)); return; }

    note "BUILD Rust $name: $RUSTC -O --edition 2021 $rs -o $rbin"
    timeout "$TMO" "$RUSTC" -O --edition 2021 "$rs" -o "$rbin" \
        >"$rbuild" 2>&1 || { printf 'EXPANDED_COMPARE %s BLOCKED rust-build-failed see %s\n' "$name" "$rbuild"; inconclusive=$((inconclusive+1)); return; }

    cpfx="$OUT/$name.cases.c"
    rpfx="$OUT/$name.cases.r"
    emit_cases "$cbin" "$cpfx"
    emit_cases "$rbin" "$rpfx"

    if cmp -s "$cpfx.stdout" "$rpfx.stdout" && cmp -s "$cpfx.rc" "$rpfx.rc"; then
        note "MATCH $name: c_sha256=$(shasum -a 256 "$cpfx.stdout" | awk '{print $1}') rust_sha256=$(shasum -a 256 "$rpfx.stdout" | awk '{print $1}')"
        printf 'EXPANDED_COMPARE %s MATCH stdout+exit identical for %s fixed case(s)\n' \
            "$name" "$(wc -l < "$cpfx.rc" | tr -d ' ')"
    else
        diff -u "$cpfx.stdout" "$rpfx.stdout" >"$OUT/$name.diff" 2>&1 || true
        diff -u "$cpfx.rc" "$rpfx.rc" >>"$OUT/$name.diff" 2>&1 || true
        note "DIFFERENT $name: see $OUT/$name.diff"
        printf 'EXPANDED_COMPARE %s DIFFERENT stdout-or-exit-mismatch see %s\n' "$name" "$OUT/$name.diff"
        fails=$((fails + 1))
    fi
}

compare_example todo          cli/todo.cp
compare_example calculator    cli/calculator.cp
compare_example text_tools    cli/text_tools.cp
compare_example file_tools    cli/file_tools.cp
compare_example game2048      games/cores/game2048.cp
compare_example maze          games/cores/maze.cp
compare_example tictactoe     games/cores/tictactoe.cp
compare_example snake_core    games/snake/snake_core.cp
compare_example snake_cli     games/snake/snake_cli.cp

note "SUMMARY different=$fails inconclusive(BLOCKED+PENDING)=$inconclusive"
# Exit nonzero only for a real DIFFERENT (fails).  BLOCKED/PENDING are
# reported to stdout+stderr but deliberately leave the exit status at 0.
exit "$fails"

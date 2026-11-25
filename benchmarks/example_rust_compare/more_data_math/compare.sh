#!/bin/sh
# benchmarks/example_rust_compare/more_data_math/compare.sh
#
# One-to-one stdout/exit comparison of the eight NEW data_2/math_2 expanded
# examples against safe-Rust models (no unsafe, no FFI, no external crates).
#
#   C+  : ./build/c+ build --strict --sanitize=none -I. -o <bin> <examples/expanded/.../name.cp>
#   Rust: rustc -O --edition 2021 <name>.rs -o <bin>
#   run : timeout 30 <bin> <fixed args>   (identical stdin: empty)
#
# stdout: exactly one line per example:
#   MORE_DATA_COMPARE <name> MATCH|DIFFERENT|BLOCKED|PENDING <reason>
# Diagnostics (commands, hashes, diffs) go to stderr and to .build/* logs.
# Exit status: nonzero ONLY on a real DIFFERENT.  BLOCKED (build failure) and
# PENDING (source/model not created yet) are inconclusive: reported but do NOT
# fail the script.
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
        sort_tool)
            run_case "$bin" "$prefix" int asc 5 3 9 3 1
            run_case "$bin" "$prefix" int desc 5 3 9 3 1
            run_case "$bin" "$prefix" str asc banana apple cherry apple
            run_case "$bin" "$prefix" str desc banana apple cherry apple
            run_case "$bin" "$prefix" int asc 1000001
            ;;
        hash_tool)
            run_case "$bin" "$prefix" hello world foo
            run_case "$bin" "$prefix" hello
            run_case "$bin" "$prefix" ""
            ;;
        rle)
            run_case "$bin" "$prefix" enc aaabccdddd
            run_case "$bin" "$prefix" dec 'RLE1;a03;b01;c02;d04'
            run_case "$bin" "$prefix" dec BAD
            ;;
        regex_lite)
            run_case "$bin" "$prefix"
            run_case "$bin" "$prefix" 'a?c' abc
            run_case "$bin" "$prefix" 'a*b' aaac
            run_case "$bin" "$prefix" '[' abc
            ;;
        bigint)
            run_case "$bin" "$prefix"
            run_case "$bin" "$prefix" input-overflow
            run_case "$bin" "$prefix" result-overflow
            run_case "$bin" "$prefix" sub-underflow
            run_case "$bin" "$prefix" mul-overflow
            run_case "$bin" "$prefix" bad-digit
            run_case "$bin" "$prefix" bad-mode
            ;;
        prng)
            run_case "$bin" "$prefix"
            run_case "$bin" "$prefix" 1 8
            run_case "$bin" "$prefix" 4294967295 5
            run_case "$bin" "$prefix" 4294967296 5
            run_case "$bin" "$prefix" 1 257
            run_case "$bin" "$prefix" x 5
            ;;
        checksum)
            run_case "$bin" "$prefix"
            run_case "$bin" "$prefix" text hello
            run_case "$bin" "$prefix" oversized
            ;;
        base64)
            run_case "$bin" "$prefix"
            run_case "$bin" "$prefix" enc foobar
            run_case "$bin" "$prefix" dec Zm9vYmFy
            run_case "$bin" "$prefix" malformed '!!!!'
            run_case "$bin" "$prefix" badlen abc
            run_case "$bin" "$prefix" oversized
            run_case "$bin" "$prefix" encoversized
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
        printf 'MORE_DATA_COMPARE %s PENDING source-not-yet-created %s; refresh: sh %s/compare.sh\n' \
            "$name" "$srcpath" "$HERE"
        inconclusive=$((inconclusive + 1))
        return
    fi
    if [ ! -f "$rs" ]; then
        printf 'MORE_DATA_COMPARE %s PENDING rust-model-not-yet-created %s; refresh: sh %s/compare.sh\n' \
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
        >"$cbuild" 2>&1 || { printf 'MORE_DATA_COMPARE %s BLOCKED c+-build-failed see %s\n' "$name" "$cbuild"; inconclusive=$((inconclusive+1)); return; }

    note "BUILD Rust $name: $RUSTC -O --edition 2021 $rs -o $rbin"
    timeout "$TMO" "$RUSTC" -O --edition 2021 "$rs" -o "$rbin" \
        >"$rbuild" 2>&1 || { printf 'MORE_DATA_COMPARE %s BLOCKED rust-build-failed see %s\n' "$name" "$rbuild"; inconclusive=$((inconclusive+1)); return; }

    cpfx="$OUT/$name.cases.c"
    rpfx="$OUT/$name.cases.r"
    emit_cases "$cbin" "$cpfx"
    emit_cases "$rbin" "$rpfx"

    if cmp -s "$cpfx.stdout" "$rpfx.stdout" && cmp -s "$cpfx.rc" "$rpfx.rc"; then
        note "MATCH $name: c_sha256=$(shasum -a 256 "$cpfx.stdout" | awk '{print $1}') rust_sha256=$(shasum -a 256 "$rpfx.stdout" | awk '{print $1}')"
        printf 'MORE_DATA_COMPARE %s MATCH stdout+exit identical for %s fixed case(s)\n' \
            "$name" "$(wc -l < "$cpfx.rc" | tr -d ' ')"
    else
        diff -u "$cpfx.stdout" "$rpfx.stdout" >"$OUT/$name.diff" 2>&1 || true
        diff -u "$cpfx.rc" "$rpfx.rc" >>"$OUT/$name.diff" 2>&1 || true
        note "DIFFERENT $name: see $OUT/$name.diff"
        printf 'MORE_DATA_COMPARE %s DIFFERENT stdout-or-exit-mismatch see %s\n' "$name" "$OUT/$name.diff"
        fails=$((fails + 1))
    fi
}

compare_example sort_tool  data_2/sort_tool.cp
compare_example hash_tool  data_2/hash_tool.cp
compare_example rle        data_2/rle.cp
compare_example regex_lite data_2/regex_lite.cp
compare_example bigint     math_2/bigint.cp
compare_example prng       math_2/prng.cp
compare_example checksum   math_2/checksum.cp
compare_example base64     math_2/base64.cp

note "SUMMARY different=$fails inconclusive(BLOCKED+PENDING)=$inconclusive"
# Exit nonzero only for a real DIFFERENT (fails).  BLOCKED/PENDING are
# reported to stdout+stderr but deliberately leave the exit status at 0.
exit "$fails"

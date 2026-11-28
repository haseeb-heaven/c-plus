#!/bin/sh
# benchmarks/example_rust_compare/demos_i_l/compare.sh
#
# Build and run each original C+ demo (I..L) and its safe-Rust equivalent,
# then compare captured stdout and exit status under a hard 30s timeout.
#
# Original build:  build/c+ build <demo>.c --strict --sanitize=none -o <bin>
#                  (the C+ driver lowers the C+ extensions and compiles the
#                   generated C with /usr/bin/clang, linking the C+ archives)
# Rust build:      rustc -O <name>.rs -o <bin>          (no unsafe, no FFI)
# Run:             timeout 30 <bin> < deterministic stdin
#
# One line is printed per example:
#   EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>
#
# All artifacts (binaries, build logs, stdout/stderr/exit captures) are written
# under results/. No source file in the repository is modified.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd) || exit 2
HERE="$ROOT/benchmarks/example_rust_compare/demos_i_l"
RESULTS="$HERE/results"
BIN="$RESULTS/bin"
LOGS="$RESULTS/logs"
RUNS="$RESULTS/runs"

mkdir -p "$BIN" "$LOGS" "$RUNS" || exit 2

# Resolve toolchain paths BEFORE pinning PATH (rustc lives in ~/.cargo/bin).
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
RUSTC=${CPLUS_RUSTC:-rustc}
case "$CPLUS" in /*) ;; *) CPLUS=$(command -v "$CPLUS" 2>/dev/null || printf '%s' "$CPLUS") ;; esac
case "$RUSTC" in /*) ;; *) RUSTC=$(command -v "$RUSTC" 2>/dev/null || printf '%s' "$RUSTC") ;; esac
TIMEOUT=$(command -v timeout 2>/dev/null || true)

# Pin the toolchain environment (repo convention) after resolving the paths.
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export PATH
export LC_ALL=C
CPLUS_CLANG=${CPLUS_CLANG:-/usr/bin/clang}
export CPLUS_CLANG

BUILD_TIMEOUT=${CPLUS_BUILD_TIMEOUT:-30}
RUN_TIMEOUT=${CPLUS_RUN_TIMEOUT:-30}
case "$BUILD_TIMEOUT" in ''|*[!0-9]*) BUILD_TIMEOUT=30 ;; esac
case "$RUN_TIMEOUT" in ''|*[!0-9]*) RUN_TIMEOUT=30 ;; esac

# Deterministic stdin: J reads "count ratio label"; the others read nothing.
printf '7 2.5 demo\n' > "$RUNS/J_modern_io.stdin"
: > "$RUNS/empty.stdin"

# run_bounded <out> <err> <exitfile> <secs> <cmd...>
run_bounded() {
    _of=$1; _ef=$2; _xf=$3; _to=$4; shift 4
    if [ -z "$TIMEOUT" ]; then
        printf '%s\n' 'missing timeout utility' > "$_ef"
        printf '%s\n' 125 > "$_xf"
        : > "$_of"
        return 0
    fi
    "$TIMEOUT" -k 2 "$_to" "$@" > "$_of" 2> "$_ef"
    printf '%s\n' "$?" > "$_xf"
    return 0
}

first_error() {
    grep -m1 -E 'error:|fatal error:' "$1" 2>/dev/null | head -c 240
    [ -n "$(grep -m1 -E 'error:|fatal error:' "$1" 2>/dev/null)" ] || \
        head -n1 "$1" 2>/dev/null | head -c 240
}

compare_one() {
    name=$1
    input="$RUNS/empty.stdin"
    [ "$name" != "J_modern_io" ] || input="$RUNS/J_modern_io.stdin"

    # --- build the original C+ source under the prescribed strict flags ---
    run_bounded "$LOGS/$name.cplus.build.out" "$LOGS/$name.cplus.build.err" \
        "$LOGS/$name.cplus.build.exit" "$BUILD_TIMEOUT" \
        "$CPLUS" build "$ROOT/demos/$name.c" --strict --sanitize=none \
        -o "$BIN/cplus_$name"
    cr=$(cat "$LOGS/$name.cplus.build.exit")
    if [ "$cr" -ne 0 ]; then
        printf 'EXAMPLE_COMPARE %s BLOCKED C+ strict build failed (rc=%s): %s\n' \
            "$name" "$cr" "$(first_error "$LOGS/$name.cplus.build.err")"
        return
    fi

    # --- build the safe-Rust equivalent ---
    run_bounded "$LOGS/$name.rust.build.out" "$LOGS/$name.rust.build.err" \
        "$LOGS/$name.rust.build.exit" "$BUILD_TIMEOUT" \
        "$RUSTC" -O "$HERE/$name.rs" -o "$BIN/rust_$name"
    rr=$(cat "$LOGS/$name.rust.build.exit")
    if [ "$rr" -ne 0 ]; then
        printf 'EXAMPLE_COMPARE %s BLOCKED Rust build failed (rc=%s): %s\n' \
            "$name" "$rr" "$(first_error "$LOGS/$name.rust.build.err")"
        return
    fi

    # --- run both with identical deterministic stdin, bounded ---
    run_bounded "$RUNS/$name.cplus.out" "$RUNS/$name.cplus.err" \
        "$RUNS/$name.cplus.exit" "$RUN_TIMEOUT" "$BIN/cplus_$name" < "$input"
    run_bounded "$RUNS/$name.rust.out" "$RUNS/$name.rust.err" \
        "$RUNS/$name.rust.exit" "$RUN_TIMEOUT" "$BIN/rust_$name" < "$input"
    crx=$(cat "$RUNS/$name.cplus.exit")
    rrx=$(cat "$RUNS/$name.rust.exit")

    if [ "$crx" -eq 124 ] || [ "$rrx" -eq 124 ]; then
        printf 'EXAMPLE_COMPARE %s BLOCKED run exceeded %ss timeout (cplus=%s rust=%s)\n' \
            "$name" "$RUN_TIMEOUT" "$crx" "$rrx"
        return
    fi

    if cmp -s "$RUNS/$name.cplus.out" "$RUNS/$name.rust.out" && \
       [ "$crx" -eq "$rrx" ]; then
        printf 'EXAMPLE_COMPARE %s MATCH stdout byte-identical, exit=%s (both)\n' \
            "$name" "$crx"
    else
        printf 'EXAMPLE_COMPARE %s DIFFERENT exit cplus=%s rust=%s; stdout cplus=[%s] rust=[%s]\n' \
            "$name" "$crx" "$rrx" \
            "$(tr '\n' '|' < "$RUNS/$name.cplus.out")" \
            "$(tr '\n' '|' < "$RUNS/$name.rust.out")"
    fi
}

echo "compare: root=$ROOT"
echo "compare: cplus=$CPLUS (CPLUS_CLANG=$CPLUS_CLANG)"
echo "compare: rustc=$RUSTC"
echo "compare: build_timeout=${BUILD_TIMEOUT}s run_timeout=${RUN_TIMEOUT}s"
echo "compare: artifacts=$RESULTS"
echo

compare_one I_raii_safe
compare_one J_modern_io
compare_one K_state_class
compare_one L_class_lifecycle

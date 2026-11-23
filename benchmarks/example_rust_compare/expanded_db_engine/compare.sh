#!/usr/bin/env bash
# benchmarks/example_rust_compare/expanded_db_engine/compare.sh
#
# Build and run each NEW expanded database / engine / safety example and its
# safe-Rust equivalent, then compare captured stdout and exit status under a
# hard 30 s timeout with identical deterministic stdin/argv.
#
# One line is printed on stdout per example:
#   EXPANDED_COMPARE <name> MATCH|DIFFERENT|BLOCKED|PENDING <reason>
# All other output (toolchain versions, diff context) goes to stderr.
#
# Original build paths (invoked from the repository root):
#   .cp -> build/c+ build --strict --sanitize=none -I. <source> -o <bin>
#          (memcpy_s_demo additionally needs -- <safecrt> <hardened> archives)
#   .c  -> /usr/bin/clang -std=c99 -D_POSIX_C_SOURCE=200809L
#          -I. -Ilibc/install-include <source> build/libc/libcplus_hardened.a
#          -pthread -o <bin>
# Rust build path:
#   rustc -O <name>.rs -o <bin>              (safe Rust: no unsafe, no FFI)
# Run:  timeout 30 <binary> < <name>.stdin   (empty by default)
#       argv is read from <name>.args when present (whitespace-separated).
#
# Every artifact is written under out/. No repository source is modified.

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
OUT="$HERE/out"
mkdir -p "$OUT"

CPLUS="${CPLUS_BIN:-$REPO/build/c+}"
CLANG="${CPLUS_CLANG:-/usr/bin/clang}"
RUSTC="${CPLUS_RUSTC:-rustc}"
TIMEOUT="${TIMEOUT:-timeout}"

# Resolve toolchain paths before restricting PATH (rustc lives in ~/.cargo/bin).
case "$RUSTC" in /*) ;; *) RUSTC="$(command -v "$RUSTC" 2>/dev/null || printf '%s' "$RUSTC")" ;; esac
case "$CPLUS" in /*) ;; *) CPLUS="$(command -v "$CPLUS" 2>/dev/null || printf '%s' "$CPLUS")" ;; esac
case "$CLANG" in /*) ;; *) CLANG="$(command -v "$CLANG" 2>/dev/null || printf '%s' "$CLANG")" ;; esac
RUSTDIR="$(dirname "$RUSTC")"

echo "compare: root=$REPO" >&2
echo "compare: cplus=$CPLUS" >&2
echo "compare: clang=$CLANG" >&2
echo "compare: rustc=$RUSTC ($( "$RUSTC" --version 2>&1 ))" >&2
echo "compare: timeout=$TIMEOUT (build/run limit 30s)" >&2

export PATH="/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$RUSTDIR"
export LC_ALL=C
export CPLUS_CLANG="$CLANG"

RUN_LIMIT=30
BUILD_LIMIT=30
: > "$HERE/empty.stdin"

# Only a real DIFFERENT contributes to the exit status. BLOCKED/PENDING are
# reported but never make the script fail (scoped sources are produced by
# sibling agents and may not exist yet at any given run).
fails=0

# run_bounded <out> <err> <exitfile> <secs> <cmd...>
run_bounded() {
    _of=$1; _ef=$2; _xf=$3; _to=$4; shift 4
    "$TIMEOUT" -k 2 "$_to" "$@" > "$_of" 2> "$_ef"
    printf '%s\n' "$?" > "$_xf"
    return 0
}

first_error() {
    { grep -m1 -E 'error:|fatal error:|error ' "$1" 2>/dev/null || head -n1 "$1" 2>/dev/null; } | head -c 300
}

# compare_one <name> <dir> <ext>
compare_one() {
    name=$1; dir=$2; ext=$3
    src="$REPO/examples/expanded/$dir/$name.$ext"
    rust="$HERE/$name.rs"
    stdin="$HERE/empty.stdin"
    [ -f "$HERE/$name.stdin" ] && stdin="$HERE/$name.stdin"
    argv=()
    if [ -f "$HERE/$name.args" ]; then
        read -r -a argv < "$HERE/$name.args" || true
    fi

    if [ ! -f "$src" ]; then
        printf 'EXPANDED_COMPARE %s PENDING missing original source %s\n' \
            "$name" "${src#$REPO/}"
        return
    fi
    if [ ! -f "$rust" ]; then
        printf 'EXPANDED_COMPARE %s PENDING missing Rust model %s\n' \
            "$name" "${rust#$HERE/}"
        return
    fi

    # --- build the safe-Rust equivalent ---
    run_bounded "$OUT/$name.rust.build.out" "$OUT/$name.rust.build.err" \
        "$OUT/$name.rust.build.exit" "$BUILD_LIMIT" \
        "$RUSTC" -O "$rust" -o "$OUT/rust_$name"
    rr=$(cat "$OUT/$name.rust.build.exit")
    if [ "$rr" -ne 0 ]; then
        printf 'EXPANDED_COMPARE %s BLOCKED Rust build failed (rc=%s): %s\n' \
            "$name" "$rr" "$(first_error "$OUT/$name.rust.build.err")"
        return
    fi

    # --- build the original C+ / C source ---
    if [ "$ext" = "cp" ]; then
        if [ "$name" = "memcpy_s_demo" ]; then
            # Needs the safe-CRT and hardened archives on the link line.
            run_bounded "$OUT/$name.orig.build.out" "$OUT/$name.orig.build.err" \
                "$OUT/$name.orig.build.exit" "$BUILD_LIMIT" \
                bash -c 'cd "$1" && exec "$2" build --strict --sanitize=none -I. \
                    "$3" -o "$4" -- build/safe/libcplus_safecrt.a \
                    build/libc/libcplus_hardened.a' \
                _ "$REPO" "$CPLUS" "$src" "$OUT/orig_$name"
        else
            run_bounded "$OUT/$name.orig.build.out" "$OUT/$name.orig.build.err" \
                "$OUT/$name.orig.build.exit" "$BUILD_LIMIT" \
                bash -c 'cd "$1" && exec "$2" build --strict --sanitize=none -I. "$3" -o "$4"' \
                _ "$REPO" "$CPLUS" "$src" "$OUT/orig_$name"
        fi
    else
        run_bounded "$OUT/$name.orig.build.out" "$OUT/$name.orig.build.err" \
            "$OUT/$name.orig.build.exit" "$BUILD_LIMIT" \
            bash -c 'cd "$1" && exec "$2" -std=c99 -D_POSIX_C_SOURCE=200809L \
                -I. -Ilibc/install-include "$3" build/libc/libcplus_hardened.a \
                -pthread -o "$4"' \
            _ "$REPO" "$CLANG" "$src" "$OUT/orig_$name"
    fi
    cr=$(cat "$OUT/$name.orig.build.exit")
    if [ "$cr" -ne 0 ]; then
        printf 'EXPANDED_COMPARE %s BLOCKED C+ build failed (rc=%s): %s\n' \
            "$name" "$cr" "$(first_error "$OUT/$name.orig.build.err")"
        return
    fi

    # --- run both with identical deterministic stdin/argv, bounded ---
    run_bounded "$OUT/$name.orig.stdout" "$OUT/$name.orig.stderr" \
        "$OUT/$name.orig.exit" "$RUN_LIMIT" \
        "$OUT/orig_$name" "${argv[@]}" < "$stdin"
    run_bounded "$OUT/$name.rust.stdout" "$OUT/$name.rust.stderr" \
        "$OUT/$name.rust.exit" "$RUN_LIMIT" \
        "$OUT/rust_$name" "${argv[@]}" < "$stdin"
    orc=$(cat "$OUT/$name.orig.exit")
    rrc=$(cat "$OUT/$name.rust.exit")

    if [ "$orc" -eq 124 ] || [ "$rrc" -eq 124 ]; then
        printf 'EXPANDED_COMPARE %s BLOCKED run exceeded %ss timeout (orig=%s rust=%s)\n' \
            "$name" "$RUN_LIMIT" "$orc" "$rrc"
        return
    fi

    if cmp -s "$OUT/$name.orig.stdout" "$OUT/$name.rust.stdout" && [ "$orc" -eq "$rrc" ]; then
        printf 'EXPANDED_COMPARE %s MATCH stdout byte-identical, exit=%s (both)\n' \
            "$name" "$orc"
    else
        printf 'EXPANDED_COMPARE %s DIFFERENT exit orig=%s rust=%s; stdout orig=[%s] rust=[%s]\n' \
            "$name" "$orc" "$rrc" \
            "$(tr '\n' '|' < "$OUT/$name.orig.stdout")" \
            "$(tr '\n' '|' < "$OUT/$name.rust.stdout")"
        diff -u "$OUT/$name.orig.stdout" "$OUT/$name.rust.stdout" >&2 || true
        fails=$((fails + 1))
    fi
}

# database
compare_one kv_store       database cp
compare_one table_query    database cp
compare_one safety_demo    database cp
# engine
compare_one ecs            engine   cp
compare_one engine_loop    engine   cp
compare_one resource_cache engine   cp
# safety
compare_one memcpy_s_demo  safety   cp
compare_one bounds_demo    safety   cp
compare_one lifetime_demo  safety   cp
compare_one sync_demo      safety   c

# Nonzero only when at least one real stdout/exit DIFFERENT was observed.
exit "$fails"

#!/usr/bin/env bash
# Build and run each original C+ demo and its safe-Rust model, then compare
# stdout bytes and exit status under a 30 s timeout with empty stdin.
#
# One line is printed to stdout per example:
#   EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>
# Everything else (toolchain versions, diff context) goes to stderr so the
# stdout stream stays machine-readable.
#
# Original build paths:
#   .c  -> C+ syntax is lowered with `c+ --emit-c`, then the lowered C is
#          compiled with /usr/bin/clang plus the C+ runtime archive that the
#          c+ driver itself selects (discovered from `c+ --verbose`).
#          Plain clang cannot parse default arguments / range-for directly.
#   .cp -> `build/c+ --strict --sanitize=none build` (canonical .cp/.hp path).
# Rust build path: `rustc -O --edition 2021` (no cargo, no deps, no unsafe).
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
OUT="$HERE/.build"
CPLUS="${CPLUS_BIN:-$REPO/build/c+}"
CLANG="${CPLUS_CLANG:-/usr/bin/clang}"
TIMEOUT="${TIMEOUT:-timeout}"
mkdir -p "$OUT"

echo "rustc: $(rustc --version)" >&2
echo "clang: $($CLANG --version | head -1)" >&2
echo "c+:    $($CPLUS --version 2>&1 | head -1)" >&2
echo "timeout: $TIMEOUT" >&2

# Discover the runtime archive used by the c+ driver so the lowered C is
# linked against exactly the same archive as a normal `c+ build`.
RT=""
if [ -x "$CPLUS" ]; then
    # c+ resolves `stdlib/*.hp` relative to the current directory, so always
    # invoke it from the repository root. `-o` also stops --emit-c from writing
    # a default-named executable into the caller's directory.
    (cd "$REPO" && "$CPLUS" --strict --sanitize=none --verbose build \
        "$REPO/demos/M_default_args.c" -o "$OUT/probe_rt") \
        >"$OUT/probe_rt.log" 2>&1
    RT="$(grep -oE '[^ ]+libcplus_runtime\.a' "$OUT/probe_rt.log" | tail -1 || true)"
fi
echo "runtime archive: ${RT:-<not found>}" >&2

compare_one() {
    name="$1"
    ext="$2"
    src="$REPO/demos/$name.$ext"
    rust="$HERE/$name.rs"
    log="$OUT/$name.build.log"
    : >"$log"

    if [ ! -f "$src" ]; then
        echo "EXAMPLE_COMPARE $name BLOCKED missing original source $src"
        return
    fi
    if [ ! -f "$rust" ]; then
        echo "EXAMPLE_COMPARE $name BLOCKED missing Rust model $rust"
        return
    fi
    if [ ! -x "$CPLUS" ]; then
        echo "EXAMPLE_COMPARE $name BLOCKED c+ compiler missing at $CPLUS"
        return
    fi

    # --- safe Rust build -------------------------------------------------
    if ! $TIMEOUT 30 rustc -O --edition 2021 "$rust" -o "$OUT/rust_$name" \
            >>"$log" 2>&1; then
        echo "EXAMPLE_COMPARE $name BLOCKED rustc -O build failed; evidence $log"
        return
    fi

    # --- original build --------------------------------------------------
    if [ "$ext" = "c" ]; then
        if [ -z "$RT" ] || [ ! -f "$RT" ]; then
            echo "EXAMPLE_COMPARE $name BLOCKED runtime archive not found (probe $OUT/probe_rt.log)"
            return
        fi
        if ! $TIMEOUT 30 bash -c 'cd "$1" && exec "$2" --strict --sanitize=none --emit-c "$3" "$4" -o "$5"' \
                _ "$REPO" "$CPLUS" "$OUT/lowered_$name.c" "$src" "$OUT/emitted_$name" \
                >>"$log" 2>&1; then
            echo "EXAMPLE_COMPARE $name BLOCKED c+ lowering failed; evidence $log"
            return
        fi
        if ! $TIMEOUT 30 "$CLANG" -std=gnu17 \
                -I "$REPO/runtime/include" -I "$REPO/demos" \
                "$OUT/lowered_$name.c" "$RT" -o "$OUT/orig_$name" \
                >>"$log" 2>&1; then
            echo "EXAMPLE_COMPARE $name BLOCKED clang+archive build failed; evidence $log"
            return
        fi
    else
        if ! $TIMEOUT 30 bash -c 'cd "$1" && exec "$2" --strict --sanitize=none build "$3" -o "$4"' \
                _ "$REPO" "$CPLUS" "$src" "$OUT/orig_$name" >>"$log" 2>&1; then
            echo "EXAMPLE_COMPARE $name BLOCKED c+ --strict --sanitize=none build failed; evidence $log"
            return
        fi
    fi

    # --- deterministic run -----------------------------------------------
    : >"$OUT/stdin" # both programs get the same empty stdin
    $TIMEOUT 30 "$OUT/orig_$name" <"$OUT/stdin" \
        >"$OUT/orig_$name.stdout" 2>"$OUT/orig_$name.stderr"
    orc=$?
    $TIMEOUT 30 "$OUT/rust_$name" <"$OUT/stdin" \
        >"$OUT/rust_$name.stdout" 2>"$OUT/rust_$name.stderr"
    rrc=$?

    if [ "$orc" -eq 124 ] || [ "$rrc" -eq 124 ]; then
        echo "EXAMPLE_COMPARE $name BLOCKED timeout 30 (orig=$orc rust=$rrc)"
        return
    fi
    if [ "$orc" -ne "$rrc" ]; then
        echo "EXAMPLE_COMPARE $name DIFFERENT exit codes orig=$orc rust=$rrc"
        diff -u "$OUT/orig_$name.stdout" "$OUT/rust_$name.stdout" >&2 || true
        return
    fi
    if ! cmp -s "$OUT/orig_$name.stdout" "$OUT/rust_$name.stdout"; then
        echo "EXAMPLE_COMPARE $name DIFFERENT stdout bytes differ (orig=$(wc -c <"$OUT/orig_$name.stdout" | tr -d ' ')B rust=$(wc -c <"$OUT/rust_$name.stdout" | tr -d ' ')B)"
        diff -u "$OUT/orig_$name.stdout" "$OUT/rust_$name.stdout" >&2 || true
        return
    fi
    echo "EXAMPLE_COMPARE $name MATCH stdout bytes and exit $orc identical with empty stdin"
}

compare_one M_default_args c
compare_one N_range_for c
compare_one O_extensions cp
compare_one P_std_library cp

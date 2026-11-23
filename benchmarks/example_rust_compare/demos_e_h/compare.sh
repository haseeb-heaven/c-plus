#!/bin/sh
# Build and compare demos E-H: original C+ sources vs safe-Rust equivalents.
#
# Prints one machine-readable line per example:
#   EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>
#
# All build artifacts are written only under this directory (build/).
# The originals use C+ syntax (resource/managed), so the supported build is the
# C+ compiler, which lowers to C and invokes clang with the C+ runtime archive.
# A plain `clang -std=c99` cannot parse these files (verified in RESULTS.md).

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
OUT="$HERE/build"
mkdir -p "$OUT"

RUSTC=${RUSTC:-rustc}
CORE_CPLUS=${CPLUS_CORE:-/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/build/c+}

# Pick a working supported C+ compiler.  The core build's runtime provides
# __cplus_bounds_check (needed by E).  The stdlib build works only when pointed
# at its own checked-in runtime via CPLUS_ROOT, and still lacks that symbol.
if [ -x "$CORE_CPLUS" ]; then
    CPLUS="$CORE_CPLUS"
    unset CPLUS_ROOT
    CPLUS_LABEL="c-plus-core/build/c+ (CPLUS_ROOT unset -> core runtime)"
else
    CPLUS="$ROOT/build/c+"
    CPLUS_ROOT="$ROOT"
    export CPLUS_ROOT
    CPLUS_LABEL="c-plus-stdlib/build/c+ (CPLUS_ROOT=$ROOT)"
fi

echo "== toolchains ==" >&2
echo "CPLUS=$CPLUS" >&2
echo "CPLUS_LABEL=$CPLUS_LABEL" >&2
echo "RUSTC=$($RUSTC --version)" >&2
echo "timeout=$(command -v timeout)" >&2

# Canonicalize E's two inherent nondeterminism sources: the ephemeral port and
# the client/server line order (C uses an unbuffered forked dprintf plus a
# block-buffered printf; Rust threads use line-buffered stdout).
normalize_net() {
    sed -E 's/127\.0\.0\.1:[0-9]+/127.0.0.1:PORT/g' | LC_ALL=C sort
}

compare() {
    name=$1
    mode=$2   # exact | net
    reason=$3

    src="$ROOT/demos/$name.c"
    rs="$HERE/$name.rs"
    orig="$OUT/orig_$name"
    rust="$OUT/rust_$name"

    if [ ! -f "$src" ] || [ ! -f "$rs" ]; then
        echo "EXAMPLE_COMPARE $name BLOCKED missing source ($src or $rs)"
        return
    fi

    if ! "$CPLUS" build "$src" -o "$orig" >"$OUT/$name.orig.build.log" 2>&1; then
        echo "EXAMPLE_COMPARE $name BLOCKED original C+ build failed: $(tail -n 1 "$OUT/$name.orig.build.log")"
        return
    fi
    if ! "$RUSTC" -O "$rs" -o "$rust" >"$OUT/$name.rust.build.log" 2>&1; then
        echo "EXAMPLE_COMPARE $name BLOCKED rustc build failed: $(tail -n 1 "$OUT/$name.rust.build.log")"
        return
    fi

    timeout 30 "$orig" >"$OUT/$name.orig.out" 2>"$OUT/$name.orig.err"
    orc=$?
    timeout 30 "$rust" >"$OUT/$name.rust.out" 2>"$OUT/$name.rust.err"
    rrc=$?

    if [ "$mode" = net ]; then
        normalize_net <"$OUT/$name.orig.out" >"$OUT/$name.orig.norm"
        normalize_net <"$OUT/$name.rust.out" >"$OUT/$name.rust.norm"
    else
        cp "$OUT/$name.orig.out" "$OUT/$name.orig.norm"
        cp "$OUT/$name.rust.out" "$OUT/$name.rust.norm"
    fi

    if [ "$orc" -eq "$rrc" ] && diff -u "$OUT/$name.orig.norm" "$OUT/$name.rust.norm" >"$OUT/$name.diff" 2>&1; then
        echo "EXAMPLE_COMPARE $name MATCH $reason"
    else
        echo "EXAMPLE_COMPARE $name DIFFERENT $reason [exit orig=$orc rust=$rrc; see $OUT/$name.diff]"
    fi
}

compare E_networking net \
    "normalized ephemeral port + client/server line order; same 4 messages, CRs and exit; raw stdout differs only by those two inherent nondeterminism sources"
compare F_concurrency exact \
    "exact stdout + exit; 3 threads x 50,000 locked increments, counter=150000"
compare G_borrow exact \
    "exact stdout + exit; static Rust borrow/Drop vs C+ runtime cleanup metadata"
compare H_allocation_borrow exact \
    "exact stdout + exit; Vec length/Drop vs C+ managed-allocation length metadata"

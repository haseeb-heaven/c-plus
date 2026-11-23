#!/bin/sh
# Per-method memory-safety comparison: C+ checked APIs vs safe Rust.
#
# Builds both deterministic harnesses, runs each under a 30s timeout, diffs the
# emitted `<method> <case> <token>` lines, and prints:
#   METHOD_FORMAT <cases> <matched> <mismatched> <blocked>
# Exit status is 0 when there is no real mismatch, nonzero otherwise.
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$DIR/../../../.." && pwd)
BUILD="$DIR/.build"
mkdir -p "$BUILD"

CLANG=${CLANG:-/usr/bin/clang}
RUSTC=${RUSTC:-rustc}
INC="-I$ROOT/stdlib/safe -I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include"
LIBS="$ROOT/build/safe/libcplus_safecrt.a $ROOT/build/libc/libcplus_hardened.a"

# --- Pre-flight symbol verification (BLOCKED evidence is in NOTES.md) --------
# All six methods are present in the archives; if a future build drops one,
# linking below fails loudly rather than silently comparing an empty set.
for sym in \
    _cplus_sprintf_s_checked _cplus_snprintf_s_checked _cplus_vsnprintf_s \
    _cplus_strtol _cplus_strtoul _cplus_strtoimax; do
    if ! /usr/bin/nm -gU $LIBS 2>/dev/null | grep -q " T $sym\$"; then
        echo "PREFLIGHT MISSING SYMBOL: $sym" >&2
        echo "METHOD_FORMAT 0 0 0 6"
        exit 2
    fi
done

# --- Compile -----------------------------------------------------------------
if ! timeout 30 "$CLANG" -std=c99 $INC "$DIR/cplus_format.c" $LIBS \
        -lm -pthread -o "$BUILD/cplus_format" 2>"$BUILD/clang.log"; then
    echo "C++ harness compile failed:" >&2
    cat "$BUILD/clang.log" >&2
    echo "METHOD_FORMAT 0 0 0 6"
    exit 2
fi

if ! timeout 30 "$RUSTC" -O "$DIR/format.rs" -o "$BUILD/format_bin" \
        2>"$BUILD/rustc.log"; then
    echo "Rust harness compile failed:" >&2
    cat "$BUILD/rustc.log" >&2
    echo "METHOD_FORMAT 0 0 0 6"
    exit 2
fi

# --- Run ---------------------------------------------------------------------
if ! timeout 30 "$BUILD/cplus_format" >"$BUILD/cplus.out" 2>"$BUILD/cplus.err"; then
    echo "C++ harness run failed" >&2
    cat "$BUILD/cplus.err" >&2
    echo "METHOD_FORMAT 0 0 0 6"
    exit 2
fi

if ! timeout 30 "$BUILD/format_bin" >"$BUILD/rust.out" 2>"$BUILD/rust.err"; then
    echo "Rust harness run failed" >&2
    cat "$BUILD/rust.err" >&2
    echo "METHOD_FORMAT 0 0 0 6"
    exit 2
fi

# --- Compare -----------------------------------------------------------------
if ! diff -u "$BUILD/cplus.out" "$BUILD/rust.out" >"$BUILD/diff.txt"; then
    cat "$BUILD/diff.txt" >&2
fi

counts=$(awk '
    FNR==NR { key=$1" "$2; rtok[key]=$3; rseen[key]=1; next }
    {
        key=$1" "$2; ctok=$3; total++;
        if ((key in rtok) && rtok[key]==ctok) matched++;
        else {
            mismatched++;
            printf "MISMATCH %s cplus=%s rust=%s\n", key, ctok,
                   ((key in rtok) ? rtok[key] : "<missing>") > "/dev/stderr";
        }
        delete rseen[key];
    }
    END {
        for (k in rseen) {
            mismatched++;
            printf "MISMATCH %s cplus=<missing> rust=%s\n", k, rtok[k] > "/dev/stderr";
        }
        printf "%d %d %d\n", total, matched, mismatched;
    }
' "$BUILD/rust.out" "$BUILD/cplus.out")

set -- $counts
total=$1
matched=$2
mismatched=$3

echo "METHOD_FORMAT $total $matched $mismatched 0"
[ "$mismatched" -eq 0 ] || exit 1
exit 0

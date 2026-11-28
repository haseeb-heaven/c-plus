#!/bin/sh
# Build and run each original C+ demo and its safe-Rust equivalent, then print
# exactly one result line per example:
#   EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>
#
# Every build, run and timeout uses `timeout 30`.  All artifacts are written
# under this script's own out/ directory; no source file outside this directory
# is modified.  Override the C+ compiler with CPLUS_BIN and the timeout binary
# with TIMEOUT.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
OUT="$HERE/out"
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
TIMEOUT=${TIMEOUT:-timeout}
NAMES="A_hello B_bounds C_resource D_class"

mkdir -p "$OUT"

emit() {
    printf 'EXAMPLE_COMPARE %s %s %s\n' "$1" "$2" "$3"
}

for name in $NAMES; do
    csrc="$ROOT/demos/$name.c"
    if [ ! -f "$csrc" ]; then
        emit "$name" BLOCKED "original source not found: demos/$name.c"
        continue
    fi
    if [ ! -f "$HERE/$name.rs" ]; then
        emit "$name" BLOCKED "Rust source not found: $name.rs"
        continue
    fi

    # (1) Build the original with the supported C+ compiler.  The demos use C+
    # extensions (resource struct / class), so plain clang cannot compile them;
    # --strict --sanitize=none is the documented safe-mode build used here.
    "$TIMEOUT" 30 "$CPLUS" build --strict --sanitize=none "$csrc" \
        -o "$OUT/orig_$name" >"$OUT/orig_$name.build.log" 2>&1
    if [ $? -ne 0 ]; then
        emit "$name" BLOCKED "original c+ build failed (see out/orig_$name.build.log)"
        continue
    fi

    # (2) Build the safe-Rust equivalent with -O.
    "$TIMEOUT" 30 rustc -O "$HERE/$name.rs" -o "$OUT/rust_$name" \
        >"$OUT/rust_$name.build.log" 2>&1
    if [ $? -ne 0 ]; then
        emit "$name" BLOCKED "rustc -O build failed (see out/rust_$name.build.log)"
        continue
    fi

    # (3) Run both under timeout 30, capturing stdout, stderr and exit status.
    # Group-redirect stderr so the shell's own "Abort trap" job message for
    # the intentionally-aborting B_bounds process does not pollute the report.
    { "$TIMEOUT" 30 "$OUT/orig_$name" >"$OUT/orig_$name.stdout" \
        2>"$OUT/orig_$name.stderr"; orig_rc=$?; } 2>/dev/null
    { "$TIMEOUT" 30 "$OUT/rust_$name" >"$OUT/rust_$name.stdout" \
        2>"$OUT/rust_$name.stderr"; rust_rc=$?; } 2>/dev/null

    if [ "$orig_rc" -eq 124 ] || [ "$rust_rc" -eq 124 ]; then
        emit "$name" BLOCKED "timeout 30 exceeded (orig=$orig_rc rust=$rust_rc)"
        continue
    fi

    # (4) Compare exact bytes of stdout, stderr and the exit status.
    same=1
    cmp -s "$OUT/orig_$name.stdout" "$OUT/rust_$name.stdout" || same=0
    cmp -s "$OUT/orig_$name.stderr" "$OUT/rust_$name.stderr" || same=0
    [ "$orig_rc" -eq "$rust_rc" ] || same=0

    if [ "$same" -eq 1 ]; then
        emit "$name" MATCH "stdout+stderr+exit identical (exit $orig_rc)"
        continue
    fi

    if cmp -s "$OUT/orig_$name.stdout" "$OUT/rust_$name.stdout"; then
        reason="stdout identical"
    else
        reason="stdout differs"
    fi
    if cmp -s "$OUT/orig_$name.stderr" "$OUT/rust_$name.stderr"; then
        reason="$reason, stderr identical"
    else
        reason="$reason, stderr differs"
    fi
    emit "$name" DIFFERENT "$reason, exit $orig_rc vs $rust_rc (see out/)"
done

#!/bin/sh
# benchmarks/example_rust_compare/games/compare.sh
#
# Per-example deterministic-core comparison: the unmodified C game source is
# compiled into a driver with main renamed and rand()/srand() redirected to a
# fixed-seed LCG (seed 12345), then its board/score trace is compared with a
# safe-Rust model (rustc -O, no unsafe/FFI).  Both sides run under timeout 30.
#
# stdout: exactly one line per example:
#   EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>
# Diagnostics (commands, hashes, diffs) go to stderr and to <here>/out/*.log.
#
# Environment overrides: GAME_CLANG, GAME_RUSTC, GAME_ARCHIVES, GAME_TIMEOUT,
# GAME_OUT.

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)

CLANG=${GAME_CLANG:-/usr/bin/clang}
RUSTC=${GAME_RUSTC:-rustc}
TMO=${GAME_TIMEOUT:-30}
OUT=${GAME_OUT:-"$HERE/out"}
ARCHIVES=${GAME_ARCHIVES:-"$ROOT/build/libc/libcplus_hardened.a $ROOT/build/safe/libcplus_safecrt.a"}

mkdir -p "$OUT" || exit 1

note() { printf '%s\n' "$*" >&2; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

command -v "$CLANG" >/dev/null 2>&1 || die "clang not found: $CLANG"
command -v "$RUSTC" >/dev/null 2>&1 || die "rustc not found: $RUSTC"
command -v timeout >/dev/null 2>&1 || die "timeout utility not found"
for a in $ARCHIVES; do [ -f "$a" ] || die "archive not found: $a"; done

EXAMPLES="2048 snake tetris tiles"
fails=0

for name in $EXAMPLES; do
    src="$ROOT/examples/games/$name.c"
    [ -f "$src" ] || die "example source missing: $src"
    harness="$HERE/${name}_harness.c"
    rs="$HERE/${name}.rs"

    c_bin="$OUT/${name}_c"
    rs_bin="$OUT/${name}_rs"
    c_stdout="$OUT/${name}.c.out"
    rs_stdout="$OUT/${name}.rs.out"
    c_stderr="$OUT/${name}.c.err"
    rs_stderr="$OUT/${name}.rs.err"
    c_build="$OUT/${name}.c.build.log"
    rs_build="$OUT/${name}.rs.build.log"

    note "BUILD C $name: $CLANG -std=c99 -I$ROOT/examples/games ${name}_harness.c $ARCHIVES -lncurses -o $c_bin"
    # shellcheck disable=SC2086
    timeout "$TMO" "$CLANG" -std=c99 -I"$ROOT/examples/games" \
        "$harness" $ARCHIVES -lncurses -o "$c_bin" \
        >"$c_build" 2>&1 || die "C build failed for $name (see $c_build)"

    note "BUILD Rust $name: $RUSTC -O ${name}.rs -o $rs_bin"
    timeout "$TMO" "$RUSTC" -O "$rs" -o "$rs_bin" \
        >"$rs_build" 2>&1 || die "Rust build failed for $name (see $rs_build)"

    note "RUN C $name: timeout $TMO $c_bin"
    timeout "$TMO" "$c_bin" >"$c_stdout" 2>"$c_stderr"
    c_rc=$?
    note "RUN Rust $name: timeout $TMO $rs_bin"
    timeout "$TMO" "$rs_bin" >"$rs_stdout" 2>"$rs_stderr"
    rs_rc=$?

    if [ "$c_rc" -ne 0 ] || [ "$rs_rc" -ne 0 ]; then
        printf 'EXAMPLE_COMPARE %s BLOCKED run-failed c_exit=%s rust_exit=%s\n' \
            "$name" "$c_rc" "$rs_rc"
        fails=$((fails + 1))
        continue
    fi

    if [ ! -s "$c_stdout" ] || [ ! -s "$rs_stdout" ]; then
        printf 'EXAMPLE_COMPARE %s BLOCKED empty-trace no deterministic output\n' "$name"
        fails=$((fails + 1))
        continue
    fi

    if cmp -s "$c_stdout" "$rs_stdout" && [ "$c_rc" -eq "$rs_rc" ]; then
        note "MATCH $name: c_sha256=$(shasum -a 256 "$c_stdout" | awk '{print $1}') rust_sha256=$(shasum -a 256 "$rs_stdout" | awk '{print $1}')"
        printf 'EXAMPLE_COMPARE %s MATCH deterministic-core seed=12345; interactive UI excluded\n' "$name"
    else
        diff -u "$c_stdout" "$rs_stdout" >"$OUT/${name}.diff" 2>&1 || true
        note "DIFFERENT $name: see $OUT/${name}.diff"
        printf 'EXAMPLE_COMPARE %s DIFFERENT stdout-or-exit-mismatch see %s\n' "$name" "$OUT/${name}.diff"
        fails=$((fails + 1))
    fi
done

exit "$fails"

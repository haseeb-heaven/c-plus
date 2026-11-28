#!/bin/sh
# benchmarks/example_rust_compare/modern_secure/compare.sh
#
# One-to-one stdout + exit-status comparison of the modern_gui / secure_apps
# examples against SAFE-Rust models (no unsafe, no FFI).
#
#   C+  : ./build/c+ build <source> --strict --sanitize=none -I. -o <bin>
#         -- $(pkg-config --cflags --libs sdl3) ...   (GUI examples)
#         /usr/bin/clang -std=c99 -pthread ...       (auditlog)
#   Rust: rustc -O --edition 2021 <name>.rs -o <bin>
#   run : timeout 30 <bin> <fixed args>  (stdin /dev/null unless noted)
#         SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy for GUI binaries
#
# One stdout line per example:
#   MODERN_COMPARE <name> MATCH|DIFFERENT|BLOCKED|PENDING <reason>
# Diagnostics, commands, diffs go to stderr and to .build/* logs.
#
# PENDING means the C+ source or the Rust model has not appeared yet; it is
# reported with the exact refresh command and never fabricated as a MATCH.
# BLOCKED (build failure or timeout) is also inconclusive.  Exit status is
# nonzero only for a real DIFFERENT.
#
# Environment overrides: MS_RUSTC, MS_CPLUS, MS_TIMEOUT, MS_OUT.

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)

CPLUS=${MS_CPLUS:-"$ROOT/build/c+"}
RUSTC=${MS_RUSTC:-rustc}
TMO=${MS_TIMEOUT:-30}
OUT=${MS_OUT:-"$HERE/.build"}
CLANG=${MS_CLANG:-/usr/bin/clang}

mkdir -p "$OUT" || exit 1

note() { printf '%s\n' "$*" >&2; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[ -x "$CPLUS" ] || die "c+ compiler not found/executable: $CPLUS"
command -v "$RUSTC" >/dev/null 2>&1 || die "rustc not found: $RUSTC"
command -v timeout >/dev/null 2>&1 || die "timeout utility not found"
[ -x "$CLANG" ] || die "clang not found/executable: $CLANG"

# c+ resolves stdlib/*.hp includes relative to cwd; always build from the root.
cd "$ROOT" || die "cannot cd to repository root: $ROOT"

SDL_CFLAGS=$(pkg-config --cflags sdl3 2>/dev/null) || die "sdl3 pkg-config cflags unavailable"
SDL_LIBS=$(pkg-config --libs sdl3 2>/dev/null) || die "sdl3 pkg-config libs unavailable"
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy

AUDIT_LOG="$OUT/audit.log"

CURRENT_NAME=""
GUI=0

run_case() {
    # run_case <binary> <prefix> [args...]
    bin=$1; prefix=$2; shift 2
    if [ "$CURRENT_NAME" = auditlog ]; then
        rm -f "$AUDIT_LOG"
    fi
    timeout "$TMO" "$bin" "$@" </dev/null >>"$prefix.stdout" 2>>"$prefix.stderr"
    printf '%s\n' "$?" >>"$prefix.rc"
}

run_case_stdin() {
    # run_case_stdin <binary> <prefix> <stdin-file> [args...]
    bin=$1; prefix=$2; input=$3; shift 3
    timeout "$TMO" "$bin" "$@" <"$input" >>"$prefix.stdout" 2>>"$prefix.stderr"
    printf '%s\n' "$?" >>"$prefix.rc"
}

emit_cases() {
    # emit_cases <binary> <prefix>
    bin=$1; prefix=$2
    : >"$prefix.stdout"; : >"$prefix.rc"; : >"$prefix.stderr"
    case "$CURRENT_NAME" in
        dashboard)
            run_case "$bin" "$prefix" describe 0
            run_case "$bin" "$prefix" describe 1
            run_case "$bin" "$prefix" describe 2
            run_case "$bin" "$prefix" describe 99
            run_case "$bin" "$prefix" describe
            run_case "$bin" "$prefix" describe abc
            run_case "$bin" "$prefix" render 640 480 0
            run_case "$bin" "$prefix" render 320 240 2
            run_case "$bin" "$prefix" render 0 480 0
            run_case "$bin" "$prefix" render 640 480 99
            run_case "$bin" "$prefix" render 100000 100000 0
            run_case "$bin" "$prefix" render 640 x 0
            run_case "$bin" "$prefix" render
            run_case "$bin" "$prefix" refuse zero-width
            run_case "$bin" "$prefix" refuse negative-height
            run_case "$bin" "$prefix" refuse oversized
            run_case "$bin" "$prefix" refuse bad-scenario
            run_case "$bin" "$prefix" refuse null-context
            run_case "$bin" "$prefix" refuse unknown
            run_case "$bin" "$prefix" frobnicate
            ;;
        settings)
            run_case "$bin" "$prefix" apply
            run_case "$bin" "$prefix" text_oversize
            run_case "$bin" "$prefix" slider_range
            run_case "$bin" "$prefix" null_context
            run_case "$bin" "$prefix" null_text
            run_case "$bin" "$prefix" invalid_utf8
            run_case "$bin" "$prefix" apply text_oversize slider_range null_context null_text invalid_utf8
            run_case "$bin" "$prefix" bogus
            run_case "$bin" "$prefix"
            ;;
        breakout_ui)
            run_case "$bin" "$prefix"
            run_case "$bin" "$prefix" RLSR
            run_case "$bin" "$prefix" R L S R
            run_case "$bin" "$prefix" --grid 4x1 RLSR
            run_case "$bin" "$prefix" --grid 16x4 LLLLLLLLLLLL
            run_case "$bin" "$prefix" --probe-null
            run_case "$bin" "$prefix" --help
            run_case "$bin" "$prefix" X
            run_case "$bin" "$prefix" --grid 99x9 R
            run_case_stdin "$bin" "$prefix" "$OUT/breakout.stdin" -
            ;;
        invaders_ui)
            run_case "$bin" "$prefix" LRF
            run_case "$bin" "$prefix" LFRFLFSRF
            run_case "$bin" "$prefix" --bullets=2 FFFF
            run_case "$bin" "$prefix" --bullets=16 FFFFFFFFFFFFFFFF
            run_case "$bin" "$prefix" --null-context
            run_case "$bin" "$prefix" --null-surface
            run_case "$bin" "$prefix" X
            run_case "$bin" "$prefix" --bullets=99 F
            run_case_stdin "$bin" "$prefix" "$OUT/invaders.stdin" -
            ;;
        vault)
            run_case "$bin" "$prefix" add mail s3cr3t -- get mail -- list -- peek 0 -- audit
            run_case "$bin" "$prefix" add a 1 -- add b 2 -- delete a -- list -- get b
            run_case "$bin" "$prefix" get missing
            run_case "$bin" "$prefix" debug x
            run_case "$bin" "$prefix" add "" x
            run_case "$bin" "$prefix" add toolongservice12345 s
            run_case "$bin" "$prefix" peek 3
            run_case "$bin" "$prefix" add ok secret -- audit
            run_case "$bin" "$prefix"
            run_case "$bin" "$prefix" frobnicate
            ;;
        firewall)
            run_case "$bin" "$prefix" token alpha
            run_case "$bin" "$prefix" token admin
            run_case "$bin" "$prefix" pathseg file_name.txt
            run_case "$bin" "$prefix" pathseg ../etc/passwd
            run_case "$bin" "$prefix" sqlsafe valid_name
            run_case "$bin" "$prefix" sqlsafe "' OR 1=1"
            run_case "$bin" "$prefix" text hello
            run_case "$bin" "$prefix" text x%00y
            run_case "$bin" "$prefix" text "abc;DROP TABLE"
            run_case "$bin" "$prefix" bogus hello
            run_case "$bin" "$prefix" token ""
            ctrl=$(printf 'a\tb')
            run_case "$bin" "$prefix" text "$ctrl"
            ;;
        ratelimit)
            run_case "$bin" "$prefix" 3 1 5 0 1 1 1 2 1 3 1 4 1
            run_case "$bin" "$prefix" 10 2 4 0 3 1 7 2 3 10 10
            run_case "$bin" "$prefix" 5 1 3 0 2 0 5 0 5
            run_case "$bin" "$prefix" 0 1 0
            run_case "$bin" "$prefix" 3 1 2 0 1
            run_case "$bin" "$prefix" 3 x 0
            run_case "$bin" "$prefix" 3 1 70 0 1
            run_case "$bin" "$prefix" 3 1 1 5 1
            run_case "$bin" "$prefix" 3 1 1 0 4
            run_case "$bin" "$prefix" 3 1 1 -1 1
            ;;
        auditlog)
            run_case "$bin" "$prefix" "$AUDIT_LOG"
            run_case "$bin" "$prefix"
            ;;
        *)
            die "no fixed cases defined for $CURRENT_NAME"
            ;;
    esac
}

build_c() {
    name=$1; src=$2
    cbin="$OUT/c_$name"
    log="$OUT/$name.c.build.log"
    case "$name" in
        dashboard)
            timeout "$TMO" "$CPLUS" build "$src" --strict --sanitize=none -I. -o "$cbin" -- \
                $SDL_CFLAGS $SDL_LIBS -Ithird_party/nuklear -Lbuild/ext -lnuklear >"$log" 2>&1 ;;
        settings)
            timeout "$TMO" "$CPLUS" build "$src" --strict --sanitize=none -I. -o "$cbin" -- \
                $SDL_CFLAGS $SDL_LIBS -Ithird_party/nuklear -Lbuild/ext -lnuklear >"$log" 2>&1 ;;
        breakout_ui)
            timeout "$TMO" "$CPLUS" build "$src" --strict --sanitize=none -I. -o "$cbin" -- \
                $SDL_CFLAGS $SDL_LIBS -Ithird_party/nuklear build/ext/libnuklear.a >"$log" 2>&1 ;;
        invaders_ui)
            timeout "$TMO" "$CPLUS" build "$src" --strict --sanitize=none -I. -o "$cbin" -- \
                $SDL_CFLAGS $SDL_LIBS -Ithird_party/nuklear -Lbuild/ext -lnuklear >"$log" 2>&1 ;;
        vault|firewall|ratelimit)
            timeout "$TMO" "$CPLUS" build "$src" --strict --sanitize=none -I. -o "$cbin" >"$log" 2>&1 ;;
        auditlog)
            rm -f "$AUDIT_LOG"
            timeout "$TMO" "$CLANG" -std=c99 -pthread -Ilibc/install-include "$src" \
                build/libc/libcplus_hardened.a -o "$cbin" >"$log" 2>&1 ;;
        *)
            return 1 ;;
    esac
}

fails=0
inconclusive=0
compare_example() {
    name=$1; src=$2
    CURRENT_NAME=$name
    srcpath="$ROOT/$src"
    rs="$HERE/$name.rs"

    if [ ! -f "$srcpath" ]; then
        printf 'MODERN_COMPARE %s PENDING source-not-yet-created %s; refresh: sh %s/compare.sh\n' \
            "$name" "$srcpath" "$HERE"
        inconclusive=$((inconclusive + 1))
        return
    fi
    if [ ! -f "$rs" ]; then
        printf 'MODERN_COMPARE %s PENDING rust-model-not-yet-created %s; refresh: sh %s/compare.sh\n' \
            "$name" "$rs" "$HERE"
        inconclusive=$((inconclusive + 1))
        return
    fi

    note "BUILD C+ $name: $srcpath"
    if ! build_c "$name" "$srcpath"; then
        printf 'MODERN_COMPARE %s BLOCKED c+-build-failed see %s/%s.c.build.log\n' \
            "$name" "$OUT" "$name"
        inconclusive=$((inconclusive + 1))
        return
    fi

    rbin="$OUT/r_$name"
    rbuild="$OUT/$name.rs.build.log"
    note "BUILD Rust $name: $RUSTC -O --edition 2021 $rs -o $rbin"
    timeout "$TMO" "$RUSTC" -O --edition 2021 "$rs" -o "$rbin" >"$rbuild" 2>&1 \
        || { printf 'MODERN_COMPARE %s BLOCKED rust-build-failed see %s\n' "$name" "$rbuild"; inconclusive=$((inconclusive+1)); return; }

    printf 'LRFSF\n' >"$OUT/invaders.stdin"
    printf 'RLS\n' >"$OUT/breakout.stdin"

    cpfx="$OUT/$name.cases.c"
    rpfx="$OUT/$name.cases.r"
    emit_cases "$OUT/c_$name" "$cpfx"
    emit_cases "$rbin" "$rpfx"

    if cmp -s "$cpfx.stdout" "$rpfx.stdout" && cmp -s "$cpfx.rc" "$rpfx.rc"; then
        note "MATCH $name: c_sha256=$(shasum -a 256 "$cpfx.stdout" | awk '{print $1}') rust_sha256=$(shasum -a 256 "$rpfx.stdout" | awk '{print $1}')"
        printf 'MODERN_COMPARE %s MATCH stdout+exit identical for %s fixed case(s)\n' \
            "$name" "$(wc -l < "$cpfx.rc" | tr -d ' ')"
    else
        diff -u "$cpfx.stdout" "$rpfx.stdout" >"$OUT/$name.diff" 2>&1 || true
        printf '\n--- exit-status diff ---\n' >>"$OUT/$name.diff"
        diff -u "$cpfx.rc" "$rpfx.rc" >>"$OUT/$name.diff" 2>&1 || true
        note "DIFFERENT $name: see $OUT/$name.diff"
        printf 'MODERN_COMPARE %s DIFFERENT stdout-or-exit-mismatch see %s\n' "$name" "$OUT/$name.diff"
        fails=$((fails + 1))
    fi
}

compare_example dashboard     examples/modern_gui/dashboard/dashboard.cp
compare_example settings      examples/modern_gui/settings/settings_panel.cp
compare_example breakout_ui   examples/modern_gui/games/breakout_ui/breakout_ui.cp
compare_example invaders_ui   examples/modern_gui/games/invaders_ui/invaders_ui.cp
compare_example vault         examples/secure_apps/vault/vault.cp
compare_example firewall      examples/secure_apps/firewall/firewall.cp
compare_example ratelimit     examples/secure_apps/ratelimit/ratelimit.cp
compare_example auditlog      examples/secure_apps/auditlog/auditlog.c

note "SUMMARY different=$fails inconclusive(BLOCKED+PENDING)=$inconclusive"
exit "$fails"

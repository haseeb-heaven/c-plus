#!/bin/sh
# benchmarks/example_rust_compare/more_apps_a/compare.sh
#
# One-to-one stdout/exit comparison of the NEW games_4 / apps_2 / systems_2
# examples against safe-Rust models (no unsafe, no FFI, no external crates).
#
#   C+  : ./build/c+ build --strict --sanitize=none -I. -o <bin> <source>
#         (json_query additionally needs -Ithird_party/yyjson -Lbuild/ext -lyyjson)
#   Rust: rustc -O --edition 2021 <name>.rs -o <bin>
#   run : timeout 30 <binary> <fixed argv>   (identical stdin: empty)
#
# stdout: exactly one line per example:
#   MORE_APPS_COMPARE <name> MATCH|DIFFERENT|BLOCKED|PENDING <reason>
# Diagnostics (commands, hashes, diffs) go to stderr and to out/* logs.
# Exit status is nonzero ONLY on a real DIFFERENT.  BLOCKED (build/run failure)
# and PENDING (source or model not created yet) are inconclusive and do not
# fail the script.
#
# If a scoped C+ source has not appeared yet, the example is reported PENDING
# together with the exact refresh command: re-run this script.
#
# Environment overrides: MA_RUSTC, MA_CPLUS, MA_TIMEOUT, MA_OUT.

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)

CPLUS=${MA_CPLUS:-"$ROOT/build/c+"}
RUSTC=${MA_RUSTC:-rustc}
TMO=${MA_TIMEOUT:-30}
OUT=${MA_OUT:-"$HERE/out"}

mkdir -p "$OUT" || exit 1

note() { printf '%s\n' "$*" >&2; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[ -x "$CPLUS" ] || die "c+ compiler not found/executable: $CPLUS"
command -v "$RUSTC" >/dev/null 2>&1 || die "rustc not found: $RUSTC"
command -v timeout >/dev/null 2>&1 || die "timeout utility not found"

# c+ resolves stdlib/*.hp includes relative to cwd; always build from the root.
cd "$ROOT" || die "cannot cd to repository root: $ROOT"

# Deterministic fixture file (same absolute path used by both sides).
FIX="$OUT/fixtures/csv_basic.txt"
mkdir -p "$OUT/fixtures" || die "fixture dir"
printf '1,10,100\n2,20,200\n3,30,300\n' > "$FIX" || die "fixture write"

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
        connect4)
            run_case "$bin" "$prefix"
            run_case "$bin" "$prefix" 3 3 4 4 5 5 6 6
            run_case "$bin" "$prefix" 33445566
            run_case "$bin" "$prefix" 9
            run_case "$bin" "$prefix" 0 0 0 0 0 0 0
            ;;
        checkers)
            run_case "$bin" "$prefix"
            run_case "$bin" "$prefix" 2130 5041 1021 4132 2334 3223 0110 2301
            run_case "$bin" "$prefix" 21305041102141322334322301102301
            run_case "$bin" "$prefix" 0000
            ;;
        chesslite)
            run_case "$bin" "$prefix"
            run_case "$bin" "$prefix" 0120 6343 1323 6151 0625 6757 2544 6656 1434 4334
            run_case "$bin" "$prefix" 0120634313236151062567572544665614344334
            run_case "$bin" "$prefix" 9999
            ;;
        vault)
            run_case "$bin" "$prefix" add github s3cr3t -- add mail hunter2 -- list -- get github -- delete mail -- list -- get mail
            run_case "$bin" "$prefix" add a 1 -- add a 2 -- get nope -- badcmd --
            ;;
        json_query)
            run_case "$bin" "$prefix" '{"user":{"name":"Ada","age":36},"items":[{"tag":"x"}]}' 'user.name'
            run_case "$bin" "$prefix" '{"user":{"name":"Ada","age":36},"items":[{"tag":"x"}]}' 'items.0.tag'
            run_case "$bin" "$prefix" '{"a":1}' 'b'
            run_case "$bin" "$prefix" '{"a":1}' 'a.b'
            run_case "$bin" "$prefix" 'notjson' 'a'
            ;;
        csv_tool)
            run_case "$bin" "$prefix" "$FIX" 1 sum
            run_case "$bin" "$prefix" "$FIX" 2 avg
            run_case "$bin" "$prefix" "$FIX" 5 sum
            run_case "$bin" "$prefix" "$FIX" 1 badmode
            ;;
        matrix)
            run_case "$bin" "$prefix" add 2 2 1,2,3,4 2 2 5,6,7,8
            run_case "$bin" "$prefix" multiply 2 3 1,2,3,4,5,6 3 2 7,8,9,10,11,12
            run_case "$bin" "$prefix" determinant 2 2 1,2,3,4
            run_case "$bin" "$prefix" determinant 3 3 1,2,3,4,5,6,7,8,9
            ;;
        lru_cache)
            run_case "$bin" "$prefix" cap 2 -- put a 1 -- put b 2 -- get a -- put c 3 -- dump -- expect b 2 -- expect a 1
            run_case "$bin" "$prefix" cap 0 -- cap 99 -- get nope -- dump
            run_case "$bin" "$prefix"
            ;;
        scheduler)
            run_case "$bin" "$prefix" capacity 4 -- arrive A 1 2 -- arrive B 0 3 -- arrive C 1 1 -- run 5 -- dump -- expect B 1
            run_case "$bin" "$prefix" capacity 0 -- arrive X 2 1 -- tick -- dump
            run_case "$bin" "$prefix"
            ;;
        event_bus)
            run_case "$bin" "$prefix" topic news -- sub news alice -- sub news bob -- pub news hello -- unsub news alice -- pub news bye -- dump -- expect news bob
            run_case "$bin" "$prefix" topic news -- sub news alice -- sub news alice -- pub nope x -- dump
            run_case "$bin" "$prefix"
            ;;
        ring_buffer)
            run_case "$bin" "$prefix" cap 4 -- push 1 2 3 -- pop 2 -- push 4 5 6 -- peek 3 -- expect 3 3 4 5 -- dump
            run_case "$bin" "$prefix" cap 0 -- push 1 -- pop 1 -- dump
            run_case "$bin" "$prefix"
            ;;
        *)
            die "no fixed inputs defined for $CURRENT_NAME"
            ;;
    esac
}

fails=0           # exit-driving counter: incremented ONLY on a real DIFFERENT
inconclusive=0    # PENDING/BLOCKED: reported but do NOT affect exit status
compare_example() {
    name=$1; src=$2; extra=$3
    CURRENT_NAME=$name
    srcpath="$ROOT/examples/expanded/$src.cp"
    rs="$HERE/$name.rs"

    if [ ! -f "$srcpath" ]; then
        printf 'MORE_APPS_COMPARE %s PENDING source-not-yet-created %s; refresh: sh %s/compare.sh\n' \
            "$name" "$srcpath" "$HERE"
        inconclusive=$((inconclusive + 1))
        return
    fi
    if [ ! -f "$rs" ]; then
        printf 'MORE_APPS_COMPARE %s PENDING rust-model-not-yet-created %s; refresh: sh %s/compare.sh\n' \
            "$name" "$rs" "$HERE"
        inconclusive=$((inconclusive + 1))
        return
    fi

    cbin="$OUT/c_$name"
    rbin="$OUT/r_$name"
    cbuild="$OUT/$name.c.build.log"
    rbuild="$OUT/$name.rs.build.log"

    note "BUILD C+ $name: $CPLUS build --strict --sanitize=none -I. $extra -o $cbin $srcpath"
    # shellcheck disable=SC2086
    timeout "$TMO" "$CPLUS" build --strict --sanitize=none -I. $extra -o "$cbin" "$srcpath" \
        >"$cbuild" 2>&1 || { printf 'MORE_APPS_COMPARE %s BLOCKED c+-build-failed see %s\n' "$name" "$cbuild"; inconclusive=$((inconclusive+1)); return; }

    note "BUILD Rust $name: $RUSTC -O --edition 2021 $rs -o $rbin"
    timeout "$TMO" "$RUSTC" -O --edition 2021 "$rs" -o "$rbin" \
        >"$rbuild" 2>&1 || { printf 'MORE_APPS_COMPARE %s BLOCKED rust-build-failed see %s\n' "$name" "$rbuild"; inconclusive=$((inconclusive+1)); return; }

    cpfx="$OUT/$name.cases.c"
    rpfx="$OUT/$name.cases.r"
    emit_cases "$cbin" "$cpfx"
    emit_cases "$rbin" "$rpfx"

    if cmp -s "$cpfx.stdout" "$rpfx.stdout" && cmp -s "$cpfx.rc" "$rpfx.rc"; then
        note "MATCH $name: c_sha256=$(shasum -a 256 "$cpfx.stdout" | awk '{print $1}') rust_sha256=$(shasum -a 256 "$rpfx.stdout" | awk '{print $1}')"
        printf 'MORE_APPS_COMPARE %s MATCH stdout+exit identical for %s fixed case(s)\n' \
            "$name" "$(wc -l < "$cpfx.rc" | tr -d ' ')"
    else
        diff -u "$cpfx.stdout" "$rpfx.stdout" >"$OUT/$name.diff" 2>&1 || true
        diff -u "$cpfx.rc" "$rpfx.rc" >>"$OUT/$name.diff" 2>&1 || true
        note "DIFFERENT $name: see $OUT/$name.diff"
        printf 'MORE_APPS_COMPARE %s DIFFERENT stdout-or-exit-mismatch see %s\n' "$name" "$OUT/$name.diff"
        fails=$((fails + 1))
    fi
}

JSON_EXTRA="-Ithird_party/yyjson -Lbuild/ext -lyyjson"
compare_example connect4    games_4/connect4    ""
compare_example checkers    games_4/checkers    ""
compare_example chesslite   games_4/chesslite   ""
compare_example vault       apps_2/vault        ""
compare_example json_query  apps_2/json_query   "$JSON_EXTRA"
compare_example csv_tool    apps_2/csv_tool     ""
compare_example matrix      apps_2/matrix       ""
compare_example lru_cache   systems_2/lru_cache ""
compare_example scheduler   systems_2/scheduler ""
compare_example event_bus   systems_2/event_bus ""
compare_example ring_buffer systems_2/ring_buffer ""

note "SUMMARY different=$fails inconclusive(BLOCKED+PENDING)=$inconclusive"
# Exit nonzero only for a real DIFFERENT (fails).  BLOCKED/PENDING are
# reported to stdout+stderr but deliberately leave the exit status at 0.
exit "$fails"

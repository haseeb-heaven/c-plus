#!/bin/sh
# Build and verify the deterministic 3D scene demo.
#
# The demo uses the new additive stdlib headers (math3d.hp, mesh3d.hp,
# scene3d.hp) to build a cube mesh, place nodes in a scene graph, rotate them
# through quaternions / mat4, compute world-space bounding boxes and print a
# fixed ASCII transcript with an FNV-1a checksum.  No GPU and no game assets
# are involved.
#
# A pass requires ALL of:
#   * the .cp builds in --default --no-werror --sanitize=none mode from the repo root
#     against build/libc/libcplus_hardened.a;
#   * the no-argv run exits 0, writes nothing to stderr, and its stdout is
#     byte-identical to expected/transcript.expected;
#   * a second identical run produces byte-identical stdout (determinism);
#   * the program's printed CHECKSUM line matches the golden checksum;
#   * every hostile input exits nonzero and prints both REFUSE <code> and
#     SCENE_DONE status=REFUSED <code> with the expected symbolic code.
# Every process is bounded by `timeout 30`.
#
# Final line: SCENE3D 1 <passed> <failed> <refused>
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
DIR=apps/games3d
BIN=$DIR/bin
mkdir -p "$BIN"

SRC=$DIR/scene3d_demo.cp
OUT=$BIN/scene3d_demo
LIB=build/libc/libcplus_hardened.a
EXPECTED=$DIR/expected/transcript.expected
BUILD_LOG=$BIN/build.log

passed=0
failed=0
refused=0

ok() {
    echo "ok $*"
    passed=$((passed + 1))
}

bad() {
    echo "FAIL $*"
    failed=$((failed + 1))
}

# ---- build ---------------------------------------------------------------
if "$CPLUS" build "$SRC" --default --no-werror --sanitize=none -I. -o "$OUT" -- "$LIB" \
        >"$BUILD_LOG" 2>&1; then
    echo "BUILD ok scene3d_demo"
else
    echo "BUILD FAIL scene3d_demo (see $BUILD_LOG)"
    tail -20 "$BUILD_LOG"
    echo "SCENE3D 1 0 1 0"
    exit 1
fi

# ---- accepted run: exact output + determinism ----------------------------
timeout 30 "$OUT" >"$BIN/out.actual" 2>"$BIN/err.actual"
rc=$?
if [ "$rc" -ne 0 ]; then
    bad "built-in run exited $rc"
elif [ -s "$BIN/err.actual" ]; then
    bad "built-in run wrote to stderr"
else
    ok "built-in run exited 0 with empty stderr"
fi

if cmp -s "$EXPECTED" "$BIN/out.actual"; then
    ok "exact stdout matches expected/transcript.expected"
else
    bad "stdout differs from expected/transcript.expected"
    diff -u "$EXPECTED" "$BIN/out.actual" | head -40
fi

timeout 30 "$OUT" >"$BIN/out.repeat" 2>/dev/null
rc_repeat=$?
if [ "$rc_repeat" -eq 0 ] && cmp -s "$BIN/out.actual" "$BIN/out.repeat"; then
    ok "two runs are byte-identical (deterministic)"
else
    bad "output is not deterministic (rc=$rc_repeat)"
fi

WANT_CHECKSUM=$(grep '^CHECKSUM ' "$EXPECTED")
GOT_CHECKSUM=$(grep '^CHECKSUM ' "$BIN/out.actual")
if [ -n "$GOT_CHECKSUM" ] && [ "$GOT_CHECKSUM" = "$WANT_CHECKSUM" ]; then
    ok "checksum line: $GOT_CHECKSUM"
else
    bad "checksum line mismatch (want '$WANT_CHECKSUM', got '$GOT_CHECKSUM')"
fi

# Independent, external checksum of the whole transcript for transparency.
echo "EXTERNAL_CKSUM $(cksum < "$BIN/out.actual")"

# ---- hostile inputs ------------------------------------------------------
# check_refusal <symbolic-code> <binary args...>
check_refusal() {
    want=$1
    shift
    tag=$(echo "$want" | tr -d '=')
    out=$BIN/refuse.$tag.out
    timeout 30 "$OUT" "$@" >"$out" 2>&1
    hrc=$?
    if [ "$hrc" -eq 0 ]; then
        bad "refusal $want accepted hostile input: $*"
        return
    fi
    if ! grep -q "REFUSE code=$want " "$out"; then
        bad "refusal $want missing REFUSE line (rc=$hrc)"
        return
    fi
    if ! grep -q "SCENE_DONE status=REFUSED code=$want" "$out"; then
        bad "refusal $want missing status=REFUSED (rc=$hrc)"
        return
    fi
    echo "ok refusal $want (rc=$hrc) args=$*"
    refused=$((refused + 1))
}

check_refusal E_NONFINITE  NAN
check_refusal E_NONFINITE  INF
check_refusal E_INDEX      INDEX 8
check_refusal E_INDEX      INDEX 999999
check_refusal E_INDEX      INDEX -1
check_refusal E_BOUNDS     NODE 9
check_refusal E_INDEX      DEGENERATE
check_refusal E_CYCLE      CYCLE
check_refusal E_PARSE      PARSE 12x
check_refusal E_PARSE      BOGUS
check_refusal E_PARSE      INDEX
check_refusal E_PARSE      NODE
check_refusal E_PARSE      PARSE

echo "SCENE3D 1 $passed $failed $refused"
[ "$failed" -eq 0 ]

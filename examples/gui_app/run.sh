#!/bin/sh
# Build and verify the deterministic widgets.hp + SDL3 GUI demo.
#
#   sh examples/gui_app/run.sh
#
# Each case is run under `timeout 30` with SDL_VIDEODRIVER=dummy, must exit 0,
# must produce no stderr, and must match a frozen expected stdout file BYTE FOR
# BYTE.  Every case is run twice; the two stdout files must be identical.  The
# final line is:
#
#   GUI_APP <scenarios> <passed> <failed> <refused>
#
# `refused` cases are expected recoverable refusals (they are counted as passed
# expectations, not failures).  A Windows PE COMPILE-ONLY check is also run; it
# never claims Windows runtime success (Highball/Wine is BLOCKED here).
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd) || exit 2
cd "$ROOT" || exit 2

BIN_DIR="$HERE/bin"
EXP_DIR="$HERE/expected"
RES_DIR="$HERE/results"
BIN="$BIN_DIR/gui_app"
mkdir -p "$BIN_DIR" "$RES_DIR"

C="$ROOT/build/c+"
SDL=$(pkg-config --cflags --libs sdl3 2>/dev/null || true)
if [ -z "$SDL" ]; then
    echo "BUILD_FAIL pkg-config sdl3 unavailable"
    echo "GUI_APP 0 0 1 0"
    exit 1
fi
if [ ! -x "$C" ]; then
    echo "BUILD_FAIL $C missing"
    echo "GUI_APP 0 0 1 0"
    exit 1
fi
if [ ! -f "$ROOT/build/libc/libcplus_hardened.a" ]; then
    echo "BUILD_FAIL hardened libc archive missing"
    echo "GUI_APP 0 0 1 0"
    exit 1
fi

echo "=== BUILD gui_app ==="
if ! "$C" build "$HERE/gui_app.cp" --default --sanitize=none -I. \
        $SDL -o "$BIN" -- build/libc/libcplus_hardened.a \
        >"$RES_DIR/build.log" 2>&1; then
    echo "BUILD_FAIL gui_app"
    cat "$RES_DIR/build.log"
    echo "GUI_APP 0 0 1 0"
    exit 1
fi
# The C+ driver can return 0 even when the final link failed; require a binary.
if [ ! -x "$BIN" ]; then
    echo "BUILD_FAIL no gui_app binary"
    cat "$RES_DIR/build.log"
    echo "GUI_APP 0 0 1 0"
    exit 1
fi

scenarios=0
passed=0
failed=0
refused=0

# run_case <tag> <OK|REFUSED> <argv...>
run_case() {
    tag=$1
    expect=$2
    shift 2
    scenarios=$((scenarios + 1))
    printf '=== %s (%s) ===\n' "$tag" "$expect"

    SDL_VIDEODRIVER=dummy timeout 30 "$BIN" "$@" \
        >"$RES_DIR/$tag.out" 2>"$RES_DIR/$tag.err"
    rc=$?
    cat "$RES_DIR/$tag.out"

    ok=1
    if [ "$rc" -ne 0 ]; then
        echo "FAIL $tag exit=$rc"
        ok=0
    fi
    if [ -s "$RES_DIR/$tag.err" ]; then
        echo "FAIL $tag stderr not empty"
        cat "$RES_DIR/$tag.err"
        ok=0
    fi
    if [ ! -f "$EXP_DIR/$tag.out" ]; then
        echo "FAIL $tag expected file missing: $EXP_DIR/$tag.out"
        ok=0
    elif ! cmp -s "$RES_DIR/$tag.out" "$EXP_DIR/$tag.out"; then
        echo "FAIL $tag stdout differs from expected"
        diff -u "$EXP_DIR/$tag.out" "$RES_DIR/$tag.out" || true
        ok=0
    fi

    # Determinism: identical argv must give byte-identical output on run 2.
    SDL_VIDEODRIVER=dummy timeout 30 "$BIN" "$@" \
        >"$RES_DIR/$tag.out2" 2>"$RES_DIR/$tag.err2"
    rc2=$?
    if [ "$rc2" -ne 0 ]; then
        echo "FAIL $tag second-run exit=$rc2"
        ok=0
    fi
    if ! cmp -s "$RES_DIR/$tag.out" "$RES_DIR/$tag.out2"; then
        echo "FAIL $tag nondeterministic stdout"
        ok=0
    fi
    if [ -s "$RES_DIR/$tag.err2" ]; then
        echo "FAIL $tag second-run stderr not empty"
        ok=0
    fi

    if [ "$ok" -eq 1 ]; then
        if [ "$expect" = "REFUSED" ]; then
            refused=$((refused + 1))
        else
            passed=$((passed + 1))
        fi
    else
        failed=$((failed + 1))
    fi
}

# ---- valid: pure core, real headless SDL3 frames ----------------------------
run_case describe_0 OK describe 0
run_case describe_1 OK describe 1
run_case describe_2 OK describe 2
run_case render_0  OK render 640 480 0
run_case render_2  OK render 320 240 2

# ---- expected refusals through widgets.hp / app guards ---------------------
run_case refuse_bad_scenario          REFUSED refuse bad-scenario
run_case refuse_bad_window_geometry   REFUSED refuse bad-window-geometry
run_case refuse_null_window           REFUSED refuse null-window
run_case refuse_small_grid            REFUSED refuse small-grid
run_case refuse_bad_widget_bounds     REFUSED refuse bad-widget-bounds
run_case refuse_duplicate_id          REFUSED refuse duplicate-id
run_case refuse_long_label            REFUSED refuse long-label
run_case refuse_newline_label         REFUSED refuse newline-label
run_case refuse_null_label            REFUSED refuse null-label
run_case refuse_bad_index             REFUSED refuse bad-index
run_case refuse_null_hit_output       REFUSED refuse null-hit-output
run_case refuse_null_serialize_buffer REFUSED refuse null-serialize-buffer
run_case refuse_small_serialize_buffer REFUSED refuse small-serialize-buffer
run_case refuse_bad_render_zero       REFUSED refuse bad-render-zero
run_case refuse_bad_render_huge       REFUSED refuse bad-render-huge
run_case refuse_null_serial_size_output REFUSED refuse null-serial-size-output
run_case refuse_bad_hit_test_window   REFUSED refuse bad-hit-test-window
run_case refuse_mystery               REFUSED refuse mystery
run_case cmd_bogus                    REFUSED bogus
run_case cmd_describe_missing         REFUSED describe
run_case cmd_render_missing           REFUSED render
run_case cmd_describe_bad_id          REFUSED describe 99
run_case cmd_render_zero              REFUSED render 0 480 0
run_case cmd_render_huge              REFUSED render 100000 100000 0

# ---- pure core must work with NO display environment at all ----------------
scenarios=$((scenarios + 1))
printf '=== nodesc_describe (OK, SDL_VIDEODRIVER unset) ===\n'
env -u SDL_VIDEODRIVER timeout 30 "$BIN" describe 2 \
    >"$RES_DIR/nodesc_describe.out" 2>"$RES_DIR/nodesc_describe.err"
rc=$?
cat "$RES_DIR/nodesc_describe.out"
if [ "$rc" -eq 0 ] && [ ! -s "$RES_DIR/nodesc_describe.err" ] && \
   cmp -s "$RES_DIR/nodesc_describe.out" "$EXP_DIR/nodesc_describe.out"; then
    passed=$((passed + 1))
else
    echo "FAIL nodesc_describe exit=$rc"
    failed=$((failed + 1))
fi

# ---- Windows PE compile-only check (never a runtime claim) -----------------
echo "=== Windows PE compile-only check ==="
if sh "$HERE/run_windows_cross.sh" >"$RES_DIR/windows_cross.log" 2>&1; then
    cat "$RES_DIR/windows_cross.log"
    if grep -q 'object=BLOCKED' "$RES_DIR/windows_cross.log"; then
        echo "WIN_CROSS: toolchain absent; reported BLOCKED, not a failure"
    fi
else
    echo "FAIL windows cross-compile check"
    cat "$RES_DIR/windows_cross.log"
    failed=$((failed + 1))
fi

echo "GUI_APP $scenarios $passed $failed $refused"
if [ "$failed" -ne 0 ]; then
    exit 1
fi
exit 0

#!/bin/sh
# Build and run the headless Nuklear dashboard example with fixed argv
# scenarios (valid + hostile), then print, as the last matching line:
#   GUI_DASHBOARD <scenarios> <passed> <failed> <refused>
#
# Every scenario is run twice under `timeout 30`; stdout must be byte-identical
# and stderr must be empty.  Run from anywhere; paths derive from this script.
set -u

DASH_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$DASH_DIR/../../.." && pwd)
RES="$DASH_DIR/results"
C="$ROOT/build/c+"
BIN="$RES/dashboard"

cd "$ROOT" || exit 1
mkdir -p "$RES"

# Per stdlib/gui.hp docs the Nuklear + SDL3 renderer backend is compiled once
# into build/ext/libnuklear.a.  It ships with this repository; do not rebuild
# third_party here.  Fail loudly if it is missing rather than fabricate a pass.
if [ ! -f "$ROOT/build/ext/libnuklear.a" ]; then
    echo "BUILD_FAIL missing $ROOT/build/ext/libnuklear.a" >&2
    echo "GUI_DASHBOARD 0 0 1 0"
    exit 1
fi

SDL="$(pkg-config --cflags --libs sdl3 2>/dev/null)"
NUKLEAR="-I$ROOT/third_party/nuklear -L$ROOT/build/ext -lnuklear"

scenarios=0
passed=0
failed=0
refused=0

echo "=== BUILD dashboard ==="
if ! "$C" build "$DASH_DIR/dashboard.cp" --strict --sanitize=none -I. \
        $SDL $NUKLEAR -o "$BIN" >"$RES/dashboard.build.log" 2>&1; then
    echo "BUILD_FAIL dashboard"
    cat "$RES/dashboard.build.log"
    echo "GUI_DASHBOARD 0 0 1 0"
    exit 1
fi

# run_case <tag> <OK|REFUSED> <exact-expected-line> [argv...]
run_case() {
    tag=$1
    expect=$2
    expected=$3
    shift 3
    scenarios=$((scenarios + 1))
    printf '=== %s (%s) ===\n' "$tag" "$expect"

    SDL_VIDEODRIVER=dummy timeout 30 "$BIN" "$@" \
        >"$RES/$tag.out" 2>"$RES/$tag.err"
    rc=$?
    cat "$RES/$tag.out"

    ok=1
    if [ "$rc" -ne 0 ]; then
        echo "FAIL $tag exit=$rc"
        ok=0
    fi
    if [ -s "$RES/$tag.err" ]; then
        echo "FAIL $tag stderr not empty"
        cat "$RES/$tag.err"
        ok=0
    fi
    if ! grep -qxF "$expected" "$RES/$tag.out"; then
        echo "FAIL $tag expected line missing: $expected"
        ok=0
    fi
    if ! grep -qE "^DASH_RESULT .* $expect( |$)" "$RES/$tag.out"; then
        echo "FAIL $tag missing DASH_RESULT status $expect"
        ok=0
    fi

    # Determinism: same argv must produce byte-identical stdout on a second run.
    SDL_VIDEODRIVER=dummy timeout 30 "$BIN" "$@" \
        >"$RES/$tag.out2" 2>"$RES/$tag.err2"
    rc2=$?
    if [ "$rc2" -ne 0 ]; then
        echo "FAIL $tag second run exit=$rc2"
        ok=0
    fi
    if ! cmp -s "$RES/$tag.out" "$RES/$tag.out2"; then
        echo "FAIL $tag nondeterministic stdout"
        ok=0
    fi
    if [ -s "$RES/$tag.err2" ]; then
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

# ---- valid scenarios: one fixed UI description, one real headless frame ----
run_case describe_overview OK \
    'DASH_UI id=0 title=overview labels=CPU,MEM,NET buttons=RUN:ON,STOP:OFF,RESET:ON' \
    describe 0
run_case describe_operations OK \
    'DASH_UI id=1 title=operations labels=TASKS,QUEUE,ERRORS buttons=START:OFF,PAUSE:ON,CLEAR:OFF' \
    describe 1
run_case describe_diagnostics OK \
    'DASH_UI id=2 title=diagnostics labels=TEMP,FAN,PWR buttons=ACK:ON,MUTE:ON,RESET:OFF' \
    describe 2
run_case render_overview OK \
    'DASH_RENDER 640 480 0 shown 1 widgets 6 frame 1' \
    render 640 480 0
run_case render_diagnostics OK \
    'DASH_RENDER 320 240 2 shown 1 widgets 6 frame 1' \
    render 320 240 2

# ---- hostile scenarios: every bad input is a recoverable error* refusal -----
run_case render_zero_width REFUSED \
    'DASH_REFUSED render:0,480,0 category 1 code 108 message dashboard: window dimensions must be positive' \
    render 0 480 0
run_case render_negative_height REFUSED \
    'DASH_REFUSED render:640,-5,0 category 1 code 108 message dashboard: window dimensions must be positive' \
    render 640 -5 0
run_case render_oversized REFUSED \
    'DASH_REFUSED render:100000,100000,0 category 2 code 101 message dashboard: window dimensions exceed DASH_MAX_DIM' \
    render 100000 100000 0
run_case describe_bad_id REFUSED \
    'DASH_REFUSED describe:99,0,0 category 2 code 101 message scenario: id out of range' \
    describe 99
run_case refuse_zero_width REFUSED \
    'DASH_REFUSED zero-width category 1 code 108 message dashboard: window dimensions must be positive' \
    refuse zero-width
run_case refuse_negative_height REFUSED \
    'DASH_REFUSED negative-height category 1 code 108 message dashboard: window dimensions must be positive' \
    refuse negative-height
run_case refuse_oversized REFUSED \
    'DASH_REFUSED oversized category 2 code 101 message dashboard: window dimensions exceed DASH_MAX_DIM' \
    refuse oversized
run_case refuse_null_context REFUSED \
    'DASH_REFUSED null-context category 1 code 108 message gui: invalid window or renderer' \
    refuse null-context
run_case refuse_bad_scenario REFUSED \
    'DASH_REFUSED bad-scenario category 2 code 101 message scenario: id out of range' \
    refuse bad-scenario
run_case refuse_unknown_case REFUSED \
    'DASH_REFUSED mystery category 1 code 108 message refuse: unknown case' \
    refuse mystery
run_case unknown_command REFUSED \
    'DASH_REFUSED command category 1 code 108 message dashboard: unknown command' \
    bogus

# ---- core paths must also work with no display environment at all ----------
# (separate from run_case so SDL_VIDEODRIVER stays unset)
echo "=== no_display_core (OK) ==="
env -u SDL_VIDEODRIVER timeout 30 "$BIN" describe 2 \
    >"$RES/no_display_core.out" 2>"$RES/no_display_core.err"
rc=$?
scenarios=$((scenarios + 1))
nodisp_ok=1
if [ "$rc" -ne 0 ] || [ -s "$RES/no_display_core.err" ] || \
   ! grep -qxF 'DASH_UI id=2 title=diagnostics labels=TEMP,FAN,PWR buttons=ACK:ON,MUTE:ON,RESET:OFF' \
        "$RES/no_display_core.out"; then
    echo "FAIL no_display_core exit=$rc"
    cat "$RES/no_display_core.out" "$RES/no_display_core.err"
    nodisp_ok=0
fi
cat "$RES/no_display_core.out"
if [ "$nodisp_ok" -eq 1 ]; then
    passed=$((passed + 1))
else
    failed=$((failed + 1))
fi

echo "GUI_DASHBOARD $scenarios $passed $failed $refused"
if [ "$failed" -ne 0 ]; then
    exit 1
fi
exit 0

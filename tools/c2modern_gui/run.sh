#!/bin/sh
# Build and run the headless Nuklear C->modern-C converter GUI with fixed argv
# scenarios (valid + hostile), then print, as the last matching line:
#   C2GUI <scenarios> <passed> <failed> <refused>
#
# Every scenario is run twice under `timeout 30`; stdout must be byte-identical
# and stderr must be empty.  Run from anywhere; paths derive from this script.
# This is a headless TEACHING GUI: the converter report is a deterministic MODEL
# marked PENDING (the CLI converter is not ready).  Not a production IDE.
set -u

GUI_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$GUI_DIR/../.." && pwd)
RES="$GUI_DIR/results"
C="$ROOT/build/c+"
BIN="$RES/converter_gui"

cd "$ROOT" || exit 1
mkdir -p "$RES"

# Per stdlib/gui.hp docs the Nuklear + SDL3 renderer backend is compiled once
# into build/ext/libnuklear.a.  It ships with this repository; do not rebuild
# third_party here.  Fail loudly if it is missing rather than fabricate a pass.
if [ ! -f "$ROOT/build/ext/libnuklear.a" ]; then
    echo "BUILD_FAIL missing $ROOT/build/ext/libnuklear.a" >&2
    echo "C2GUI 0 0 1 0"
    exit 1
fi

SDL="$(pkg-config --cflags --libs sdl3 2>/dev/null)"
NUKLEAR="-I$ROOT/third_party/nuklear -L$ROOT/build/ext -lnuklear"

scenarios=0
passed=0
failed=0
refused=0

echo "=== BUILD converter_gui ==="
rm -f "$BIN"
if ! "$C" build "$GUI_DIR/converter_gui.cp" --strict --sanitize=none -I. \
        $SDL $NUKLEAR -o "$BIN" >"$RES/converter_gui.build.log" 2>&1; then
    echo "BUILD_FAIL converter_gui"
    cat "$RES/converter_gui.build.log"
    echo "C2GUI 0 0 1 0"
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
    if ! grep -qE "^C2GUI_RESULT .* $expect( |$)" "$RES/$tag.out"; then
        echo "FAIL $tag missing C2GUI_RESULT status $expect"
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

# ---- valid core scenarios: deterministic widget/result transcripts --------
run_case describe_hello OK \
    'C2GUI_UI id=0 title=convert-hello widgets=input:field,convert:button,output:field,status:label,build:label,run:label input=tools/c2modern_gui/fixtures/hello.c output=tools/c2modern_gui/results/hello.cpp status=PENDING build=NOT_RUN run=NOT_RUN source_lines=2 source_bytes=61' \
    describe 0
run_case describe_util OK \
    'C2GUI_UI id=1 title=convert-util widgets=input:field,convert:button,output:field,status:label,build:label,run:label input=tools/c2modern_gui/fixtures/util.c output=tools/c2modern_gui/results/util.cpp status=PENDING build=NOT_RUN run=NOT_RUN source_lines=1 source_bytes=39' \
    describe 1
run_case describe_main OK \
    'C2GUI_UI id=2 title=convert-main widgets=input:field,convert:button,output:field,status:label,build:label,run:label input=tools/c2modern_gui/fixtures/main.c output=tools/c2modern_gui/results/main.cpp status=PENDING build=NOT_RUN run=NOT_RUN source_lines=2 source_bytes=45' \
    describe 2
run_case convert_hello OK \
    'C2GUI_REPORT input=tools/c2modern_gui/fixtures/hello.c output=out.cpp status=PENDING build=NOT_RUN run=NOT_RUN source_lines=2 source_bytes=61' \
    convert tools/c2modern_gui/fixtures/hello.c out.cpp

# ---- valid real headless frames (SDL3 + Nuklear, dummy driver) ------------
run_case render_hello OK \
    'C2GUI_RENDER 640 480 0 shown 1 widgets 8 frame 1' \
    render 640 480 0
run_case render_main OK \
    'C2GUI_RENDER 320 240 2 shown 1 widgets 8 frame 1' \
    render 320 240 2

# ---- hostile conversion inputs: recoverable error* refusals ---------------
run_case convert_empty_input REFUSED \
    'C2GUI_REFUSED convert category 1 code 108 message c2gui: empty path' \
    convert '' out.cpp
run_case convert_empty_output REFUSED \
    'C2GUI_REFUSED convert category 1 code 108 message c2gui: empty path' \
    convert tools/c2modern_gui/fixtures/hello.c ''
run_case convert_non_c REFUSED \
    'C2GUI_REFUSED convert category 1 code 108 message c2gui: input must be a .c file' \
    convert notes.txt out.cpp
run_case convert_missing_file REFUSED \
    'C2GUI_REFUSED convert category 3 code 109 message c2gui: cannot open input file' \
    convert tools/c2modern_gui/fixtures/no_such_file.c out.cpp

# ---- hostile render inputs ------------------------------------------------
run_case render_zero_width REFUSED \
    'C2GUI_REFUSED render:0,480,0 category 1 code 108 message c2gui: window dimensions must be positive' \
    render 0 480 0
run_case render_negative_height REFUSED \
    'C2GUI_REFUSED render:640,-5,0 category 1 code 108 message c2gui: window dimensions must be positive' \
    render 640 -5 0
run_case render_oversized_dims REFUSED \
    'C2GUI_REFUSED render:100000,100000,0 category 2 code 101 message c2gui: window dimensions exceed 4096' \
    render 100000 100000 0
run_case describe_bad_id REFUSED \
    'C2GUI_REFUSED describe:99 category 2 code 101 message c2gui: scenario id out of range' \
    describe 99

# ---- explicit refusal driver ----------------------------------------------
run_case refuse_empty_input REFUSED \
    'C2GUI_REFUSED empty-input category 1 code 108 message c2gui: empty path' \
    refuse empty-input
run_case refuse_oversized REFUSED \
    'C2GUI_REFUSED oversized category 5 code 101 message c2gui: path exceeds 256 bytes' \
    refuse oversized
run_case refuse_non_c REFUSED \
    'C2GUI_REFUSED non-c category 1 code 108 message c2gui: input must be a .c file' \
    refuse non-c
run_case refuse_null_input REFUSED \
    'C2GUI_REFUSED null-input category 1 code 102 message c2gui: null path' \
    refuse null-input
run_case refuse_null_context REFUSED \
    'C2GUI_REFUSED null-context category 1 code 102 message c2gui: null GUI context' \
    refuse null-context
run_case refuse_missing_file REFUSED \
    'C2GUI_REFUSED missing-file category 3 code 109 message c2gui: cannot open input file' \
    refuse missing-file
run_case refuse_unknown_case REFUSED \
    'C2GUI_REFUSED mystery category 1 code 108 message c2gui: unknown refusal case' \
    refuse mystery
run_case unknown_command REFUSED \
    'C2GUI_REFUSED command category 1 code 108 message c2gui: unknown command' \
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
   ! grep -qxF 'C2GUI_UI id=2 title=convert-main widgets=input:field,convert:button,output:field,status:label,build:label,run:label input=tools/c2modern_gui/fixtures/main.c output=tools/c2modern_gui/results/main.cpp status=PENDING build=NOT_RUN run=NOT_RUN source_lines=2 source_bytes=45' \
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

echo "C2GUI $scenarios $passed $failed $refused"
if [ "$failed" -ne 0 ]; then
    exit 1
fi
exit 0

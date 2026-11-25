#!/bin/sh
# Build and exercise the headless Nuklear + SDL3 "invaders_ui" teaching game.
#
# Four positive scenarios (compact token, per-character argv, stdin line and
# the built-in default sequence) must all exit 0 under `timeout 30`, write no
# stderr, and produce byte-identical transcripts for the same sequence.  Five
# adverse scenarios must be refused cleanly: exit exactly 2, empty stdout and
# a "REFUSED reason=... code=... category=..." line on stderr.
#
# Final line: GFX_INVADERS <scenarios> <passed> <failed> <refused>
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../../.." && pwd)
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
DIR=apps/legacy_examples/modern_gui/games/invaders_ui
BIN=$DIR/bin
mkdir -p "$BIN"

SCENARIOS=9
passed=0
failed=0
refused=0

finish() {
    echo "GFX_INVADERS $SCENARIOS $passed $failed $refused"
    if [ "$failed" -eq 0 ]; then
        exit 0
    fi
    exit 1
}

SDL_FLAGS=$(pkg-config --cflags --libs sdl3 2>/dev/null)
if [ -z "$SDL_FLAGS" ]; then
    echo "BUILD FAIL pkg-config sdl3 is unavailable"
    failed=1
    finish
fi

# Nuklear is compiled once.  Prefer the artifact the standard stdlib harness
# builds; otherwise build it locally so this example writes only inside its
# own directory.
NUK_INC="-I$ROOT/third_party/nuklear"
NUK_LIB="-L$ROOT/build/ext -lnuklear"
if [ ! -f "$ROOT/build/ext/libnuklear.a" ]; then
    CLANG=${CPLUS_CLANG:-/usr/bin/clang}
    if [ ! -x "$CLANG" ]; then
        CLANG=clang
    fi
    if ! "$CLANG" -c "$ROOT/third_party/nuklear/nuklear_impl.c" \
            -o "$BIN/nuklear_impl.o" -O2 "$NUK_INC" $SDL_FLAGS \
            >"$BIN/nuklear.build.log" 2>&1; then
        echo "BUILD FAIL nuklear_impl.c (see $BIN/nuklear.build.log)"
        failed=1
        finish
    fi
    ar rcs "$BIN/libnuklear_local.a" "$BIN/nuklear_impl.o"
    NUK_LIB="-L$ROOT/$BIN -lnuklear_local"
fi

BUILD_LOG="$BIN/invaders_ui.build.log"
if "$CPLUS" build "$DIR/invaders_ui.cp" --sanitize=none $SDL_FLAGS -I. \
        "$NUK_INC" $NUK_LIB -o "$BIN/invaders_ui" >"$BUILD_LOG" 2>&1; then
    echo "BUILD ok invaders_ui"
else
    echo "BUILD FAIL invaders_ui (see $BUILD_LOG)"
    failed=1
    finish
fi

# Run one program under the headless dummy driver with a 30-second bound.
headless_run() {
    out_file=$1
    hr_err="$out_file.err"
    shift
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 30 \
        "$BIN/invaders_ui" "$@" >"$out_file" 2>"$hr_err"
    echo $?
}

# Verify a positive scenario: rc=0, empty stderr and required HUD markers.
positive() {
    p_name=$1
    p_out=$2
    p_rc=$3
    if [ "$p_rc" -ne 0 ]; then
        echo "RUN FAIL $p_name rc=$p_rc"
        failed=$((failed + 1))
        return
    fi
    if [ -s "$p_out.err" ]; then
        echo "RUN FAIL $p_name wrote stderr"
        failed=$((failed + 1))
        return
    fi
    for marker in 'driver=dummy' 'HUD SCORE 0' 'HUD LIVES 3' \
                  'HUD WAVE 1' 'PIXEL hud_nonbg=1'; do
        if ! grep -q "$marker" "$p_out"; then
            echo "RUN FAIL $p_name missing '$marker'"
            failed=$((failed + 1))
            return
        fi
    done
    if ! grep -q '^FINAL outcome=complete ticks=21 ' "$p_out"; then
        echo "RUN FAIL $p_name missing FINAL line"
        failed=$((failed + 1))
        return
    fi
    echo "RUN ok $p_name"
    passed=$((passed + 1))
}

# Verify a clean refusal: rc=2, empty stdout, exact REFUSED prefix.
adverse() {
    a_name=$1
    a_reason=$2
    a_code=$3
    shift 3
    a_out="$BIN/refuse.$a_name.out"
    a_rc=$(headless_run "$a_out" "$@")
    if [ "$a_rc" -ne 2 ]; then
        echo "REFUSE FAIL $a_name expected rc=2 got $a_rc"
        failed=$((failed + 1))
        return
    fi
    if [ -s "$a_out" ]; then
        echo "REFUSE FAIL $a_name wrote stdout"
        failed=$((failed + 1))
        return
    fi
    if ! grep -q "^REFUSED reason=$a_reason code=$a_code category=" "$a_out.err"; then
        echo "REFUSE FAIL $a_name missing exact REFUSED line"
        failed=$((failed + 1))
        return
    fi
    echo "REFUSE ok $a_name code=$a_code"
    refused=$((refused + 1))
}

SEQ='LLLLLLLLLFFFFFFFFFFFF'

# --- positive scenario 1: one compact argv token ---------------------------
rc=$(headless_run "$BIN/compact.out" "$SEQ")
positive compact "$BIN/compact.out" "$rc"
compact_ok=$([ "$rc" -eq 0 ] && echo 1 || echo 0)

# --- positive scenario 2: one token per command ----------------------------
set --
i=1
while [ "$i" -le "${#SEQ}" ]; do
    set -- "$@" "$(printf '%s' "$SEQ" | cut -c "$i")"
    i=$((i + 1))
done
rc=$(headless_run "$BIN/argv.out" "$@")
positive argv "$BIN/argv.out" "$rc"

# --- positive scenario 3: one line from stdin ------------------------------
printf '%s\n' "$SEQ" >"$BIN/stdin.in"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 30 \
    "$BIN/invaders_ui" - <"$BIN/stdin.in" >"$BIN/stdin.out" 2>"$BIN/stdin.out.err"
rc=$?
positive stdin "$BIN/stdin.out" "$rc"

# The miss-heavy sequence must visibly spend a life (HUD LIVES 3 -> 2).
for f in "$BIN/compact.out" "$BIN/argv.out" "$BIN/stdin.out"; do
    if grep -q 'HUD LIVES 2' "$f" && grep -q 'lost=4' "$f"; then
        echo "LIVES ok $(basename "$f")"
    else
        echo "LIVES FAIL $(basename "$f")"
        failed=$((failed + 1))
    fi
done

# Byte-for-byte determinism across the three input modes.
if [ "$compact_ok" -eq 1 ] && [ "$rc" -eq 0 ]; then
    if cmp -s "$BIN/compact.out" "$BIN/argv.out" && \
       cmp -s "$BIN/compact.out" "$BIN/stdin.out"; then
        echo "TRANSCRIPT ok compact=argv=stdin"
    else
        echo "TRANSCRIPT FAIL transcripts differ"
        failed=$((failed + 1))
    fi
fi

# --- positive scenario 4: default sequence, run twice ----------------------
rc=$(headless_run "$BIN/default.out")
positive default "$BIN/default.out" "$rc"
rc2=$(headless_run "$BIN/default.repeat.out")
if [ "$rc2" -eq 0 ] && cmp -s "$BIN/default.out" "$BIN/default.repeat.out"; then
    echo "TRANSCRIPT ok default repeat"
else
    echo "TRANSCRIPT FAIL default repeat differs rc=$rc2"
    failed=$((failed + 1))
fi

# --- adverse scenarios ------------------------------------------------------
adverse bad-command bad-command 108 X
adverse pool-oversized pool-oversized 101 --bullets=64
adverse null-surface null-surface 108 --null-surface
adverse null-context null-context 108 --null-context

set --
i=0
while [ "$i" -lt 129 ]; do
    set -- "$@" L
    i=$((i + 1))
done
adverse too-many-ticks too-many-ticks 101 "$@"

finish

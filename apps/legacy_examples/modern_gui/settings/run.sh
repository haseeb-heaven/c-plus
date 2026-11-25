#!/bin/sh
# Build and run the headless Nuklear settings panel (one fixed scenario per run).
#
#   GUI_SETTINGS <scenarios> <passed> <failed> <refused>
#
# A scenario passes only if it builds, runs under `timeout 30` with
# SDL_VIDEODRIVER=dummy, exits 0 and prints its expected APPLIED/REFUSED line.
# Refusals are expected outcomes: the app exits 0 when the guard fires and
# exits nonzero on an unexpected success, a wrong error code/category or a
# modified (non-sentinel) settings state.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT" || exit 2

CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
CXX=${CPLUS_CLANG:-clang}
SRC=examples/modern_gui/settings/settings_panel.cp
BIN=examples/modern_gui/settings/settings_panel
EXT="$ROOT/build/ext"
SCENARIOS="apply text_oversize slider_range null_context null_text invalid_utf8"

TMP=$(mktemp -d "$HERE/.run.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

scenarios=0
passed=0
failed=0
refused=0
for _name in $SCENARIOS; do scenarios=$((scenarios + 1)); done

finalize() {
    printf 'GUI_SETTINGS %d %d %d %d\n' "$scenarios" "$passed" "$failed" "$refused"
    [ "$failed" -eq 0 ] && exit 0
    exit 1
}

fail_all() {
    _reason=$1
    for _name in $SCENARIOS; do
        printf 'SETTINGS %s FAIL code=-1 cat=build %s\n' "$_name" "$_reason"
        failed=$((failed + 1))
    done
    [ -n "$2" ] && cat "$2"
    finalize
}

# Build the vendored Nuklear implementation once, only if the archive is absent.
# (This repository already ships build/ext/libnuklear.a; no write then.)
if [ ! -f "$EXT/libnuklear.a" ]; then
    if ! pkg-config --exists sdl3; then
        fail_all "sdl3-pkgconfig-missing" ""
    fi
    mkdir -p "$EXT" || exit 2
    if ! "$CXX" -c "$ROOT/third_party/nuklear/nuklear_impl.c" -o "$EXT/nuklear.o" \
        -O2 -I"$ROOT/third_party/nuklear" $(pkg-config --cflags sdl3) \
        >"$TMP/nuklear.build.log" 2>&1; then
        fail_all "nuklear-build-failed" "$TMP/nuklear.build.log"
    fi
    ar rcs "$EXT/libnuklear.a" "$EXT/nuklear.o" || fail_all "nuklear-archive-failed" ""
fi

if [ ! -x "$CPLUS" ]; then
    fail_all "cplus-compiler-missing" ""
fi

rm -f "$BIN"
if ! "$CPLUS" build "$SRC" --strict --sanitize=none -I. \
    $(pkg-config --cflags sdl3) -Ithird_party/nuklear \
    -Lbuild/ext -lnuklear $(pkg-config --libs sdl3) \
    -o "$BIN" >"$TMP/settings_panel.build.log" 2>&1; then
    fail_all "build-command-error" "$TMP/settings_panel.build.log"
fi
# The C+ driver can exit 0 even when linking failed; require the binary.
if [ ! -x "$BIN" ]; then
    fail_all "no-binary" "$TMP/settings_panel.build.log"
fi

for name in $SCENARIOS; do
    SDL_VIDEODRIVER=dummy timeout 30 "./$BIN" "$name" >"$TMP/$name.out" 2>&1
    rc=$?
    cat "$TMP/$name.out"

    if [ "$name" = "apply" ]; then want=APPLIED; else want=REFUSED; fi
    if [ "$rc" -eq 0 ] && grep -q "SETTINGS $name $want code=" "$TMP/$name.out"; then
        passed=$((passed + 1))
    else
        printf 'SETTINGS %s FAIL exit=%d expected=%s\n' "$name" "$rc" "$want"
        failed=$((failed + 1))
    fi
    if grep -q "SETTINGS $name REFUSED code=" "$TMP/$name.out"; then
        refused=$((refused + 1))
    fi
done

finalize

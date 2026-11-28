#!/bin/sh
# Windows PE cross-compile (COMPILE-ONLY) check for apps/gui_app/.
#
#   sh apps/gui_app/run_windows_cross.sh
#
# What this DOES:
#   1. Compile win_portability_probe.c natively and run it; its CRC32 must match
#      the real gui_app.cp GUI_SERIAL checksum (a52b03af) so the C mirror is
#      validated against the C+ program before the cross-compile is trusted.
#   2. Compile that same source with x86_64-w64-mingw32-gcc -c and verify the
#      artifact is a genuine Intel amd64 COFF object with `file`.
#
# What this does NOT do:
#   - It does NOT compile or run the SDL3 GUI layer on Windows (mingw-w64 here
#     has no SDL3 headers, and C+ .hp headers cannot be parsed by gcc).
#   - It does NOT produce a runnable .exe and does NOT claim Windows runtime
#     success.  Highball/Wine runtime is BLOCKED in this repository; no Windows
#     execution attempt is made.
#
# Final line: GUI_APP_WINDOWS_COFF compile_only=1 native_probe=PASS object=PASS
#             runtime=BLOCKED   (or toolchain=BLOCKED when mingw is absent)
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd) || exit 2
cd "$ROOT" || exit 2

OUT="$HERE/results/win_cross"
mkdir -p "$OUT"
SRC="$HERE/win_portability_probe.c"
NATIVE="$OUT/win_probe_native"
OBJ="$OUT/win_portability_probe.o"
EXPECT='WIN_PROBE bytes=157 crc32=a52b03af OK'

CC_NATIVE=${CPLUS_CLANG:-/usr/bin/clang}
if [ ! -x "$CC_NATIVE" ]; then
    CC_NATIVE=$(command -v clang 2>/dev/null || command -v cc 2>/dev/null || true)
fi

if [ -z "$CC_NATIVE" ] || ! command -v "$CC_NATIVE" >/dev/null 2>&1; then
    echo "WIN_CROSS_FAIL no native C compiler for probe validation"
    echo "GUI_APP_WINDOWS_COFF compile_only=1 native_probe=FAIL object=FAIL runtime=BLOCKED"
    exit 1
fi

if ! "$CC_NATIVE" -O2 "$SRC" -o "$NATIVE" >"$OUT/native.build.log" 2>&1; then
    echo "WIN_CROSS_FAIL native probe build"
    cat "$OUT/native.build.log"
    echo "GUI_APP_WINDOWS_COFF compile_only=1 native_probe=FAIL object=FAIL runtime=BLOCKED"
    exit 1
fi

timeout 30 "$NATIVE" >"$OUT/native.out" 2>"$OUT/native.err"
rc=$?
if [ "$rc" -ne 0 ] || [ -s "$OUT/native.err" ] || \
   ! grep -qxF "$EXPECT" "$OUT/native.out"; then
    echo "WIN_CROSS_FAIL native probe output rc=$rc"
    cat "$OUT/native.out" "$OUT/native.err"
    echo "GUI_APP_WINDOWS_COFF compile_only=1 native_probe=FAIL object=FAIL runtime=BLOCKED"
    exit 1
fi
cat "$OUT/native.out"

CC_WIN=x86_64-w64-mingw32-gcc
if ! command -v "$CC_WIN" >/dev/null 2>&1; then
    echo "WIN_CROSS_BLOCKED $CC_WIN not installed; compile-only check not run"
    echo "GUI_APP_WINDOWS_COFF compile_only=1 native_probe=PASS object=BLOCKED runtime=BLOCKED"
    exit 0
fi

if ! timeout 30 "$CC_WIN" -O2 -c "$SRC" -o "$OBJ" >"$OUT/mingw.build.log" 2>&1; then
    echo "WIN_CROSS_FAIL mingw compile-only build"
    cat "$OUT/mingw.build.log"
    echo "GUI_APP_WINDOWS_COFF compile_only=1 native_probe=PASS object=FAIL runtime=BLOCKED"
    exit 1
fi
if [ ! -f "$OBJ" ]; then
    echo "WIN_CROSS_FAIL mingw produced no object"
    echo "GUI_APP_WINDOWS_COFF compile_only=1 native_probe=PASS object=FAIL runtime=BLOCKED"
    exit 1
fi

KIND=$(file -b "$OBJ" 2>/dev/null || true)
case "$KIND" in
    *"COFF object file"*) kind_ok=1 ;;
    *) kind_ok=0 ;;
esac
if [ "$kind_ok" -ne 1 ]; then
    echo "WIN_CROSS_FAIL object is not COFF: $KIND"
    echo "GUI_APP_WINDOWS_COFF compile_only=1 native_probe=PASS object=FAIL runtime=BLOCKED"
    exit 1
fi
printf 'WIN_CROSS_COFF %s\n' "$KIND"

echo "GUI_APP_WINDOWS_COFF compile_only=1 native_probe=PASS object=PASS runtime=BLOCKED"
exit 0

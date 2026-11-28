#!/bin/sh
# Native C+ runner for the 100-configuration fuzz corpus.
#
#   sh tests/fuzz100/run_native.sh [--config F001] [--dry-run] [--json-stdout]
#
# This wrapper builds tests/fuzz100/run.cp with the repository C+ driver and
# then executes it.  All discovery, build, run and reporting lives in run.cp;
# this script only fixes the repository root and the toolchain paths, so it
# works when invoked from any directory.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
SRC="$ROOT/tests/fuzz100/run.cp"
OUT="$ROOT/build/fuzz100"
BIN="$OUT/run_native.bin"

if [ ! -x "$CPLUS" ]; then
    echo "error: C+ compiler not found at $CPLUS (build with cmake --build build)" >&2
    exit 1
fi

mkdir -p "$OUT"
cd "$ROOT"

# cplus_system() lives in the hardened libc archive.  Pass it after "--" so
# the C+ driver forwards it to the linker only (a bare .a operand would be
# treated as another source file by the backend).
"$CPLUS" build "$SRC" --strict --sanitize=none \
    -I"$ROOT/third_party/yyjson" -L"$ROOT/build/ext" -lyyjson \
    -o "$BIN" -- "$ROOT/build/libc/libcplus_hardened.a"

F100_ROOT="$ROOT"
export F100_ROOT
exec "$BIN" --root "$ROOT" "$@"

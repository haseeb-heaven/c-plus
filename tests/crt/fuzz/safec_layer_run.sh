#!/bin/sh
# Deterministic fuzz driver for the checked ordinary-name C layer.
# Builds each harness with /usr/bin/clang against the two prebuilt static
# libraries, runs it under `timeout 30`, and also replays with UBSan enabled.
# All generated binaries live in a temporary directory and are removed on exit.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

CC=${CPLUS_CLANG:-/usr/bin/clang}
OUTDIR=$(mktemp -d "${TMPDIR:-/tmp}/safec_layer_fuzz.XXXXXX")
trap 'rm -rf "$OUTDIR"' EXIT INT TERM

INCLUDES="-I. -Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
LIBS="build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a"
BASEFLAGS="-O1 -g -std=c99 -Wall -Wextra -pedantic $INCLUDES"

for name in safec_layer_memory safec_layer_string safec_layer_alloc; do
    src="tests/crt/fuzz/$name.c"
    echo "=== $name source digest ==="
    shasum -a 256 "$src" tests/crt/fuzz/safec_layer_common.h

    echo "=== $name compile (clang, -O1) ==="
    echo "$CC $BASEFLAGS $src $LIBS -lm -pthread -o $OUTDIR/$name"
    # shellcheck disable=SC2086
    $CC $BASEFLAGS "$src" $LIBS -lm -pthread -o "$OUTDIR/$name"

    echo "=== $name run (timeout 30) ==="
    echo "timeout 30 $OUTDIR/$name"
    timeout 30 "$OUTDIR/$name"

    echo "=== $name compile (clang, UBSan) ==="
    echo "$CC $BASEFLAGS -fsanitize=undefined -fno-sanitize-recover=all $src $LIBS -lm -pthread -o $OUTDIR/${name}_ubsan"
    # shellcheck disable=SC2086
    $CC $BASEFLAGS -fsanitize=undefined -fno-sanitize-recover=all "$src" $LIBS -lm -pthread -o "$OUTDIR/${name}_ubsan"

    echo "=== $name run (UBSan, timeout 30) ==="
    echo "timeout 30 $OUTDIR/${name}_ubsan"
    timeout 30 "$OUTDIR/${name}_ubsan"
done

echo "safec_layer_run: ALL HARNESS PASS"

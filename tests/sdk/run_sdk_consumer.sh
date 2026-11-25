#!/bin/sh
# Prove the installed SDK works for an EXTERNAL CMake consumer via
# find_package(cplus_hardened_libc CONFIG), with no source-tree paths.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CC=${CPLUS_CLANG:-clang}

command -v cmake >/dev/null 2>&1 || {
    echo "sdk consumer: FAIL: cmake not found" >&2
    exit 1
}
command -v "$CC" >/dev/null 2>&1 || {
    echo "sdk consumer: FAIL: compiler '$CC' not found" >&2
    exit 1
}

WORK=$(mktemp -d "${TMPDIR:-/tmp}/cplus-sdk-consumer.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

LIBC_BLD="$WORK/libc-build"
PREFIX="$WORK/install-prefix"
CONSUMER_BLD="$WORK/external-consumer-build"

fail() {
    echo "sdk consumer: FAIL: $1" >&2
    shift
    for detail in "$@"; do
        printf '%s\n' "$detail" >&2
    done
    for log in "$WORK"/*.log; do
        [ -f "$log" ] || continue
        echo "--- $log ---" >&2
        cat "$log" >&2
    done
    exit 1
}

# 1. Build and install the SDK into a clean prefix.
cmake -S "$ROOT/libc" -B "$LIBC_BLD" -G "Unix Makefiles" \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    >"$WORK/libc-configure.log" 2>&1 || fail "libc configure failed"
cmake --build "$LIBC_BLD" \
    >"$WORK/libc-build.log" 2>&1 || fail "libc build failed"
cmake --install "$LIBC_BLD" \
    >"$WORK/libc-install.log" 2>&1 || fail "libc install failed"

# 2. Configure/build an independent consumer against ONLY the install prefix.
cmake -S "$ROOT/tests/sdk/consumer" -B "$CONSUMER_BLD" -G "Unix Makefiles" \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_PREFIX_PATH="$PREFIX" \
    >"$WORK/consumer-configure.log" 2>&1 || fail "external consumer configure failed"
cmake --build "$CONSUMER_BLD" \
    >"$WORK/consumer-build.log" 2>&1 || fail "external consumer build failed"

BIN="$CONSUMER_BLD/sdk_consumer"
[ -x "$BIN" ] || fail "consumer executable was not produced"

OUT=$("$BIN" 2>&1) || fail "consumer exited non-zero" "$OUT"
printf '%s\n' "$OUT" | grep -qx 'sdk consumer: ok' || fail "unexpected consumer output" "$OUT"

echo "sdk consumer: ok"

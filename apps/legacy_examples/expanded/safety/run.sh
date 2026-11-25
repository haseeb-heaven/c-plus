#!/bin/sh
# Build and run all four safety demos, then print the aggregate verdict.
#
#   SAFETY_DEMOS <demos> <passed> <failed>
#
# A demo passes only if it builds, runs under `timeout 30`, and exits 0 (the
# demo itself exits nonzero on any unexpected success or canary corruption).
# The vendored safeclib default handler prints an "IGNORE CONSTRAINT HANDLER:"
# line on a refused cplus_memcpy_s call; those lines are stripped from the
# recorded output so only "<demo> <case> OK|REFUSED <detail>" remains.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT" || exit 2

SAFE=apps/legacy_examples/expanded/safety
TMP=$(mktemp -d "${TMPDIR:-/tmp}/safety-demos.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

passed=0
failed=0
demos=0

# run_one <name> <build-command...>  -- builds, then times out the binary.
run_one() {
    name=$1
    shift
    demos=$((demos + 1))
    bin="$SAFE/$name"

    rm -f "$bin"
    if ! "$@" >"$TMP/$name.build" 2>&1; then
        printf 'SAFETY_DEMO %s FAIL build-command-error\n' "$name"
        cat "$TMP/$name.build"
        failed=$((failed + 1))
        return
    fi
    # The C+ driver can return 0 even when linking failed; require the binary.
    if [ ! -x "$bin" ]; then
        printf 'SAFETY_DEMO %s FAIL no-binary\n' "$name"
        cat "$TMP/$name.build"
        failed=$((failed + 1))
        return
    fi

    timeout 30 "./$bin" >"$TMP/$name.raw" 2>&1
    rc=$?
    # Strip safeclib's default-handler diagnostic lines.
    grep -v '^IGNORE CONSTRAINT HANDLER:' "$TMP/$name.raw" >"$TMP/$name.out"
    cat "$TMP/$name.out"

    if [ "$rc" -eq 0 ]; then
        printf 'SAFETY_DEMO %s PASS exit=0\n' "$name"
        passed=$((passed + 1))
    else
        printf 'SAFETY_DEMO %s FAIL exit=%d\n' "$name" "$rc"
        failed=$((failed + 1))
    fi
}

run_one memcpy_s_demo \
    ./build/c+ build "$SAFE/memcpy_s_demo.cp" --strict --sanitize=none -I. \
    -o "$SAFE/memcpy_s_demo" -- \
    build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a

run_one bounds_demo \
    ./build/c+ build "$SAFE/bounds_demo.cp" --strict --sanitize=none -I. \
    -o "$SAFE/bounds_demo"

run_one lifetime_demo \
    ./build/c+ build "$SAFE/lifetime_demo.cp" --strict --sanitize=none -I. \
    -o "$SAFE/lifetime_demo"

run_one sync_demo \
    /usr/bin/clang -std=c99 -D_POSIX_C_SOURCE=200809L -I. -Ilibc/install-include \
    "$SAFE/sync_demo.c" build/libc/libcplus_hardened.a -pthread \
    -o "$SAFE/sync_demo"

printf 'SAFETY_DEMOS %d %d %d\n' "$demos" "$passed" "$failed"
[ "$failed" -eq 0 ]

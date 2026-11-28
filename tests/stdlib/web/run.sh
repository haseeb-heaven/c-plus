#!/bin/sh
# stdlib/web.hp suite: real web-server app (apps/webserver) plus issue #61
# regressions (fd bounds, reply guards, short writes, Slowloris timeouts).
# Prints: TEST_WEB <cases> <pass> <fail>.  Exit 0 == pass.
set -u
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$D/../../.." && pwd)
cd "$ROOT" || exit 2
pass=0; fail=0; cases=0
cases=$((cases + 1))
timeout 900 sh apps/webserver/run.sh >/dev/null 2>&1 && pass=$((pass + 1)) || fail=$((fail + 1))

cases=$((cases + 1))
TMP=${TMPDIR:-/tmp}/cplus-web-regressions-$$
mkdir -p "$TMP" || exit 2
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
if timeout 120 "$ROOT/build/c+" build "$D/regressions.cp" --default --no-werror \
        --sanitize=none -I"$ROOT" -o "$TMP/regressions" -- \
        "$ROOT/build/libc/libcplus_hardened.a" >"$TMP/build.log" 2>&1 && \
   timeout 60 "$TMP/regressions" >"$TMP/out.txt" 2>"$TMP/err.txt" && \
   diff -q "$D/regressions.expected" "$TMP/out.txt" >/dev/null 2>&1 && \
   [ ! -s "$TMP/err.txt" ]; then
    pass=$((pass + 1))
else
    echo "TEST_WEB regressions FAIL" >&2
    cat "$TMP/build.log" "$TMP/out.txt" "$TMP/err.txt" >&2
    fail=$((fail + 1))
fi
echo "TEST_WEB $cases $pass $fail"
[ "$fail" -eq 0 ]

#!/bin/sh
# Real game-engine headless app (stdlib/gameeng.hp) verification: run the app
# and diff stdout against expected/app.txt.
# Prints: APP gameeng <pass> <fail>.  Exit 0 == pass.
set -u
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$D/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
pass=0; fail=0

"$ROOT/build/c+" build "$D/app.cp" --default --no-werror --sanitize=none -I. \
    -o "$D/app" -- "$ROOT/build/libc/libcplus_hardened.a" \
    >"$D/build.log" 2>&1 || { echo "APP gameeng FAIL build"; fail=$((fail + 1)); exit 1; }

timeout 30 "$D/app" >"$D/run_out.txt" 2>&1
if diff -q "$D/expected/app.txt" "$D/run_out.txt" >/dev/null 2>&1; then
    pass=$((pass + 1))
else
    echo "APP gameeng FAIL diff"; fail=$((fail + 1))
fi
echo "APP gameeng $pass $fail"
[ "$fail" -eq 0 ]

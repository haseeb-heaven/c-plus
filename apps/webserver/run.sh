#!/bin/sh
# Real web-server app (stdlib/web.hp) verification: start the app, curl three
# paths, diff stdout and bodies against expected/.
# Prints: APP webserver <pass> <fail>.  Exit 0 == pass.
set -u
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$D/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
pass=0; fail=0

"$ROOT/build/c+" build "$D/app.cp" --default --sanitize=none -I. \
    -o "$D/app" -- "$ROOT/build/libc/libcplus_hardened.a" \
    >"$D/build.log" 2>&1 || { echo "APP webserver FAIL build"; fail=$((fail + 1)); exit 1; }

timeout 12 "$D/app" >"$D/run_out.txt" 2>&1 &
APP=$!
sleep 1
curl -s -m 2 http://127.0.0.1:18099/health -o "$D/health.out"
sleep 0.5
curl -s -m 2 http://127.0.0.1:18099/other -o "$D/other.out"
sleep 0.5
curl -s -m 2 http://127.0.0.1:18099/ -o "$D/root.out"
wait $APP

for pair in "expected/app.txt run_out.txt" "health.out expected/health.out" \
            "other.out expected/other.out" "root.out expected/root.out"; do
    set -- $pair
    if diff -q "$D/$1" "$D/$2" >/dev/null 2>&1; then
        pass=$((pass + 1))
    else
        echo "APP webserver FAIL diff $1"; fail=$((fail + 1))
    fi
done
echo "APP webserver $pass $fail"
[ "$fail" -eq 0 ]

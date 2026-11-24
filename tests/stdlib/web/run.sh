#!/bin/sh
# stdlib/web.hp suite: real web-server app (apps/webserver).
# Prints: TEST_WEB <cases> <pass> <fail>.  Exit 0 == pass.
set -u
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$D/../../.." && pwd)
cd "$ROOT" || exit 2
pass=0; fail=0; cases=0
cases=$((cases + 1))
timeout 900 sh apps/webserver/run.sh >/dev/null 2>&1 && pass=$((pass + 1)) || fail=$((fail + 1))
echo "TEST_WEB $cases $pass $fail"
[ "$fail" -eq 0 ]

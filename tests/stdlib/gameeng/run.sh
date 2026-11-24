#!/bin/sh
# stdlib/gameeng.hp suite: real headless game-engine app (apps/gameeng).
# Prints: TEST_GAMEENG <cases> <pass> <fail>.  Exit 0 == pass.
set -u
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$D/../../.." && pwd)
cd "$ROOT" || exit 2
pass=0; fail=0; cases=0
cases=$((cases + 1))
timeout 900 sh apps/gameeng/run.sh >/dev/null 2>&1 && pass=$((pass + 1)) || fail=$((fail + 1))
echo "TEST_GAMEENG $cases $pass $fail"
[ "$fail" -eq 0 ]

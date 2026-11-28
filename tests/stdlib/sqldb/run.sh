#!/bin/sh
# stdlib/sqldb.hp suite: real SQL-like database app (apps/sqldb).
# Prints: TEST_SQLDB <cases> <pass> <fail>.  Exit 0 == pass.
set -u
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$D/../../.." && pwd)
cd "$ROOT" || exit 2
pass=0; fail=0; cases=0
cases=$((cases + 1))
timeout 900 sh apps/sqldb/run.sh >/dev/null 2>&1 && pass=$((pass + 1)) || fail=$((fail + 1))
echo "TEST_SQLDB $cases $pass $fail"
[ "$fail" -eq 0 ]

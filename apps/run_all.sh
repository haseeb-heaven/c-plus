#!/bin/sh
# Aggregate verifier for the real apps (stdlib/web.hp, stdlib/sqldb.hp,
# stdlib/gameeng.hp).  Runs each app's run.sh.
# Prints: APPS <cases> <pass> <fail>.  Exit 0 == all pass.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 2
cases=0; pass=0; fail=0
for a in webserver sqldb gameeng; do
    cases=$((cases + 1))
    if timeout 900 sh "apps/$a/run.sh" >/dev/null 2>&1; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
done
echo "APPS $cases $pass $fail"
[ "$fail" -eq 0 ]

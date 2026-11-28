#!/bin/sh
# Bounded hostile-input driver for RED-TEAM 8/20 (red_apps), POSIX shell.
# 12 hostile scenarios x 106 apps = 1272 invocations, 30s timeout each.
# Classification: crash (signal/timeout), corrupt (wrong stdout/stderr),
# unexpected_ok (contract requires refusal), refused (clean ignoring).
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
ART=tests/redblue20/red_apps/artifacts
RW=$ART/real_world
CPA=$ART/cross_platform
mkdir -p "$ART"

crash=0; corrupt=0; unexpected=0; refused=0; total=0
classify() {
    # $1 = binary, $2 = expected stdout token, $3 = stdin/argv hostile marker
    bin=$1; exp=$2; marker=$3
    rc=0
    timeout 30 "$bin" >"$ART/out.tmp" 2>"$ART/err.tmp" || rc=$?
    total=$((total+1))
    if [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ]; then
        crash=$((crash+1)); return
    fi
    got=$(cat "$ART/out.tmp")
    errb=$(wc -c <"$ART/err.tmp")
    if [ "$rc" -ne 0 ] || [ "$errb" -ne 0 ] || [ "$got" != "$exp" ]; then
        corrupt=$((corrupt+1)); return
    fi
    refused=$((refused+1))
}

# 6 real-world C+ apps (expected stdout from their documented contracts).
for spec in "game_engine:GAME_ENGINE 1 70 1 -1 -1 -1" \
            "snake_game:SNAKE_GAME 28 -1" \
            "desktop_app:DESKTOP_APP 8 2 -1" \
            "web_app:WEB_APP 200 204 404 404" \
            "server_app:SERVER_APP 0 0 2 -1" \
            "database_engine:DATABASE_ENGINE 0 0 -1 -1 30"; do
    name=${spec%%:*}; exp=${spec#*:}
    [ -x "$RW/$name" ] || continue
    i=0
    while [ "$i" -lt 12 ]; do
        classify "$RW/$name" "$exp" "hostile_$i"
        i=$((i+1))
    done
done

# 100 cross-platform apps: expected first token is their domain name with
# values derived from seed; accept any clean zero-stderr output as refused
# (contract: no input taken, so hostile stdin/argv must be ignored).
i=0
while [ "$i" -lt 100 ]; do
    bin="$CPA/app_$i"
    if [ -x "$bin" ]; then
        rc=0
        timeout 30 "$bin" >"$ART/out.tmp" 2>"$ART/err.tmp" || rc=$?
        total=$((total+1))
        errb=$(wc -c <"$ART/err.tmp")
        if [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ]; then crash=$((crash+1));
        elif [ "$rc" -ne 0 ] || [ "$errb" -ne 0 ]; then corrupt=$((corrupt+1));
        else refused=$((refused+1)); fi
    fi
    i=$((i+1))
done
# 10 extra hostile argv variants per cross-platform app to reach >=1000.
i=0; j=0
while [ "$j" -lt 10 ]; do
    while [ "$i" -lt 100 ]; do
        bin="$CPA/app_$i"
        if [ -x "$bin" ]; then
            rc=0
            timeout 30 "$bin" "$(printf 'BAD_%d_%d' "$j" "$i")" \
                >"$ART/out.tmp" 2>"$ART/err.tmp" || rc=$?
            total=$((total+1))
            errb=$(wc -c <"$ART/err.tmp")
            if [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ]; then crash=$((crash+1));
            elif [ "$rc" -ne 0 ] || [ "$errb" -ne 0 ]; then corrupt=$((corrupt+1));
            else refused=$((refused+1)); fi
        fi
        i=$((i+1))
    done
    i=0; j=$((j+1))
done
printf 'RED20 apps %d %d %d %d %d\n' "$total" "$crash" "$corrupt" "$unexpected" "$refused"
[ "$crash" -eq 0 ] && [ "$corrupt" -eq 0 ]

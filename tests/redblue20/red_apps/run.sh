#!/bin/sh
# RED-TEAM agent 8/20 - red_apps
# Build every target app fresh into our own artifacts dir, then run a bounded
# hostile-input fuzz driver (>=1000 invocations, timeout 30s each).
# STRICT OWNERSHIP: this script only writes under tests/redblue20/red_apps/.
set -u
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
cd "$ROOT" || exit 2
export PATH="$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH"
unset SDKROOT || true

RED="$ROOT/tests/redblue20/red_apps"
ART="$RED/artifacts"
RW="$ART/real_world"
CPA="$ART/cross_platform"
mkdir -p "$RW" "$CPA"

build_fail=0

# --- Build the 6 real-world C+ examples with the project compiler ----------
for n in game_engine snake_game desktop_app web_app server_app database_engine; do
    if timeout 90 ./build/c+ build "apps/legacy_examples/real_world/$n.cp" --default \
        --sanitize=none -I. -o "$RW/$n" >"$RW/$n.build.log" 2>&1; then
        :
    else
        echo "BUILDFAIL real_world/$n"
        build_fail=$((build_fail + 1))
    fi
done

# --- Build the 100 cross-platform C apps natively (Apple clang) ------------
i=0
while [ "$i" -lt 100 ]; do
    if timeout 60 /usr/bin/clang -O2 -Wall -Wextra -pedantic \
        "tests/cross_platform_apps/src/app_$i.c" -o "$CPA/app_$i" \
        >"$CPA/app_$i.build.log" 2>&1; then
        :
    else
        echo "BUILDFAIL cross_platform/app_$i"
        build_fail=$((build_fail + 1))
    fi
    i=$((i + 1))
done

echo "BUILD_FAILURES=$build_fail"
[ "$build_fail" -eq 0 ] || exit 3

exec sh "$RED/fuzz.sh" "$ROOT"

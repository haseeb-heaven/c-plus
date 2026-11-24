#!/bin/sh
# Build and verify both CTL/C+ examples.  No Python, no network, no game data.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export PATH
unset SDKROOT CPLUS_CLANG

CPLUS=${CPLUS_BIN:-./build/c+}
OUT=$(mktemp -d "${TMPDIR:-/tmp}/ctl_demo.XXXXXX")
trap 'rm -rf "$OUT"' 0 1 2 3 15

pass=0
fail=0
cases=2

check_case() {
    name=$1
    src="examples/ctl_demo/$name.cp"
    expected="examples/ctl_demo/expected/$name.txt"
    echo "== $name =="

    if timeout 180 "$CPLUS" build "$src" --default --sanitize=none \
        -I. -Ithird_party/ctl -o "$OUT/$name" \
        -- build/libc/libcplus_hardened.a >"$OUT/$name.build.log" 2>&1; then

        if timeout 30 "$OUT/$name" >"$OUT/$name.out" 2>"$OUT/$name.err"; then
            if diff -u "$expected" "$OUT/$name.out" >"$OUT/$name.diff" 2>&1; then
                echo "ok $name"
                pass=$((pass + 1))
                return 0
            fi
            echo "FAIL $name output"
            cat "$OUT/$name.diff"
        else
            rc=$?
            echo "FAIL $name run (rc=$rc)"
            cat "$OUT/$name.err"
        fi
    else
        rc=$?
        echo "FAIL $name build (rc=$rc)"
        tail -20 "$OUT/$name.build.log"
    fi
    fail=$((fail + 1))
    return 1
}

check_case ctl_collections
check_case ctl_goose_hybrid

printf 'CTL_DEMO %d %d %d\n' "$cases" "$pass" "$fail"
[ "$fail" -eq 0 ]

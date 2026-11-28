#!/bin/sh
# Build and run the five offline network/IO validators with fixed inputs.
# Prints: UTIL_NET <tools> <passed> <failed> <refused>
#   tools   = number of utilities exercised (always 5)
#   passed  = utilities that built, ran cleanly and matched their expected summary
#   failed  = utilities that did not
#   refused = total REFUSED cases observed across all five summaries
#
# Every case is fixed; no clock, no network, no user path. Each utility is run
# twice under `timeout 30` and the two stdout streams must be byte-identical.
# Run from anywhere: paths are derived from this script's location.
set -u

NET_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$NET_DIR/../../../.." && pwd)
RES="$NET_DIR/results"
C="$ROOT/build/c+"

cd "$ROOT" || exit 1
mkdir -p "$RES"

tools=0
passed=0
failed=0
refused_total=0

# run_tool <name> <expected-summary-line> [fixed argv...]
run_tool() {
    name=$1
    expected=$2
    shift 2
    tools=$((tools + 1))

    echo "=== BUILD $name ==="
    if ! "$C" build "$NET_DIR/$name.cp" --strict --sanitize=none -I. \
            -o "$RES/$name" >"$RES/$name.build.log" 2>&1; then
        echo "BUILD_FAIL $name"
        cat "$RES/$name.build.log"
        failed=$((failed + 1))
        return
    fi

    echo "=== RUN $name ==="
    if timeout 30 "$RES/$name" "$@" >"$RES/$name.out" 2>"$RES/$name.err"; then
        rc=0
    else
        rc=$?
    fi
    cat "$RES/$name.out"

    ok=1
    if [ "$rc" -ne 0 ]; then
        echo "FAIL $name exit=$rc"
        ok=0
    fi
    if [ -s "$RES/$name.err" ]; then
        echo "FAIL $name stderr not empty"
        cat "$RES/$name.err"
        ok=0
    fi
    if ! grep -qxF "$expected" "$RES/$name.out"; then
        echo "FAIL $name summary line missing: $expected"
        ok=0
    fi

    # Determinism: a second run with the same fixed argv must be byte-identical.
    if timeout 30 "$RES/$name" "$@" >"$RES/$name.out2" 2>"$RES/$name.err2"; then
        :
    else
        echo "FAIL $name second run exit != 0"
        ok=0
    fi
    if ! cmp -s "$RES/$name.out" "$RES/$name.out2"; then
        echo "FAIL $name nondeterministic stdout"
        ok=0
    fi
    if [ -s "$RES/$name.err2" ]; then
        echo "FAIL $name second-run stderr not empty"
        cat "$RES/$name.err2"
        ok=0
    fi

    if [ "$ok" -eq 1 ]; then
        passed=$((passed + 1))
        # Summary layout: <PREFIX>_SUMMARY <checks> <valid> <refused> <failed>
        r=$(printf '%s\n' "$expected" | awk '{print $4}')
        refused_total=$((refused_total + r))
    else
        failed=$((failed + 1))
    fi
}

run_tool urlcheck 'URL_SUMMARY 15 4 11 0' \
    https://good.example:443/x ftp://bad/
run_tool emailcheck 'EM_SUMMARY 16 4 12 0' \
    good@site.org bad@@x.com
run_tool ipcheck 'IP_SUMMARY 15 4 11 0' \
    10.0.0.1 256.1.1.1
run_tool portcheck 'PORT_SUMMARY 14 4 10 0' \
    80 65536
run_tool httpline 'HTTP_SUMMARY 15 4 11 0' \
    'GET /a HTTP/1.1' 'BREW /x HTTP/1.1'

echo "UTIL_NET $tools $passed $failed $refused_total"
if [ "$failed" -ne 0 ]; then
    exit 1
fi
exit 0

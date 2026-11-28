#!/bin/sh
# Build and run all three in-memory database teaching examples with fixed
# inputs, then print: DB_EXAMPLES <apps> <passed> <failed>
#
# Run from anywhere; all paths are derived from this script's location.
set -u

DB_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$DB_DIR/../../../.." && pwd)
RES="$DB_DIR/results"
C="$ROOT/build/c+"

cd "$ROOT" || exit 1
mkdir -p "$RES"

apps=0
passed=0
failed=0

# run_case <name> <expected-summary-line> [fixed argv...]
run_case() {
    name=$1
    expected=$2
    shift 2
    apps=$((apps + 1))

    echo "=== BUILD $name ==="
    if ! "$C" build "$DB_DIR/$name.cp" --strict --sanitize=none -I. \
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

    # Determinism: a second run with the same input must be byte-identical.
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
        ok=0
    fi

    if [ "$ok" -eq 1 ]; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
}

run_case kv_store 'KV_SUMMARY 9 7 2 0' \
    insert alpha one insert beta two insert alpha duplicate \
    get alpha get gamma update alpha ALPHA delete beta list expect_size 1

run_case table_query 'TQ_SUMMARY 17 15 2 0' \
    row 1 10 100 row 2 10 200 row 3 20 300 row 4 20 400 row 2 10 999 \
    eq grp 10 range amt 200 400 count grp 10 sum grp 10 \
    expect_count grp 10 2 expect_sum grp 10 300 \
    row 5 30 2147483647 row 6 30 1 range grp 30 30 sum grp 30 \
    expect_sum_refusal grp 30 101 check 6

run_case safety_demo 'SAFETY_SUMMARY 5 5 5 0'

echo "DB_EXAMPLES $apps $passed $failed"
if [ "$failed" -ne 0 ]; then
    exit 1
fi
exit 0

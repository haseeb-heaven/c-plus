#!/bin/sh
# Build and verify the deterministic Snake core + CLI driver.
# All paths are relative to this script; builds run with cwd = repository root.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../../.." && pwd)
cd "$ROOT" || exit 1

OUTDIR="$HERE/.build"
mkdir -p "$OUTDIR" || exit 1
CORE="$OUTDIR/snake_core"
CLI="$OUTDIR/snake_cli"

passed=0
failed=0

echo "== build =="
if ./build/c+ build apps/legacy_examples/expanded/games/snake/snake_core.cp --strict --sanitize=none -I. -o apps/legacy_examples/expanded/games/snake/.build/snake_core; then
    echo "build snake_core OK"
else
    echo "build snake_core FAIL"
    failed=$((failed + 1))
fi
if ./build/c+ build apps/legacy_examples/expanded/games/snake/snake_cli.cp --strict --sanitize=none -I. -o apps/legacy_examples/expanded/games/snake/.build/snake_cli; then
    echo "build snake_cli OK"
else
    echo "build snake_cli FAIL"
    failed=$((failed + 1))
fi

echo "== core self-test =="
if timeout 30 "$CORE" > "$OUTDIR/self_test.out" 2>&1; then
    echo "self-test OK: $(cat "$OUTDIR/self_test.out")"
else
    echo "self-test FAIL"
    cat "$OUTDIR/self_test.out"
    failed=$((failed + 1))
fi

run_case() {
    name=$1
    shift
    output=$(timeout 30 "$CLI" "$name" "$@" 2>&1)
    rc=$?
    summary=$(printf '%s\n' "$output" | grep '^SCENARIO ' | tail -1)
    if [ "$rc" -eq 0 ] && printf '%s\n' "$output" | grep -q "^SCENARIO $name .* ok 1$"; then
        echo "CASE $name PASS $summary"
        passed=$((passed + 1))
    else
        echo "CASE $name FAIL rc=$rc $summary"
        printf '%s\n' "$output" | tail -8
        failed=$((failed + 1))
    fi
}

echo "== scenarios =="
run_case growth R
run_case wall RRRRRRRRRR
run_case self RULD

echo "SNAKE_EXAMPLES 3 $passed $failed"
[ "$failed" -eq 0 ] || exit 1
exit 0

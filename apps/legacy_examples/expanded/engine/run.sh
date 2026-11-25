#!/bin/sh
# Build and run the three deterministic engine examples from the repository
# root, then byte-compare each transcript against expected/*.out.
#
#   sh apps/legacy_examples/expanded/engine/run.sh
#
# Final line: ENGINE_EXAMPLES <apps> <passed> <failed>
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd) || exit 1
HERE="$ROOT/apps/legacy_examples/expanded/engine"
BUILD_DIR="$HERE/.build"
CPLUS="$ROOT/build/c+"

mkdir -p "$BUILD_DIR"
cd "$ROOT" || exit 1

passed=0
failed=0
apps=3

run_case() {
    name=$1
    shift
    log="$BUILD_DIR/$name.build.log"
    out="$BUILD_DIR/$name.out"
    err="$BUILD_DIR/$name.err"

    if ! "$CPLUS" build "$HERE/$name.cp" --strict --sanitize=none \
        -I. -o "$BUILD_DIR/$name" > "$log" 2>&1; then
        echo "FAIL $name build (see $log)"
        failed=$((failed + 1))
        return
    fi
    if ! timeout 30 "$BUILD_DIR/$name" "$@" > "$out" 2> "$err"; then
        echo "FAIL $name run (see $err)"
        failed=$((failed + 1))
        return
    fi
    if ! cmp -s "$out" "$HERE/expected/$name.out"; then
        echo "FAIL $name transcript mismatch (see $out)"
        failed=$((failed + 1))
        return
    fi
    echo "PASS $name"
    passed=$((passed + 1))
}

run_case ecs
run_case engine_loop N N N N N N F F F F N N
run_case resource_cache

echo "ENGINE_EXAMPLES $apps $passed $failed"
[ "$failed" -eq 0 ]

#!/bin/sh
# Build and run the native C+ API-coverage tool (no Python in the path).
# This replaces the former tests/stdlib/coverage.py invocation: the C+ tool
# reports the same "all libraries >= N% API coverage" result and uses the
# same exit codes (0 pass, 1 below threshold, 2 setup error).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"

PATH="$ROOT/build:$PATH"
export PATH

CPLUS=${CPLUS_BIN:-c+}
OUT=build/native-tools/coverage
mkdir -p "$(dirname "$OUT")"

if ! "$CPLUS" build tests/stdlib/coverage.cp --strict --sanitize=none \
        -o "$OUT" >"$OUT.build.log" 2>&1; then
    echo "error: native coverage tool build failed; see $OUT.build.log" >&2
    cat "$OUT.build.log" >&2
    exit 1
fi

# Maintain 100% API coverage: build and run the closure test that directly
# invokes every API the static scanner cannot see a call-site for.  A failure
# here is a real coverage regression, not a soft warning.
CLOSURE=build/native-tools/coverage_closure
# The closure only calls json_close_stream(), so yyjson's header is needed but
# its object library is not linked: keep this script runnable without a prior
# tests/stdlib/run_stdlib_tests.sh build of build/ext.
if ! "$CPLUS" build tests/stdlib/coverage_closure.cp --default --no-werror --sanitize=none \
        -I. -Ithird_party/yyjson -o "$CLOSURE" \
        >"$CLOSURE.build.log" 2>&1; then
    echo "error: coverage-closure test build failed; see $CLOSURE.build.log" >&2
    cat "$CLOSURE.build.log" >&2
    exit 1
fi
if ! "$CLOSURE" >"$CLOSURE.out" 2>"$CLOSURE.err"; then
    echo "error: coverage-closure test failed; see $CLOSURE.out" >&2
    cat "$CLOSURE.out" >&2
    exit 1
fi

exec "$OUT" "$ROOT"

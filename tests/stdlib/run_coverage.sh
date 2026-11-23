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

exec "$OUT" "$ROOT"

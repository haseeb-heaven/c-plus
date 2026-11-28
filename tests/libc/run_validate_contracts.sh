#!/bin/sh
# Build and run the native C+ contract validator (no Python in the path).
#
# Resolves the repository root from this script's own location, so it works
# from the root or any subdirectory. Exports a PATH that includes build/ so
# the C+ driver is found by name, then builds with the supported strict,
# unsanitised configuration and runs the resulting executable from the root.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"

PATH="$ROOT/build:$PATH"
export PATH

CPLUS=${CPLUS_BIN:-c+}
OUT=build/native-tools/validate_contracts
mkdir -p "$(dirname "$OUT")"

if ! "$CPLUS" build tests/libc/validate_contracts.cp --strict --sanitize=none \
        -Ithird_party/yyjson -Lbuild/ext -lyyjson -o "$OUT" \
        >"$OUT.build.log" 2>&1; then
    echo "error: native contract validator build failed; see $OUT.build.log" >&2
    cat "$OUT.build.log" >&2
    exit 1
fi

exec "$OUT"

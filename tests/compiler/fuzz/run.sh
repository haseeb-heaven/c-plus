#!/bin/sh
# Convenience wrapper. All knobs are environment variables (see README.md).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
: "${CPLUS_BIN:=$ROOT/build/c+}"
export CPLUS_BIN
exec python3 "$HERE/fuzz.py" "$@"

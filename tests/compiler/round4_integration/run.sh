#!/bin/sh
# Thin wrapper for run.py -- run from anywhere, no build/commit/merge side effects.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$HERE/run.py" "$@"

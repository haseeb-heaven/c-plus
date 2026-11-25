#!/bin/sh
# Dedicated runner for the 100-configuration fuzz corpus.
#
#   sh tests/fuzz100/run.sh [--config F001] [--dry-run]
#
# All discovery, building, running and reporting lives in run.py.  This wrapper
# only fixes the repository root, which works when invoked from any directory.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
F100_ROOT="$ROOT"
export F100_ROOT
sh "$ROOT/tests/native/build_native_tools.sh" >/dev/null
exec "$ROOT/build/native-tools/fuzz100_run" --root "$ROOT" "$@"

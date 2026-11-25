#!/bin/sh
# Compatibility shim: the verified games3d app now lives in apps/games3d/.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec sh "$ROOT/apps/games3d/run.sh" "$@"

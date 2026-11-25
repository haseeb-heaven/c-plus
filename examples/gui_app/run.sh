#!/bin/sh
# Compatibility shim: the verified gui_app app now lives in apps/gui_app/.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec sh "$ROOT/apps/gui_app/run.sh" "$@"

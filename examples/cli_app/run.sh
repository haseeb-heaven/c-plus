#!/bin/sh
# Compatibility shim: the verified cli_app app now lives in apps/cli_app/.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec sh "$ROOT/apps/cli_app/run.sh" "$@"

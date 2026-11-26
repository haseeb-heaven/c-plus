#!/bin/sh
# Compatibility shim: canonical sample ports live in examples/modern_c_ports.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec sh "$ROOT/examples/modern_c_ports/run.sh" "$@"

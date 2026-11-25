#!/bin/sh
# Dedicated adversarial/fuzz stdlib regression runner.
#
# Usage (from anywhere):
#   sh tests/stdlib/run_adversarial.sh [--suite all|deep|agent2|regressions|fuzz]
#                                      [--mode strict-none,default,ubsan,asan,debug,release]
#                                      [--only NAME] [--fast]
#
# Builds every discovered test independently, applies timeouts, supplies stdin
# fixtures, checks exit codes and byte-exact stdout/stderr fixtures, preserves
# logs under tests/stdlib/reports/logs/, and writes adversarial.json/.txt.
# No failure-suppressing shell no-op is used anywhere in this runner.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
sh "$ROOT/tests/native/build_native_tools.sh" >/dev/null
exec "$ROOT/build/native-tools/run_adversarial" "$ROOT" "$@"

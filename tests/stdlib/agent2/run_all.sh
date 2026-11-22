#!/bin/sh
# Agent 2 aggregate regression runner.
#
# Usage (from the repository root):
#   sh tests/stdlib/agent2/run_all.sh [options]
#
# Options:
#   --test NAME        run exactly one test by base name
#   --module NAME      run one module group (deep-test name prefix family)
#   --clean            clear result files before running
#   --keep-logs        keep the per-run work directory (default: keep result logs)
#   --timeout N        runtime timeout in seconds (default 30)
#   --build-timeout N  build timeout in seconds (default 90)
#   --help             print this help
#
# Discovers tests/stdlib/deep_*.cp, tests/stdlib/agent2/*.cp and the curated
# existing tests in tests/stdlib/agent2/existing_tests.txt. Every test is built
# with --strict --sanitize=none, executed with its stdin fixture under a
# timeout, and classified as PASS / FAIL / BUILD_FAILURE / TIMEOUT / SKIPPED /
# BLOCKED. Results and all logs are written to tests/stdlib/agent2/results/.
# The runner never uses `|| true` and returns non-zero if any test is not PASS.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd) || exit 1
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
RESULTS=${AGENT2_RESULTS:-"$ROOT/tests/stdlib/agent2/results"}
TEST=""; MODULE=""; CLEAN=0; KEEP=0; TIMEOUT=30; BUILD_TIMEOUT=90

while [ "$#" -gt 0 ]; do
  case "$1" in
    --test) TEST=${2:-}; shift 2 ;;
    --module) MODULE=${2:-}; shift 2 ;;
    --clean) CLEAN=1; shift ;;
    --keep-logs) KEEP=1; shift ;;
    --timeout) TIMEOUT=${2:-30}; shift 2 ;;
    --build-timeout) BUILD_TIMEOUT=${2:-90}; shift 2 ;;
    --help)
      sed -n '2,20p' "$0"
      exit 0 ;;
    *) echo "error: unknown option: $1" >&2; exit 2 ;;
  esac
done

mkdir -p "$RESULTS" || exit 1
exec python3 "$ROOT/tests/stdlib/agent2/run_all.py" \
  "$ROOT" "$RESULTS" "$CPLUS" "$BUILD_TIMEOUT" "$TIMEOUT" \
  "$TEST" "$MODULE" "$CLEAN" "$KEEP"

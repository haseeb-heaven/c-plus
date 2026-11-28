#!/bin/sh
# Native C+ aggregate regression runner (replacement for run_all.sh + run_all.py).
#
# Usage (from the repository root):
#   sh tests/stdlib/agent2/run_all_native.sh [options]
#
# Options:
#   --test NAME        run exactly one test by base name
#   --module NAME      run one module group (deep-test name family)
#   --clean            clear result files before running
#   --keep-logs        accepted for parity; logs are always kept
#   --timeout N        runtime timeout in seconds (default 30)
#   --build-timeout N  build timeout in seconds (default 90)
#   --help             print this help
#
# Builds tests/stdlib/agent2/run_all.cp with --strict --sanitize=none and the
# hardened libc archive (cplus_system lives there), then executes it with the
# same nine positional arguments the Python runner received.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd) || exit 1
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
RESULTS=${AGENT2_RESULTS:-"$ROOT/tests/stdlib/agent2/results"}
SRC="$ROOT/tests/stdlib/agent2/run_all.cp"
BIN="$ROOT/build/agent2/run_all_native"
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
      sed -n '2,18p' "$0"
      exit 0 ;;
    *) echo "error: unknown option: $1" >&2; exit 2 ;;
  esac
done

if [ ! -x "$CPLUS" ]; then
  echo "BLOCKED: compiler missing at $CPLUS" >&2
  exit 1
fi

mkdir -p "$ROOT/build/agent2" "$RESULTS" || exit 1
if [ ! -x "$BIN" ] || [ "$SRC" -nt "$BIN" ]; then
  "$CPLUS" build "$SRC" --strict --sanitize=none \
    -o "$BIN" -- build/libc/libcplus_hardened.a || exit 1
fi

# Match the environment the Python runner forced on its subprocesses.
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export PATH
if [ -z "${CPLUS_CLANG:-}" ]; then
  CPLUS_CLANG=/usr/bin/clang
  export CPLUS_CLANG
fi

exec "$BIN" "$ROOT" "$RESULTS" "$CPLUS" "$BUILD_TIMEOUT" "$TIMEOUT" \
  "$TEST" "$MODULE" "$CLEAN" "$KEEP"

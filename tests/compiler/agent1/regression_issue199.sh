#!/bin/sh
# STRICT must reject a hostile extern that merely looks like a runtime helper.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
SRC="$ROOT/tests/compiler/agent1/issue199_hostile_extern.c"
OUT="$ROOT/build/issue199_hostile_extern.log"
if ! "$CPLUS" check --strict "$SRC" >"$OUT" 2>&1; then
  if grep -qF "[CPLUS-FFI] function 'cplus_check_backdoor' has safety-relevant pointer effects" "$OUT"; then
    echo "issue #199 STRICT extern regression: PASS"; exit 0
  fi
fi
echo "issue #199 STRICT extern regression: FAIL"; cat "$OUT" 2>/dev/null; exit 1

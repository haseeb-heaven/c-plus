#!/bin/sh
# STRICT must classify variadic / aggregate-with-pointer externs and reserved
# helpers declared in a user (non-system) header as unverified.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
D="$ROOT/tests/compiler/agent1"
OUT="$ROOT/build/issue227_strict.log"

expect_fail() {
  src=$1; token=$2
  if "$CPLUS" check --strict "$src" >"$OUT" 2>&1; then
    echo "issue #227 STRICT regression: FAIL ($src accepted)"; cat "$OUT"; exit 1
  fi
  if ! grep -qF "$token" "$OUT"; then
    echo "issue #227 STRICT regression: FAIL ($src missing diagnostic)"; cat "$OUT"; exit 1
  fi
}

expect_fail "$D/issue227_variadic.c" "[CPLUS-FFI] function 'ext_variadic'"
expect_fail "$D/issue227_aggregate.c" "[CPLUS-FFI] function 'ext_struct'"

# A scalar-only extern remains accepted in STRICT (no regression to pure C ABI).
if ! "$CPLUS" check --strict "$D/issue227_scalar.c" >"$OUT" 2>&1; then
  echo "issue #227 STRICT regression: FAIL (scalar extern rejected)"; cat "$OUT"; exit 1
fi

# A reserved helper declared in the main file is still an interposition.
if "$CPLUS" check --strict "$D/issue227_helper_main.c" >"$OUT" 2>&1; then
  echo "issue #227 STRICT regression: FAIL (main-file helper accepted)"; cat "$OUT"; exit 1
fi

echo "issue #227 STRICT regression: PASS"
exit 0

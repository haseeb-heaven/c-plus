#!/bin/sh
# Verify the explicit DEFAULT vs STRICT safety modes.
#   DEFAULT: proven errors are rejected; unverified operations are warnings.
#   STRICT:  unverified operations are rejected (no silent warning downgrade).
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
FIX="$ROOT/tests/core/modes"
OUT="$ROOT/tests/out/core/modes"
mkdir -p "$OUT"
PASS=0; FAIL=0
ok() { echo "  ok   $1"; PASS=$((PASS+1)); }
bad() { echo "  FAIL $1: $2"; FAIL=$((FAIL+1)); }

# Proven unsafe regardless of mode.
expect_reject() {
  name=$1; mode=$2; src=$3
  if "$CPLUS" check "$mode" "$src" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"; then
    bad "$name" "unsafe program accepted in $mode"
  else
    ok "$name"
  fi
}
# Verified-safe program must compile in both modes.
expect_accept() {
  name=$1; mode=$2; src=$3
  if "$CPLUS" check "$mode" "$src" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"; then
    ok "$name"
  else
    bad "$name" "safe program rejected in $mode"
  fi
}
# Unverified: warning in DEFAULT, error in STRICT.
expect_warn_then_reject() {
  name=$1; src=$2
  if ! "$CPLUS" check --default "$src" >"$OUT/$name.d.stdout" 2>"$OUT/$name.d.stderr"; then
    bad "$name-default" "unverified program rejected in DEFAULT"; return
  fi
  if ! grep -q "warning:" "$OUT/$name.d.stderr"; then
    bad "$name-default" "expected a warning in DEFAULT"; return
  fi
  ok "$name-default"
  if "$CPLUS" check --strict "$src" >"$OUT/$name.s.stdout" 2>"$OUT/$name.s.stderr"; then
    bad "$name-strict" "unverified program accepted in STRICT"; return
  fi
  if ! grep -q "error:" "$OUT/$name.s.stderr"; then
    bad "$name-strict" "expected an error in STRICT"; return
  fi
  ok "$name-strict"
}

expect_reject proven_uaf_default --default "$FIX/uaf.c"
expect_reject proven_uaf_strict  --strict  "$FIX/uaf.c"
expect_reject proven_double_free_default --default "$FIX/double_free.c"
expect_reject proven_double_free_strict  --strict  "$FIX/double_free.c"
expect_accept safe_malloc_default --default "$FIX/safe_malloc.c"
expect_accept safe_malloc_strict  --strict  "$FIX/safe_malloc.c"
expect_accept safe_realloc_strict --strict "$FIX/safe_realloc.c"
expect_warn_then_reject unverified_realloc "$FIX/unverified_realloc.c"
expect_warn_then_reject unverified_thread  "$FIX/unverified_thread.c"
expect_warn_then_reject branch_uaf "$ROOT/tests/safety/branch_uaf.c"
expect_warn_then_reject thread_volatile_race "$ROOT/tests/safety/thread_volatile_race.c"

# UAF diagnostic must name the pointer and show the source-location chain.
"$CPLUS" check --default "$FIX/uaf_chain.c" >"$OUT/chain.stdout" 2>"$OUT/chain.stderr" || true
for needle in "dereference of released pointer 'q'" "note: allocation:" \
              "note: alias created:" "note: owner released:" \
              "suggested correction:"; do
  if grep -q "$needle" "$OUT/chain.stderr"; then
    ok "diag-chain $needle"
  else
    bad "diag-chain" "missing: $needle"
  fi
done

echo ""
echo "strict/default modes: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]

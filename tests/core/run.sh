#!/bin/sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
OUT="$ROOT/tests/out/core"
mkdir -p "$OUT"
PASS=0; FAIL=0

ok() { echo "  ok   $1"; PASS=$((PASS+1)); }
bad() { echo "  FAIL $1: $2"; FAIL=$((FAIL+1)); }

build_run() {
  name=$1; src=$2
  if ! "$CPLUS" build "$src" -o "$OUT/$name" >"$OUT/$name.log" 2>&1; then
    bad "$name" "build failed"; return
  fi
  if ! "$OUT/$name" >"$OUT/$name.stdout" 2>>"$OUT/$name.log"; then
    bad "$name" "runtime failed"; return
  fi
  ok "$name"
}

build_expect_stderr() {
  name=$1; src=$2; needle=$3
  if ! "$CPLUS" build "$src" -o "$OUT/$name" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"; then
    bad "$name" "build failed"; return
  fi
  if ! grep -q "$needle" "$OUT/$name.stderr"; then
    bad "$name" "missing warning: $needle"; return
  fi
  ok "$name"
}

expect_reject() {
  name=$1; src=$2
  if "$CPLUS" check "$src" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"; then
    bad "$name" "unsafe program accepted"
  else
    ok "$name"
  fi
}

expect_trap() {
  name=$1; src=$2; code=$3
  if ! "$CPLUS" build "$src" -o "$OUT/$name" >"$OUT/$name.log" 2>&1; then
    bad "$name" "build failed"; return
  fi
  "$OUT/$name" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"
  rc=$?
  if [ "$rc" -eq "$code" ]; then ok "$name"; else bad "$name" "exit $rc != $code"; fi
}

echo "core safety: positive"
build_run raii_basic "$ROOT/tests/core/positive/raii_basic.cp"
build_run move_and_borrow "$ROOT/tests/core/positive/move_and_borrow.cp"
build_run libc_interop "$ROOT/tests/core/positive/libc_interop.c"
build_expect_stderr longjmp_warning "$ROOT/tests/safety/classic_bug34_longjmp.c" "bypasses C+ deterministic cleanup"

if "$CPLUS" build "$ROOT/tests/core/positive/multi_tu_main.c" \
   "$ROOT/tests/core/positive/multi_tu_helper.c" -o "$OUT/multi_tu" \
   >"$OUT/multi_tu.log" 2>&1 && [ "$("$OUT/multi_tu")" = "40" ]; then
  ok multi_tu
else
  bad multi_tu "build/link/run failed"
fi

if "$CPLUS" build "$ROOT/tests/safety/release_mode.cp" --release -o "$OUT/release_mode" \
   >"$OUT/release_mode.log" 2>&1 && "$OUT/release_mode"; then
  ok release_mode
else
  bad release_mode "release build/run failed"
fi

echo "core safety: ownership warnings"
build_expect_stderr moved_from_warning "$ROOT/tests/safety/moved_from_warning.cp" "use of moved-from"
build_expect_stderr double_move_warning "$ROOT/tests/safety/double_move_warning.cp" "moved more than once"

echo "core safety: rejected"
expect_reject bounds_loop "$ROOT/tests/safety/bounds_loop.c"
expect_reject uninit "$ROOT/tests/safety/uninit.c"
expect_reject null_deref "$ROOT/tests/safety/null_deref.c"

echo "core safety: traps"
expect_trap double_release "$ROOT/tests/safety/double_release.cp" 104
expect_trap uaf_check "$ROOT/tests/safety/uaf_check.cp" 105

echo
echo "core safety: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]

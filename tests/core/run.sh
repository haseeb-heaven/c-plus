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
build_run init_all_branches "$ROOT/tests/core/positive/init_all_branches.c"
build_run pointer_one_past "$ROOT/tests/core/positive/pointer_one_past.c"
build_run explicit_drop_once "$ROOT/tests/core/positive/explicit_drop_once.cp"
build_run loop_cleanup "$ROOT/tests/core/positive/loop_cleanup.cp"
build_run recursive_resource "$ROOT/tests/core/positive/recursive_resource.cp"
build_run partial_init "$ROOT/tests/core/positive/partial_init.cp"
build_run double_explicit_drop "$ROOT/tests/core/positive/double_explicit_drop.cp"
build_run safe_memory "$ROOT/tests/core/positive/safe_memory.c"
build_expect_stderr thread_stack_borrow "$ROOT/tests/safety/thread_stack_borrow.c" "stack borrow passed to thread"
build_expect_stderr longjmp_warning "$ROOT/tests/safety/classic_bug34_longjmp.c" "bypasses C+ deterministic cleanup"

if "$CPLUS" build "$ROOT/tests/core/positive/multi_tu_main.c" \
   "$ROOT/tests/core/positive/multi_tu_helper.c" -o "$OUT/multi_tu" \
   >"$OUT/multi_tu.log" 2>&1 && [ "$("$OUT/multi_tu")" = "40" ]; then
  ok multi_tu
else
  bad multi_tu "build/link/run failed"
fi

if "$CPLUS" build "$ROOT/tests/core/positive/recursive_resource.cp" --release -o "$OUT/release_recursive" \
   >"$OUT/release_recursive.log" 2>&1 && "$OUT/release_recursive"; then
  ok release_recursive
else
  bad release_recursive "release recursive build/run failed"
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
expect_reject constructor_early_return "$ROOT/tests/safety/constructor_early_return.cp"
expect_reject bounds_loop "$ROOT/tests/safety/bounds_loop.c"
expect_reject uninit_branch "$ROOT/tests/safety/uninit_branch.c"
expect_reject pointer_arithmetic "$ROOT/tests/safety/pointer_arithmetic.c"
expect_reject uninit "$ROOT/tests/safety/uninit.c"
expect_reject null_deref "$ROOT/tests/safety/null_deref.c"

echo "core safety: traps"
expect_trap double_release "$ROOT/tests/safety/double_release.cp" 104
expect_trap uaf_check "$ROOT/tests/safety/uaf_check.cp" 105
expect_trap string_overflow "$ROOT/tests/safety/string_overflow.c" 101
expect_trap null_memcpy "$ROOT/tests/safety/null_memcpy.c" 102

if "$CPLUS" build "$ROOT/tests/core/positive/safe_memory.c" --log --log-level trace \
   -o "$OUT/safe_memory_log" >"$OUT/safe_memory_log.build" 2>&1 && \
   "$OUT/safe_memory_log" >"$OUT/safe_memory_log.stdout" 2>"$OUT/safe_memory_log.stderr" && \
   grep -q "cplus_memcpy" "$OUT/safe_memory_log.stdout"; then
  ok runtime_logging
else
  bad runtime_logging "missing runtime trace log"
fi

echo
echo "core safety: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]

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
build_run move_chain_ok "$ROOT/tests/core/positive/move_chain_ok.cp"
build_run managed_len_fold "$ROOT/tests/core/positive/managed_len_fold.c"
build_run nested_raii "$ROOT/tests/core/positive/nested_raii.cp"
build_run borrow_valid "$ROOT/tests/core/positive/borrow_valid.cp"
build_run realloc_chain "$ROOT/tests/core/positive/realloc_chain.c"
build_run zero_alloc "$ROOT/tests/core/positive/zero_alloc.c"
build_run thread_guard_ok "$ROOT/tests/core/positive/thread_guard_ok.cp"
build_run allocator_fuzz "$ROOT/tests/core/allocator_fuzz.c"
build_run checked_copy_overlap "$ROOT/tests/core/positive/checked_copy_overlap.c"
build_run null_outparam "$ROOT/tests/core/positive/null_outparam.c"
build_run libc_valid "$ROOT/tests/core/positive/libc_valid.c"
build_run interproc_borrow "$ROOT/tests/core/positive/interproc_borrow.c"
build_run expr_array_bound "$ROOT/tests/core/positive/expr_array_bound.c"
build_run uaf_conditional "$ROOT/tests/core/positive/uaf_conditional.c"
build_run borrow_field_ok "$ROOT/tests/core/positive/borrow_field_ok.c"
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

if "$CPLUS" build "$ROOT/tests/core/positive/multi_tu_resource_main.c" \
   "$ROOT/tests/core/positive/multi_tu_resource_a.c" \
   -I"$ROOT/tests/core/positive" -o "$OUT/multi_tu_resource" \
   >"$OUT/multi_tu_resource.log" 2>&1 && "$OUT/multi_tu_resource"; then
  ok multi_tu_resource
else
  bad multi_tu_resource "multi-TU resource build/run failed"
fi

if "$CPLUS" build "$ROOT/tests/core/multi_tu_lib/part_a.cp" \
   "$ROOT/tests/core/multi_tu_lib/part_b.cp" \
   -I "$ROOT/tests/core/multi_tu_lib" -o "$OUT/multi_tu_lib" \
   >"$OUT/multi_tu_lib.log" 2>&1 && "$OUT/multi_tu_lib"; then
  ok multi_tu_lib
else
  bad multi_tu_lib "multi-TU stdlib-like .hp build/link/run failed"
fi

if "$CPLUS" build "$ROOT/tests/core/multi_tu_hp/part_a.cp" \
   "$ROOT/tests/core/multi_tu_hp/part_b.cp" \
   -I "$ROOT/tests/core/multi_tu_hp" -o "$OUT/multi_tu_hp" \
   >"$OUT/multi_tu_hp.log" 2>&1 && "$OUT/multi_tu_hp"; then
  ok multi_tu_hp
else
  bad multi_tu_hp "multi-TU .hp class build/link/run failed"
fi

echo "core safety: ownership warnings"
build_expect_stderr moved_from_warning "$ROOT/tests/safety/moved_from_warning.cp" "use of moved-from"
build_expect_stderr double_move_warning "$ROOT/tests/safety/double_move_warning.cp" "moved more than once"

echo "core safety: rejected"
build_expect_stderr move_chain "$ROOT/tests/safety/move_chain.c" "use of moved-from"
expect_reject borrow_after_move "$ROOT/tests/safety/borrow_after_move.c"
expect_reject managed_escape_expr "$ROOT/tests/safety/managed_escape_expr.c"
expect_reject multi_dim_negative "$ROOT/tests/safety/multi_dim_negative.c"
expect_trap alloc_overflow_mul "$ROOT/tests/safety/alloc_overflow_mul.cp" 107
expect_trap double_release_after_realloc "$ROOT/tests/safety/double_release_after_realloc.c" 104
expect_reject alias_borrow_escape "$ROOT/tests/safety/alias_borrow_escape.c"
expect_reject thread_global_write "$ROOT/tests/safety/thread_global_write.c"
build_run alloc_bytes_wrong "$ROOT/tests/safety/alloc_bytes_wrong.c"
expect_reject constructor_early_return "$ROOT/tests/safety/constructor_early_return.cp"
expect_reject bounds_loop "$ROOT/tests/safety/bounds_loop.c"
expect_reject uninit_branch "$ROOT/tests/safety/uninit_branch.c"
expect_reject pointer_arithmetic "$ROOT/tests/safety/pointer_arithmetic.c"
expect_reject uninit "$ROOT/tests/safety/uninit.c"
expect_reject null_deref "$ROOT/tests/safety/null_deref.c"

echo "core safety: traps"
expect_reject double_release "$ROOT/tests/safety/double_release.cp"
expect_trap uaf_check "$ROOT/tests/safety/uaf_check.cp" 105
expect_trap string_overflow "$ROOT/tests/safety/string_overflow.c" 101
expect_trap null_memcpy "$ROOT/tests/safety/null_memcpy.c" 134
expect_trap null_memmove "$ROOT/tests/safety/null_memmove.c" 134
expect_trap strnlen_null "$ROOT/tests/safety/strnlen_null.c" 134
expect_reject double_release_alias "$ROOT/tests/safety/double_release_from_alias.c"
expect_trap release_stack_pointer "$ROOT/tests/safety/release_stack_pointer.c" 105

if "$CPLUS" build "$ROOT/tests/core/positive/safe_memory.c" --log --log-level trace \
   -o "$OUT/safe_memory_log" >"$OUT/safe_memory_log.build" 2>&1 && \
   "$OUT/safe_memory_log" >"$OUT/safe_memory_log.stdout" 2>"$OUT/safe_memory_log.stderr" && \
   grep -q "c+ driver start" "$OUT/safe_memory_log.build"; then
  ok runtime_logging
else
  bad runtime_logging "missing runtime trace log"
fi

if "$CPLUS" build "$ROOT/tests/core/positive/quarantine_probe.c" \
   -o "$OUT/quarantine_probe" >"$OUT/quarantine_probe.log" 2>&1; then
  CPLUS_QUARANTINE=1 "$OUT/quarantine_probe" \
    >"$OUT/quarantine_probe.stdout" 2>"$OUT/quarantine_probe.stderr"
  qrc=$?
  if [ "$qrc" -eq 105 ]; then ok quarantine_uaf; else bad quarantine_uaf "exit $qrc"; fi
else
  bad quarantine_uaf "build failed"
fi

echo
echo "core safety: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]

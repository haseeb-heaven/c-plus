# Deep thread Task 5 contract (genuine remaining gaps)

Scope: `stdlib/thread.hp` only. This contract is defined by one new
self-checking program, `tests/stdlib/deep_thread_task5.cp`. It deliberately
contains **no** case already covered by `deep_thread_guard.cp` or
`deep_thread_nested.cp` (see `docs/stdlib/DEEP_THREAD_CONTRACT.md`): guard
unlock/drop/holds, repeated unlock, nested guards, zero-init/NULL guards, the
forked ERRORCHECK double-unlock detector with positive control, the forked
deadlock watchdog with positive control, cross-TU-style release and multi-TU
class linkage are all excluded here.

Driver: `build/c+` (the binary in this checkout, built from the sibling
`c-plus-core` tree which supplies native `--strict` and `--sanitize=none`).

## 1. New cases and contract decisions

| Case | Contract exercised | Observed tags |
| --- | --- | --- |
| 1. Real worker thread | `mutex()` creates a valid handle; `thread::start(fn, &ctx, &err)` succeeds and marks the handle joinable; `join()` returns `STDLIB_OK`, clears joinability and yields the worker's `NULL` return; the worker's mutex-protected increment is visible after join; a second `join()` is a clean `STDLIB_FAIL` with `STDLIB_CATEGORY_LIFECYCLE` / `CPLUS_EXIT_RESOURCE`. | `t5_worker_valid`, `t5_worker_start`, `t5_worker_join`, `t5_worker_counter`, `t5_worker_rejoin_fails` |
| 2. `cplus_move` guard transfer | Exactly one `pthread_mutex_unlock`: the destination guard holds the owner and unlocks at scope exit; the source guard's `owner` field is zeroed by `cplus_move`, so its (also emitted) drop is inert. Checked against an ERRORCHECK mutex, so any hidden second unlock would be fatal (107). The moved-from guard's inertness is read through a `void *` captured **before** the move — referring to the moved-from name afterwards is a compile-time use-after-move error. A post-scope re-lock succeeds. | `t5_move_src_holds`, `t5_move_dst_holds`, `t5_move_src_inert`, `t5_move_relock_after` |
| 3. Guard across early return | The compiler emits the destructor on the `return` path (`{ ... guard_drop(&g); return ...; }`). The ERRORCHECK mutex also proves no duplicate drop; an ordinary re-lock after the call proves the lock was actually released. | `t5_early_return_value`, `t5_early_relock` |
| 4. 64 mutex create/drop cycles | Every cycle yields a valid mutex; no stdlib telemetry failure counter (invalid ops, lifecycle failures, allocation failures) increases; the open-fd count is unchanged. Note: `cplus_mutex_new/free` use libc `malloc/free`, not stdlib telemetry, so alloc/free counts are intentionally not compared — failure counters are the meaningful signal. Each mutex is explicitly `drop()`ped; the implicit drop is then a no-op because `handle` is set to `NULL`. | `t5_cycles_complete`, `t5_cycles_telemetry_clean`, `t5_cycles_fd_stable` |
| 5. null / zero-init recoverability | Zero-init `mutex z;` has `handle == NULL`: `is_valid()` false, `lock/unlock/native()/drop()` safe no-ops, double `drop()` inert. A guard constructed from `&z` still reports `holds() == 1` (owner pointer non-NULL — the documented edge case from the parent contract) while its unlock/drop remain no-ops. `guard(NULL)` holds nothing. A real mutex still locks/unlocks afterwards. | `t5_zero_invalid`, `t5_zero_native_null`, `t5_zero_after_drop`, `t5_zero_guard_holds`, `t5_zero_guard_released`, `t5_null_guard_holds`, `t5_null_guard_safe`, `t5_null_recover` |

### Implementation notes

- The worker copies `ctx->mtx` into a local `mutex *` before calling
  `lock()`/`unlock()`. C+ mis-rewrites a chained `ctx->mtx->lock()` into
  `ctx->mutex_lock(m)`; a single-arrow call on a local pointer lowers correctly.
  This is a compiler lowering quirk, not a `thread.hp` defect.
- `t5_to_errorcheck()` re-initialises the opaque handle's first
  `pthread_mutex_t` field with `PTHREAD_MUTEX_ERRORCHECK` — the same test-only
  white-box technique used by the parent deep thread tests. It is isolated to
  this test file.
- Every owning local has a unique name within `main` (RaiiLowering
  de-duplicates ownership by variable name — parent-contract finding F3).
- stderr is empty in every passing run. The binary is always invoked under an
  external `timeout 30`; a missing early-return drop would otherwise deadlock
  the re-lock.

## 2. Exact expected stdout

`tests/stdlib/deep_thread_task5.cp` — exit 0, empty stderr:

```
t5_worker_valid 1
t5_worker_start 1
t5_worker_join 1
t5_worker_counter 1
t5_worker_rejoin_fails 1
t5_move_src_holds 1
t5_move_dst_holds 1
t5_move_src_inert 1
t5_move_relock_after 1
t5_early_return_value 1
t5_early_relock 1
t5_cycles_complete 1
t5_cycles_telemetry_clean 1
t5_cycles_fd_stable 1
t5_zero_invalid 1
t5_zero_native_null 1
t5_zero_after_drop 1
t5_zero_guard_holds 1
t5_zero_guard_released 1
t5_null_guard_holds 1
t5_null_guard_safe 1
t5_null_recover 1
THREAD_TASK5 22 0
```

Every `t5_check` prints `<tag> <1|0>` (`1` = pass); the final line is
`THREAD_TASK5 <checks> <failures>`. Exit is `0` only when `failures == 0`, so
no `.expected` sidecar is needed.

## 3. Verification commands and results

Run from the repository root. STRICT `--sanitize=none` was run **first**, then
default and UBSan; every compiler and binary invocation is bounded by
`timeout 30`.

```sh
# 1) STRICT, sanitizer off (run first)
timeout 30 ./build/c+ build tests/stdlib/deep_thread_task5.cp \
    --strict --sanitize=none -o /tmp/deep_thread_task5_strict_none
timeout 30 /tmp/deep_thread_task5_strict_none

# 2) Default mode
timeout 30 ./build/c+ build tests/stdlib/deep_thread_task5.cp \
    -o /tmp/dtt5_default
timeout 30 /tmp/dtt5_default

# 3) UBSan (and the harness's strict+UBSan combination)
timeout 30 ./build/c+ build tests/stdlib/deep_thread_task5.cp \
    --sanitize=undefined -o /tmp/dtt5_ubsan
timeout 30 /tmp/dtt5_ubsan
timeout 30 ./build/c+ build tests/stdlib/deep_thread_task5.cp \
    --strict --sanitize=undefined -o /tmp/dtt5_strict_ubsan
timeout 30 /tmp/dtt5_strict_ubsan

# Integration (build<=60s, run<=30s per invocation)
timeout 120 sh tests/stdlib/run_deep_tests.sh deep_thread_task5
```

Results:

| Mode | Build rc | Run rc | Summary line | stdout vs strict-none |
| --- | --- | --- | --- | --- |
| `--strict --sanitize=none` (first) | 0 | 0 | `THREAD_TASK5 22 0` | baseline |
| default | 0 | 0 | `THREAD_TASK5 22 0` | identical |
| `--sanitize=undefined` | 0 | 0 | `THREAD_TASK5 22 0` | identical |
| `--strict --sanitize=undefined` | 0 | 0 | `THREAD_TASK5 22 0` | identical |
| harness default / strict-none / ubsan | 0 | 0 | `THREAD_TASK5 22 0` (x3) | identical |

Harness line: `deep stdlib tests: 6 passed, 0 failed, 0 skipped (2 file(s) x 3
modes)` when run with both Task 5 patterns; for this test alone it is
`3 passed, 0 failed, 0 skipped`.

**Totals:** 4 direct build+run invocations + 3 harness invocations = 7 passing
runs; 22 check assertions per run, 0 failures, 0 timeouts, empty stderr, byte
identical stdout in all modes.

## 4. Findings

No real defect was reproduced in `stdlib/thread.hp`. All five new cases pass
in all modes:

- `cplus_move` on a borrowed-owner `guard` transfers exactly one unlock; the
  moved-from owner field is zeroed, confirming the second emitted drop is inert.
- The compiler emits guard cleanup on an early `return`, and exactly once.
- Worker-thread lock/unlock/join, 64-cycle lifecycle and zero-init/NULL
  recoverability all behave per the header contract.

Two non-defect compiler behaviours were observed and worked around inside the
test (not patched): (a) chained `ctx->mtx->lock()` is mis-lowered, so a local
pointer is used; (b) using a moved-from name is a compile-time error, so its
inertness is checked through a pre-move-captured raw address. Both are recorded
above; neither required a header change.

**Classification:** all runs are **success**. No recoverable or fatal defect
was found, so no minimal repro is reported.

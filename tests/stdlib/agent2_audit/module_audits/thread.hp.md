# Safety audit — `stdlib/thread.hp`

Audit scope: `stdlib/thread.hp` (199 lines) plus the committed deep fixtures
`tests/stdlib/deep_thread_{guard,nested,task5}.cp`, the cross-family fixture
`tests/stdlib/deep_cross_family.cp` (CF2/CF3/CF6 shared error/allocator state),
their contract documents under `docs/stdlib/`, and the compile-time manifest
`tests/stdlib/compile_time/thread.json`. `stdlib/error.hp` is cited only as the
error-reporting dependency. No file outside this document was created or
modified; **no test runner, compiler, or binary was executed for this audit**.

## Evidence vocabulary

- **[VERIFIED]** — recorded as passing by a committed contract document with
  exact stdout/exit status, and the corresponding check exists in the committed
  `.cp` source read during this audit. Reported, not re-executed.
- **[DOCUMENTED-ONLY]** — promised by a header comment or contract but no
  committed fixture pins the branch.
- **[UNTESTED]** — code path exists in the header with no committed fixture.

> **Safety-claim boundary.** `THREAD_TASK5 22 0`, `SELF_CHECK_OK` and the
> `trap_rc 107` / `deadlock_detector 1` positive controls are contract-conformance
> evidence for the probed inputs only, **not proof of memory safety**. The
> ERRORCHECK and watchdog detectors are test-only white-box techniques over the
> opaque mutex handle (`DEEP_THREAD_CONTRACT.md:39-59`); ASan is unavailable on
> this host (`docs/stdlib/TASK6_FINAL_REPORT.md:110-113`). The runtime fatals
> used as detectors occur in **forked children** so the parent test still exits 0.

**Defect count for this module:** **1 confirmed library defect fixed**
(F2, the `--strict` build blocker; `DEEP_THREAD_CONTRACT.md:117-126`).
**1 reproduced, out-of-scope compiler defect not fixed** (F3, RAII name
de-duplication; `DEEP_THREAD_CONTRACT.md:128-145`). **1 success finding** with no
guard defect (F1) and **1 success finding** for multi-TU linkage (F4). The task-5
suite found **0 real defects** (`DEEP_THREAD_TASK5_CONTRACT.md:122-140`).

---

## 1. Public API list

The static inventory counts **21 public callable entries** for this header,
**17 exercised, 4 unexercised**
(`docs/stdlib/REGRESSION_INVENTORY.md:66,95,138,170,209`).

| Class | Public methods (header lines) | Ownership |
| --- | --- | --- |
| `mutex` | `mutex`, `is_valid`, `lock`, `unlock`, `native`, `drop` (`:29-60`) | owns an OS mutex handle from `cplus_mutex_new`; `drop` frees and NULLs it |
| `guard` | `guard(mutex*)`, `holds`, `unlock`, `drop` (`:63-88`) | borrows the mutex; owns only the critical section; clears `owner` on unlock |
| `condvar` | `condvar`, `is_valid`, `wait`, `signal`, `broadcast`, `drop` (`:91-133`) | owns a `stdlib_alloc`-backed `pthread_cond_t` |
| `thread` | `thread`, `is_joinable`, `start`, `join`, `drop` (`:136-197`) | owns a `pthread_t` + `joinable` flag; `drop` joins if joinable |

The 4 unexercised entries are
`condvar.condvar`, `condvar.wait`, `condvar.broadcast`, `condvar.drop`
(`REGRESSION_INVENTORY.md:209`); `condvar.is_valid` and `condvar.signal` appear in
the exercised list (`:138`) but no deep fixture pins their behaviour (§7).

---

## 2. Allocation paths and exact counts asserted by tests

| Path | Header lines | Count / assertion | Evidence |
| --- | --- | --- | --- |
| `mutex::mutex` → `cplus_mutex_new()` | `:32-34` | uses libc `malloc`, **not** stdlib telemetry (`DEEP_THREAD_TASK5_CONTRACT.md:22`); 64 create/drop cycles assert `invalid_ops`, `lifecycle_failures`, `alloc_failures` unchanged and fd count stable | [VERIFIED] `t5_cycles_complete/telemetry_clean/fd_stable` (`deep_thread_task5.cp:179-204`) |
| `condvar::condvar` → `stdlib_alloc(sizeof(pthread_cond_t))`, `pthread_cond_init`, rollback on init failure | `:94-100` | 1 allocation; failure → `cv==NULL`, block released; **no fixture** | [UNTESTED for allocation/injection/rollback] |
| `guard` ctor / `unlock` / `drop` | `:66-87` | no allocation; exactly one `pthread_mutex_unlock` per lock, verified against ERRORCHECK mutexes | [VERIFIED] `deep_thread_guard.cp:43-135`, `deep_thread_nested.cp:51-145`, `deep_thread_task5.cp:142-177` |
| `thread::start` → `pthread_create` | `:148-172` | no header-level allocation; `arg` passed via `(void *)(uintptr_t)arg` round-trip (F2 fix); success → `joinable==1` | [VERIFIED] `t5_worker_start/join/counter` (`deep_thread_task5.cp:112-140`) |
| `thread::join` / `drop` → `pthread_join` | `:174-196` | no allocation; `joinable` cleared after join attempt; second join is `STDLIB_FAIL` with `LIFECYCLE`/`CPLUS_EXIT_RESOURCE` | [VERIFIED] `t5_worker_rejoin_fails` (`deep_thread_task5.cp:132-137`) |
| `stdlib_release` on condvar drop | `:126-132` | metadata forget + free counter; not exercised (condvar drop untested) | [UNTESTED] |

**Exact test totals (from committed fixtures/contracts, not re-run here):**

- `tests/stdlib/deep_thread_task5.cp` — **22 checks**, final
  `THREAD_TASK5 22 0`, 23 stdout lines
  (`DEEP_THREAD_TASK5_CONTRACT.md:41-69`;
  `tests/stdlib/deep_thread_task5.expected`, 23 lines):
  5 worker (`:112-140`) + 4 move (`:142-163`) + 2 early-return (`:165-177`) +
  3 cycles (`:179-204`) + 8 zero/NULL (`:206-241`) = **22**.
- `tests/stdlib/deep_thread_guard.cp` — **14 check assertions** +
  `SELF_CHECK_OK`, 15 stdout lines; stderr contains one expected
  `C+ runtime error: mutex failure` from the forked positive control
  (`DEEP_THREAD_CONTRACT.md:65-87,182-188`).
- `tests/stdlib/deep_thread_nested.cp` — **12 check assertions** +
  `SELF_CHECK_OK`, 13 stdout lines, empty stderr
  (`DEEP_THREAD_CONTRACT.md:89-108`).
- Combined parent contract: **2 tests, 6 build+run invocations (3 configs × 2
  tests), 6/6 passing, 26 check assertions + 2 `SELF_CHECK_OK` = 28 exact
  stdout lines, 0 mismatches, 2 fault-injection positive controls**
  (`DEEP_THREAD_CONTRACT.md:151-194`).
- Task-5 totals: 4 direct build+run invocations + 3 harness invocations = 7
  passing runs, 22 checks each, empty stderr
  (`DEEP_THREAD_TASK5_CONTRACT.md:75-120`).

---

## 3. Input-validation matrix

| Input / state | Condition | Observable contract | Class | Evidence |
| --- | --- | --- | --- | --- |
| **NULL function** | `thread::start(NULL, arg, err)` (`:151-154`) | `STDLIB_FAIL`, `UNSUPPORTED/INVALID`, error message "null function" | [DOCUMENTED-ONLY] — branch in header; no fixture | `thread.hp:151-154` |
| **already-running thread** | `thread::start` when `joinable` (`:155-159`) | `STDLIB_FAIL`, `LIFECYCLE`/`CPLUS_EXIT_RESOURCE` | [DOCUMENTED-ONLY] | `thread.hp:155-159` |
| **join on non-joinable** | `thread::join` when `!joinable` (`:177-181`) | `STDLIB_FAIL`, `LIFECYCLE`/`CPLUS_EXIT_RESOURCE` | [VERIFIED] | `t5_worker_rejoin_fails` (`deep_thread_task5.cp:132-137`) |
| `pthread_create` / `pthread_join` failure | `rc != 0` (`:166-168,184-187`) | `STDLIB_FAIL`, `RESOURCE`/`MEMORY` | [UNTESTED] — error paths not injectable by fixtures | `thread.hp:166-168,184-187` |
| **NULL guard owner** | `guard(NULL)` (`:66-70`) | `holds()==0`; unlock/drop no-ops | [VERIFIED] | `null_holds/null_after` (`deep_thread_guard.cp:92-98`); `t5_null_guard_holds/safe` (`deep_thread_task5.cp:226-232`) |
| **zero-init guard** | `guard z;` → `{0}` | holds/unlock/drop safe no-ops | [VERIFIED] | `zero_holds/zero_after` (`deep_thread_guard.cp:83-90`) |
| **invalid mutex handle** | `mutex` with NULL handle; `lock/unlock/native/drop` | safe no-ops; `is_valid()==0` | [VERIFIED] | `t5_zero_invalid/zero_native_null/zero_after_drop` (`deep_thread_task5.cp:206-215`) |
| guard over invalid mutex | `guard(&zero_mutex)` | `holds()==1` (owner non-NULL) but unlock/drop remain no-ops — documented edge case, **not a defect** | [VERIFIED] | `t5_zero_guard_holds/released` (`deep_thread_task5.cp:216-225`); `DEEP_THREAD_CONTRACT.md:24`, `DEEP_THREAD_TASK5_CONTRACT.md:23` |
| **repeated unlock** | `unlock()` ×2 then `drop()` then scope exit | all after first are no-ops; no double unlock (ERRORCHECK would trap 107) | [VERIFIED] | `repeat_unlock_drop`, `relock_after_repeat` (`deep_thread_guard.cp:62-72`) |
| **explicit drop + implicit drop** | `drop()` then scope exit | idempotent; no double unlock | [VERIFIED] | `explicit_drop` (`deep_thread_guard.cp:100-107`); `t5_cycles_complete` (explicit drop then implicit no-op) |
| **nested guards** | two different mutexes; reverse drop | each mutex releases exactly once | [VERIFIED] | `nested_outer_holds/inner_holds/inner_released/outer_released` (`deep_thread_nested.cp:58-72`) |
| re-acquire same mutex | outer unlocked first, inner lock | legal; two locks, two unlocks | [VERIFIED] | `seq_inner_holds/seq_released` (`deep_thread_nested.cp:74-87`) |
| cross-function release | acquire in one function, release through `guard *` | single owner; callee's unreachable drop cannot double-release | [VERIFIED] | `cross_acquire_holds/cross_release_holds/cross_relock` (`deep_thread_nested.cp:89-98`) |
| **`cplus_move` guard transfer** | move `guard` to new owner | exactly one unlock; source zeroed (read via pointer captured *before* move); ERRORCHECK traps a hidden second unlock | [VERIFIED] | `t5_move_src_holds/dst_holds/src_inert/relock_after` (`deep_thread_task5.cp:142-163`) |
| guard across **early return** | function returns while guard in scope | compiler emits drop on the `return` path; re-lock succeeds; ERRORCHECK traps duplicate | [VERIFIED] | `t5_early_return_value/early_relock` (`deep_thread_task5.cp:165-177`) |
| **double-unlock detector** | forked child double-unlocks ERRORCHECK mutex | exit 107 (`CPLUS_EXIT_RESOURCE`) | [VERIFIED positive control] | `trap_rc 107` (`deep_thread_guard.cp:109-126`); stderr line in contract `:67-69` |
| **deadlock detector** | forked child leaves mutex locked, re-locks under `alarm(2)` | `WIFSIGNALED` (SIGALRM); `child_rc 0` when unlock works | [VERIFIED positive control] | `child_rc`, `deadlock_detector` (`deep_thread_nested.cp:100-135`) |
| condvar NULL inputs | `wait(NULL m)`, `signal/broadcast` on `cv==NULL` | `-1` | [DOCUMENTED-ONLY] | `thread.hp:106-124` |
| concurrent contention | multiple threads contending on one mutex/condvar | header says mutex serialises; condvar requires external mutex held around `wait()` | [UNTESTED] — one worker only | `thread.hp:12-16`; `DEEP_THREAD_TASK5_CONTRACT.md:19` |

---

## 4. Failure-atomicity evidence

- **Guard ownership clearing.** `guard::unlock()` sets `owner = NULL` after
  `mutex_unlock` (`:76-81`); `drop()` checks `owner != NULL` before unlocking and
  always clears it (`:83-87`). Hence unlock→drop→destructor never double-unlocks.
  [VERIFIED] by ERRORCHECK mutexes in all three guard suites.
- **Move transfers exactly one unlock.** The moved-from owner field is zeroed by
  `cplus_move`, so its also-emitted drop is inert
  (`DEEP_THREAD_TASK5_CONTRACT.md:20,124-131`).
- **Thread state on failure.** `thread::start` returns `STDLIB_FAIL` and leaves
  `joinable==0` when `fn==NULL` or already running; `join` clears `joinable` even
  on `pthread_join` failure (`:182-187`). Second join is a clean failure, not a
  crash (`t5_worker_rejoin_fails`).
- **Mutex drop is idempotent.** `mutex::drop` NULLs `handle` after
  `cplus_mutex_free` (`:54-59`); the implicit drop is then a no-op
  (`t5_cycles_complete`, 64 cycles).
- **No leaked OS resources.** 64 create/lock/unlock/drop cycles leave telemetry
  failure counters and open-fd count unchanged
  (`deep_thread_task5.cp:179-204`; `DEEP_THREAD_TASK5_CONTRACT.md:22`).

---

## 5. Lifecycle and ownership

- Zero-initialised handles hold no OS resources and are safe no-ops in `drop()`
  (`thread.hp:8`, `DEEP_THREAD_TASK5_CONTRACT.md:23`).
- `guard` never owns the mutex, only the critical section (`:62-63`); it is
  single-thread scope-bound (`:13`). `mutex` serialises access; `condvar`
  requires the external mutex held around `wait()` (`:14`).
- `thread` is move-only by contract; `drop()` joins if still joinable so the
  thread cannot outlive a stack-owned handle (`:15-16,191-196`).
- `error *err` is caller-owned and reset by `start`/`join` via `error_reset`
  (`:150,176`); failures are reported through the `error` record, never by
  terminating the process (`stdlib/error.hp:3-6`).
- **F2 fix rationale (in header comment).** Chained `ctx->mtx->lock()` is
  mis-lowered, so the worker copies the pointer to a local before a single-arrow
  call (`deep_thread_task5.cp:62-76`; `DEEP_THREAD_TASK5_CONTRACT.md:26-34`).

---

## 6. Confirmed defects fixed (reproducer → fix → regression path)

**F1 — [VERIFIED] success, no guard double-unlock defect.** All guard lifecycle
scenarios ran against ERRORCHECK mutexes without a fatal; repeat-unlock/drop and
unlock-then-destructor cases left the mutex releasable. No guard change needed
(`DEEP_THREAD_CONTRACT.md:110-116`).

**F2 — [VERIFIED] recoverable, FIXED in `stdlib/thread.hp`: strict-build blocker.**
- *Reproducer:* `thread::start` passed its opaque `void *arg` directly to
  `pthread_create`; the C+ stack-borrow heuristic emitted
  `[CPLUS-BORROW] stack borrow passed to thread: 'arg'` at `stdlib/thread.hp:159`,
  and `--strict` promoted it to an error. **Any** program including `thread.hp`
  failed to build in strict mode (`build_rc=1` for both deep tests before the fix).
- *Fix:* value-preserving `(void *)(uintptr_t)arg` round-trip plus
  `<stdint.h>` include (`thread.hp:25,160-165`; `DEEP_THREAD_CONTRACT.md:117-126`).
  API and runtime behaviour unchanged.
- *Regression path:* `containers_safety_test.cp` (`thread-ok 1`) and the
  thread-guard stress case (`GUARD 99`); both deep tests build/run in all three
  modes after the fix (`DEEP_THREAD_CONTRACT.md:124-126,163-168`).

**F3 — [VERIFIED] reproduced, OUT OF SCOPE, NOT FIXED: compiler RAII lowering
omits a destructor when an owning local reuses a variable name.**
- *Reproducer* via `--emit-c`:
  `{ guard g = guard_ctor(&m); guard_unlock(&g); guard_drop(&g); }` followed by
  `{ guard g = guard_ctor(&m); }` → **no drop for the second `g`**, while a
  uniquely named `h` gets its drop. `RaiiLowering::registerOwned` de-duplicates by
  variable name within a function, so a later same-named owning local in a
  sibling scope is never registered and can stay locked past scope exit. This
  initially caused `deep_thread_nested.cp` to fatal on re-lock
  (`pthread_mutex_lock failed`).
- *Status:* worked around in the tests — every owning local uses a unique name
  per scope (`deep_thread_guard.cp:48-51`, `deep_thread_nested.cp:74-76`,
  `deep_thread_task5.cp:25-27`, `deep_cross_family.cp:27-31`). Root cause is in
  the compiler (`compiler/Sema/RaiiLowering.cpp` in-tree / `c-plus-core` binary),
  outside this task's file ownership; reported for Agent 1, **not patched**
  (`DEEP_THREAD_CONTRACT.md:128-145`).

**F4 — [VERIFIED] success: multi-TU class-header linkage works.**
`class_linkage` and `multi_tu` suite targets pass; cross-TU release is therefore
simulated with distinct functions in one TU (`DEEP_THREAD_CONTRACT.md:147-149`).

**Task 5 — [VERIFIED] no real defect.** No recoverable or fatal defect was
reproduced; the two observed compiler behaviours (chained-call mis-lowering,
moved-from-name compile error) were worked around inside the test, not patched
(`DEEP_THREAD_TASK5_CONTRACT.md:122-140`).

---

## 7. Remaining untested paths

All [UNTESTED] unless noted.

1. **`condvar` lifecycle** — constructor, `wait`, `broadcast`, `drop` are the 4
   entries the static inventory marks unexercised
   (`REGRESSION_INVENTORY.md:209`). `pthread_cond_init` failure rollback
   (`thread.hp:96-99`), `pthread_cond_wait` failure (`-1` at `:109-111`),
   condvar `signal`/`broadcast` error paths and `pthread_cond_destroy` are not
   pinned by any deep fixture.
2. `thread::start` with `fn==NULL` and with an already-running thread
   (`thread.hp:151-159`); `pthread_create` failure (`:166-168`).
3. `thread::join` `pthread_join` failure path (`:184-187`) — only the clean
   failure for non-joinable handle is tested.
4. **RAII drop of a joinable `thread`** (implicit `pthread_join` at scope exit,
   `:191-196`). Task 5 joins explicitly then calls `drop()`, so the join-in-drop
   branch is not pinned.
5. Actual multi-translation-unit linkage of class-bearing `.hp` files is
   reported passing via the `class_linkage`/`multi_tu` suites
   (`DEEP_THREAD_CONTRACT.md:26-29,147-149`) but the two deep tests simulate it
   inside one TU.
6. Multi-threaded contention, condvar wait/signal coordination, and the
   "external mutex held around wait" requirement (`thread.hp:12-16`): only one
   real worker thread is exercised.
7. Compile-time capability gaps in `tests/stdlib/compile_time/thread.json`:
   `thread_double_unlock`, `thread_unlock_without_ownership`,
   `thread_guard_after_destroy`, `thread_invalid_mutex_owner` are all ACCEPT —
   safety is **runtime-by-design**, not a static proof. Only `thread_valid_guard`
   and `thread_valid_lock` are ACCEPT positive controls.
8. No ASan on this host; UBSan/bounds/null/alignment passing in three modes is
   not a memory-safety proof (`TASK6_FINAL_REPORT.md:110-113`).

---

## 8. Compiler dependencies (C+ RAII lowering and tagged diagnostics)

- **RAII lowering is load-bearing for this module.** The destructor calls
  (`mutex_drop`, `guard_drop`, `thread_drop`) are *compiler-inserted*; the tests
  depend on: (a) a drop emitted at scope exit (`deep_thread_guard.cp:57,69`),
  (b) nested scopes dropping inner-first (`deep_thread_nested.cp:58-72`),
  (c) a drop emitted on the **early-return** path (`deep_thread_task5.cp:165-177`,
  `DEEP_THREAD_TASK5_CONTRACT.md:21`), and (d) `cplus_move` zeroing the source so
  its also-emitted drop is inert (`deep_thread_task5.cp:142-163`). Finding F3
  shows this lowering is **not reliable when an owning variable name is reused**;
  the tests' uniqueness rule is a workaround, not a language guarantee.
- **Tagged diagnostics gate enforcement.** `[CPLUS-BORROW]` fired as a
  *false positive* at `stdlib/thread.hp:159` and became a hard error under
  `--strict` (F2). Conversely, the four unsafe shapes in
  `compile_time/thread.json` are ACCEPT with **no** `[CPLUS-` diagnostic — only
  explicitly tagged rejections count as C+ enforcement
  (`docs/stdlib/TASK6_FINAL_REPORT.md:84-87`). The manifest also records that
  the current checked-in header compiles cleanly after the F2 fix.
- **Error reporting via `error *`:** `start`/`join` call `error_reset` and
  `error_fail`/`error_fail_cat` (`thread.hp:150-168,176-187`), reusing
  `stdlib/error.hp`'s category mapping and `stdlib_note` diagnostics (tagged
  categories such as `LIFECYCLE` → `lifecycle_failures` in CF6).
- **Lowering quirks the fixtures must accommodate:** chained `ctx->mtx->lock()`
  is mis-rewritten, so a local pointer is used (`deep_thread_task5.cp:62-76`);
  referring to a moved-from name is a compile-time error, so its inertness is
  checked through a pre-move-captured raw address
  (`deep_thread_task5.cp:150-156`; `DEEP_THREAD_TASK5_CONTRACT.md:26-36,133-137`).
- **Test-only white-box dependencies:** the ERRORCHECK detector casts
  `mutex::native()` to `pthread_mutex_t *` and assumes the handle's first field
  is the pthread mutex — an assumption isolated to the test files
  (`deep_thread_guard.cp:32-41`, `deep_thread_nested.cp:30-38`,
  `deep_thread_task5.cp:52-60`; `DEEP_THREAD_CONTRACT.md:54-59`). The parent
  process must `fflush(stdout)` before `fork()` because `cplus_print_fatal`
  flushes stdout (`DEEP_THREAD_CONTRACT.md:61-63`).
- **Strict-mode semantics.** `--strict` is accepted by the checked-in driver but
  still emits `-DCPLUS_SAFE=1` without an extra strictness flag
  (`DEEP_COLLECTIONS_CONTRACT.md:176-182`); the meaningful difference in
  `--strict --sanitize=none` is removal of instrumentation. No ASan on this host
  (`TASK6_FINAL_REPORT.md:110-113`), so absence of a report is not a
  memory-safety proof.

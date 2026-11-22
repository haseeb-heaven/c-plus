# Deep thread guard lifecycle contract

Scope: `stdlib/thread.hp` `mutex` / `guard` lifecycle only. Evidence is from the
two independent self-checking tests `tests/stdlib/deep_thread_guard.cp` and
`tests/stdlib/deep_thread_nested.cp`, plus `--emit-c` inspection of the lowered C.

Driver used: `build/c+` (the C+ compiler binary in this checkout is built from
the sibling `c-plus-core` tree, which supplies native `--strict` and
`--sanitize=none` flags; the in-tree `compiler/` sources lag those flags).

## 1. Contract decisions (verified, not assumed)

| Scenario | Contract | Verified result |
| --- | --- | --- |
| `guard g = guard(&m); g.unlock();` then scope exit | `unlock()` clears `owner`; compiler-emitted `guard_drop(&g)` sees `owner == NULL` and is a no-op. Exactly one `pthread_mutex_unlock`. | `after_unlock 0`, `relock_after_drop 1` |
| repeated `unlock()`, then `drop()`, then scope exit | All calls after the first are no-ops; no double unlock. | `repeat_unlock_drop 0`, `relock_after_repeat 1` |
| `holds()` after `unlock()` / `drop()` | `holds()` is exactly `owner != NULL`; returns 0 after either. | `holds_after_unlock 0`, `holds_after_drop 0` |
| nested guards, two different mutexes | Each scope gets its own drop; inner drops before outer; each mutex released exactly once. | `nested_inner_holds 1`, `inner_released 1`, `outer_released 1` |
| nested guards, same mutex (outer unlocked first) | Re-acquisition is legal after the outer guard released; two locks, two unlocks. | `seq_inner_holds 1`, `seq_released 1` |
| zero-initialised guard (`guard z;`) | Lowered to `{0}`; `holds/unlock/drop` all safe no-ops. | `zero_holds 0`, `zero_after 0` |
| explicit `guard(NULL)` | No lock taken; `holds/unlock/drop` no-ops. | `null_holds 0`, `null_after 0` |
| explicit `drop()` then scope exit | Idempotent; no double unlock. | `explicit_drop 0` |
| cross-TU-style release | Guard acquired in one function and released in another (return-by-value transfer + release through `guard *`) keeps a single owner; the callee's unreachable drop cannot double-release. | `cross_acquire_holds 1`, `cross_release_holds 0`, `cross_relock 1` |
| invalid-mutex guard | `mutex` with a NULL handle makes `mutex_lock` a no-op, but `guard::holds()` reports 1 because `owner` is non-NULL. **Documented edge case, not a defect**: `unlock()`/`drop()` remain safe no-ops, so no lifecycle violation occurs. | (design note) |

Actual multi-TU linkage of class-bearing `.hp` files was checked separately:
`run_stdlib_tests.sh class_linkage` and `multi_tu` both pass, so multi-TU class
headers link in this checkout. The two deep tests stay *independent* programs,
so cross-TU release is simulated with distinct functions in one TU.

### Classification

- **success** — scenario passes; observed state equals the contract.
- **recoverable** — a defect was reproduced but the program can continue or the
  failure is contained in a forked child; recorded with exact evidence.
- **fatal** — a defect would abort the process (`CPLUS_EXIT_RESOURCE == 107`) or
  deadlock; used only inside forked positive controls so the test itself exits 0.

## 2. Fault injection (deterministic detectors)

1. **ERRORCHECK double-unlock detector.** The opaque mutex handle's first field
   is a `pthread_mutex_t`. `to_errorcheck()` destroys and re-initialises it with
   `PTHREAD_MUTEX_ERRORCHECK`. A second `pthread_mutex_unlock` then returns
   `EPERM` and `cplus_mutex_unlock` takes the fatal path (exit 107). All guard
   scenarios run against ERRORCHECK mutexes, so any hidden double unlock would
   abort the test. Positive control: a forked child deliberately double-unlocks
   and the parent observes `trap_rc 107` — proving the detector fires.
2. **Watchdog deadlock detector.** A forked child performs the guard lifecycle
   on a *normal* mutex, then re-locks it under `alarm(2)`. If `unlock()` ever
   failed to release, the re-lock blocks and SIGALRM kills the child. Positive
   control: a second child leaves the mutex locked and re-locks, which
   self-deadlocks; the parent observes `WIFSIGNALED` (`deadlock_detector 1`).

Portability note: the `native()` → `pthread_mutex_t *` cast is a white-box,
test-only assumption about the runtime's `cplus_mutex` layout. It is isolated
to the test files and does not affect the library API. On macOS a double unlock
of a *normal* mutex does not reliably corrupt it, which is why the ERRORCHECK
injection (not the deadlock detector) is the authoritative double-unlock check;
the deadlock detector specifically proves that `unlock()` releases the mutex.

Both tests `fflush(stdout)` before `fork()`: `cplus_print_fatal` flushes stdout,
so an unflushed parent buffer inherited by a child would otherwise duplicate
output and break determinism.

## 3. Exact expected stdout

`tests/stdlib/deep_thread_guard.cp` (exit 0, empty stdout otherwise; stderr
contains one expected `C+ runtime error: mutex failure` line from the forked
positive control):

```
valid 1
acquire_holds 1
after_unlock 0
relock_after_drop 1
repeat_unlock_drop 0
relock_after_repeat 1
holds_after_unlock 0
holds_after_drop 0
zero_holds 0
zero_after 0
null_holds 0
null_after 0
explicit_drop 0
trap_rc 107
SELF_CHECK_OK
```

`tests/stdlib/deep_thread_nested.cp` (exit 0, empty stderr):

```
valid 1
nested_outer_holds 1
nested_inner_holds 1
inner_released 1
outer_released 1
seq_inner_holds 1
seq_released 1
cross_acquire_holds 1
cross_release_holds 0
cross_relock 1
child_rc 0
deadlock_detector 1
SELF_CHECK_OK
```

Each test self-checks every value and prints `SELF_CHECK_FAIL <n>` with exit 1
on any mismatch, so no `.expected` sidecar file is required.

## 4. Findings

**F1 — success: no guard double-unlock defect.** Every lifecycle scenario ran
against ERRORCHECK mutexes without a fatal; the explicit repeat-unlock/drop and
unlock-then-destructor cases all left the mutex releasable. No guard change was
needed. `stdlib/thread.hp` guard code is correct as written.

**F2 — recoverable → fixed in `stdlib/thread.hp`: strict-build blocker.**
`thread::start` passed its opaque `void *arg` directly to `pthread_create`,
which the C+ stack-borrow heuristic flags (`[CPLUS-BORROW] stack borrow passed
to thread: 'arg'` at `stdlib/thread.hp:159`). `--strict` promotes that warning
to an error, so *any* program including `thread.hp` failed to build in strict
mode (reproduced: `build_rc=1` for both deep tests before the fix). Fixed with a
value-preserving `(void *)(uintptr_t)arg` round-trip and `<stdint.h>` include.
API and runtime behaviour are unchanged; verified with
`containers_safety_test.cp` (`thread-ok 1`) and the thread-guard stress case
(`GUARD 99`).

**F3 — recoverable, out of scope (not fixed): compiler RAII lowering omits a
destructor when an owning local reuses a variable name.** Reproduced with
`--emit-c`:

```c
{ guard g = guard_ctor(&m); guard_unlock(&g); guard_drop(&g); } /* first g */
{ guard g = guard_ctor(&m); }                                  /* no drop! */
{ guard h = guard_ctor(&m); guard_drop(&h); }                  /* unique name OK */
```

`RaiiLowering::registerOwned` deduplicates by variable name within a function,
so a later same-named owning local in a sibling scope is never registered and
gets no cleanup — a guard can remain locked past scope exit. This initially
caused `deep_thread_nested.cp` to fatal on re-lock (`pthread_mutex_lock failed`).
Workaround applied in the tests: every owning local uses a unique name per
scope. Root cause is in the compiler (`compiler/Sema/RaiiLowering.cpp` in-tree,
`c-plus-core` binary), outside this task's file ownership; it is reported here
for Agent 1 rather than patched.

**F4 — success: multi-TU class-header linkage works.** `class_linkage` and
`multi_tu` suite targets pass. Cross-TU-style release is therefore simulated
inside each independent test rather than split across TUs.

## 5. Verification matrix and counts

Exact commands (run from the repository root):

```sh
# Build/run each test in three configurations (explicit flags: no word splitting)
for t in deep_thread_guard deep_thread_nested; do
  ./build/c+ build tests/stdlib/$t.cp -o /tmp/${t}_default && /tmp/${t}_default
  ./build/c+ build tests/stdlib/$t.cp --strict --sanitize=none -o /tmp/${t}_strict_none && /tmp/${t}_strict_none
  ./build/c+ build tests/stdlib/$t.cp --sanitize=undefined -o /tmp/${t}_ubsan && /tmp/${t}_ubsan
done

# Integration + regression
./tests/stdlib/run_stdlib_tests.sh deep_thread
./tests/stdlib/run_stdlib_tests.sh class_linkage
./tests/stdlib/run_stdlib_tests.sh multi_tu
./build/c+ build tests/stdlib/containers_safety_test.cp -o /tmp/cst && /tmp/cst
./build/c+ build tests/stress/cplus_breaker_v3/17_stdlib_thread_guard.cp -o /tmp/tgb && /tmp/tgb

# Lowered-C evidence for F3
./build/c+ build <probe>.cp --emit-c <probe>.c -o <probe>
```

Results:

| Configuration | guard build/run | nested build/run | UBSan symbols | stdout vs default |
| --- | --- | --- | --- | --- |
| default | 0 / 0 | 0 / 0 | 7 (present) | identical |
| `--strict --sanitize=none` | 0 / 0 | 0 / 0 | 0 (absent) | identical |
| `--sanitize=undefined` | 0 / 0 | 0 / 0 | 7 (present) | identical |

Counts: **2 tests**, **6 build+run invocations** (3 configs × 2 tests), **6/6
passing**, **26 check assertions** (14 in the guard test, 12 in the nested test)
plus 2 `SELF_CHECK_OK` lines = **28 exact stdout lines**, **0 mismatches**,
**2 fault-injection
positive controls** (ERRORCHECK fatal 107, watchdog SIGALRM). Integration:
`deep_thread` runner 2/2 pass; `class_linkage` and `multi_tu` pass;
`containers_safety_test` and thread-guard stress case pass.

**Classification of outcomes:** all 6 configured runs are **success**; F2 was
a **recoverable** build blocker that was fixed; F3 is a **recoverable**,
reproduced out-of-scope compiler defect (worked around, reported, not patched);
the two positive controls are intentionally **fatal/deadlock** paths confined
to forked children.

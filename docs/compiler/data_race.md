# Compiler-level data-race analysis

**Claim boundary.** STRICT reports the patterns listed below from a single
translation unit using Clang AST facts; it is not a race detector and does not
replace ThreadSanitizer. A clean check means "none of the implemented patterns
was found", not "the program is data-race free".

## Already implemented and independently verified (skipped in this work)

- Direct writes to a shared global from a thread entry function called by two
  or more `pthread_create` sites (`CPLUS-FFI`, `THREAD-RACE`). Evidence:
  `tests/compiler/strict_rules/` (`neg_thread_race.c` / `pos_thread_race.c`),
  `tests/core/run.sh` (`thread_global_write`), `tests/compile_fail/classic_bug24_race.c`.
- Stack object borrowed by a thread (`CPLUS-BORROW`): `tests/core/run.sh`,
  `tests/core/strict_mode.sh`, `redteam_100.py`.
- The same heap object passed to two threads with no lock anywhere in the TU:
  `tests/compiler/safety_breakers/` (`05_data_race_shared_mut.cp`).
- `fork()` inside a thread: `tests/compile_fail/classic_bug49_fork_threads.c`.
- Signal-handler globals (`volatile sig_atomic_t`) and async-signal-unsafe
  handler calls: `docs/LIMITATIONS.md` and `AstSafetyCheck.cpp`.
- TSan driver integration (`--sanitize=thread`, compile-and-run probe, runtime
  recompilation, conflict rules): `compiler/Driver/Driver.cpp`. No new
  concurrency runtime was added.

## Added in this change

- **Transitive thread effects.** `GlobalWriteFinder` follows same-TU helper
  bodies with a visited set, so a worker that delegates the mutation to a
  helper is no longer reported clean. Opaque declarations without a body stay
  unknown; no effect is invented for an external function.
- **Lock discipline.** `AstSafetyCheck` tracks mutexes held in the traversed
  function (argument resolved through `&m` to the variable declaration):
  - returning while a mutex is held is a STRICT error;
  - a mutex still held at function exit is reported;
  - unlocking a mutex that was never locked in that function is reported;
  - opposite nesting orders in two functions are reported as a potential
    lock-order deadlock.
  Branch traversal restores the pre-branch lock state on fall-through, so an
  unlock or return in one arm does not corrupt the state of code after the
  `if`. This is linear AST analysis with path-local save/restore, not a new
  CFG engine; the existing CFG/dataflow facility remains in
  `OwnershipAnalysis::checkDefiniteInitialization`.

Evidence: `tests/compiler/data_race/run_suite.py` — 4 unsafe and 5 paired safe
cases, exact commands and outputs in `results/summary.{txt,json}`.

## Unsupported / OPEN patterns (honest limits)

- **Decoy locks remain OPEN.** A `pthread_mutex_lock/unlock` pair on an
  unrelated mutex still suppresses the historical global-write heuristic;
  this is recorded as KNOWN_OPEN in `tests/compiler/red_blue_round/`. The new
  lock discipline does not prove that a lock actually guards a given object.
- Atomic operations are accepted as synchronization boundaries, but no
  memory-order or mixed atomic/non-atomic-access analysis is performed.
  `_Atomic` is recognized only as a type token elsewhere; no misuse check
  exists.
- Locks acquired in one function and released in another are not modeled;
  lock state is per-function. Function-pointer callbacks, `atexit` handlers
  and cross-TU lock acquisitions are outside the per-TU proof.
- Lock-free algorithms, `volatile`-based protocols, condition variables,
  rwlocks beyond name substring detection, and thread cancel/cleanup handlers
  are not verified.
- No claim of absence of deadlock in general: only opposite acquisition
  orders of two named mutexes within this TU are diagnosed.
- ThreadSanitizer cannot be used as an alternative on this host with the
  default backend (`/usr/bin/clang`): `c+ build --sanitize=thread` fails the
  driver's compile-and-run probe. Homebrew Clang 23 with an explicit sysroot
  has separate evidence in `docs/CAPABILITIES.md`; that is a runtime check,
  not a guarantee of this static analysis.

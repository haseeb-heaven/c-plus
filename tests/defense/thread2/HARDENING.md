# thread2 — thread / lifecycle hardening (Agent G, PHASES 5-7)

Branch `agent/modern-stdlib`. Files owned and changed:

| File | Change |
|---|---|
| `stdlib/async.hp` | **2 genuine defects fixed** (static, header-only) |
| `tests/defense/thread2/` | created (harness, pre-fix header fixture, runner, this record) |

No other file was created or modified. No compiler/runtime/edit, no `git add` /
`git commit`. `stdlib/thread.hp`, `stdlib/lifetime_adapt.hp`,
`libc/src/thread/sync_checked.c` and `libc/install-include/cplus_sync_checked.h`
were **audited and left unchanged** (see §4: no new fixable gap within the
ABI-neutral constraint).

## 1. Method

Deterministic C+ harness `test_thread2.cp`:

- Fixed splitmix64 seed `0x7A11D2C0FFEE`; no pid, clock, wall-time value,
  environment randomness or network input.
- **1000 cases**, each in a **forked child**; the parent reaps with a blocking
  `waitpid` and checks `WIFEXITED`/`WEXITSTATUS`. Every child sets `alarm(5)` and
  the parent sets `alarm(55)`; `run.sh` adds `timeout 60`. A signaled child is a
  failure. **No wall-clock value is asserted** and no wall-clock-based pass
  criterion exists.
- All threads started inside children are short-lived and always joined (no
  detached/unjoined threads, no condvar waits without a prior signal).
- Two-value canaries are not needed here because every case's native resource
  is released inside the child before exit; the parent checks only the child's
  deterministic exit code.
- Output: `THREAD2 <cases> <passed> <failed>` + 20 per-family counts.
- Regression discipline: the **exact pre-fix `async.hp`** is kept verbatim in
  `before/async_original.hp` (SHA-256
  `f43a36035ee07e72c6b7b685341e0dc848c2042bf701652827fd5867778ec950`). The
  runner builds the identical harness against both header trees, so the only
  difference is the fixed header.

## 2. Gaps found and fixed (`stdlib/async.hp`)

### GAP A1 — `loop::run()` accepted any `int` as a `uv_run_mode`

`loop::run(int mode, error *err)` cast its `int` argument straight to
`uv_run_mode` and called `uv_run`. libuv only defines `UV_RUN_DEFAULT=0`,
`UV_RUN_ONCE=1`, `UV_RUN_NOWAIT=2`; for any other value its `switch` falls
through and returns `0`. Result: `loop::run(999, &e)` returned `STDLIB_OK` with
a **cleared error**, claiming work was done when none was. This is a silent
contract violation and was already flagged (not fixed) by the static audit
`tests/stdlib/agent2_audit/module_audits/async.hp.md:66-69`.

**Fix (header-only static helper, no ABI change):** added
`static inline int stdlib_uv_run_mode_valid(int mode)` and a fail-closed guard
in `run()` that returns `STDLIB_FAIL` with
`STDLIB_CATEGORY_INVALID` / `CPLUS_EXIT_UNSUPPORTED` for an out-of-range mode.
The three legal modes keep their exact behaviour.

### GAP A2 — `loop::drop()` lied about close failure and made retry impossible

`uv_loop_close()` fails with `UV_EBUSY` while any handle is active (verified on
this host: a started-but-not-fired timer makes `uv_loop_close` return `-16`).
The old `drop()` cleared `initialized = 0` unconditionally, so:

1. `valid()` returned false although native loop resources were still alive;
2. a later explicit `drop()` (and the compiler-emitted drop at scope exit)
   became a no-op, so the loop could **never** be drained or closed again — a
   permanent resource leak on the failure path.

**Fix (no ABI change):** on `uv_loop_close` failure, keep `initialized == 1`
and `return`; only a successful close clears it. A later `drop()` now retries
the bounded drain. Verified in the harness: with an active timer,
`drop(); valid()==1`; after `timer.stop()/timer.drop()`, a second `loop.drop()`
succeeds and `valid()==0`.

## 3. Exact before / after output

Commands (from the repository root; `timeout 60` bound per harness run):

```
# BEFORE: pre-fix headers (fixture) + identical harness
cd <before-tree> && ./build/c+ build test_thread2.cp --strict --sanitize=none \
   -I. -I/opt/homebrew/include -L/opt/homebrew/lib -luv -o thread2_before
timeout 60 ./thread2_before

# AFTER: current fixed headers + identical harness
./build/c+ build tests/defense/thread2/test_thread2.cp --strict --sanitize=none \
   -I. -I/opt/homebrew/include -L/opt/homebrew/lib -luv -o tests/defense/thread2/out/thread2_after
timeout 60 tests/defense/thread2/out/thread2_after
```

Observed (full logs in `out/before.run.log` and `out/after.run.log`):

```
BEFORE (exit 1):
THREAD2 1000 846 154
F async_invalid_mode 56
F async_close_retry 50
F async_invalid_mode2 48
... (all 17 non-async families: 0 failures each)

AFTER (exit 0):
THREAD2 1000 1000 0
F async_invalid_mode 56
F async_close_retry 50
F async_invalid_mode2 48
... (all 17 non-async families: 0 failures each)
```

The 154 pre-fix failures are exactly the two defect families
(`async_invalid_mode` 56 + `async_invalid_mode2` 48 = 104 for GAP A1;
`async_close_retry` 50 for GAP A2). Every other family passes both before and
after: the fixes do not weaken any existing behaviour. A second bounded run of
the fixed harness is byte-identical (`cmp` returned 0).

`sh tests/defense/thread2/run.sh` end-to-end output:

```
== BEFORE build (pre-fix async.hp fixture) ==
before_rc=1  THREAD2 1000 846 154
OK: pre-fix harness exposed the defects
== AFTER build (current fixed headers) ==
after_rc=0  THREAD2 1000 1000 0
OK: fixed harness passed all 1000 cases
OK: fixed harness output deterministic
== checked-sync suite ==
sync_rc=0  sync_checked: 156 0
OK: checked-sync suite unchanged (156 0)
== method suite (hundred_logging_threads) ==
method_rc=0  hundred_logging_threads: 100/100 scenarios ok
OK: method suite unchanged (100/100)
== ABI fingerprint ==
abi_rc=0  abi_fp: ABI fingerprint MATCHES baseline (...).
OK: ABI fingerprint MATCHES baseline
THREAD2 RUNNER FAILURES=0
```

Runner exit 0.

## 4. Reviewed, no change made (honest statement)

The following requested scenarios were actively probed by the harness families
or traced through the source; **no new fixable defect was found** that could be
closed with a static helper without changing a signature, layout, or exported
symbol. They are recorded, not silently "fixed".

- **Mutex double lock / recursive locking** (`mutex_basic`,
  `mutex_double_unlock`): `guard::unlock()` clears `owner`, so unlock + drop
  cannot double-unlock; a second lock on a plain mutex deadlocks by POSIX
  design (the deep-thread suite uses an ERRORCHECK injection to observe it).
  No gap.
- **Mutex destroy while locked**: on this host `pthread_mutex_destroy` returns
  `EBUSY`; the additive C11 wrapper `cplus_mtx_destroy_checked` already reports
  it and rolls back so a retry works (`tests/libc/test_sync_checked.c` step 5).
  Covered by the existing 156/0 suite; not duplicated.
- **Condvar destroy with waiters — genuine defect, NOT fixed here (abi scope).**
  On Darwin `pthread_cond_destroy` returns **0** when threads are waiting
  (local probe: `cond_destroy_with_waiter rc=0`), so
  `cplus_cnd_destroy_checked` reports success although destroying a condvar
  with waiters is UB. Closing this requires counting waiters, which requires
  intercepting `cnd_wait` through a **new exported API** (or a layout field on
  `cplus_cnd_t`). Both are symbol/layout changes, explicitly out of scope for
  this task ("no signature/layout/symbol change"), and the ABI fingerprint
  (`nm -gU` records) would fail. Reproducer command is retained in §5; this is
  reported honestly as an open finding, not papered over.
- **Thread double join / join self / failed join** (`thread_start_join`,
  `thread_badstart`): `joinable` is cleared only on a successful `join()`, and
  a second join is refused; join-self returns EDEADLK (no deadlock on Darwin)
  and the failed-join lifecycle keeps the handle retryable. Already hardened
  by the earlier checked wrappers; no new gap.
- **Future single-consumption**: `stdlib/async.hp` has no future type; the
  equivalent is a `lifetime_owner`/`lifetime_borrow` result slot, whose
  token-based single-consumption is exercised by `lifetime_transfer` /
  `lifetime_recycle`. No change.
- **Lifetime stale borrow / double release / transfer chains / recycled
  address** (`lifetime_valid`, `lifetime_double_release`, `lifetime_transfer`,
  `lifetime_recycle`): metadata generation checks refuse stale tokens even at
  a recycled address; moved-from sources cannot be released. Already hardened
  by the earlier `lifetime_adapt.hp` work; no new gap.
- **Timer double-release**: the C+ compiler enforces move-only on RAII classes
  (a `thread t2 = t1;` copy is rejected: `error: resource 'b' cannot be copied
  from 'a'`), and `drop()` clears the handle before `uv_close`, so a second
  drop is a no-op. No gap.

## 5. Reproducer for the open condvar finding (not fixed)

```
cat > /tmp/cnd_waiters.c <<'EOF'
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int ready = 0;
static void *waiter(void *a){(void)a; pthread_mutex_lock(&m); ready=1;
  pthread_cond_wait(&cv,&m); pthread_mutex_unlock(&m); return 0;}
int main(void){pthread_t t; pthread_create(&t,0,waiter,0);
  while(!ready) usleep(1000); usleep(20000);
  printf("rc=%d\n", pthread_cond_destroy(&cv)); return 0;}
EOF
/usr/bin/clang -std=c11 /tmp/cnd_waiters.c -pthread -o /tmp/cnd_waiters && /tmp/cnd_waiters
# observed: rc=0   (POSIX: UB to destroy a condvar with waiters)
```

## 6. ABI rationale and verification

- Both fixes are `static inline` / small control-flow changes inside
  `stdlib/async.hp`. No struct field, enum value, macro, function signature,
  global, or exported symbol changed; no new symbol was added.
- `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt`
  → **MATCHES baseline**, empty diff (fingerprint records include `nm -gU`
  symbols and all layout/size/align values).
- Regression re-runs (all under `timeout`, exit 0, 0 failures):
  - `tests/libc/test_sync_checked.c` → `sync_checked: 156 0`
  - `tests/methods/hundred_logging_threads.c` → `hundred_logging_threads: 100/100 scenarios ok`
  - `tests/defense/thread2/test_thread2.cp` (fixed) → `THREAD2 1000 1000 0`

## 7. Scope / evidence limits

This is evidence for the enumerated 1000 deterministic cases and the two fixed
defects, not a proof of safety for all interleavings or build modes. The run
used `--strict --sanitize=none`; no concurrent stress, ASan/TSan, or live-game
validation was performed, and no live-game/injected-DLL claim is made.

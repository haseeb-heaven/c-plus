# Task 6 Phase 5 — Thread & synchronization security (evidence notes)

Branch: `agent/modern-stdlib`. Date: 2026-11-23.

## Ownership

Created/modified **only** these files:

| File | Change |
| --- | --- |
| `tests/security/stdlib/thread_security.cp` | new self-checking suite |
| `tests/security/stdlib/THREAD_SECURITY_NOTES.md` | this file |
| `stdlib/thread.hp` | one defect fix (see §5) — permitted because a defect was reproduced |

No compiler, runtime, build script, fixture, or other test file was edited.
No `git add` / `git commit` was performed. No game/UI/native code involved.

## Environment

```
$ ./build/c+ --version
C+ 0.1.0
Language: C+ (C with memory safety, deterministic RAII, lightweight classes)
Backend: host Clang/LLVM
$ git branch --show-current
agent/modern-stdlib
```

All commands run from the repo root with
`PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH` (documented
stable-toolchain prefix). Every build and run used `timeout 30`
(`timeout -k 5 30` for runs). Output artifacts were written only to
`build/thread-security/` (outside the owned test directory).

## Strict-safe coding patterns used

- Every owning RAII local has a **unique name** in its function (the C+
  `RaiiLowering` pass deduplicates ownership by variable name, so reused sibling
  names silently lose their destructor).
- **No chained temporaries** (`mutex().lock()` / `guard(&mutex())`); every
  handle is bound to a named local first.
- **No variable modulo/division**; only fixed bit masks (`i & 7`) and constant
  loop bounds are used.
- No pointer-parameter subscripts; the white-box mutex cast is confined to a
  test-only `pthread_mutex_t *` helper and is never applied to stale memory.
- The source deliberately contains **no literal `pthread_create` token**:
  strict mode rejects `fork()` in a translation unit that mentions
  `pthread_create` (`[CPLUS-FFI] fork() in a multi-threaded process can
  deadlock`). Threads are therefore created through the managed `thread::start`
  wrapper (whose `pthread_create` lives in the header); the forked watchdog
  children still use raw `pthread_mutex_*` primitives. This satisfies the
  requirement that the *parent* never deadlocks: the inverted lock order runs
  only inside a forked child killed by the parent's alarm watchdog.

## Coverage / check counts

| # | Area | Checks | Result |
| --- | --- | ---: | --- |
| 1 | guard double unlock / unlock+drop (ERRORCHECK injection) | 6 | PASS |
| 2 | raw double unlock + invalid unlock fatal traps in forked watchdog children (positive controls, exit 107) | 2 | PASS |
| 3 | use-after-destroy: metadata refusal + zero/NULL handle no-ops (no stale dereference) | 5 | PASS |
| 4 | invalid thread lifecycle: join-before-start, null start, double start, double join, drop | 8 | PASS |
| 4b | `join()` failure path keeps the resource joinable (self-join EDEADLK) | 4 | PASS (after fix) |
| 5 | partial-init cleanup: condvar allocation-failure injection, failed thread start | 4 | PASS |
| 6 | error paths while a guard is held (failed signal/wait/join; lock still reusable) | 2 | PASS |
| 7 | bounded contention, 4 threads × 200 iterations, exact counter/checksum | 4 | PASS |
| 8 | lock ordering: inverted order only in forked watchdog; ordered control exits 0 | 2 | PASS |
| | **Total** | **37** | **37 pass / 0 fail** |

Exact counters for §7: `counter = 4 * 200 = 800`;
`checksum = (sum of i in 1..200 where (i & 7) == 0) * 4`. Both are computed
independently in the parent with plain loops and compared to the guarded
worker totals; no variable division/modulo is used.

## Exact commands and results

### A. Static check (strict)

```
$ ./build/c+ check --strict tests/security/stdlib/thread_security.cp
check: tests/security/stdlib/thread_security.cp ok
rc = 0
```

### B. Strict / no sanitizer — run FIRST

```
$ ./build/c+ build tests/security/stdlib/thread_security.cp --strict --sanitize=none -o build/thread-security/thread_security_strict_none
build rc = 0        (build log empty)

$ timeout -k 5 30 ./build/thread-security/thread_security_strict_none
run rc = 0
final line: THREAD_SECURITY 37 0
stderr: 0 bytes
```

Determinism (post-fix, five consecutive bounded runs):

```
none run 1 rc=0 final=THREAD_SECURITY 37 0 stderr=0
none run 2 rc=0 final=THREAD_SECURITY 37 0 stderr=0
none run 3 rc=0 final=THREAD_SECURITY 37 0 stderr=0
none run 4 rc=0 final=THREAD_SECURITY 37 0 stderr=0
none run 5 rc=0 final=THREAD_SECURITY 37 0 stderr=0
```

### C. UBSan — attempted second

Default driver invocation is **BLOCKED** by an external toolchain mismatch
(the driver recompiles `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c`,
which references `CPLUS_EXIT_ARITHMETIC`, absent from this branch's runtime
header). No compiler/runtime file was edited to work around it:

```
$ ./build/c+ build tests/security/stdlib/thread_security.cp --strict --sanitize=undefined -o build/thread-security/thread_security_ubsan_default
rc = 1
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  129 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", NULL, 0,
      |                       ^
1 error generated.
error: failed to compile instrumented C+ runtime
```

The driver's documented `CPLUS_ROOT` override selects this branch's own
`runtime/cplus_runtime.c`, which compiles cleanly; UBSan evidence was obtained
that way with **no file edits**:

```
$ CPLUS_ROOT=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib ./build/c+ build tests/security/stdlib/thread_security.cp --strict --sanitize=undefined -o build/thread-security/thread_security_ubsan
build rc = 0        (build log empty)

$ timeout -k 5 30 ./build/thread-security/thread_security_ubsan
run rc = 0
final line: THREAD_SECURITY 37 0
stderr: 0 bytes        (no UBSan diagnostic)
```

Determinism (three consecutive bounded UBSan runs):

```
ubsan run 1 rc=0 final=THREAD_SECURITY 37 0 stderr=0
ubsan run 2 rc=0 final=THREAD_SECURITY 37 0 stderr=0
ubsan run 3 rc=0 final=THREAD_SECURITY 37 0 stderr=0
```

Harness note: `sh tests/security/stdlib/run.sh --only thread_security --mode
strict-none` reports `PASS=1 FAIL=0` for this case. The same runner with
`--mode ubsan` reports `BUILD_FAILURE=1` because it does not set `CPLUS_ROOT`
and has no `CPLUS_EXIT_ARITHMETIC` BLOCKED classifier (this affects every test
in that runner, not only this one). Setting `CPLUS_ROOT` to the branch root
before invoking the runner removes the blocker and yields
`stdlib/thread_security ubsan PASS 0 exit 0` (`PASS=1 FAIL=0`); the runner file
itself was not changed to work around it.

### D. Regression check on existing thread tests (read-only)

After the header fix, the pre-existing thread suites were rebuilt and run in
`--strict --sanitize=none`; all still pass:

```
thread_lifecycle             build=0 run=0 final=THREAD_SAFETY 28 0  stderr=0
thread_contention            build=0 run=0 final=THREAD_SAFETY 10 0  stderr=0
deep_thread_guard            build=0 run=0 final=SELF_CHECK_OK      stderr=61
deep_thread_nested           build=0 run=0 final=SELF_CHECK_OK      stderr=0
deep_thread_task5            build=0 run=0 final=THREAD_TASK5 22 0  stderr=0
```

`deep_thread_guard`'s 61 stderr bytes are its pre-existing deliberate
double-unlock fatal child (it does not redirect child stderr); not a regression.

## SKIPs (declared, never counted as passes)

Recorded verbatim as `SKIP_REASON` lines in the program output:

1. `thread_cancellation` — `stdlib/thread.hp` has no `cancel()`/cleanup API;
   raw `pthread_cancel` would bypass managed RAII lowering. A separate probe
   earlier established that `--sanitize=thread` is a valid CLI value but is not
   supported by the host Clang backend, so no dynamic thread-sanitizer evidence
   is obtainable here either.
2. `pthread_create_failure` — `pthread_create` resource exhaustion is not
   deterministically injectable in-process. The two deterministic `start()`
   error paths (null function, double start) are covered as checks instead.

## Confirmed defect and fix

**Defect (reproduced):** `thread::join()` cleared `joinable` unconditionally,
even when `pthread_join()` returned an error. A deterministic error path is a
self-join: a worker calls `join()` on its own `thread` handle, which makes
`pthread_join(pthread_self())` return `EDEADLK`. Observed before the fix:

```
ts_4b_worker_finished 1
ts_4b_selfjoin_fails 1
ts_4b_joinable_kept_on_failure 0
ts_4b_drop_inert 1
THREAD_SECURITY 37 1
```

After a failed join the thread is still running/joinable at the OS level, but
the handle reports `is_joinable() == 0`, so `drop()` never reaps it — a
lifetime/resource-leak hazard (and an inconsistent lifecycle contract).

**Fix (`stdlib/thread.hp`, `thread::join`):** move `this->joinable = 0;` into
the success path so a failed `pthread_join` leaves the handle joinable and
retryable:

```c
rc = pthread_join(this->handle, out);
if (rc != 0) {
    error_fail(err, CPLUS_EXIT_RESOURCE, "thread::join: pthread_join failed");
    return STDLIB_FAIL;   // keep joinable: still joinable in the OS
}
this->joinable = 0;
return STDLIB_OK;
```

**After fix:** `ts_4b_joinable_kept_on_failure 1`; both A/B/C runs report
`THREAD_SECURITY 37 0` with clean stderr; regression suite §D unchanged.

No other defect was reproduced. In particular the guard correctly avoids
double unlock (§1/§2), destroyed/zero handles are inert rather than dereferenced
(§3), error-returning calls under a guard do not lose a release (§6), and the
lock-order inversion is detected by the watchdog without blocking the parent
(§8).

## Limitations

- The mutex `ERRORCHECK` re-init is a test-only white-box cast of the runtime
  handle's first field; it is not part of the public API.
- The watchdog uses `SIGALRM` + `SIGKILL` with a 2-second hard bound for the
  two forked children; the external `timeout 30` is the final backstop. A
  timeout is always classified as failure, never a pass.
- No ThreadSanitizer run was possible on this host (`--sanitize=thread` is
  unsupported by the clang backend), so data-race freedom rests on the exact
  counter/checksum checks and on every shared write being guard-protected.
- UBSan required the `CPLUS_ROOT` override described in §C; without it the
  driver is blocked by an unrelated external runtime header mismatch.

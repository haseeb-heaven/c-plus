# AUDIT_lifecycle — lifecycle / concurrency / logging / signal / complex wrappers

**Branch:** `agent/modern-stdlib`  **Date:** 2026-11-23  **Role:** AUDIT agent 3 of 5 (parallel audit)
**Mode:** READ-ONLY. No source, test, build, or git-index file was modified. Only this document was created.

## 1. Scope and method

Sources audited (all lines read in full):

| Family | Files |
|---|---|
| pthread wrappers | `libc/src/thread/thread.c` (11 exports) |
| C11 adapters | `libc/src/thread/threads_c11.c` (25 exports), `libc/install-include/cplus_threads.h` |
| Safe runtime | `stdlib/safe/src/safe_runtime.c` (18 exports) |
| Safe telemetry | `stdlib/safe/src/safe_stats.c` (4 exports) |
| Safe logging | `stdlib/safe/src/safe_log.c` (3 exports) |
| Safe exceptions | `stdlib/safe/src/safe_except.c` (1 export) |
| libc logger | `libc/log/log.c` (2 exports) |
| Signal | `libc/src/signal/signal.c` (2), `signal2.c` (10) |
| Complex | `complex.c` (7), `complex_extra.c` (41), `complex_inverse.c` (18) |
| fenv | `fenv.c` (6), `fenv_extra.c` (5) |
| errno | `libc/src/errno/errno.c` (1) |
| uchar | `libc/src/uchar/uchar.c` (4) |

Total exported functions accounted for: **158**.

Method: static review of every body, cross-checked against the installed SDK headers
(`pthread_impl.h:48` `_PTHREAD_ONCE_SIG_init 0x30B1BCBA`, `pthread.h:220` `PTHREAD_ONCE_INIT`,
`limits.h:112-113` `PTHREAD_DESTRUCTOR_ITERATIONS 4` / `PTHREAD_KEYS_MAX 512`), the project headers,
the existing tests (`tests/libc/test_threads_c11.c`, `test_logging_attack.c`, `test_fenv_extra.c`,
`test_signal.c`, `test_extra7_signal.c`, `tests/runtime/*`), and the project notes
(`docs/LOGGING.md`, `docs/libc/RUNTIME_RECOVERY.md`, `tests/adversarial/logging_attack_NOTES.md`).
Three local probe programs were compiled in `/tmp` (no repo files touched) to confirm ABI/size facts:
`sizeof(cplus_once_flag)=64`, first 8 bytes = `ba bc b1 30` (correct Darwin signature);
`sizeof(mbstate_t)=128` vs `sizeof(unsigned)=4`; bounded logger emits **4095** bytes for a 100000+100000-char input.

Uniform families (complex, fenv) are recorded as grouped rows that still enumerate every export by name.

## 2. Severity counts

| Severity | Count | Finding IDs |
|---|---|---|
| Critical | 0 | — |
| High | 3 | F1, F2, F3 |
| Medium | 11 | F4–F13, F20 |
| Low | 7 | F14–F19, F21 |
| **Total** | **21** | |

Definitions: **High** = credible memory-safety failure or broken recovery contract; **Medium** = crash on
caller misuse, data race, silent contract violation, or fragile ABI; **Low** = latent/portability/documentation/
verification gap with no observed failure on the stated host.

## 3. Findings catalog (evidence + recommendation)

### F1 — High — `cplus_thrd_create` start context lifetime is tied to a caller-owned handle
**Where:** `threads_c11.c:119-159` (`cplus_thrd_ctx`, `CPLUS_THRD_PT_OFFSET`, trampoline, create), `:217-223` (detach).
**Evidence:** The context `{func,arg}` is memcpy'd into the *caller's* `cplus_thrd_t` at offset 0; the trampoline
(`:138`) reads it when the new thread starts, which can be arbitrarily later than `pthread_create` returning.
The comment at `:119-121` claims "its lifetime is then tied to the joinable handle" — false for a detached
thread. `tests/libc/test_threads_c11.c:323-325` exercises exactly the unsafe pattern: create a local `tdet`,
`cplus_thrd_detach(tdet)` immediately, then `main` returns (stack frame dies while the worker may not have read
the context). C11 does not require the handle to outlive `thrd_detach` for implementation-private state.
**Unsafe case:** use-after-scope → wild `func` pointer → crash or arbitrary call.
**Fix:** allocate `cplus_thrd_ctx` on the heap in `cplus_thrd_create`, pass it to `pthread_create`, and have the
trampoline copy-and-`free` it before invoking `func`. Store only `pthread_t` in the public handle. (`malloc`
failure maps to `thrd_nomem`.)
**Test:** barrier-synchronized worker that sleeps ≥50 ms before reading `arg`; detach it; return from a frame
that poisons its stack with `0xA5`; run under ASan — expect no UAF.

### F2 — High — uchar uses a 4-byte `unsigned` where the host requires a 128-byte `mbstate_t`
**Where:** `uchar.c:29-35` (`mbrtoc16`), `:57-61` (`c16rtomb`), `:89-94` (`mbrtoc32`).
**Evidence:** Verbatim musl code declares `static unsigned internal_state;` and does
`if (!ps) ps = (void *)&internal_state; unsigned *pending = (unsigned *)ps;`. That pointer is passed to
`cplus_mbrtowc`/`cplus_wcrtomb`, which forward it unchanged to host `mbrtowc`/`wcrtomb`
(`wchar_multibyte.c:28-38`). On this host `sizeof(mbstate_t) == 128` (probe) while the object provided is 4 bytes.
This is UB by the C standard even though macOS UTF-8 `mbrtowc` did not observe writes to the state in our probe.
**Unsafe case:** latent global-buffer overflow / adjacent-static corruption on stateful locales or a host whose
`mbrtowc` initializes/writes the whole `mbstate_t`; surrogate state also aliases host state in the first 4 bytes.
**Fix:** use `static mbstate_t internal_state;` (zero-initialized) for the `ps` passed to the host, and keep the
pending-surrogate word in a *separate* `static unsigned` or in the tail of a real `mbstate_t` via `memcpy` guarded
by `sizeof(unsigned) <= sizeof(mbstate_t)`; never pass a non-`mbstate_t` object to the host.
**Test:** compile-time size assert; runtime canary around `internal_state` with a stateful locale under ASan;
multi-call surrogate sequence `mbrtoc16` twice with `ps == NULL`.

### F3 — High — longjmp recovery cannot run C++ destructors / RAII (documented, inherent)
**Where:** `safe_runtime.c:115-134` (`cplus_runtime_guard`), `:49-61` (`cplus_runtime_violation`), `:140-149`;
`safe_runtime.h:10-25`; `RUNTIME_RECOVERY.md:43`.
**Evidence:** `longjmp(g_top->jb,1)` skips all intermediate frames. Only entries explicitly registered with
`cplus_runtime_push_cleanup()` run, and only via the guard's `cplus_runtime_cleanup_unwind_to(frame.base)`.
The header already admits unregistered resources leak. `RUNTIME_RECOVERY.md:43` records the Apple-Clang UBSan
empirical limit: a second UBSan fault on the same thread after recovery aborts with
`UndefinedBehaviorSanitizer: nested bug in the same thread, aborting.`
**Unsafe case:** any scoped lock, `FILE*`, `std::string`, smart pointer, or `std::lock_guard` in a skipped frame
is leaked / left locked; nested UB aborts.
**Fix (compiler side):** emit cleanup registrations for compiler-owned destructors, or replace longjmp with C++
exceptions in C++ mode. **Mitigation (runtime):** prefer `failstop=1` outside isolated guards.
**Test:** guarded body holding a mutex + `fopen` without explicit cleanup; assert leak counters and lock state;
a nested-UB test that documents the accepted abort.

### F4 — Medium — signal wrappers pass NULL `sigset_t *` straight to host; no validation
**Where:** `signal2.c:14-33` (sigemptyset/sigfillset/sigaddset/sigdelset/sigismember), `:38-45`
(sigprocmask/sigsuspend), `:50-53` (sigwait).
**Evidence:** every setter/predicate dereferences its `set` via the host call with no NULL check.
`tests/libc/test_signal.c` / `test_extra7_signal.c` contain no NULL or invalid-signo cases (only valid sets and
post-restoration checks). Invalid `signo` is delegated to the host (returns -1/EINVAL) — acceptable propagation.
**Unsafe case:** SIGSEGV on `cplus_sigemptyset(NULL)`, `cplus_sigaddset(NULL, s)`, etc.; `sigwait(NULL,...)`
is UB.
**Fix:** reject required NULL pointers with `errno = EINVAL; return -1;` consistently with fenv_extra.c style;
add tests for every pointer argument.
**Test:** table-driven NULL-arg test asserting nonzero return + EINVAL for all 10 exports with pointer params.

### F5 — Medium — fenv NULL hardening is inconsistent across exports
**Where:** `fenv_extra.c:28-32` (fegetexceptflag guarded), `:42-46` (feholdexcept guarded); **unguarded:**
`fenv.c:10-11` (`cplus_fegetenv`, `cplus_fesetenv`), `fenv_extra.c:35-38` (`fesetexceptflag`), `:50-53`
(`feupdateenv`). `test_fenv_extra.c:60-66` only covers the two guarded functions.
**Unsafe case:** NULL deref through host `fesetenv`/`feupdateenv`/`fesetexceptflag`/`fegetenv`.
**Fix:** add `if (!p) { errno = EINVAL; return -1; }` to all four; extend the test to all pointer-taking exports.
**Note (hostile objects):** non-NULL but garbage `fenv_t`/`fexcept_t` is not validated; ISO C makes this caller
UB, but on x86 `fesetenv` can program trapping/denormal control bits. Recommend documenting the "obtained from
fegetenv/feholdexcept only" contract.

### F6 — Medium — `cplus_libc_log` can clobber `errno`, contradicting its documented guarantee
**Where:** `log.c:5-13` (header comment "Never changes ... errno"), `:24` (`CPLUS_LOG_WRITE` ignores result).
**Evidence:** POSIX `write(2)` sets `errno` on failure (EBADF/EPIPE/EINTR/ENOSPC). With fd 2 closed or a full
pipe, an instrumented call that logs "enter" before the real operation can leave a stale `errno` on a later
successful operation that does not set errno.
**Fix:** save `errno` on entry and restore before return; this makes the guarantee literal and keeps
`cplus_thrd_sleep`'s post-call `errno` check (`threads_c11.c:181-189`) robust.
**Test:** set `errno = 1234`, close fd 2, call `cplus_libc_log`, assert `errno == 1234`.

### F7 — Medium — logger ignores short writes, EINTR and SIGPIPE
**Where:** `log.c:64` `(void)write(...)`.
**Evidence:** a single `write` of up to 4095 bytes to a nonblocking/full pipe can return a short count or -1/EINTR;
the partial line is silently lost and a subsequent handler call can interleave. Writing to a broken pipe raises
SIGPIPE.
**Fix:** loop on partial writes and EINTR with a bounded retry count; document fd-2 ownership.
**Test:** nonblocking pipe with a 100-byte drain, emit a 4000-byte line, assert full delivery or documented cap.

### F8 — Medium — safe_runtime control state is racy under C11/C++11 memory model
**Where:** `safe_runtime.c:34-36` (`static cplus_runtime_handler g_handler;` plain, `volatile int g_failstop`,
`volatile int g_violation_trap`), `:38-47`, `:54-60`.
**Evidence:** one thread calling `cplus_runtime_set_handler`/`set_failstop` while another reports a violation is
a data race (UB); `volatile` is not atomic. `g_top`/`g_cleanups` are TLS, so guards are per-thread (good), but the
configuration globals are shared.
**Fix:** `_Atomic(cplus_runtime_handler)` and `_Atomic int` with acquire/release, or make them thread-local if
the contract is per-thread (header says "process/thread handler" — ambiguous).
**Test:** TSan two-thread config churn + report loop.

### F9 — Medium — cleanup registry TLS is not unwound on `thrd_exit`/`pthread_exit` inside a guard
**Where:** `safe_runtime.c:29-33` (TLS globals, no destructor registration), `:115-134`.
**Evidence:** if a guarded body calls `cplus_thrd_exit`/`pthread_exit`, the TLS is destroyed without running
`cplus_runtime_cleanup_unwind_to(0)`; registered drops are lost and `g_leaked`/`g_unwound` are not updated.
**Fix:** register a `pthread_key_create` destructor for the cleanup stack; call it from the C11 `thrd_exit`
adapter (`threads_c11.c:197-202`) as a best-effort hook.
**Test:** guarded body pushes two cleanups then calls `cplus_thrd_exit`; assert both drops ran in a sibling thread.

### F10 — Medium — unbounded recursion between report/violation and the user handler
**Where:** `safe_runtime.c:49-72` (`cplus_runtime_violation`, `cplus_runtime_report` calls `g_handler`),
`:140-149` (`__ubsan_on_report` → `cplus_runtime_report`).
**Evidence:** no reentrancy guard. A handler that triggers UB or calls `cplus_runtime_report` recursively recurses
until stack exhaustion; `fflush(NULL)` in the trap path is also not async-signal-safe.
**Fix:** `atomic_flag` reentrancy latch; on reentry go straight to `_exit(70)` without stdio.
**Test:** handler that calls `cplus_runtime_report` once; assert bounded behavior (exit 70, no stack overflow).

### F11 — Medium — safe_log / safe_stats are not async-signal-safe; logger recursion can deadlock
**Where:** `safe_log.c:20` (`fprintf(stderr, ...)`), `safe_stats.c:21,33-45` (`g_reg_lock` on first registration).
**Evidence:** `fprintf` takes the stderr `FILE*` lock; a SIGALRM handler interrupting a `cplus_safe_log`/`fprintf`
in `cplus_runtime_report` (`safe_runtime.c:65,70`) can deadlock. `cplus_safe_stat_record` is lock-free only after
the API name is registered; the first call for an API takes a pthread mutex.
**Fix:** route safe logging through `cplus_libc_log` (write-based) or document "not signal-safe"; pre-register all
stat slots at startup so the mutex path is unreachable from handlers.
**Test:** signal-based reentrancy harness (like A4) against `cplus_safe_log` and first-call `cplus_safe_stat_record`.

### F12 — Medium — `mtx_timedlock` fallback is a polling loop with unenforced preconditions
**Where:** `threads_c11.c:226-276` (`mtx_init` masks off `CPLUS_MTX_TIMED`; `:253-276` loop).
**Evidence:** `kind = type & ~CPLUS_MTX_TIMED` accepts and discards the timed flag, so any plain/recursive mutex
can be passed to timedlock (ISO C requires `mtx_timed`). The loop calls `pthread_mutex_trylock` then
`nanosleep`, ignoring `nanosleep` failures and handling invalid `tv_nsec` by tight-spin until the wall-clock
deadline. It is CLOCK_REALTIME-dependent (documented as a Darwin limitation); no generation/ownership token
is checked, so a mutex destroyed and re-created while a timedlock is in flight is not detected.
**Fix:** track a timed flag in the opaque `cplus_mtx_t` (e.g., a magic word in unused bytes) and reject
`CPLUS_THRD_ERROR` when absent; validate `time_point`; retry `nanosleep` on EINTR.
**Test:** timedlock on a PLAIN mutex must return error (current behavior returns success/timeout); monotonic-clock
skew test; contention fairness test.

### F13 — Medium — `ONCE_FLAG_INIT` hardcodes a platform ABI signature
**Where:** `cplus_threads.h:60-69`; verified against this SDK (`pthread_impl.h:48`, `pthread.h:220`).
**Evidence:** `{ (void *)(uintptr_t)0x30B1BCBAu }` is correct for this macOS SDK and the resulting first 8 bytes
match `PTHREAD_ONCE_INIT`; non-Apple uses all-zero. But the constant is private Apple API and can change; on a
newer SDK or other pthread implementation the adapter would silently call `pthread_once` on an invalid once flag
(UB/crash). `call_once` itself (`threads_c11.c:344-350`) discards the `pthread_once` return and has no recursive
call detection.
**Fix:** initialize the once flag lazily from a helper that uses the platform's `PTHREAD_ONCE_INIT` macro when
available, or provide a per-platform source file; assert the ABI signature at build time.
**Test:** compile-time `static_assert` that the first bytes equal `PTHREAD_ONCE_INIT`; multi-thread once smoke test.

### F14 — Low — errno plumbing is incomplete/dead
**Where:** `errno.c:1-4`; `install-include/cplus_libc.h:140`.
**Evidence:** `__cplus_errno_location()` is declared but no install header defines
`#define errno (*__cplus_errno_location())`; all audited wrappers use host `errno` (host headers are included).
The separate `__thread int cplus_errno_value` is therefore unused — the comment in `errno.c:4` describes a macro
that does not exist in this tree. Using `__thread` also breaks MSVC.
**Fix:** either wire the macro deliberately and audit every host call's errno, or remove the stub and document
host-errno delegation. Use a portable TLS macro.

### F15 — Low — adversarial logging test and notes are stale relative to the hardened logger
**Where:** `tests/libc/test_logging_attack.c:11-15` (still describes `fprintf` with no truncation), A2 assertion
(`:380-425`) expects `expected = prefix + 200000 chars + '\n'` = 200016 bytes; `logging_attack_NOTES.md:11-13,15-29,31-57`.
**Evidence:** probe of current `log.c:38-65` produces **4095** bytes with a `...` marker for the same input, so A2 now
reports a false `FINDING` (output length wrong/capped). A4's "not async-signal-safe" and A5's stderr error-indicator
finding were fixed by commit `ccad805` (write(2)+stack buffer) but the notes still claim them.
**Fix:** update A2 to assert bounded 4095-byte output with "...\n" tail; re-run A4/A5 and rewrite notes as
"fixed with evidence"; add truncation tests for exact-boundary lengths (4090/4091).

### F16 — Low — telemetry has a fixed 64-slot cap and racy reset
**Where:** `safe_stats.c:18-21,33-45,58-65`.
**Evidence:** registration beyond `CPLUS_SAFE_STATS_CAP` (64) silently drops counters; `stats_reset` uses relaxed
stores concurrent with relaxed `fetch_add`, so a record racing a reset can be lost (no data race, but logically
racy). `scan_slots` is O(n) on every record.
**Fix:** expose an overflow counter; use a seqlock or acquire/release reset; document snapshot semantics.

### F17 — Low — TSS destructor limits and `call_once` recursion are delegated without checks
**Where:** `cplus_threads.h:44` (`TSS_DTOR_ITERATIONS 4`, matches this SDK's `PTHREAD_DESTRUCTOR_ITERATIONS 4`),
`threads_c11.c:344-381`.
**Evidence:** the constant is not cross-checked at compile time against the host's; `tss_create` does not enforce
`TSS_DTOR_ITERATIONS` or cap keys; `call_once` with the same flag from inside `func` deadlocks in `pthread_once`
(ISO C UB, undetected).
**Fix:** compile-time equality assert against host macro; document "no recursive call_once".

### F18 — Low — uchar's internal state is process-wide when `ps == NULL`
**Where:** `uchar.c:31,59,92` (`static unsigned internal_state`).
**Evidence:** concurrent `cplus_mbrtoc16(..., NULL)` calls from multiple threads share one variable. musl has the
same design and ISO C does not require thread-safety for the null-`ps` mode, but the project's concurrency surface
should state this explicitly.
**Fix:** make it `__thread` (portable TLS macro) or document the restriction.

### F19 — Low — complex/fenv wrappers rely entirely on host semantics for hostile inputs
**Where:** all of `complex.c`, `complex_extra.c`, `complex_inverse.c`, `fenv.c`, `fenv_extra.c`.
**Evidence:** no validation of NaN/Inf/overflow inputs or uninitialized `fenv_t*`; host sets errno and FP flags.
No FP-state save/restore is performed around the wrappers — but logging (`write`) does not touch FP state, so no
clobber was found. This is acceptable host delegation; the risk is documentation/ABI drift only.
**Fix:** document "host errno/FP flags pass through; inputs must be valid `double complex`/`fenv_t` objects".

### F20 — Medium — thread cancellation is unhandled across the pthread wrappers
**Where:** `thread.c:9-21`, `threads_c11.c` (all mutex/cond adapters).
**Evidence:** `pthread_cancel` is not wrapped or blocked. A cancelled thread holding `cplus_mtx_t` leaves it
locked forever; cancellation during `cplus_pthread_cond_wait` re-acquires the mutex as POSIX requires, but the
adapter does not expose cancellation points or cleanup handlers. C11 `<threads.h>` has no cancellation by design,
so this is a POSIX-side gap of the re-used wrappers.
**Fix:** document that C11 adapters do not support cancellation; provide `pthread_cleanup_push` guards in an
explicit POSIX extension if cancellation is needed; never cancel a thread that owns a `cplus_mtx_t`.

### F21 — Low — signal-safe logging still reads caller strings without lifetime guarantees
**Where:** `log.c:38-54` (`log_append` reads `*src` up to the cap).
**Evidence:** `write` + atomic flag + stack buffer make `cplus_libc_log` reentrancy-safe (improvement over the
old fprintf path), but a handler interrupting code that is mutating the `api`/`msg` string can log torn content.
The strings are normally static literals in these wrappers, so exposure is limited.
**Fix:** document that `api`/`msg` must be immutable NUL-terminated strings.

## 4. Per-family export table

Columns: **Signature** (export), **Backend**, **Checks present**, **Unsafe cases NOT handled** (→ finding IDs),
**Sev**, **Evidence / Recommendation** (short form; full detail in §3).

### 4.1 `libc/src/thread/thread.c` (11 exports)

| Signature | Backend | Checks | Unsafe cases NOT handled | Sev | Evidence / rec. |
|---|---|---|---|---|---|
| `int cplus_pthread_create(pthread_t*, const pthread_attr_t*, void*(*)(void*), void*)` | host pthread_create | log entry only | NULL thread/start, cancellation, attr lifetime; error code is pthread errno not C11 | Low | `thread.c:9`; add NULL guards or document host contract |
| `int cplus_pthread_join(pthread_t, void**)` | host pthread_join | log entry | double join, join self, detached thread → EDEADLK/EINVAL returned but not translated | Low | `:10` |
| `int cplus_pthread_mutex_init(pthread_mutex_t*, const pthread_mutexattr_t*)` | host | log entry | NULL mutex, re-init live mutex, stale attr | Low | `:12` |
| `int cplus_pthread_mutex_lock(pthread_mutex_t*)` | host | log entry | NULL, uninitialized/destroyed mutex, self-deadlock (non-recursive), cancellation while owning | Medium | `:13`; F20 |
| `int cplus_pthread_mutex_unlock(pthread_mutex_t*)` | host | log entry | unlock unowned/NULL mutex (UB) | Medium | `:14` |
| `int cplus_pthread_mutex_destroy(pthread_mutex_t*)` | host | log entry | destroy locked/busy mutex → EBUSY returned; double destroy UB | Medium | `:15` |
| `int cplus_pthread_cond_init(pthread_cond_t*, const pthread_condattr_t*)` | host | log entry | NULL cond, attr clock mismatch with C11 TIME_UTC | Low | `:17` |
| `int cplus_pthread_cond_signal(pthread_cond_t*)` | host | log entry | NULL, signal-before-wait lost wakeup (caller protocol) | Low | `:18` |
| `int cplus_pthread_cond_broadcast(pthread_cond_t*)` | host | log entry | NULL | Low | `:19` |
| `int cplus_pthread_cond_wait(pthread_cond_t*, pthread_mutex_t*)` | host | log entry | NULL, mutex not owned, spurious wakeup (caller must loop) | Low | `:20` |
| `int cplus_pthread_cond_destroy(pthread_cond_t*)` | host | log entry | destroy with waiters → EBUSY | Low | `:21` |

### 4.2 `libc/src/thread/threads_c11.c` (25 exports)

| Signature | Backend / mapping | Checks | Unsafe cases NOT handled | Sev | Evidence / rec. |
|---|---|---|---|---|---|
| `int cplus_thrd_create(cplus_thrd_t*, cplus_thrd_start_t, void*)` | wrapper + in-handle ctx | NULL thr/func; memset | **detached/stack-lifetime UAF (F1)**; no handle generation token; EAGAIN→BUSY (C11 allows ERROR/NOMEM) | **High** | `:143-159`; F1 |
| `int cplus_thrd_equal(cplus_thrd_t, cplus_thrd_t)` | pthread_equal | log only | uninitialized handles → garbage pthread_t compare | Low | `:161-168` |
| `cplus_thrd_t cplus_thrd_current(void)` | pthread_self | log, zero-init | result is a value handle, not joinable; misuse with join/equal | Low | `:170-179` |
| `int cplus_thrd_sleep(const timespec*, timespec*)` | cplus_nanosleep | NULL duration→-2; EINTR→-1 | invalid tv_nsec; `errno` dependence (F6 code note); cancellation point | Low | `:181-189` |
| `void cplus_thrd_yield(void)` | sched_yield | log only | none material | Info | `:191-195` |
| `void cplus_thrd_exit(int)` | pthread_exit + loop | log only | **cleanup registry not unwound (F9)**; TLS destructors are host-managed | Medium | `:197-202`; F9 |
| `int cplus_thrd_join(cplus_thrd_t, int*)` | cplus_pthread_join | log only | double join / self join / detached → mapped via errno; result cast int↔void* | Low | `:204-215` |
| `int cplus_thrd_detach(cplus_thrd_t)` | pthread_detach | log only | **triggers F1**; double detach → EINVAL | High | `:217-223`; F1 |
| `int cplus_mtx_init(cplus_mtx_t*, int)` | attr + wrapper | NULL, bad type bits | **timed flag silently discarded (F12)**; uninitialized on failure | Medium | `:226-244`; F12 |
| `int cplus_mtx_lock(cplus_mtx_t*)` | wrapper | NULL | F20; no owner tracking; double-lock non-recursive deadlock | Medium | `:246-251` |
| `int cplus_mtx_timedlock(cplus_mtx_t*, const timespec*)` | trylock+nanosleep loop | NULL args | **F12** (no timed flag, polling, invalid timespec spin, no generation token) | Medium | `:253-276`; F12 |
| `int cplus_mtx_trylock(cplus_mtx_t*)` | pthread_mutex_trylock | NULL | EAGAIN/EBUSY mapping; no timed flag enforcement | Low | `:278-283` |
| `int cplus_mtx_unlock(cplus_mtx_t*)` | wrapper | NULL | unlock unowned UB | Medium | `:285-290` |
| `void cplus_mtx_destroy(cplus_mtx_t*)` | wrapper | NULL | destroy while locked / waiters → EBUSY **ignored** (void return) | Medium | `:292-297` |
| `int cplus_cnd_init(cplus_cnd_t*)` | wrapper, NULL attr | NULL | clock is host default (CLOCK_REALTIME here) | Low | `:300-306` |
| `int cplus_cnd_signal(cplus_cnd_t*)` | wrapper | NULL | none material | Low | `:308-313` |
| `int cplus_cnd_broadcast(cplus_cnd_t*)` | wrapper | NULL | none material | Low | `:315-320` |
| `int cplus_cnd_wait(cplus_cnd_t*, cplus_mtx_t*)` | wrapper | NULL args | mutex ownership / spurious wakeup (caller loop) | Low | `:322-327` |
| `int cplus_cnd_timedwait(cplus_cnd_t*, cplus_mtx_t*, const timespec*)` | pthread_cond_timedwait | NULL args | CLOCK_REALTIME assumption; invalid timespec → EINVAL mapped to ERROR | Low | `:329-334` |
| `void cplus_cnd_destroy(cplus_cnd_t*)` | wrapper | NULL | destroy with waiters → EBUSY ignored (void return) | Medium | `:336-341` |
| `void cplus_call_once(cplus_once_flag*, void(*)(void))` | pthread_once | NULL args; ABI init image (F13) | recursive call deadlock; pthread_once rc discarded; F13 | Medium | `:344-350`; F13 |
| `int cplus_tss_create(cplus_tss_t*, void(*)(void*))` | pthread_key_create | NULL key | destructor-iteration count not cross-checked (F17); no key cap; destructor may call unsafe APIs | Low | `:353-363`; F17 |
| `void *cplus_tss_get(cplus_tss_t)` | pthread_getspecific | log only | invalid key → EINVAL but returns NULL indistinguishable from unset | Low | `:365-369` |
| `int cplus_tss_set(cplus_tss_t, void*)` | pthread_setspecific | log only | invalid key mapped to ERROR; destructor loops if value re-set | Low | `:371-375` |
| `void cplus_tss_delete(cplus_tss_t)` | pthread_key_delete | log only | values not cleared for existing threads; double delete error ignored | Low | `:377-381` |

### 4.3 `stdlib/safe/src/safe_runtime.c` (18 exports)

| Signature | Backend | Checks | Unsafe cases NOT handled | Sev | Evidence / rec. |
|---|---|---|---|---|---|
| `void cplus_runtime_set_handler(cplus_runtime_handler)` | global store | log + stat | **data race (F8)**; NULL handler silently accepted (report then skips) | Medium | `:38-42`; F8 |
| `void cplus_runtime_set_failstop(int)` | `volatile int` store | normalize 0/1 | **F8** (volatile ≠ atomic) | Medium | `:44`; F8 |
| `int cplus_runtime_get_failstop(void)` | volatile load | none | F8 | Medium | `:45`; F8 |
| `void cplus_runtime_set_violation_trap(int)` | volatile store | normalize | F8 | Medium | `:46`; F8 |
| `int cplus_runtime_get_violation_trap(void)` | volatile load | none | F8 | Medium | `:47`; F8 |
| `void cplus_runtime_violation(const char*, const char*)` | fprintf+fflush+longjmp/_exit | NULL→defaults; trap flag; guard present | F3 (RAII), F8, F10 (recursion), `fflush(NULL)` not signal-safe | High | `:49-61`; F3/F8/F10 |
| `void cplus_runtime_report(const char*)` | safe_log+stat+handler+fprintf | NULL→default | F8, F10, F11 (fprintf recursion); handler may be re-entered | Medium | `:63-72` |
| `int cplus_runtime_push_cleanup(void(*)(void*), void*)` | TLS LIFO | NULL drop, cap 64 → -1 | silent drop on overflow; no ownership validation of `self` | Low | `:74-83`; F16-like |
| `void cplus_runtime_pop_cleanup(int)` | TLS LIFO | underflow guarded | `execute` drop can itself unwind/re-enter | Low | `:85-92` |
| `int cplus_runtime_cleanup_mark(void)` | TLS | none | mark from another thread/thread-exit is meaningless | Low | `:94` |
| `int cplus_runtime_cleanup_depth(void)` | TLS | none | mark/depth conflation | Info | `:95` |
| `void cplus_runtime_cleanup_unwind_to(int)` | TLS LIFO | negative mark→0 | mark > depth silently no-op; reentrant drop can corrupt depth | Low | `:99-108` |
| `void cplus_runtime_unwind_cleanups(void)` | TLS | delegates to unwind_to(0) | F9 on thread exit | Medium | `:110`; F9 |
| `unsigned long cplus_runtime_leaked_cleanups(void)` | TLS | none | diagnostics only | Info | `:111` |
| `unsigned long cplus_runtime_unwound_cleanups(void)` | TLS | none | diagnostics only | Info | `:112` |
| `void cplus_runtime_reset_counters(void)` | TLS | none | diagnostics only | Info | `:113` |
| `int cplus_runtime_guard(int(*)(void*), void*)` | setjmp/longjmp | NULL body; nested frames; leak accounting | **F3 (RAII), F9 (TLS exit), F10 (recursion)**; setjmp frame local is unmodified so determinate | High | `:115-134` |
| `void __ubsan_on_report(void)` | UBSan hook | report + guard check + failstop | F3, F10; documented nested-UB abort (`RUNTIME_RECOVERY.md:43`); no recursion latch | High | `:140-149`; F3/F10 |

### 4.4 `stdlib/safe/src/safe_stats.c` (4 exports)

| Signature | Backend | Checks | Unsafe cases NOT handled | Sev | Evidence / rec. |
|---|---|---|---|---|---|
| `void cplus_safe_stat_record(const char*, int)` | atomics + reg mutex | NULL/empty ignored; bounded strncpy | **F11** (mutex on first registration, not signal-safe); cap 64 silent drop (F16) | Medium | `:33-56`; F11/F16 |
| `void cplus_safe_stats_reset(void)` | relaxed atomic stores | none | F16 (racy with record) | Low | `:58-65`; F16 |
| `int cplus_safe_stats_get(const char*, cplus_safe_stat*)` | atomic loads | NULL args →0 | relaxed snapshot can be torn | Low | `:67-78`; F16 |
| `size_t cplus_safe_stats_dump(cplus_safe_stat*, size_t)` | atomic loads | NULL out→0; max bound | relaxed snapshot; `max==0` returns 0 | Low | `:80-93`; F16 |

### 4.5 `stdlib/safe/src/safe_log.c` (3 exports)

| Signature | Backend | Checks | Unsafe cases NOT handled | Sev | Evidence / rec. |
|---|---|---|---|---|---|
| `void cplus_safe_log_set_level(cplus_safe_log_level)` | atomic store | none (config) | out-of-range enum accepted | Info | `:7-10` |
| `cplus_safe_log_level cplus_safe_log_get_level(void)` | atomic load | none | — | Info | `:11-13` |
| `void cplus_safe_log(cplus_safe_log_level, const char*, const char*)` | `fprintf(stderr)` | NULL→"(null)"; level filter | **F11** (not async-signal-safe, no recursion guard, no truncation, can deadlock under signal reentry) | Medium | `:14-21`; F11 |

### 4.6 `stdlib/safe/src/safe_except.c` (1 export)

| Signature | Backend | Checks | Unsafe cases NOT handled | Sev | Evidence / rec. |
|---|---|---|---|---|---|
| `void cplus_except_init(void)` | e4c + TLS context | none | TLS `cplus_context` lifetime on thread exit handled by e4c; calling init twice / from multiple threads is unsynchronized (e4c supplier global) | Low | `:13-23`; document one-time init |

### 4.7 `libc/log/log.c` (2 exports)

| Signature | Backend | Checks | Unsafe cases NOT handled | Sev | Evidence / rec. |
|---|---|---|---|---|---|
| `void cplus_libc_log_set_enabled(int)` | atomic store | normalize | — | Info | `:34-36` |
| `void cplus_libc_log(const char*, const char*)` | `write(2)`, 4096B stack buffer | atomic gate; NULL→"?"; **bounded truncation with "...\n" (4095B max)**; no allocation | **F6** errno clobber, **F7** short write/EINTR/SIGPIPE, **F21** caller string lifetime; stale test F15 | Medium | `:38-65`; F6/F7/F15/F21 |

### 4.8 Signal — `signal.c` (2) + `signal2.c` (10)

| Signature | Backend | Checks | Unsafe cases NOT handled | Sev | Evidence / rec. |
|---|---|---|---|---|---|
| `void(*cplus_signal(int, void(*)(int)))(int)` | host signal | log; host validates signo → SIG_ERR | handler must be a real function; BSD vs SysV restart semantics differ; unsafe in MT processes (use sigaction) | Low | `signal.c:4`; document MT caveat |
| `int cplus_raise(int sig)` | host raise | log; host validates signo | in MT process signal goes to calling thread; handler must be async-safe | Low | `:5` |
| `int cplus_sigemptyset(sigset_t*)` | host sigemptyset | log | **NULL set → crash (F4)** | Medium | `signal2.c:14-17`; F4 |
| `int cplus_sigfillset(sigset_t*)` | host sigfillset | log | **F4** | Medium | `:18-21` |
| `int cplus_sigaddset(sigset_t*, int)` | host sigaddset | log | **F4**; invalid signo delegated (EINVAL) | Medium | `:22-25` |
| `int cplus_sigdelset(sigset_t*, int)` | host sigdelset | log | **F4** | Medium | `:26-29` |
| `int cplus_sigismember(const sigset_t*, int)` | host sigismember | log | **F4**; invalid signo delegated | Medium | `:30-33` |
| `int cplus_sigaction(int, const struct sigaction*, struct sigaction*)` | host sigaction | log; host validates signo | sa_handler must be async-safe; SIGKILL/SIGSTOP silently rejected by host | Low | `:34-37` |
| `int cplus_sigprocmask(int, const sigset_t*, sigset_t*)` | host sigprocmask | log | invalid `how` delegated; NULL set with SIG_BLOCK → crash; MT use should be pthread_sigmask | Medium | `:38-41`; F4 |
| `int cplus_sigsuspend(const sigset_t*)` | host sigsuspend | log | NULL mask → EFAULT; handler safety | Low | `:42-45` |
| `int cplus_kill(pid_t, int)` | host kill | log; host validates | negative pid / permission errors propagated; async-signal-safe on POSIX (good) | Low | `:46-49` |
| `int cplus_sigwait(const sigset_t*, int*)` | host sigwait | log | NULL set/sig → UB/crash; not async-signal-safe | Medium | `:50-53`; F4 |

### 4.9 Complex — 66 exports (3 files, all host-delegated)

Common to every row: **Backend** = installed host `<complex.h>`/`libm`; **Checks** = `cplus_libc_log("name","enter")` only;
**Unsafe cases NOT handled** = hostile/NaN/Inf inputs are passed to host (F19); no FP-state save/restore (none needed for
logger); errno set only by host; cancellation/signal context not considered. **Sev = Low** for all.

| Signature(s) | Backend | Checks | Unsafe cases | Sev | Evidence / rec. |
|---|---|---|---|---|---|
| `double cplus_cabs/carg/creal/cimag(double complex)`; `double complex cplus_conj/cexp/csqrt(double complex)` | host libm | log | F19 | Low | `complex.c:8-14` |
| `double complex cplus_csin/ccos/ctan/csinh/ccosh/ctanh/clog/cproj(double complex)`, `cpow(double complex,double complex)` | host libm | log | F19 | Low | `complex_extra.c:9-17` |
| `float cplus_cabsf/cargf/crealf/cimagf(float complex)`; `float complex cplus_conjf/cexpf/csqrtf/csinf/ccosf/ctanf/csinhf/ccoshf/ctanhf/clogf/cprojf(float complex)`, `cpowf(float complex,float complex)` | host libm | log | F19 | Low | `complex_extra.c:20-35` |
| `long double cplus_cabsl/cargl/creall/cimagl(long double complex)`; `long double complex cplus_conjl/cexpl/csqrtl/csinl/ccosl/ctanl/csinhl/ccoshl/ctanhl/clogl/cprojl(long double complex)`, `cpowl(long double complex,long double complex)` | host libm | log | F19 | Low | `complex_extra.c:38-53` |
| `double complex cplus_cacos/casin/catan/cacosh/casinh/catanh(double complex)` | host libm | log | F19 | Low | `complex_inverse.c:11-16` |
| `float complex cplus_cacosf/casinf/catanf/cacoshf/casinhf/catanhf(float complex)` | host libm | log | F19 | Low | `:19-24` |
| `long double complex cplus_cacosl/casinl/catanl/cacoshl/casinhl/catanhl(long double complex)` | host libm | log | F19 | Low | `:27-32` |

`cadd`/`cmul` are intentionally absent (host does not provide them; documented in file headers).

### 4.10 fenv — 11 exports

| Signature | Backend | Checks | Unsafe cases NOT handled | Sev | Evidence / rec. |
|---|---|---|---|---|---|
| `int cplus_feclearexcept(int)` | host fenv | log | invalid except mask delegated | Low | `fenv.c:6` |
| `int cplus_fetestexcept(int)` | host fenv | log | invalid mask delegated | Low | `:7` |
| `int cplus_fegetround(void)` | host fenv | log | — | Low | `:8` |
| `int cplus_fesetround(int)` | host fenv | log | invalid rounding constant delegated | Low | `:9` |
| `int cplus_fegetenv(fenv_t*)` | host fenv | log | **NULL deref (F5)**; hostile object n/a (out-param) | Medium | `:10`; F5 |
| `int cplus_fesetenv(const fenv_t*)` | host fenv | log | **NULL (F5)**; hostile/uninitialized fenv_t can program trap bits (F19) | Medium | `:11`; F5/F19 |
| `int cplus_feraiseexcept(int)` | host fenv | log | may trap/SIGFPE if exception unmasked; invalid mask | Low | `fenv_extra.c:20-23` |
| `int cplus_fegetexceptflag(fexcept_t*, int)` | host fenv | log + **NULL→EINVAL** | invalid mask delegated | Low | `:28-32` |
| `int cplus_fesetexceptflag(const fexcept_t*, int)` | host fenv | log only | **NULL (F5)**; hostile flag object | Medium | `:35-38`; F5 |
| `int cplus_feholdexcept(fenv_t*)` | host fenv | log + **NULL→EINVAL** | — | Low | `:42-46` |
| `int cplus_feupdateenv(const fenv_t*)` | host fenv | log only | **NULL (F5)**; hostile env → re-raise arbitrary flags | Medium | `:50-53`; F5/F19 |

### 4.11 uchar — 4 exports

| Signature | Backend | Checks | Unsafe cases NOT handled | Sev | Evidence / rec. |
|---|---|---|---|---|---|
| `size_t cplus_mbrtoc16(char16_t*, const char*, size_t, mbstate_t*)` | musl-derived + host mbrtowc | NULL s→reset; NULL pc16→length-only; surrogate pending | **F2 (mbstate_t size mismatch)**, F18 (shared internal state), NULL `s` with `n==0` | **High** | `uchar.c:28-55`; F2/F18 |
| `size_t cplus_c16rtomb(char*, char16_t, mbstate_t*)` | musl-derived + host wcrtomb | NULL s→length query; lone-surrogate→EILSEQ | **F2**, F18; passes `ps=0` to host by design (state tracked in `*x`) | **High** | `:57-87`; F2 |
| `size_t cplus_mbrtoc32(char32_t*, const char*, size_t, mbstate_t*)` | musl-derived + host mbrtowc | NULL s/pc32 | **F2** (internal_state 4B as mbstate_t*), F18 | **High** | `:89-99`; F2 |
| `size_t cplus_c32rtomb(char*, char32_t, mbstate_t*)` | host wcrtomb | log only | host validates; F18 if ps==NULL | Low | `:101-104` |

### 4.12 errno — 1 export

| Signature | Backend | Checks | Unsafe cases NOT handled | Sev | Evidence / rec. |
|---|---|---|---|---|---|
| `int *__cplus_errno_location(void)` | `__thread int` (GCC/Clang) | none | **F14**: no install-header `errno` macro, so this TLS value is unused; `__thread` not MSVC-portable | Low | `errno.c:1-4`; F14 |

## 5. Special-focus cross-cut summary

| Concern | Status in audited families |
|---|---|
| **Thread cancellation** | Not handled (F20). No cleanup handlers; cancelled thread can leave `cplus_mtx_t` locked. C11 adapters intentionally have no cancellation. |
| **Signal async-signal-safety** | `cplus_libc_log` is now safe (write + atomic + stack buffer) — improvement over old fprintf path; F21 remains for caller strings. `cplus_safe_log`, `cplus_safe_stat_record` (first call), `cplus_runtime_violation/report` (`fprintf`, `fflush(NULL)`) are **not** safe (F10/F11). Signal wrappers themselves are setup/control functions and are not safe to call from handlers except `cplus_kill`. |
| **longjmp / RAII** | Broken by design; manual cleanup registry only (F3). Nested UBSan faults abort empirically (`RUNTIME_RECOVERY.md:43`). |
| **TLS lifetime** | Guards/cleanups correctly `__thread`; but not unwound on `pthread_exit`/`thrd_exit` (F9); uchar internal state is process-wide (F18); `__thread` is not MSVC-portable (F14). |
| **Duplicate close** | N/A — none of these families owns a file descriptor. `cplus_runtime_report` writes to fd 2 but never closes it. |
| **Recent-free cache heuristics** | N/A — none of these families owns a free-list/allocator. `safe_stats` uses an append-only registry, not a free cache. |
| **Lock ordering** | `safe_stats` registration mutex is never held while calling user code or stdio (no inversion found), but it is acquired from `stat_record`, which can be reached from signal handlers (F11). `cplus_mtx_destroy`/`cnd_destroy` silently discard EBUSY. `call_once` recursion deadlocks (F17). |
| **Logging recursion** | libc logger is reentrancy-safe; safe logger is not (F11); safe_runtime report/handler path has no recursion latch (F10). |

## 6. Runtime vs compiler vs caller ownership

**Runtime (this codebase — fix in `libc/`, `stdlib/safe/`):**
F1, F2, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F16, F18, F20, F21, plus stale test F15.

**Compiler (Agent 1 / driver — not in these files):**
F3's complete solution (destructor-aware unwinding / exception-based recovery; `-fsanitize-recover=all` instead of
`-fno-sanitize-recover=all` in `compiler/Driver/Driver.cpp` per `RUNTIME_RECOVERY.md:45-51`); F13's robust once-flag
initialization should be generated from the target SDK rather than hard-coded in a public header.

**Caller (document/contract — no code fix expected):**
Invalid `signo` values delegated to host (F4 host behavior); non-`mtx_timed` mutex passed to timedlock (F12
precondition); hostile `fenv_t`/complex inputs and NaN/Inf (F19); immutable `api`/`msg` strings for logging (F21);
one-time `cplus_except_init`; no recursive `call_once` (F17); not cancelling a thread that owns a mutex (F20);
using `sigaction` rather than `signal` in multithreaded processes.

## 7. Top 5 prioritized issues with concrete fixes and tests

1. **F1 (High) — detached-thread use-after-scope in `cplus_thrd_create`.**
   *Fix:* heap-allocate `cplus_thrd_ctx` in `cplus_thrd_create`; pass the heap pointer to `pthread_create`; the
   trampoline copies `{func,arg}`, calls `free(p)`, then invokes `func`. Keep only `pthread_t` in the public
   handle; map allocation failure to `CPLUS_THRD_NOMEM`.
   *Test:* worker blocks on a barrier for ≥50 ms before touching `arg`; caller detaches, returns from a frame
   whose stack is poisoned with `0xA5`, and sleeps; run under ASan and assert no UAF and correct `arg`.

2. **F2 (High) — 4-byte `unsigned` used as 128-byte `mbstate_t` in uchar conversions.**
   *Fix:* use a real `static mbstate_t` for the host-facing state; keep the pending surrogate in a separate word
   with `sizeof(unsigned) <= sizeof(mbstate_t)` asserted; never cast a non-`mbstate_t` object to `mbstate_t*`.
   *Test:* compile-time size assert; canary bytes around the static under a stateful locale with ASan; repeated
   `mbrtoc16`/`c16rtomb` surrogate round-trips with `ps == NULL`; make the internal state thread-local.

3. **F3 + F8 + F9 + F10 (High/Medium) — recovery subsystem: RAII gap, data races, TLS exit leak, recursion.**
   *Fix:* make `g_handler` and the two config flags atomic (or per-thread); add an `atomic_flag` reentrancy latch
   in `report`/`violation` that falls back to `_exit(70)` without stdio; register a `pthread_key` destructor for
   the cleanup stack and invoke it from the `thrd_exit` adapter; update `RUNTIME_RECOVERY.md` with the nested-UB
   limit as an accepted, tested boundary.
   *Test:* TSan two-thread config churn; handler that re-enters `cplus_runtime_report`; guarded body that pushes
   two cleanups and calls `cplus_thrd_exit` (assert both drops run).

4. **F4 + F5 (Medium) — missing NULL/invalid-argument hardening in signal and fenv families.**
   *Fix:* add `if (!p) { errno = EINVAL; return -1; }` to all 10 signal exports with pointer parameters and to
   `fegetenv`, `fesetenv`, `fesetexceptflag`, `feupdateenv`; add compile-time check that
   `TSS_DTOR_ITERATIONS == PTHREAD_DESTRUCTOR_ITERATIONS` (F17).
   *Test:* table-driven NULL-argument tests for all 14 functions asserting nonzero return and `errno == EINVAL`,
   no crash, under UBSan.

5. **F6 + F7 + F11 + F15 (Medium) — logging correctness and verification debt.**
   *Fix:* save/restore `errno` around `cplus_libc_log`; loop partial writes and EINTR with a bounded retry;
   re-route `cplus_safe_log` through the write-based logger (or document it as non-signal-safe); pre-register all
   stat names; update `test_logging_attack.c` A2 to expect 4095 bytes with a `...\n` tail and rewrite
   `logging_attack_NOTES.md` to mark A4/A5 as fixed with empirical evidence.
   *Test:* closed-fd errno-preservation probe; nonblocking-pipe short-write test; updated A2 plus exact-boundary
   truncation cases (4090/4091/4092 chars); signal-reentry harness rerun and recorded.

*Next tier (not top 5):* F12 timedlock flag enforcement/polling, F13 once-flag ABI fragility, F16 telemetry cap,
F14 errno stub, F19/F21 documentation contracts.

## 8. Closing note on evidence quality

No live-game/Wine or injected-DLL context applies to this repository. Findings are grounded in source inspection,
SDK header verification, existing test/notes review, and three `/tmp` probe programs. The most consequential
observations (F1, F2, F15) are reproducible with the concrete steps given above; the remaining findings are
static-analysis results with explicit line-number evidence. No source files were modified and no git index
operation was performed.

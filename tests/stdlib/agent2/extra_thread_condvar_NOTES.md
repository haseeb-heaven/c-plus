# `extra_thread_condvar.cp` — Agent 2 notes

Scope: close the previously UNTESTED `condvar` lifecycle in `stdlib/thread.hp`.
Only two files were authored: `extra_thread_condvar.cp` and this notes file.
No header, existing test, or other source file was edited. No `git add`/commit.

## Contract observed in `stdlib/thread.hp`

- `condvar()` allocates a `pthread_cond_t` with `stdlib_alloc` and calls
  `pthread_cond_init(cv, NULL)`; on init failure it releases and leaves `cv=NULL`.
- `is_valid()` → `cv != NULL`.
- `wait(mutex*)` → `-1` if `cv==NULL || m==NULL || m->handle==NULL`, else
  `pthread_cond_wait(...) == 0 ? 0 : -1`.
- `signal()` / `broadcast()` → `-1` if `cv==NULL`, else the pthread call's
  `== 0 ? 0 : -1` result.
- `drop()` → `pthread_cond_destroy`, `stdlib_release`, then `cv=NULL`.
- Zero-initialised handles are documented as safe no-ops. A **dropped** condvar
  has `cv=NULL`, i.e. exactly the zero-initialised state, so its methods are
  defined safe no-ops returning `-1` / `0` by the same null checks. This is why
  the post-drop cases below are asserted rather than skipped (no UB: the
  object's storage is still live; `drop()` is an ordinary method, not a C++
  destructor).

## Coverage

| Area | Tags |
|---|---|
| Construction / explicit destruction / double drop | `ca_*` |
| Constructor allocation-failure path (`stdlib_alloc_fail_at(1)`) | `nf_*` |
| Zero-init condvar + `wait(NULL)` / zero-mutex invalid-handle returns | `zb_*` |
| `wait()` with a real worker thread + mutex, woken by `signal()` | `sw_*` |
| `broadcast()` waking three blocked waiters | `bc_*` |
| Repeated `signal()`/`broadcast()` with no waiter | `rn_*` |
| Use after drop (defined no-op) | `ua_*` |
| Alloc/free balance, invalid-op counter, fd stability, N cycles complete | `g_*` |

Exact return values are asserted (`== 0` for successful pthread operations,
`== -1` for null/invalid handles), not merely "nonzero vs zero".

**Unique owned-variable names:** every owning local in `main()` has a distinct
name (`ca_cv`, `nf_cv`, `zb_cv`, `zb_valid_cv`, `sw_m`/`sw_cv`/`sw_e`/`sw_t`,
`bc_m`/`bc_cv`/`bc_e`/`bc_t0..2`, `rn_cv`, `ua_cv`/`ua_m`, `g_cv`), because the
C+ RAII lowering de-duplicates ownership by identifier within a function and
silently drops the destructor of a same-named variable in a sibling scope.

**Bounding / no hang:** a `SIGALRM` handler calls `_Exit(99)`. `alarm(3)` guards
the signal/wait section and `alarm(5)` guards the broadcast section; they are
cancelled with `alarm(0)` on completion. The runner's `timeout 30` is a second
independent bound. A wake-up defect therefore yields a deterministic non-zero
exit (99) rather than a hang. The final `EXTRA_THREAD_CONDVAR` line is only
printed if no watchdog fired.

## SKIP (with reason)

```
SKIP drop_with_waiter reason=POSIX_cond_destroy_with_waiters_is_UB_EBUSY_and_drop_frees_storage_without_sync
```

Reason: destroying a condition variable that has a blocked waiter is not a
defined contract. POSIX `pthread_cond_destroy` returns `EBUSY` / is undefined
here (see `docs/libc/POSIX_EXTENSION_INVENTORY.md` row for
`pthread_cond_destroy`: "Destroying a condvar with waiters is undefined"; and
`docs/libc/AUDIT_lifecycle.md`: "destroy with waiters → EBUSY").
`condvar::drop()` ignores the destroy result and unconditionally calls
`stdlib_release(this->cv)`, so a blocked waiter would be left referencing freed
storage. No safe way to exercise this from the public API exists, so the case
is skipped with the reason printed on stdout rather than silently omitted.

No real defect was reproduced; no header edit was made.

## Exact stdout (byte-exact, identical in every mode)

38 lines; 36 `<tag> 1` checks, 1 `SKIP` line, 1 summary line. stderr is empty
(0 bytes). Trailing newline present.

```
ca_construct_valid 1
ca_signal_no_waiter 1
ca_after_drop_invalid 1
ca_double_drop_safe 1
nf_construct_fails 1
nf_signal_noop 1
nf_bcast_noop 1
nf_wait_noop 1
zb_cv_invalid 1
zb_cv_signal 1
zb_cv_broadcast 1
zb_cv_wait 1
zb_cv_wait_null 1
zb_wait_null_m 1
zb_wait_zero_m 1
zb_double_drop_safe 1
sw_valid 1
sw_start 1
sw_join 1
sw_wait_rc 1
sw_ready 1
sw_signal_rc 1
bc_valid 1
bc_starts 1
bc_all_waiting 1
bc_broadcast_rc 1
bc_all_woke 1
SKIP drop_with_waiter reason=POSIX_cond_destroy_with_waiters_is_UB_EBUSY_and_drop_frees_storage_without_sync
rn_repeat_no_waiter 1
ua_invalid 1
ua_signal 1
ua_broadcast 1
ua_wait 1
g_cycles_complete 1
g_alloc_free_balanced 1
g_no_invalid_ops 1
g_fd_stable 1
EXTRA_THREAD_CONDVAR 36 0
```

## Build / run commands and results

All builds with `CPLUS_CLANG=/usr/bin/clang`, compiler `$ROOT/build/c+`, source
`tests/stdlib/agent2/extra_thread_condvar.cp`, each under `timeout 30`, working
directory the repository root. Output binaries were written outside the repo
(to the session temp dir). `RUN_RC` is the test process exit code.

### Required modes (run in this order)

1. **STRICT `--sanitize=none` (first)**
   ```
   timeout 30 ./build/c+ build tests/stdlib/agent2/extra_thread_condvar.cp --strict --sanitize=none -o <tmp>/etcv_none
   timeout 30 <tmp>/etcv_none
   ```
   Result: BUILD_RC=0, RUN_RC=0, stdout = block above, stderr 0 bytes.
   Final line `EXTRA_THREAD_CONDVAR 36 0`. **PASS.**

2. **default (`--default`)**
   ```
   timeout 30 ./build/c+ build tests/stdlib/agent2/extra_thread_condvar.cp --default -o <tmp>/etcv_default
   timeout 30 <tmp>/etcv_default
   ```
   Result: BUILD_RC=0, RUN_RC=0, stdout byte-identical to block above,
   stderr 0 bytes, final line `EXTRA_THREAD_CONDVAR 36 0`. **PASS.**

3. **UBSan (`--sanitize=undefined`)**
   ```
   timeout 30 ./build/c+ build tests/stdlib/agent2/extra_thread_condvar.cp --strict --sanitize=undefined -o <tmp>/etcv_ubsan
   timeout 30 <tmp>/etcv_ubsan
   ```
   Result: BUILD_RC=0, RUN_RC=0, stdout byte-identical to block above,
   stderr 0 bytes (no UBSan diagnostics), final line `EXTRA_THREAD_CONDVAR 36 0`.
   **PASS.**

### Additional sanity check (not one of the three required modes)

Plain driver invocation with no mode/sanitizer flag:
```
timeout 30 ./build/c+ build tests/stdlib/agent2/extra_thread_condvar.cp -o <tmp>/etcv_plain
timeout 30 <tmp>/etcv_plain
```
Result: BUILD_RC=0, RUN_RC=0, stdout byte-identical, stderr 0 bytes. **PASS.**

### Determinism / aggregation

- 5 consecutive STRICT-none runs: all exit 0, identical SHA-256 of stdout
  (`fd77de31afb2f3c9…`), 0 stderr bytes. Cross-mode SHA check (strict-none,
  default, UBSan) returned one distinct hash — all byte-identical.
- `sh tests/stdlib/agent2/run_all.sh --test extra_thread_condvar --timeout 30
  --build-timeout 90` classified the test `PASS` (build 0, run 0). The harness's
  generated result artifacts were removed afterwards so the authored footprint
  remains exactly the two files listed above.

## Defects found

None. Every check passed in all modes; no sanitizer diagnostic, no fd growth,
allocations balanced against releases across 32 condvar create/drop cycles, and
all bounded waits completed well inside the watchdog windows.

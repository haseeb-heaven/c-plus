# Threading safety / deadlock regressions (Task 3 Phase 5) — verification notes

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Repo root:** `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
**Compiler:** `build/c+` (prebuilt binary; NOT rebuilt by this task)
**Timeout utility:** `/usr/bin/timeout` (`-k 5 30` on every build and every run)
**File ownership:** only `tests/stdlib/system_safety/thread_*.cp` and this
`thread_NOTES.md` were created. **`stdlib/thread.hp` was NOT edited** (no library
defect was reproduced). No compiler, runtime, runner, or other source file was
touched. No `git add` / `git commit` was run.

---

## 1. Files created

| File | Purpose |
| --- | --- |
| `thread_lifecycle.cp` | mutex create/drop and idempotent drop; invalid unlock and repeated unlock (ERRORCHECK forked children, exact exit 107); guard unlock/repeated-unlock/drop/no-op guards; nested guards; partial-init cleanup (condvar allocation-failure injection + deterministic `thread::start` errors); 64 mutex+condvar create/drop cycles; fd and telemetry balance |
| `thread_contention.cp` | 6 bounded worker threads × 500 guarded increments with exact counter/checksum; 4 yielding contenders with exact checksum; stdlib `thread` wrapper start/join with exact increment; post-contention lock reusability |
| `thread_condvar.cp` | condvar `wait`/`signal`/`broadcast` with a one-waiter signal case and a three-waiter broadcast case, both predicate-based under the external mutex; zero-init condvar no-ops; SIGALRM watchdog |
| `thread_deadlock.cp` | two real deadlocks confined to forked children (raw self-relock, and re-lock against a live guard) detected by SIGALRM with **exact exit code 77**; healthy relock negative control exits 0; parent process remains responsive |
| `thread_NOTES.md` | this file |

All logs/binaries were written outside the source tree to
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/thread-safety/`.

---

## 2. Output protocol

Each check prints `<tag> <1|0>` (1 = pass). The final stdout line is always
`THREAD_SAFETY <checks> <failures>`; exit code is 1 when `<failures> > 0`, else 0.
`SKIP_REASON <name> <reason>` lines are informational and are **not** counted as
checks or passes. Fixed loop bounds, exact counters, and the shared runner's
30-second bound mean reaching the final line is the no-hang proof.

---

## 3. Requirement → coverage map

| Requirement | Test file | Representative tags |
| --- | --- | --- |
| mutex create/drop | lifecycle | `lc_mutex_created`, `lc_mutex_lock_unlock`, `lc_mutex_dropped`, `lc_mutex_double_drop_safe` |
| invalid unlock | lifecycle | `lc_invalid_unlock_trap` (fork, ERRORCHECK, exit 107) |
| repeated unlock | lifecycle | `lc_guard_repeated_unlock_safe` (guard is a no-op), `lc_repeated_unlock_trap` (raw mutex, fork, exit 107) |
| nested guards | lifecycle, contention | `lc_nested_outer`, `lc_nested_inner`, `lc_nested_inner_released`, `lc_nested_outer_released`, `ct_checksum_exact` |
| contention, bounded workers, exact counters | contention | `ct_all_workers_started`, `ct_counter_exact` (3000), `ct_checksum_exact` (93744), `ct_yield_contention_exact`, `ct_wrapper_join_exact` |
| thread creation failure | lifecycle | **SKIP** `thread_create_failure` (pthread_create resource failure not deterministically injectable); the two deterministic `thread::start` failures are covered by `lc_start_null_fn`, `lc_double_start_fails` |
| cleanup after partial init | lifecycle | `lc_partial_cv_invalid`, `lc_partial_cv_signal`, `lc_partial_cv_broadcast` (`stdlib_alloc_fail_at(1)` injects the condvar ctor allocation failure); `lc_start_null_fn`, `lc_start_after_failure` |
| condvar wait/signal/broadcast + watchdog | condvar | `cv_signal_wakes_waiter`, `cv_broadcast_wakes_all`, `cv_broadcast_rc`, `cv_zero_*`; process-wide `alarm(20)` |
| deadlock regression, forked child, SIGALRM, exact exit code | deadlock | `dl_raw_self_deadlock_exit`, `dl_guard_relock_deadlock_exit` (both == 77), `dl_healthy_child_exit` (== 0), `dl_parent_alive` |
| repeated create/drop cycles | lifecycle | `lc_cycles_complete` (64 mutex + 64 condvar cycles) |
| fd / telemetry balance | lifecycle, contention | `lc_cycles_telemetry_clean`, `lc_cycles_alloc_free_balanced`, `lc_cycles_fd_stable`, `ct_locks_reusable` |
| never run an unguarded race | all | every shared write is under a `guard`; the condvar `entered` flag uses `__atomic_*` and a bounded `sched_yield` spin; deadlocks/hangs are confined to forked children or bounded by SIGALRM |

---

## 4. Verification 1 — strict / no sanitizer (run FIRST)

Exact commands, from the repo root, with `OUT` set to the temp dir above:

```sh
OUT=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/thread-safety
mkdir -p "$OUT"
for t in thread_lifecycle thread_contention thread_condvar thread_deadlock; do
  timeout -k 5 30 build/c+ build tests/stdlib/system_safety/$t.cp \
    --strict --sanitize=none -o "$OUT/${t}_strict_none" \
    > "$OUT/${t}_strict_none.build.log" 2>&1
  echo "$t build_rc=$?"
  timeout -k 5 30 "$OUT/${t}_strict_none" </dev/null \
    > "$OUT/${t}_strict_none.out" 2> "$OUT/${t}_strict_none.err"
  echo "$t run_rc=$? final=$(tail -n 1 "$OUT/${t}_strict_none.out")"
done
```

Observed (all builds rc=0, all runs rc=0, all stderr 0 bytes):

```
thread_lifecycle     build_rc=0 run_rc=0 final=THREAD_SAFETY 28 0
thread_contention    build_rc=0 run_rc=0 final=THREAD_SAFETY 10 0
thread_condvar       build_rc=0 run_rc=0 final=THREAD_SAFETY 16 0
thread_deadlock      build_rc=0 run_rc=0 final=THREAD_SAFETY 4 0
```

**Strict/no-sanitizer total: 4/4 builds, 4/4 runs, 58 checks, 0 failures.**

---

## 5. Verification 2 — UBSan (run AFTER strict/no-sanitizer)

### 5.1 External blocker encountered first

The direct UBSan build initially failed for **every** test (including untouched
peer tests) with an external toolchain error, not a defect in these files or in
`stdlib/thread.hp`:

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23:
error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  129 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", ...
error: failed to compile instrumented C+ runtime
```

This is the known blocker already encoded in `tests/stdlib/system_safety/run.sh`:
the external `c-plus-core` compiler binary recompiles its instrumented runtime
from a source that references `CPLUS_EXIT_ARITHMETIC`, but this branch's
`runtime/include/cplus/cplus.h` does not define it. Per the file-ownership and
"no compiler/runtime edits" rules, **the runtime/header was not patched.**

The external core's matching runtime header
(`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include/cplus/cplus.h`)
*does* define `CPLUS_EXIT_ARITHMETIC = 110`. Pairing the prebuilt compiler with
its matching header via the compiler's supported `CPLUS_INCLUDE` environment
override resolves the blocker without any source edit. Setting
`CPLUS_RUNTIME_LIB` alone did **not** help (UBSan rebuilds the instrumentation
runtime regardless).

### 5.2 UBSan commands and results (with the supported override)

```sh
OUT=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/thread-safety
CORE_INC=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include
for t in thread_lifecycle thread_contention thread_condvar thread_deadlock; do
  CPLUS_INCLUDE="$CORE_INC" timeout -k 5 30 build/c+ build \
    tests/stdlib/system_safety/$t.cp --strict --sanitize=undefined \
    -o "$OUT/${t}_ubsan" > "$OUT/${t}_ubsan.build.log" 2>&1
  brc=$?
  timeout -k 5 30 "$OUT/${t}_ubsan" </dev/null \
    > "$OUT/${t}_ubsan.out" 2> "$OUT/${t}_ubsan.err"
  rrc=$?
  echo "$t build=$brc run=$rrc final=$(tail -n 1 "$OUT/${t}_ubsan.out") stderr=$(wc -c < "$OUT/${t}_ubsan.err")"
done
```

Observed (no UBSan diagnostics on stderr):

```
thread_lifecycle     build=0 run=0 final=THREAD_SAFETY 28 0 stderr_bytes=0
thread_contention    build=0 run=0 final=THREAD_SAFETY 10 0 stderr_bytes=0
thread_condvar       build=0 run=0 final=THREAD_SAFETY 16 0 stderr_bytes=0
thread_deadlock      build=0 run=0 final=THREAD_SAFETY 4 0 stderr_bytes=0
```

**UBSan total: 4/4 builds, 4/4 runs, 58 checks, 0 failures.**

### 5.3 Determinism (strict-none stdout == UBSan stdout, byte-exact)

`cmp -s` on each pair (after rebuilding `thread_contention` strict-none following
a cosmetic tag rename) reported identical output for all four tests. SHA-256:

```
4e2016a7cdf5389ea0f8facbbbaee7a947dd70f2807ff8a87ed374dffa609ff2  thread_lifecycle   (both modes)
841daf021c0b7996568dd4e600d47e212e60c10f095c813646afdef8c0b759d3  thread_contention  (both modes)
fcf7f68c970f20c4e0052b7c43e9c3e99499e4b4abb349e8ad92487dc428018a  thread_condvar     (both modes)
d8d1c62ba8b66d86dd22054bab79ad95e566d9f4f73c64c65eebae3226b40ede  thread_deadlock    (both modes)
```

---

## 6. Verification 3 — shared bounded runner (filtered to these files)

The shared runner auto-discovers all `*.cp` in the directory; `--only thread_`
restricts it to these four tests. It was **not** modified.

```sh
sh tests/stdlib/system_safety/run.sh --only thread_ --mode strict-none
CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include \
  sh tests/stdlib/system_safety/run.sh --only thread_ --mode strict-none,ubsan
CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include \
  sh tests/stdlib/system_safety/run.sh --only thread_ --mode default
```

Observed:

```
strict-none    thread_condvar           PASS           THREAD_SAFETY 16 0
strict-none    thread_contention        PASS           THREAD_SAFETY 10 0
strict-none    thread_deadlock          PASS           THREAD_SAFETY 4 0
strict-none    thread_lifecycle         PASS           THREAD_SAFETY 28 0
system_safety: 4 passed, 0 failed, 0 build-failures, 0 timeouts, 0 skipped, 0 blocked

strict-none/ubsan (8 invocations): 8 passed, 0 failed, 0 build-failures, 0 timeouts, 0 skipped, 0 blocked
default        (4 invocations):   4 passed, 0 failed, 0 build-failures, 0 timeouts, 0 skipped, 0 blocked
system_safety suite: PASS
```

---

## 7. SKIPs

| Case | File | Reason |
| --- | --- | --- |
| `thread_create_failure` (actual `pthread_create` resource failure) | `thread_lifecycle.cp` | Not deterministically injectable in-process. Emitted as `SKIP_REASON thread_create_failure ...`, never counted as a pass. The deterministic `thread::start` failure paths (NULL function, double start) are covered instead. |

No test invocation timed out; there were zero runner-level SKIP/BLOCKED cases
once the `CPLUS_INCLUDE` override was used for UBSan/default.

---

## 8. Defects / corrections during construction

**No library defect in `stdlib/thread.hp` was reproduced, so no header edit was
made.** Every scenario passed in strict/no-sanitizer, UBSan, and default modes
with empty stderr.

Issues encountered while authoring were all in the test/toolchain layer:

1. **External UBSan runtime blocker** (`CPLUS_EXIT_ARITHMETIC`, §5.1) — an
   out-of-scope mismatch between the prebuilt external compiler and this
   branch's runtime header. Worked around with the supported `CPLUS_INCLUDE`
   environment override; no source file edited. In an environment without that
   override the shared runner classifies these UBSan builds as BLOCKED, never
   as PASS.
2. **C+ RAII name de-duplication** (known finding F3 in the existing thread
   audit): every owning local in these files uses a unique name per function
   (`lc_*`, `cv_t1`..`cv_t4`, `ct_wa`/`ct_wb`, ...), and guard/mutex locals inside
   the 64-cycle loop are explicitly dropped so cleanup cannot be lost by loop
   lowering. This is a compiler constraint, not a `thread.hp` defect.
3. **Chained temporaries / pointer-parameter subscripts / variable
   division**: avoided by design — worker bodies copy `s->m` / `s->cv` to named
   locals before single-arrow calls, loop predicates use bitmasks (`& 7L`,
   `& 3L`) instead of `%`, and fd counting uses a bounded `fcntl` loop with no
   array indices.
4. **Lost-wakeup avoidance in condvar tests**: the waiter publishes an atomic
   `entered` flag before blocking and the main thread spins on it with a bounded
   iteration count before taking the same mutex and signalling, so the wait
   path is exercised deterministically rather than racing the signal.

Fatal/deadlock paths (exit 107 and exit 77) are strictly confined to forked
children with stderr redirected to `/dev/null`, so the parent harness always
keeps clean stderr and exits 0. No assertion was weakened and no header,
compiler, or runner behaviour was changed to make a check pass.

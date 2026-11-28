# Recoverable runtime errors (Agent 2 library surface)

`build/c+` safe mode currently hard-codes
`-fsanitize=undefined,bounds,null,alignment -fno-sanitize-recover=all`
(`compiler/Driver/Driver.cpp:464-465`), so a detected runtime error aborts the
process with a signal (SIGABRT/SIGTRAP). Agent 2 does not edit `compiler/**`.

## What Agent 2 provides (no compiler change needed)

`stdlib/safe/safe_runtime.{h,c}` + `scripts/build_safe_recover.sh`:

- `cplus_runtime_guard(body, ctx)` runs work under a recovery guard.
- `cplus_runtime_report(msg)` reports a runtime error (logged + stderr + telemetry).
- `__ubsan_on_report()` (UBSan recover-mode hook):
  - inside a guard → reports and `longjmp`s to the guard → guarded program
    continues with `CPLUS_RUNTIME_ERROR` (exit 0 if the program handles it);
  - no guard → reports and `_exit(70)` — a **controlled failure with a diagnostic,
    never a crash signal and never a silent continuation**.

Build a program with recoverable checks:

```sh
scripts/build_safe_recover.sh myprog.c out
./out   # OOB/null/etc: prints "[CPLUS-RUNTIME] runtime error: ..." then exits 70 (or unwinds to the guard)
```

## Verified

`sh tests/runtime/run_runtime_tests.sh` → **3 passed, 0 failed**:
- guarded OOB → `cplus_runtime_guard` returns `CPLUS_RUNTIME_ERROR` (no abort);
- unguarded OOB → exit **70** (no SIGABRT/SIGTRAP), diagnostic printed;
- diagnostic contains the runtime-error report.

## Agent 2 updates (cleanup stack, failstop, nested guard frames & honest limits)

`stdlib/safe/safe_runtime.h` and `stdlib/safe/src/safe_runtime.c` now also provide:
- `cplus_runtime_set_failstop(int enabled)` / `cplus_runtime_get_failstop(void)`: forces `_exit(70)` even inside a guard when corruption is suspected.
- Nested `guard_frame` stack with saved `base` cleanup depth.
- Thread-local LIFO cleanup registry (`cplus_runtime_push_cleanup`, `pop_cleanup`, `cleanup_mark`, `cleanup_unwind_to`, `unwind_cleanups`, `leaked_cleanups`, `unwound_cleanups`, `reset_counters`) so registered destructors execute in reverse order on unwind.

Verified by `sh tests/runtime/run_runtime_tests.sh` (**4 passed, 0 failed**, including `test_runtime_cleanup.c`).

Honest limitation observed on Apple Clang UBSan: UBSan sets a per-thread reentry guard before calling `__ubsan_on_report()`, so triggering a *second* UBSan fault on the same thread after `longjmp`-recovering from a first UBSan fault aborts with `UndefinedBehaviorSanitizer: nested bug in the same thread, aborting.` Because `longjmp` also skips stack frame destructors unless explicitly registered via `cplus_runtime_push_cleanup()`, recoverable mode should be used only for isolated recoverable boundaries rather than resuming after arbitrary memory corruption.

## Handoff to Agent 1 (to make `build/c+` output non-aborting by default)

In `compiler/Driver/Driver.cpp`, safe mode should use recoverable checks and link
the runtime handler, e.g. replace `-fno-sanitize-recover=all` with
`-fsanitize-recover=all`, and ensure `runtime/**` (or `stdlib/safe/src/safe_runtime.c`)
provides `__ubsan_on_report` so detected UB becomes a reported, controlled failure
(or a catchable C+ exception) instead of a signal.

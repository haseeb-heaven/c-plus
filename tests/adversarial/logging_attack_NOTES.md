# Logging attack — findings and honest limitations

Test: `tests/libc/test_logging_attack.c` (standalone C99, POSIX-guarded stderr
capture, 256 KiB hard cap with a nonblocking drainer thread).

Target: `libc/log/log.c` — `static atomic_int g_enabled`, `fprintf(stderr,
"[CPLUS-LIBC] %s: %s\n", api, msg)`, no allocation.

## Verdict

The core implementation held under almost all attacks. One real behavioral
defect was found (A5), and the logging path is **not async-signal-safe**
(A4, intermittent hard failures). Everything else held.

## FINDING A5 — logging silently dies after stderr is closed, even after dup2 restore

Exact behavior: after `dup(2); close(2); <logged calls with logging ON>;
dup2(saved, 2)`, the stderr `FILE` error indicator set by the failed writes to
closed fd 2 is still set. Subsequent `cplus_libc_log()` calls emit **0 bytes**
until the caller invokes `clearerr(stderr)`. The test verifies this by capturing
stderr immediately after the `dup2` restore and emitting a post-restore probe
line (captured length = 0). The test then calls `clearerr(stderr)` as test
hygiene so later attacks see a fresh stream.

Attribution: this is inherited from C stdio (`fprintf` on a stream whose error
indicator is set), not from `log.c`'s atomic flag or allocation logic. `log.c`
does not detect or clear the condition, so from a caller's perspective logging
silently stops working after a closed-stderr episode. Marked `FINDING:` because
it is a real, reproducible behavior change in the logging path.

## FINDING A4 — reentrant logging from a signal handler is unsafe

Attack: child process installs a `SIGALRM` handler that calls
`cplus_libc_log("sigalrm-handler", "reentrant-enter")` while the main thread is
inside instrumented `cplus_memcpy` calls (each of which calls `fprintf`). A
1 ms repeating `setitimer` produces ~70–150 handler entries per run. Parent
bounds the child to 3 s and classifies the outcome.

Observed over repeated runs (final build, 20 consecutive runs):
- **4/20 children terminated with SIGKILL (signal 9).**
- **1/20 children hung** and had to be killed after the 3 s ceiling.
- 15/20 completed with no observable failure.

Control experiment (identical 1 ms signal load, but the handler only increments
a counter and does **not** call `cplus_libc_log`): **20/20 clean**, no signals,
no hangs. This isolates the failures to the reentrant `fprintf` path, not to
the signal/timer harness itself. An earlier teardown race (a pending SIGALRM
delivered after `SIG_DFL` was restored, producing signal 14) was eliminated by
blocking SIGALRM, disarming the timer, discarding pending signals via
`SIG_IGN`, restoring the old handler and unblocking; signal 14 no longer occurs.

Honesty: `fprintf()` is **not async-signal-safe** per POSIX. Even a clean child
run does NOT prove async-signal-safety — output can be interleaved or the
`FILE` internal state corrupted when a handler interrupts `fprintf` on the same
thread. The implementation provides no alternative signal-safe logging path.
No claim of safety is made; the intermittent hard failures are reported as
findings.

## What held (no defect)

- **A1 NULL args.** `cplus_libc_log(NULL,NULL)`, `(NULL,"m")`, `("a",NULL)`
  produced exactly `[CPLUS-LIBC] ?: ?`, `?: m`, `a: ?` with no crash.
- **A2 100000-char api and msg.** Output was exactly 200016 bytes
  (`prefix + 100000 A's + ": " + 100000 B's + '\n'`), newline-terminated,
  no truncation, `cplus_alloc_live_count` unchanged across the call.
  Capture used a strict 256 KiB cap with a nonblocking drainer, so the test
  cannot block on pipe backpressure.
- **A3 sensitive data.** Password-like sentinel passed through `cplus_memcpy`
  and `cplus_snprintf("%s")` never appeared anywhere in the log stream; only
  `memcpy:`, `strlen:`, `snprintf:` entry markers were emitted.
- **A5 crash-free requirement.** Logging with fd 2 closed did not crash; fd 2
  was restored with `dup2` (the silent-stop issue above is separate).
- **A6 toggle storm.** 4 threads × 400 coordinated enable/disable cycles
  produced 1600 well-formed lines. Every line matched `^\[CPLUS-LIBC\] .*:
  .*$`, prefix count equalled line count (no merged/torn lines), no deadlock.
- **A7 return values.** 20 deterministic instrumented calls returned
  identical results with logging ON vs OFF.
- **A8 allocation.** `cplus_alloc_live_count` was identical before and after
  10000 direct `cplus_libc_log()` calls.

## Build / runtime verification

- Exact prescribed build+run command: clean compile, **no warnings**, exit 0.
- UBSan (`-fsanitize=undefined -fno-sanitize-recover=all`, library and test
  rebuilt instrumented): clean, no diagnostics, exit 0.
- Wall-clock: ~0.5 s nominal; A4 worst case adds ≤3 s, total well under 8 s.
- Output ends with `logging_attack: ok` (with a finding count when defects were
  observed).

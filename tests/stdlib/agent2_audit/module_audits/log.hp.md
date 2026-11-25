# Safety audit — `stdlib/log.hp` (+ `libc/log/log.c` telemetry backend)

Branch: `agent/modern-stdlib`. Module owner for this document: agent2.
Scope: the C+ logging facade `stdlib/log.hp`, the suites `tests/stdlib/log_test.cp`
and `tests/stdlib/deep_log_task5.cp`, and the separate C telemetry logger
`libc/log/log.c` exercised by `tests/libc/test_logging_telemetry.c`. These are
**two distinct logging systems**: the C+ facade uses libc `stdio` (`fopen`/
`fwrite`/`flockfile`); the C telemetry logger uses `write(2)` on fd 2 and is
where the errno/short-write fixes live. This document audits recorded evidence;
a passing test is not a memory-safety proof.

## Sources read (primary evidence)

| Item | Path |
| --- | --- |
| Header under audit | `stdlib/log.hp` |
| Facility contract | `docs/stdlib/DEEP_LOG_TASK5_CONTRACT.md` |
| Basic suite | `tests/stdlib/log_test.cp` |
| Deep Task-5 suite | `tests/stdlib/deep_log_task5.cp` |
| C telemetry implementation / public header | `libc/log/log.c`, `libc/log/log.h` |
| C telemetry regression test | `tests/libc/test_logging_telemetry.c` |
| Finding F6/F7 (defect origin) | `docs/libc/AUDIT_lifecycle.md` §F6 (lines 122-129), §F7 (lines 131-135) |
| Fix commit (errno + bounded write loop) | `5fe387d` "Fix HIGH/MEDIUM safety findings from systematic audit" |
| Earlier rewrite (fprintf → bounded write) | `ccad805` "Fix real defects found by adversarial libc testing" |
| Recorded run artifacts | `build/stdlib-deep/deep_log_task5_default.out`, `deep_log_task5_default.err`, `deep_log_task5_ubsan.out`, `build/libc-cov/test_logging_telemetry.out` |

Defect counts: `stdlib/log.hp` **0 confirmed**; `libc/log/log.c` **2 confirmed and
fixed** (F6 errno clobber, F7 short-write/EINTR loss — see §6). UNTESTED paths
listed in §9. No SKIPs in either logging suite (all 49 Task-5 checks are executed).

---

## 1. `stdlib/log.hp` public API surface (VERIFIED — lines 18-224)

Constants: `LOG_TRACE`=0, `LOG_DEBUG`=1, `LOG_INFO`=2, `LOG_WARN`=3,
`LOG_ERROR`=4, `LOG_FATAL`=5, `LOG_LEVELS`=6, `LOG_LINE_SIZE`=512.

| Function / member | Behaviour |
| --- | --- |
| `log_level_name(int)` | level→`TRACE/DEBUG/INFO/WARN/ERROR/FATAL`; anything else (incl. −1, 6, 99) → `"UNKNOWN"` |
| `log_level_parse(const char*, error*)` | case-insensitive **name-only** match via `strcasecmp`; NULL/numeric/unknown → −1, INVALID(1)/UNSUPPORTED(108) |
| `class log_sink`: `valid()`, `native()`, `level()`, `set_level(int)`, `enabled(int severity)` | `enabled()` = `valid() && severity >= threshold` |
| `write(severity, message, keys, values, field_count, file, line, error*)` | builds `stamp[32]` + `text[512]` on the stack, checks `snprintf` truncation at each step, emits under `flockfile`, `fwrite`, `fflush` |
| `info()`, `warn()`, `failure()` | convenience wrappers for LOG_INFO / LOG_WARN / LOG_ERROR |
| `drop()` | `fclose` only when `owns`; borrowed sinks are never closed; idempotent |
| `log_open_file(path, threshold, err)` | owned sink (`fopen(path,"a")`); NULL path → INVALID/UNSUPPORTED, unopenable path → IO(3)/IO(109) |
| `log_stderr(threshold)`, `log_stdout(threshold)` | borrowed sinks, `owns == 0` |

C telemetry API (`libc/log/log.h`): `void cplus_libc_log(const char *api,
const char *msg)` and `void cplus_libc_log_set_enabled(int enabled)` — atomic
on/off flag, OFF by default, no return value, no user-buffer logging.

---

## 2. Allocation paths

| Path | Backend | Notes |
| --- | --- | --- |
| `log_open_file` → `fopen(path,"a")` (`stdlib/log.hp:203`) | libc stdio FILE object | Not routed through `stdlib_alloc`; not reachable by `stdlib_alloc_fail_*` |
| `log_sink::write` stack buffers `char stamp[32]`, `char text[LOG_LINE_SIZE]` (lines 101-102) | stack, no heap | No allocation in the formatter |
| `time_format_iso8601` / `time_now_ns` (`stdlib/time.hp`) | stack `snprintf` | No allocation |
| `log_sink::drop` → `fclose` (line 187) | deallocation of owned FILE only | Borrowed sinks are never closed |
| `cplus_libc_log` (`libc/log/log.c`) | **no heap allocation**; `char line[CPLUS_LOG_CAP]` (4096) stack, direct `write(2)` | Confirmed by source; observable allocation count asserted by telemetry test §6 |

No allocation-failure SKIP applies to logging: neither facade nor telemetry
logger allocates through an injectable allocator. The telemetry test's §6
(`tests/libc/test_logging_telemetry.c:517-547`) asserts
`cplus_alloc_live_count()` is unchanged across 1000 direct log calls.
This is an observable-count check, not a proof that `write(2)` is allocation-free.

---

## 3. Level / threshold / message-length parity (all VERIFIED)

**Level name/parse parity** — `log_test.cp` spot checks (`level-name`, `level-parse`)
plus the exhaustive Task-5 matrix:

- All six names accepted case-insensitively (`t5_parse_valid_mixed_case`).
- Numeric strings `"0"`, `"3"`, `"5"`, `"-1"`, empty string and NULL → −1 with
  INVALID/UNSUPPORTED (`t5_parse_numeric_*`, `t5_parse_empty`, `t5_parse_null`).
  `_parse` compares names only; numeric levels are **not** parsed — a documented
  contract, not a defect.
- Levels 0..5 map to expected names; −1, `LOG_LEVELS` (6), 99 → `UNKNOWN`
  (`t5_name_all_levels`, `t5_name_out_of_range`).

**Threshold parity** — `deep_log_task5.cp` §4 (`t5_threshold_*`):

- With threshold `LOG_WARN`, writes at TRACE/DEBUG/INFO are suppressed yet
  return `STDLIB_OK` with a clear error; WARN/ERROR/FATAL are emitted.
- File read-back: exactly 3 well-formed newline-terminated lines; no `below_`
  marker; `at_warn`, `above_error`, `above_fatal` all present.
- Basic suite additionally checks filtering, structured fields (`user=42`,
  `attempt=3`), `set_level(LOG_DEBUG)` and method wrappers (`log_test.cp` lines
  30-58).

**Message-length parity** — `deep_log_task5.cp` §5 (`t5_len_*`):

- Empty, 255-byte and 256-byte messages: `STDLIB_OK`; file contains `M255-`
  and `M256-` markers.
- 4095-byte message: `STDLIB_FAIL`, BOUNDS(5)/CPLUS_EXIT_BOUNDS(101), error
  text contains `truncated`; **no partial line is flushed** (`t5_len_no_oversized`);
  a following write still succeeds (`t5_len_after_overflow`).
- Read-back: 4 well-formed lines, each strictly shorter than `LOG_LINE_SIZE`.

**Enabled/disabled parity** — `deep_log_task5.cp` §6: for each of `write`,
`info`, `warn`, `failure`, the disabled (threshold FATAL) and enabled
(threshold TRACE) calls have identical return value (`STDLIB_OK`), identical C+
error state (`ok()` true, code OK, category OK) and identical **libc `errno`**
(reset to 0 before each call, remains 0). Only enabled calls emit a line;
4 lines total (`t5_parity_write/info/warn/failure`, `t5_parity_lines`).

**Resource/cleanup parity** — `deep_log_task5.cp` §7: 32 open→info→drop cycles
produce exactly 32 lines and leave the open-fd count unchanged
(`t5_cycles_complete`, `t5_cycles_fd_stable`, `t5_cycles_lines`). Temp files
are PID-unique (`cplus_deep_log_task5_<pid>.log`), unlinked before each case and
at the end; `t5_cleanup_no_leftovers` asserts the final path is absent. Basic
suite verifies a dead borrowed sink does not close `stderr` (`stderr-open`,
fcntl `F_GETFD`) and that `log_open_file` of an unopenable path is a
recoverable IO failure (`open-fail`). `zero-sink` checks default construction
and double `drop()` inertness.

---

## 4. Test counts and recorded results (VERIFIED from artifacts)

| Suite | Expected | Recorded artifact | Result |
| --- | --- | --- | --- |
| `deep_log_task5.cp` | `LOG_TASK5 49 0` (49 checks, 0 failures) | `build/stdlib-deep/deep_log_task5_default.out`, `deep_log_task5_ubsan.out` | `LOG_TASK5 49 0`; `deep_log_task5_default.err` is empty |
| `log_test.cp` | print-based checks, no `RESULT` line | contract §Scope of `DEEP_LOG_TASK5_CONTRACT.md` | basic filtering/fields/RAII/borrowed/open-fail checks (no numeric count defined) |
| `test_logging_telemetry.c` | `logging_telemetry: ok` | `build/libc-cov/test_logging_telemetry.out` | `logging_telemetry: ok` |

Exact count: **LOG_TASK5 49**. The contract records 7 passing runs (4 direct
build+run invocations + 3 harness invocations) across strict+no-sanitize,
default, UBSan and strict+UBSan, with byte-identical stdout and empty stderr.
No runner registration for `test_logging_telemetry` by bare name was found in
`tests/libc/run_libc_tests.sh` or `tests/libc/coverage_libc.sh`; the compiled
binaries and the passing `.out` artifact exist under `build/libc/` and
`build/libc-cov/`. These results establish the asserted behaviours only.

---

## 5. `stdlib/log.hp` confirmed defects

**Confirmed defect count for the C+ facade: 0.** Every Task-5 case passed in
all modes; the contract's §4 classifies all runs as success. Deliberate
contracts (not defects): name-only `log_level_parse`; suppression returns OK;
oversized messages rejected atomically with BOUNDS; dead sinks are recoverable
with the exact category/code from the header.

---

## 6. `libc/log/log.c` — confirmed defects, fixes and regression evidence

Two real defects were found by the systematic audit and **fixed in commit
`5fe387d`** (the diff was read directly from git history). The earlier commit
`ccad805` replaced the original `fprintf(stderr, ...)` with a bounded stack
buffer + single `write(2)` to make logging reentrancy-safe and immune to a
stale stderr error indicator; that rewrite exposed F6/F7, which `5fe387d` then
closed.

**F6 — errno clobber (confirmed, fixed).**
Audit evidence: `docs/libc/AUDIT_lifecycle.md` lines 122-129 — `write(2)` sets
`errno` on failure (EBADF/EPIPE/EINTR/ENOSPC) while the header promises "never
changes errno"; a logged API could corrupt a later operation's errno.
Fix in `libc/log/log.c`: `int saved_errno = errno;` on entry (line 71),
`if (!atomic_load(&g_enabled)) goto done;` (line 77) and `done: errno =
saved_errno;` (lines 118-119) restore on **every** exit path, including the
disabled early-out.
**Regression evidence** — `tests/libc/test_logging_telemetry.c` §8 (lines
549-565): close fd 2 so `write(2)` fails with EBADF, set `errno = 4321`, call
`cplus_libc_log`, assert `errno == 4321` afterwards. Artifact
`build/libc-cov/test_logging_telemetry.out` contains `logging_telemetry: ok`.

**F7 — short writes / EINTR / EAGAIN dropped (confirmed, fixed).**
Audit evidence: `docs/libc/AUDIT_lifecycle.md` lines 131-135 — the original
`(void)write(...)` could silently lose a partial line on a nonblocking/full
pipe or after EINTR, letting a later handler call interleave.
Fix in `libc/log/log.c`: bounded write loop (lines 93-116) — `while (off < n)`
advances by the actual count `w`, retries EINTR and (non-Windows) EAGAIN/
EWOULDBLOCK with `sched_yield()`, resets the retry budget on progress, and
stops on a hard error / zero-byte write / `CPLUS_LOG_MAX_RETRIES` (100000) so
it can never spin forever. SIGPIPE disposition is deliberately left at the
process default.
**Regression evidence** — `tests/libc/test_logging_telemetry.c` §9 (lines
567-641): redirect fd 2 to a nonblocking pipe, fill it, open a 128-byte gap so
the first `write()` is a short write, drain concurrently, then assert the full
expected ~4000-byte line is present byte-for-byte
(`contains_bytes(..., g_expected, elen)`). Same passing artifact as above.

Supporting checks in the same file: default-OFF (§1), enable/disable (§2),
failure-path return-value parity (§3), no sensitive-data leak (§4),
4-thread concurrency (§5), no allocation growth (§6), hard 64 KiB capture cap.

**Residual note:** `CPLUS_LOG_MAX_RETRIES` is a liveness bound, not a delivery
guarantee — a permanently full nonblocking fd eventually stops with a partial
line. The audit accepts this as the documented bounded-retry contract; a
long-lived full pipe with a permanently stalled drainer is **UNTESTED**.

---

## 7. Resource cleanup (logging)

- Owned sinks (`log_open_file`) are closed exactly once by `drop()`;
  `owns` is cleared and `stream` NULLed, so repeated drops are inert.
- Borrowed sinks (`log_stderr`, `log_stdout`) have `owns == 0`; `drop()` never
  closes the process streams (`log_test.cp` `stderr-open` fcntl check).
- `write()` always `funlockfile`s: on `fwrite` shortfall it unlocks before
  returning the IO error (`stdlib/log.hp:160-169`).
- 32-cycle fd-stability check proves no descriptor leak on the open/write/drop
  path; PID-unique temp files leave no leftovers.
- Telemetry logger holds no descriptors it did not already have; it writes to
  the caller's fd 2 and never changes signal dispositions.

---

## 8. UNTESTED / residual paths

**`stdlib/log.hp` facade:**
1. Structured fields with NULL `keys`/`values` when `field_count > 0` (the guard
   at lines 112-116 exists but is not asserted), nor a NULL individual key
   (the `"?"` fallback at line 134 is untested).
2. `set_level()` to out-of-range thresholds (e.g. −1, 99) and parity of
   `enabled()` at exactly the boundary for all six severities — only WARN and
   TRACE/FATAL thresholds are exercised.
3. Truncation at the *field* and *source-location* `snprintf` steps (lines
   132-152); only the message-level 4095-byte rejection is tested.
4. `time_format_iso8601` failure path (`stamp[0]='\0'` fallback, lines 120-122).
5. `fwrite`/`fflush` failure path → IO/CPLUS_EXIT_IO (line 161-166); not
   injectable with the available tests.
6. `log_stdout` emission/contents (only `valid()` and `enabled(LOG_FATAL)` are
   checked in `log_test.cp`).
7. Concurrent writes from multiple threads to the same sink — `flockfile`
   locking is by design but no cross-thread interleaving test exists for the
   C++ facade (the C telemetry logger has one).
8. `log_open_file` with a path that is a directory or a permission-denied file.
9. Message containing embedded NUL bytes or multibyte/locale-specific content;
   `%`-format characters in a message (passed as `%s`, so safe, but untested).

**`libc/log/log.c` telemetry backend:**
10. Retry-budget exhaustion with a permanently full pipe (partial-line outcome).
11. `api == NULL` / `msg == NULL` inputs (the `"?"` fallback at
    `log.c:63` is not black-box asserted).
12. SIGPIPE delivery on a closed pipe reader — documented as unchanged default
    behaviour, not asserted.
13. Windows `_write` path (all recorded evidence is POSIX/macOS).
14. Async-signal-safety is explicitly a design goal, not a formally proven
    property (`log.c:7-9`); no signal-handler invocation test exists.

**Bottom line:** 0 confirmed defects in `stdlib/log.hp`; 2 confirmed-and-fixed
defects in `libc/log/log.c` (F6 errno preservation, F7 bounded write loop) with
regression assertions in `tests/libc/test_logging_telemetry.c` §8/§9 and the
passing artifact `build/libc-cov/test_logging_telemetry.out`. Exact suite count:
**LOG_TASK5 49**. No SKIPs. Passing tests do not establish absence of
memory-safety defects on the untested paths listed above.

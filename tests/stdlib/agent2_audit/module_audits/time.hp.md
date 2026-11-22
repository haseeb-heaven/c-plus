# Safety audit — `stdlib/time.hp`

Audit scope: header `stdlib/time.hp` (87 lines) and the time portions of the
two independent deep suites that exercise it:

| Evidence | Exact path | Result |
|---|---|---|
| Deep behavioural suite (54 checks total; 16 time checks + 1 interrupted-sleep check) | `tests/stdlib/deep_fs_time.cp:197-324` / `.expected` | `deep_fs_time: 54 checks, 0 failures` |
| Task-5 gap suite (8 time checks inside the 35-check total) | `tests/stdlib/deep_fs_task5.cp:380-449` / `.expected` | `FS_TASK5 35 0` |
| Contract for suite 1 | `docs/stdlib/DEEP_FS_TIME_CONTRACT.md:74-107` | table of exact ISO-8601 outputs and clock/sleep properties |
| Contract for suite 2 | `docs/stdlib/DEEP_FS_TASK5_CONTRACT.md:147-164` (section F) | boundary/sampling results |
| Historical hardening | git `df3bc7b` (`git show df3bc7b -- stdlib/time.hp`) | EINTR resume, negative-timespec guards, range guard, category split added |

Both suites were built and run in default Safe/UBSan, `--strict
--sanitize=none`, and explicit `--sanitize=undefined` modes with rc 0, empty
stderr and byte-identical stdout (`DEEP_FS_TIME_CONTRACT.md:10-19`,
`DEEP_FS_TASK5_CONTRACT.md:12-22`).

**Evidence standard:** the suites assert return values, exact formatted
strings, error categories/codes and elapsed-time bounds. They are not proof of
memory safety or absence of UB; ASan is unavailable on this host
(`docs/LIMITATIONS.md:139-141`). Clean UBSan builds cover only instrumented
paths. No memory-safety claim is made from passing tests.

## 1. Public API surface

All functions are `static inline`, stateless, and use `error *` only where a
signature provides it (`err == NULL` is tolerated).

| Symbol (path with line) | Signature | Result protocol |
|---|---|---|
| `time_now_ns` (`stdlib/time.hp:15`) | `unsigned long long time_now_ns(void)` | `clock_gettime(CLOCK_REALTIME)`; `0` on failure or negative timespec |
| `time_now_ms` (`stdlib/time.hp:25`) | `unsigned long long time_now_ms(void)` | `time_now_ns()/1e6` |
| `time_monotonic_ns` (`stdlib/time.hp:30`) | `unsigned long long time_monotonic_ns(void)` | `clock_gettime(CLOCK_MONOTONIC)`; `0` on failure or negative timespec |
| `time_monotonic_ms` (`stdlib/time.hp:40`) | `unsigned long long time_monotonic_ms(void)` | `time_monotonic_ns()/1e6` |
| `time_sleep_ms` (`stdlib/time.hp:44`) | `void time_sleep_ms(unsigned ms)` | blocks ≥ `ms` (best effort); retries `EINTR` with `rem` |
| `time_format_iso8601` (`stdlib/time.hp:54`) | `int time_format_iso8601(unsigned long long unix_ns, char *buf, size_t size, error *err)` | `STDLIB_OK` or `STDLIB_FAIL` with error record; output always `YYYY-MM-DDTHH:MM:SSZ` + NUL (21 bytes) |

Error categories/codes (`stdlib/error.hp:39-54`):
`INVALID`(1)/`UNSUPPORTED`(108), `RANGE`(2)/`BOUNDS`(101),
`BOUNDS`(5)/`BOUNDS`(101). Deliberate asymmetry: `size == 0` → INVALID;
`size` 1..20 → BOUNDS (`DEEP_FS_TIME_CONTRACT.md:93-95`).

## 2. Resource paths (fd / socket / temp dir)

| Resource | Acquisition and lifetime | Evidence |
|---|---|---|
| File descriptors | **None.** No open/read/write/close in `stdlib/time.hp`. | fd-count checks in the fs suite (`deep_fs_task5.cp:333-366`) cover the shared process; time itself adds no fds. |
| Sockets | **None.** | n/a |
| Temp dirs / files | **None.** All output goes to a caller-owned `char *buf`. | `deep_fs_time.cp:199, 230-258`; `deep_fs_task5.cp:390-415` |
| Kernel resources | `clock_gettime` (2 clock IDs), `nanosleep`, `gmtime_r`, `strftime`; `sigaction`/`setitimer` in the forked interruption child. | `stdlib/time.hp:17, 32, 49, 72, 78`; `deep_fs_time.cp:283-295` |
| Shared global state | The optional `stdlib_error_callback` (`stdlib/error.hp:163-165`) is used by fs tests, not by time tests; time calls only `error_reset`/`error_fail_cat`, which are no-ops on `err == NULL`. | `stdlib/error.hp:581-624` |
| Forked child in interruption test | `fork()` child installs `SIGALRM` handler, arms a one-shot 50 ms `setitimer`, sleeps 300 ms, `fflush`es and `_exit`s; parent uses 5 s `waitpid(WNOHANG)` watchdog with `SIGKILL` fallback. | `deep_fs_time.cp:277-312` |

No artifact survives either suite (`DEEP_FS_TIME_CONTRACT.md:17`,
`DEEP_FS_TASK5_CONTRACT.md:214-218`).

## 3. Input-validation matrix

Legend **V** = VERIFIED by named check/tag; **D** = documented consequence but
not a reachable/executed failure; **U** = untested (§7). Codes as in §1.

| Input class / example | Expected result | Status & exact evidence |
|---|---|---|
| `buf == NULL` | FAIL/`INVALID`(1)/UNSUPPORTED(108); buffer not written | **V** `deep_fs_time.cp:260-263` (`time.null-buffer`); `deep_fs_task5.cp:390-396` (`FS5_TIME_NULL_ZERO_RESET`) |
| `size == 0` (non-NULL buffer) | FAIL/`INVALID`(1)/UNSUPPORTED(108); `buf[0]` untouched (`'Z'` sentinel preserved) | **V** `deep_fs_time.cp:229-236` (`time.buffer-zero`); `deep_fs_task5.cp:397-403` |
| `size` 1, 4, 20 (truncated) | FAIL/`BOUNDS`(5)/BOUNDS(101); `buf[0] = '\0'` | **V** `deep_fs_time.cp:237-248` (`time.buffer-one`, `-four`, `-truncated`); `DEEP_FS_TIME_CONTRACT.md:89` |
| `size == 21` (exact fit) | OK; exactly 20 chars + NUL at index 20; adjacent guard byte untouched | **V** `deep_fs_time.cp:249-252` (`time.buffer-exact-21`); `deep_fs_task5.cp:405-415` (`FS5_TIME_EXACT21_GUARD`, `guard == 0x5a`) |
| `size == 32` (roomy) | OK; sentinel at index 21 untouched | **V** `deep_fs_time.cp:253-258` (`time.buffer-roomy-no-overrun`) |
| `err == NULL` | tolerated; `STDLIB_OK` | **V** `deep_fs_time.cp:264-265` (`time.null-error`) |
| Error record reset after failure | following valid call returns OK and `err.ok()` | **V** `deep_fs_task5.cp:390-403` (`FS5_TIME_NULL_ZERO_RESET`) |
| `0` ns, `1` ns, `999999999` ns | OK; `"1970-01-01T00:00:00Z"` (fractional ns truncated, not rounded) | **V** `deep_fs_time.cp:203-208` (`time.epoch-zero`, `time.sub-second-truncated`); `deep_fs_task5.cp:381-383` (`FS5_TIME_ONE_NS`) |
| `1000000000` ns | OK; `"1970-01-01T00:00:01Z"` | **V** `deep_fs_time.cp:209-211`, `deep_fs_task5.cp:400-402` |
| `1700000000000000000` ns | OK; `"2023-11-14T22:13:20Z"` | **V** `deep_fs_time.cp:212-214` (`time.known-value`) |
| `ULLONG_MAX` ns | OK; exact `2554-07-21T23:34:33Z` on this 64-bit host | **V** `deep_fs_time.cp:220-224` (well-formed), `deep_fs_task5.cp:385-388` (exact string) |
| `(unsigned long long)(-1)` and `-86400000000000LL` ns | OK; wraps to large positive future date, not rejected | **V** `deep_fs_time.cp:225-227`; `deep_fs_task5.cp:417-422` (`FS5_TIME_NEGATIVE_WRAP`) — **documented non-defect** of the unsigned-ns API (`DEEP_FS_TIME_CONTRACT.md:87`, `DEEP_FS_TASK5_CONTRACT.md:155, 159-164`) |
| `secs_u > LONG_MAX` guard (`stdlib/time.hp:65`) | would FAIL/RANGE(2)/BOUNDS(101) with `buf[0]='\0'` | **D** unreachable on this host (`ULLONG_MAX/1e9 ≈ 1.84e10 < LONG_MAX`); `DEEP_FS_TASK5_CONTRACT.md:161-164` — see §7 |
| `gmtime_r` failure branch (`stdlib/time.hp:72`) | would FAIL/RANGE(2)/BOUNDS(101) with `buf[0]='\0'` | **D** unreachable for the tested range; see §7 |
| `time_sleep_ms(0)` | returns promptly (< 100 ms) | **V** `deep_fs_task5.cp:424-433` (`FS5_TIME_SLEEP_ZERO`) |
| `time_sleep_ms(1)` | ≥ 0.5 ms and < 100 ms | **V** `deep_fs_task5.cp:429-435` (`FS5_TIME_SLEEP_ONE_MS`) |
| `time_sleep_ms(20)` | monotonic elapsed ≥ 10 ms | **V** `deep_fs_time.cp:271-274` (`time.monotonic-order`) |
| `time_now_*`/`time_monotonic_*` positivity | all four > 0 | **V** `deep_fs_time.cp:267-270` (`time.clocks-positive`) |
| Monotonic non-decreasing | 2-sample ordering and 100 consecutive samples never decrease | **V** `deep_fs_time.cp:271-274`; `deep_fs_task5.cp:438-449` (`FS5_TIME_MONOTONIC_100`) |
| `time_sleep_ms((unsigned)-1)` (~49 days) | would block; deliberately not invoked | **D** `DEEP_FS_TASK5_CONTRACT.md:159-160` — see §7 |

No time input in either suite produced a `cplus_fatal` code or process
termination; all boundary failures are recoverable.

## 4. Interruption / timeout behavior

- **`time_sleep_ms` EINTR resume (VERIFIED).** `stdlib/time.hp:44-51` uses
  `nanosleep(&request, &rem)` and loops `while (... == -1 && errno == EINTR)
  request = rem;`. The contract test forks a child, installs a non-SA_RESTART
  `SIGALRM` handler, arms a one-shot 50 ms `setitimer`, then calls
  `time_sleep_ms(300)`; the child exits 0 only if the alarm was observed and
  elapsed monotonic time ≥ 200 ms (measured ~301 ms). Parent watchdog: 100 ×
  50 ms = 5 s, then `SIGKILL` (`deep_fs_time.cp:277-312`, check
  `time.sleep-eintr-resume` at line 320; evidence
  `DEEP_FS_TIME_CONTRACT.md:101-106`).
- **No timeout parameter** exists in the API. Bounds are test-side: `timeout 30`
  wrappers around each binary; the internal 5 s sleep watchdog.
- `clock_gettime`/`gmtime_r`/`strftime` have no retry logic; no signal
  injection is applied to them.
- **UNTESTED:** `nanosleep` returning an error other than `EINTR` (loop exits;
  no error record because the API is `void` — inherently unobservable);
  repeated/multiple interrupts within a single sleep (only one 50 ms signal is
  delivered); clock adjustments while `time_now_ns` is read.

## 5. Cleanup evidence (VERIFY)

- No descriptors, sockets, files or directories are allocated by
  `stdlib/time.hp`; the only child process is reaped with a bounded `waitpid`
  loop and `SIGKILL` fallback (`deep_fs_time.cp:300-311`).
- The interruption child `fflush`es both streams and writes nothing, so stdout
  stays deterministic (`deep_fs_time.cp:280-282`,
  `DEEP_FS_TIME_CONTRACT.md:105-106`).
- All task-5 time fixtures are stack buffers; the exact-fit guard test uses an
  adjacent sentinel and asserts it is unchanged (`deep_fs_task5.cp:405-415`).
- stderr expected files are empty: `tests/stdlib/deep_fs_time.stderr.expected`,
  `tests/stdlib/deep_fs_task5.stderr.expected`. No temp artifact is reported by
  either contract (`DEEP_FS_TIME_CONTRACT.md:17`,
  `DEEP_FS_TASK5_CONTRACT.md:209-218`).

## 6. Confirmed defects fixed (reproducer / regression)

### 6.1 Header defects

- **Open header defects found in this audit: 0.** Both contracts explicitly
  state no source defect was confirmed in `fs.hp`/`time.hp` and no header was
  modified (`DEEP_FS_TIME_CONTRACT.md:21-23`,
  `DEEP_FS_TASK5_CONTRACT.md:206-207`).
- Historical hardening with in-suite regression coverage
  (`git show df3bc7b -- stdlib/time.hp`):

| Historical defect | Reproducer before fix | Regression now in suite |
|---|---|---|
| `time_sleep_ms` used `nanosleep(&request, NULL)`, so a signal returned early and the caller slept less than requested. | A 50 ms `SIGALRM` during a 300 ms request returns after ~50 ms. | `time.sleep-eintr-resume` (`deep_fs_time.cp:277-312, 320`) asserts full duration; `DEEP_FS_TIME_CONTRACT.md:101-106` |
| Negative `tv_sec`/`tv_nsec` from a clock could wrap into a huge `unsigned long long`. | Host-dependent; guard added at `stdlib/time.hp:19-20, 34-35`. | Positive-value checks only (`deep_fs_time.cp:267-274`); negative-timespec path itself remains untestable on this host (§7) |
| `secs_u` cast to `time_t` without a range check; `gmtime_r` failure left `buf` unspecified. | Guard + `buf[0]='\0'` added at `stdlib/time.hp:65-77`. | Buffer ladder (`deep_fs_time.cp:229-258`); range branch unreachable on 64-bit (§7) |
| Truncation reported with mixed category; buffer not explicitly cleared. | Category split introduced (INVALID for 0, BOUNDS for 1..20) and `buf[0]='\0'` on every failure. | `time.buffer-zero`, `-one`, `-four`, `-truncated`; `FS5_TIME_NULL_ZERO_RESET` |

### 6.2 Test-side defects

- **0 documented.** Neither time contract records a test-authoring bug
  analogous to the net EOF/brace incidents or the fs `strlen` draft. The
  "wrapped negative" and "unreachable guard" behaviours were adopted as
  documented consequences rather than patched (`DEEP_FS_TIME_CONTRACT.md:87`,
  `DEEP_FS_TASK5_CONTRACT.md:155-164`).

### 6.3 Documented non-defects

1. Unsigned-nanoseconds API: negative caller values wrap to a large positive
   and format as a well-formed future date; not rejected
   (`DEEP_FS_TIME_CONTRACT.md:87`, `DEEP_FS_TASK5_CONTRACT.md:155`).
2. `secs_u > LONG_MAX` and `gmtime_r` failure guards are effectively
   unreachable on this 64-bit host (`DEEP_FS_TASK5_CONTRACT.md:161-164`).
3. `size == 0` → INVALID vs `size` 1..20 → BOUNDS asymmetry is deliberate
   (`DEEP_FS_TIME_CONTRACT.md:93-95`).

**Defect counts (time):** header defects fixed in audited version **0**;
historical header hardening items with regression coverage **4**; test-side
defects fixed **0**; open defects **0**; documented non-defects **3**.

## 7. Remaining untested paths (UNTESTED — not claimed verified)

1. `secs_u > LONG_MAX` range-failure branch (`stdlib/time.hp:65-70`) —
   unreachable on a 64-bit host, so never executed.
2. `gmtime_r == NULL` failure branch (`stdlib/time.hp:72-77`) — unreachable for
   the tested values; never executed.
3. `clock_gettime` failure (`CLOCK_REALTIME`/`CLOCK_MONOTONIC`) returning 0 —
   not injectable in the suites.
4. Negative `tv_sec`/`tv_nsec` from a clock — guard present but untestable
   with real clocks.
5. `time_sleep_ms((unsigned)-1)` (~49-day sleep) — deliberately not invoked
   (`DEEP_FS_TASK5_CONTRACT.md:159-160`).
6. Non-`EINTR` `nanosleep` failures, and multiple successive interrupts in one
   sleep — API is `void`, so no observable failure path.
7. `time_format_iso8601` with `buf` pointing to read-only memory or an
   overlapping/aliased `err` buffer — untested.
8. Thread-safety of concurrent `time_format_iso8601` calls sharing the
   `stdlib_error_callback` global (`stdlib/error.hp:194-222`) — untested.
9. Wall-clock jumps (CLOCK_REALTIME adjustments) during `time_now_ms` —
   untested; the monotonic tests use CLOCK_MONOTONIC only.
10. Output exactness at year-2038 and other `time_t` boundary values reachable
    through 32-bit `time_t` builds — untested on this 64-bit host.

## 8. SKIPs

- None. Neither `tests/stdlib/deep_fs_time.expected` nor
  `tests/stdlib/deep_fs_task5.expected` contains a `SKIP` record; every time
  case executed and passed in all three build modes. (The only documented skip
  *policy* in the shared fs/time programme is the root-only permission case in
  `deep_fs_task5.cp:255-257`, which is an fs case and did not trigger.)

## 9. What this audit does and does not establish

VERIFIED: exact ISO-8601 output strings and truncation semantics; the full
buffer-size ladder 0/1/4/20/21/32/NULL with exact categories; unsigned-wrap
representation; sleep(0/1/20 ms); monotonic non-decrease over 100 samples; and
full-duration sleep after a real `SIGALRM` interruption in a forked child with
a bounded watchdog.

UNTESTED / NOT ESTABLISHED: the branches in §7; memory safety or freedom from
UB in the header or compiler lowering. Passing suites are behavioural evidence
only.

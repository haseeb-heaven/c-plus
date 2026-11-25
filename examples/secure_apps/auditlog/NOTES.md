# Secure audit-log example (`examples/secure_apps/auditlog/`)

## Teaching model — NOT a safety proof

This is a **teaching example of defense-in-depth input validation around the
additive `cplus_violation_log_*` API**, not a formal proof and not a production
audit subsystem. What it shows is **observed runtime evidence**:

- hostile inputs are rejected by explicit checks and reported as `REFUSED`
  lines carrying the error code actually observed from the API;
- accepted events produce fixed-format, append-only lines that are read back
  and compared byte-for-byte against expected text;
- the whole transcript is a pure function of static fixtures plus the argv
  path — no clock, no randomness, no real stack/heap addresses.

The checks are ordinary C99 conditionals. They demonstrate that these specific
inputs are refused on this toolchain; they do **not** establish absence of all
memory errors, do not prove the hardened libc is formally correct, and do not
constitute a security certification. The hardening properties (bounded line
formatting, mutex-serialized `O_APPEND` writes, no caller-memory writes) live in
`libc/src/runtime/violation_log.c`; this app only exercises them and adds its
own validation layer.

### Honest note on the "oversized kind" refusal

The library does **not** return an error for an over-long `kind`: it truncates
the value to 96 bytes at format time (`VIO_KIND_CAP`). It cannot legitimately be
reported as an API error. Silent truncation would corrupt an audit record, so
this **application** refuses kinds longer than 96 bytes *before* calling the
API. Its `REFUSED ... code=APP_POLICY api_ret=n/a` line is therefore an
app-level policy refusal, not an `errno` from `cplus_violation_log_event`.
The other six refusals are genuine API codes: `EINVAL` (NULL/empty path,
NULL/empty kind) and `EBADF` (event while the log is closed).

No `git add`, commit, or edit to any file outside
`examples/secure_apps/auditlog/` was made by this work. Build and run artifacts
go to a `mktemp -d` directory that `run.sh` removes via an `EXIT` trap.

## Files

| file | role |
| --- | --- |
| `auditlog.c` | C99 app: argv-supplied log path, fixed event sequence, refusals, append, read-back |
| `run.sh` | builds, runs once under `timeout 30`, prints `SECURE_AUDIT <scenarios> <passed> <failed> <refused>` |
| `NOTES.md` | this file |

## Exact build commands (from the repository root)

Mandated form (verified, exits 0):

```
/usr/bin/clang -std=c99 -pthread -Ilibc/install-include \
  examples/secure_apps/auditlog/auditlog.c \
  build/libc/libcplus_hardened.a -o <bin>
```

`run.sh` uses the same command with `-Wall -Wextra -pedantic` added; it emits
zero compiler diagnostics. The header used is
`libc/install-include/cplus_violation_log.h`; the archive member providing the
symbols is `build/libc/violation_log.o` inside `libcplus_hardened.a`.

## Exact run command

```
sh examples/secure_apps/auditlog/run.sh
```

`run.sh` creates a fresh temp directory, passes `<tempdir>/audit.log` as argv[1]
(the app has no hard-coded path), runs `timeout 30 <bin> <log>`, relays the full
transcript, then prints the summary. Exit status 0 requires process rc == 0,
`failed == 0`, `passed == scenarios`, and exactly 7 refusals.

## Exact output of `sh examples/secure_apps/auditlog/run.sh`

```
BUILD ok auditlog
REFUSED event_before_open code=EBADF api_ret=9
REFUSED open_null_path code=EINVAL api_ret=22
REFUSED open_empty_path code=EINVAL api_ret=22
REFUSED event_null_kind code=EINVAL api_ret=22
REFUSED event_empty_kind code=EINVAL api_ret=22
REFUSED event_oversized_kind code=APP_POLICY api_ret=n/a kind_len=120 cap=96 detail=library_truncates_long_kind
EVENT code=100 kind=auth.login ptr=0x1000 size=4 rc=0
EVENT code=101 kind=auth.logout ptr=0x2000 size=0 rc=0
EVENT code=102 kind=acl.denied ptr=0x3000 size=16 rc=0
EVENT code=103 kind=cfg.changed ptr=0x4000 size=8 rc=0
EVENT code=104 kind=integrity.ok ptr=0x5000 size=32 rc=0
REFUSED event_after_close code=EBADF api_ret=9
EVENT code=105 kind=session.end ptr=0x6000 size=1 rc=0
READBACK idx=0 line=100 auth.login 0x1000 4
READBACK idx=1 line=101 auth.logout 0x2000 0
READBACK idx=5 line=105 session.end 0x6000 1
RESULT 27 27 0 7
SECURE_AUDIT 27 27 0 7
```

`api_ret` values 9 and 22 are this platform's `EBADF` and `EINVAL`; the
symbolic names are the stable part of the transcript. `run.sh` exit status: 0.

## Refusal evidence (7)

| label | input | observed code | meaning |
| --- | --- | --- | --- |
| `event_before_open` | event with log closed | `EBADF` (9) | API refuses logging when no log is open |
| `open_null_path` | `cplus_violation_log_open(NULL)` | `EINVAL` (22) | no implicit/hard-coded destination |
| `open_empty_path` | `cplus_violation_log_open("")` | `EINVAL` (22) | empty path rejected |
| `event_null_kind` | `kind == NULL` | `EINVAL` (22) | null event argument rejected |
| `event_empty_kind` | `kind == ""` | `EINVAL` (22) | empty event argument rejected |
| `event_oversized_kind` | 120-byte kind | `APP_POLICY` (n/a) | app-level guard: 120 > 96; API would truncate |
| `event_after_close` | event after `close()` | `EBADF` (9) | close is a real state transition |

A separate expectation confirms that none of these refusals changed the
cumulative event count. Refused events are not counted.

## Append / format evidence

The app re-opens the same path, appends one record, closes, then reads the file
back and checks **all six** lines byte-for-byte against fixed expected text.
The printed subset is indices 0, 1 and 5 (first two records and the appended
last record):

```
100 auth.login 0x1000 4
101 auth.logout 0x2000 0
105 session.end 0x6000 1
```

Line 1 is still first after the append (no truncation) and the appended
`session.end` record is last (correct append order). Every line matches the
documented format `<code> <kind> 0x<hex-pointer> <size>`; pointer values are
synthetic constants, never real addresses, and no path is printed.

## Scenario accounting (27 total = 20 checks + 7 refusals)

| # | scenario |
| --- | --- |
| 1 | count is zero after initial `close()` |
| 2 | refuse: event before open (`EBADF`) |
| 3 | refuse: `open(NULL)` (`EINVAL`) |
| 4 | refuse: `open("")` (`EINVAL`) |
| 5 | open argv-supplied path succeeds |
| 6 | refuse: event with NULL kind (`EINVAL`) |
| 7 | refuse: event with empty kind (`EINVAL`) |
| 8 | refuse: oversized kind (app policy) |
| 9 | refused events did not change the count |
| 10–14 | five fixed audit events (100..104) all accepted |
| 15 | count == 5 after the sequence |
| 16 | refuse: event after `close()` (`EBADF`) |
| 17 | re-open the same path succeeds |
| 18 | appended event (105) accepted |
| 19 | cumulative count == 6 |
| 20 | log file readable |
| 21 | file has exactly six lines |
| 22–27 | each of the six lines matches expected text exactly |

## Additional verification performed

Two runs against **different** fresh log paths produced byte-identical stdout
and byte-identical log files (6 lines each):

```
DETERMINISM identical (fresh paths)
LOG FILES byte-identical
```

The destination is intentionally append-only. Re-running against the **same**
log path appends a second copy of the six records; that run then fails the
"exactly six lines" expectation (`RESULT 27 26 1 7`, nonzero exit). This is
correct behavior, not a defect, and is why `run.sh` always uses a fresh
`mktemp -d` log path.

# Deep contract — Task 5: remaining gaps in `stdlib/fs.hp` / `stdlib/time.hp`

Independent, self-checking contract established by
`tests/stdlib/deep_fs_task5.cp`.  It covers **only** cases absent from
`tests/stdlib/deep_fs_time.cp` (54 checks) and that file's
`DEEP_FS_TIME_CONTRACT.md`.  No header was edited and no runtime/compiler
source was changed.

Every check prints `<tag> <1|0>` (1 = passed).  The final line is
`FS_TASK5 <checks> <failures>`; the process exits 0 only when `failures == 0`.

## Result summary

| Mode | Build command | Run | Final line | stderr |
|---|---|---|---|---|
| **STRICT, sanitizers off** (run first) | `timeout 30 build/c+ build --strict --sanitize=none tests/stdlib/deep_fs_task5.cp -o $T/fs5_strict` | build rc 0, run rc 0 | `FS_TASK5 35 0` | empty |
| Default | `timeout 30 build/c+ build tests/stdlib/deep_fs_task5.cp -o $T/fs5_default` | build rc 0, run rc 0 | `FS_TASK5 35 0` | empty |
| UBSan | `timeout 30 build/c+ build --strict --sanitize=undefined tests/stdlib/deep_fs_task5.cp -o $T/fs5_ubsan` | build rc 0, run rc 0 | `FS_TASK5 35 0` | empty |

Each binary invocation was also wrapped in `timeout 30`.  No UBSan diagnostic,
no fatal exit, no timeout, no build warning, and no `/tmp/cplus_fs5_*` artifact.
All three stdout streams were byte-identical.

## Commands (executed in this exact order)

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
T=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode

# 1) STRICT / sanitizers off -- FIRST
timeout 30 build/c+ build --strict --sanitize=none tests/stdlib/deep_fs_task5.cp -o $T/fs5_strict
timeout 30 $T/fs5_strict > $T/fs5_strict.out 2> $T/fs5_strict.err

# 2) default
timeout 30 build/c+ build tests/stdlib/deep_fs_task5.cp -o $T/fs5_default
timeout 30 $T/fs5_default > $T/fs5_default.out 2> $T/fs5_default.err

# 3) UBSan
timeout 30 build/c+ build --strict --sanitize=undefined tests/stdlib/deep_fs_task5.cp -o $T/fs5_ubsan
timeout 30 $T/fs5_ubsan > $T/fs5_ubsan.out 2> $T/fs5_ubsan.err

# cleanup proof / verification
ls -d /tmp/cplus_fs5_* 2>/dev/null || echo '(none)'
wc -c $T/fs5_*.err
```

## Exact stdout (identical in all three modes)

```
FS5_FILE_PARENT_MKDIR 1
FS5_FILE_PARENT_RENAME 1
FS5_FILE_PARENT_MKDTEMP 1
FS5_FILE_PARENT_CLEAN 1
FS5_NUL_SIZE 1
FS5_NUL_REMOVE 1
FS5_NUL_MKDTEMP_INVALID 1
FS5_NUL_TRUNCATION 1
FS5_EMPTY_QUERIES 1
FS5_EMPTY_OPS 1
FS5_LONG_LENGTH 1
FS5_LONG_PATH 1
FS5_LONG_MKDTEMP 1
FS5_PERM_SETUP 1
FS5_PERM_CREATE_DENIED 1
FS5_PERM_MKDIR_DENIED 1
FS5_PERM_CLEANUP 1
FS5_MISSING_REMOVE_REPEAT 1
FS5_MISSING_RENAME_SRC 1
FS5_RENAME_OVERWRITE_CONTENT 1
FS5_MKDTEMP_FAILED_TEMPLATE_REUSE 1
FS5_RESOURCE_BASELINE 1
FS5_REPEAT_FAILURES 1
FS5_RECOVER_AFTER_FAILURES 1
FS5_RESOURCE_NO_LEAK 1
FS5_REPEATED_REMOVE 1
FS5_TIME_ONE_NS 1
FS5_TIME_UINT64_MAX 1
FS5_TIME_NULL_ZERO_RESET 1
FS5_TIME_EXACT21_GUARD 1
FS5_TIME_NEGATIVE_WRAP 1
FS5_TIME_SLEEP_ZERO 1
FS5_TIME_SLEEP_ONE_MS 1
FS5_TIME_MONOTONIC_100 1
FS5_NO_LEFTOVERS 1
FS_TASK5 35 0
```

stderr was empty (`0` bytes) in all three modes.

## What each new case established

### A. Invalid paths — defined failures, no artifact

| Tag | Input | Observed result |
|---|---|---|
| `FS5_FILE_PARENT_MKDIR` | `mkdir("<regular-file>/child")` | `STDLIB_FAIL`, **IO / IO(109)**, non-empty message; no child directory; parent still a regular file |
| `FS5_FILE_PARENT_RENAME` | `rename(<src>, "<regular-file>/child")` | FAIL / IO; source file intact; no artifact |
| `FS5_FILE_PARENT_MKDTEMP` | template `<regular-file>/XXXXXX` | FAIL / IO; no directory created |
| `FS5_NUL_SIZE` | buffer `"/tmp/…_nulprefix_absent\0TRAILING"` | `-1`, IO / IO(109) (syscalls truncate at the first NUL; the visible prefix is absent) |
| `FS5_NUL_REMOVE` | same embedded-NUL path | FAIL / IO; no artifact |
| `FS5_NUL_MKDTEMP_INVALID` | embedded NUL + explicit `size = sizeof(buffer)`; visible prefix ends `ABCDEF`, bytes after NUL are `XXXXXX` | FAIL, **INVALID / UNSUPPORTED(108)** — `strnlen` stops at the NUL, so the visible template has no `XXXXXX` suffix; no artifact |
| `FS5_NUL_TRUNCATION` | file at visible prefix, path `prefix\0IGNORED` | `fs_exists == 1`, `fs_size == 3`, `fs_remove == OK`; characterises the defined truncation semantics (additional evidence, not one of the four required failures) |
| `FS5_EMPTY_QUERIES` / `FS5_EMPTY_OPS` | `""` | queries `0`; `fs_size`/`fs_remove`/`fs_mkdir`/`fs_rename` FAIL **IO**; `fs_mkdtemp` FAIL **INVALID**; no artifact |
| `FS5_LONG_LENGTH` / `FS5_LONG_PATH` / `FS5_LONG_MKDTEMP` | length `PATH_MAX + 127` (host `PATH_MAX = 1024`, so 1151 bytes) | length asserted `> PATH_MAX`; all fallible calls FAIL **IO** (`ENAMETOOLONG`/`ENOENT`); no artifact |

### B. Permissions — executed, not skipped

The build user has `euid = 501` (non-root), so this case ran for real.
`fs_mkdir` has no mode argument (it always calls `mkdir(path, 0777)`), so the
mode-000 directory was created by `fs_mkdir` followed by `chmod(path, 0000)`.
As a non-root owner, DAC applies: `fopen(dir + "/inside", "w")` returned
`NULL` and `fs_mkdir(dir + "/subdir")` returned FAIL / IO.  The directory was
`chmod`ed back to 0755 and removed with `rmdir()` (`FS5_PERM_CLEANUP`).

**Skip policy (documented, not triggered here):** when `geteuid() == 0`, root
bypasses DAC and the outcome is non-deterministic.  In that case the test emits
`SKIP FS5_PERMISSIONS reason=euid=0-root-bypasses-DAC-nondeterministic`, does
not print any `FS5_PERM_* 0|1` record, and does not count a pass or a failure.
No skip appeared in the three runs above.

### C. Missing-file operations and atomic overwrite

`FS5_MISSING_REMOVE_REPEAT`: three `fs_remove` calls on the same absent path
each FAIL / IO(109) with `fs_exists() == 0` — no state change.
`FS5_MISSING_RENAME_SRC`: FAIL / IO and the existing target's **content**
(`"KEEP"`, 4 bytes) is unchanged (deep_fs_time checked only size/existence).
`FS5_RENAME_OVERWRITE_CONTENT`: `src="NEWCONTENT"` (10 bytes) renamed over
`dst="OLDLONGCONTENT"` (14 bytes) → OK; source gone; destination is exactly
`"NEWCONTENT"` with size 10 — byte-exact atomic replacement verified, not just
the size.

### D. Resource cleanup after failures

90 forced failures (30 × failed `rename`, `remove`, `mkdtemp`) followed by a
successful `mkdtemp`, `rename`, and `remove`.  The `/dev/fd` entry count and the
number of `/tmp` entries beginning with `cplus_fs5_<pid>_` were measured before
and after: both unchanged (`FS5_RESOURCE_NO_LEAK`).  Note that the `mkdtemp`
fixture template is re-seeded to `XXXXXX` on each iteration; see Finding 1.

### E. Repeated remove (the fs analogue of repeated close/drop)

`fs.hp` is stateless and exposes no handle to close or drop.  Instead,
`FS5_REPEATED_REMOVE` removes a real file once (OK), then calls `fs_remove`
four more times on the same path: each returns FAIL / IO(109), `fs_exists`
stays 0, and the process does not crash.

### F. Time boundaries

| Tag | Result |
|---|---|
| `FS5_TIME_ONE_NS` | `1` ns → `1970-01-01T00:00:00Z`, OK, error clear |
| `FS5_TIME_UINT64_MAX` | `18446744073709551615` ns → exact `2554-07-21T23:34:33Z`, OK — the documented unsigned-nanosecond wrap representation on this 64-bit host (deep_fs_time only asserted the string was well formed) |
| `FS5_TIME_NULL_ZERO_RESET` | NULL buffer and `size == 0` → FAIL / INVALID(1)/UNSUPPORTED(108) with non-empty message; a following valid call returns OK and clears the record (`err.ok()`) |
| `FS5_TIME_EXACT21_GUARD` | 21-byte buffer writes exactly 20 chars + NUL, no write to the adjacent guard byte |
| `FS5_TIME_NEGATIVE_WRAP` | a negative caller value (`-86400000000000LL` ns, different from deep_fs_time's `-1`) wraps to a large positive and formats as a well-formed ISO string — documented consequence of the unsigned API, not a rejection |
| `FS5_TIME_SLEEP_ZERO` / `FS5_TIME_SLEEP_ONE_MS` | `time_sleep_ms(0)` returns in < 100 ms; `time_sleep_ms(1)` takes ≥ 0.5 ms and < 100 ms by `time_monotonic_ns()` |
| `FS5_TIME_MONOTONIC_100` | 100 consecutive `time_monotonic_ns()` samples are non-decreasing (deep_fs_time used only two) |

There is no negative or malformed-input surface for `time_sleep_ms`
(parameter is `unsigned`).  `time_sleep_ms((unsigned)-1)` is deliberately
**not** invoked: it would request a ~49-day sleep.  The `secs_u > LONG_MAX`
range guard in `time_format_iso8601` is unreachable on this 64-bit host because
`ULLONG_MAX / 1e9 ≈ 1.8447e10 < LONG_MAX`; this is accepted as an API-type
consequence, matching `DEEP_FS_TIME_CONTRACT.md`.

## Reproduced behavioural findings (no header change made)

Neither finding is a memory-safety violation or a crash; both are caller-visible
`fs_mkdtemp` quirks that the existing contract did not characterise.  They are
reported with minimal repros rather than "fixed" in the header.

### Finding 1 — failed `fs_mkdtemp` consumes the `XXXXXX` suffix

A failed `mkdtemp` (bad parent → ENOENT) replaces the trailing `XXXXXX` in the
caller's buffer with a random suffix.  Retrying with the **same buffer** then
fails `strnlen`/suffix validation as INVALID instead of IO, even after the
original cause is gone.  No directory is created in either call.

Minimal repro (observations from the instrumented run):

```c
error err = error();
char t[128];
snprintf(t, sizeof t, "/tmp/no_such_parent_dir/XXXXXX");
fs_mkdtemp(t, sizeof t, &err);  /* -1, category IO(3), message ENOENT */
/* t is now e.g. "/tmp/no_such_parent_dir/KSV7T3" */
fs_mkdtemp(t, sizeof t, &err);  /* -1, category INVALID(1) — suffix is gone */
```

The fixture loop now restores `XXXXXX` before each attempt; the dedicated
`FS5_MKDTEMP_FAILED_TEMPLATE_REUSE` check asserts that both calls are defined
failures and leave no artifact.  Callers who need to retry after a transient
failure must rebuild the template.

### Finding 2 — `size == strlen(template)` is rejected as INVALID

`fs_mkdtemp` validates `len = strnlen(tmpl, size)` and rejects `len >= size`.
Passing the exact string length (instead of the buffer capacity) therefore
returns FAIL / INVALID(1)/UNSUPPORTED(108) with the message
"template must end with XXXXXX" even when the string does end in `XXXXXX`.
The test passes `sizeof(buffer)` throughout (as `deep_fs_time.cp` does); an
early draft that passed `strlen()` produced INVALID where IO was expected, and
the three affected checks were corrected on the **test** side, not in the
header.  Callers should always pass the buffer capacity.

**No source defect was confirmed in `fs.hp` or `time.hp`, and no `.hp` header
was modified.**

## Cleanup proof

- In-process: `FS5_RESOURCE_NO_LEAK` (fd count and PID-prefixed `/tmp` entry
  count identical before/after 90 failures + recovery) and `FS5_NO_LEFTOVERS`
  both report 1 in all three modes.
- External: `ls -d /tmp/cplus_fs5_*` returned `(none)` after all three runs.
- Files are unlinked with `fs_remove`, directories with `rmdir()` (fs_remove is
  `unlink`-only), and the mode-000 permission directory is restored to 0755
  before removal.
- `wc -c` on all three `.err` files reported 0 bytes.

## Non-duplication statement

No case from `deep_fs_time.cp` is repeated: the 54 existing checks cover normal
paths, NULL paths, missing-path queries, `fs_size` on a missing path, one
rename-over-existing (size only), `fs_mkdtemp` with a valid/invalid template,
`fs_mkdir` exists, one error-callback capture, the ISO-8601 buffer ladder
0/1/4/20/21/32, `-1`/`ULLONG_MAX` well-formedness, two-clock ordering, a 20 ms
sleep, and the EINTR-resume fork test.  This task adds the invalid-path,
permission, content-exact, leak-accounting, repeated-remove, and
boundary/sampling cases listed above.

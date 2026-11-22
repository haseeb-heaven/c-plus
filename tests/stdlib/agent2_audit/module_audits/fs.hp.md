# Safety audit — `stdlib/fs.hp`

Audit scope: header `stdlib/fs.hp` (119 lines) plus the two independent deep
contract suites that exercise it:

| Evidence | Exact path | Result |
|---|---|---|
| Deep behavioural suite (54 checks: 37 fs, 16 time, 1 interrupted sleep) | `tests/stdlib/deep_fs_time.cp` / `.expected` | `deep_fs_time: 54 checks, 0 failures` |
| Task-5 gap suite (35 checks) | `tests/stdlib/deep_fs_task5.cp` / `.expected` | `FS_TASK5 35 0` |
| Contract for suite 1 | `docs/stdlib/DEEP_FS_TIME_CONTRACT.md` | "No source defect was confirmed" (lines 21-23) |
| Contract for suite 2 | `docs/stdlib/DEEP_FS_TASK5_CONTRACT.md` | "No source defect was confirmed" (lines 206-207) |
| Header introduction / hardening | git `74ae6a7`, `df3bc7b` | `fs_mkdtemp` added in `df3bc7b` |

Both suites are self-checking (in-process `expect`/`f5` assertions; non-zero
exit on any mismatch), use PID-unique `/tmp` paths, and were executed in three
build modes — default Safe/UBSan, `--strict --sanitize=none`, and explicit
`--sanitize=undefined` — with rc 0, empty stderr and identical stdout in every
mode (contracts, "Result summary" tables). The standard runner also reports
`ok deep_fs_time`.

**Evidence standard:** these suites assert returned values, error
categories/codes and filesystem-artifact state. They are *not* proof of memory
safety or of absence of undefined behaviour. ASan is unavailable on this host
(`docs/LIMITATIONS.md:139-141`); clean UBSan builds cover only the instrumented
subset. No claim of memory safety is made from passing tests here.

## 1. Public API surface

All functions are `static inline`, stateless, and report failure through
`error *` (which may be `NULL`); queries never consult `err`.

| Symbol (path with line) | Signature | Result protocol |
|---|---|---|
| `fs_exists` (`stdlib/fs.hp:17`) | `int fs_exists(const char *path)` | `1` if `stat` succeeds, else `0`; `NULL` → `0` |
| `fs_is_file` (`stdlib/fs.hp:24`) | `int fs_is_file(const char *path)` | `1` if `S_ISREG`, else `0` |
| `fs_is_dir` (`stdlib/fs.hp:31`) | `int fs_is_dir(const char *path)` | `1` if `S_ISDIR`, else `0` |
| `fs_size` (`stdlib/fs.hp:39`) | `long long fs_size(const char *path, error *err)` | byte size; `-1` with error record |
| `fs_remove` (`stdlib/fs.hp:54`) | `int fs_remove(const char *path, error *err)` | `STDLIB_OK` / `STDLIB_FAIL` |
| `fs_rename` (`stdlib/fs.hp:68`) | `int fs_rename(const char *from, const char *to, error *err)` | `STDLIB_OK` / `STDLIB_FAIL` |
| `fs_mkdir` (`stdlib/fs.hp:82`) | `int fs_mkdir(const char *path, error *err)` | `STDLIB_OK` / `STDLIB_FAIL`; mode fixed at `0777` |
| `fs_mkdtemp` (`stdlib/fs.hp:98`) | `int fs_mkdtemp(char *tmpl, size_t size, error *err)` | `STDLIB_OK` / `STDLIB_FAIL`; mutates `tmpl` to created path |

Error categories/codes used (from `stdlib/error.hp:39-54`):
`STDLIB_CATEGORY_INVALID`(1)/`CPLUS_EXIT_UNSUPPORTED`(108),
`STDLIB_CATEGORY_IO`(3)/`CPLUS_EXIT_IO`(109),
`STDLIB_CATEGORY_RANGE`(2) and `STDLIB_CATEGORY_BOUNDS`(5)/`CPLUS_EXIT_BOUNDS`(101).

## 2. Resource paths (fd / socket / temp dir)

| Resource | Acquisition and lifetime | Release evidence |
|---|---|---|
| File descriptors | None owned by `fs.hp`. Every call is a one-shot syscall (`stat`, `unlink`, `rename`, `mkdir`, `mkdtemp`). The fds opened by `fopen` in the test fixtures (`deep_fs_task5.cp:73-96`) are closed by `fclose` / `fclose` on all paths. | `FS5_RESOURCE_NO_LEAK`, `FS5_NO_LEFTOVERS`; `/dev/fd` entry count and `/tmp` PID-prefix entry count identical before/after 90 forced failures (`deep_fs_task5.cp:317-366, 452`) |
| Sockets | None. `fs.hp` is filesystem-only. | n/a |
| Temp files | `/tmp/cplus_deep_fs_<pid>_*` (`deep_fs_time.cp:95-194`) and `/tmp/cplus_fs5_<pid>_*` (`deep_fs_task5.cp:142-143`, counted by `f5_count_tmp` at lines 114-128). | Files removed with `fs_remove` (unlink); directories removed with `rmdir()` (`deep_fs_time.cp:191-192`; `deep_fs_task5.cp:273, 359`) |
| Temp directories | `fs_mkdtemp` requires a **mutable** buffer ending in `XXXXXX` and `size` equal to the **buffer capacity** (`sizeof`), not `strlen` (`stdlib/fs.hp:98-117`). | `rmdir()` on the mutated template; `fs_exists == 0` asserted after every cleanup (`deep_fs_time.cp:192-194`, `deep_fs_task5.cp:451-452`) |
| Error callback | Global `stdlib_set_error_callback` (`stdlib/error.hp:163-165`) used once at `deep_fs_time.cp:173-182`, then reset to `NULL`. | Reset in-process; no global state left installed |

**`fs_remove` is `unlink`-only** (`stdlib/fs.hp:61`). It cannot remove a
directory: applying it to a directory yields FAIL/IO and the directory remains
(VERIFY `fs.remove-cannot-rmdir`, `deep_fs_time.cp:189-190`). This is documented
API behaviour, not a defect (`DEEP_FS_TIME_CONTRACT.md:63-67`).

## 3. Input-validation matrix

Legend: **V** = VERIFIED by a named check/tag in the cited file; **D** =
documented in the contract but not a current assertion; **U** = untested
(listed in §7). Codes: `INV` = CATEGORY_INVALID/UNSUPPORTED(108);
`IO` = CATEGORY_IO/IO(109); `BDS` = CATEGORY_BOUNDS/BOUNDS(101).

| Input class / example | Expected result | Status & exact evidence |
|---|---|---|
| NULL path (`fs_exists`/`is_file`/`is_dir`) | `0`, inert | **V** `deep_fs_time.cp:69-71` (`fs.exists-null`, `fs.isfile-null`, `fs.isdir-null`); `DEEP_FS_TIME_CONTRACT.md:43` |
| NULL path for fallible call | `-1`/FAIL, `INV` | **V** `deep_fs_time.cp:72-92` (`fs.size-null`, `fs.remove-null`, `fs.rename-null-from/-to`, `fs.mkdir-null`, `fs.mkdtemp-null`); `deep_fs_task5.cp:223-225` |
| `err == NULL` on fallible calls | tolerated, never fatal | **V** `deep_fs_time.cp:87-92` (`fs.null-error-pointer`) |
| Empty string `""` | queries `0`; size/remove/mkdir/rename FAIL/`IO`; mkdtemp FAIL/`INV` | **V** `deep_fs_task5.cp:211-225` (`FS5_EMPTY_QUERIES`, `FS5_EMPTY_OPS`) |
| Embedded NUL in path | syscalls truncate at first NUL: `fs_size` FAIL/`IO`, `fs_remove` FAIL/`IO`; a file at the visible prefix is found/sized/removed | **V** `deep_fs_task5.cp:171-209` (`FS5_NUL_SIZE`, `FS5_NUL_REMOVE`, `FS5_NUL_TRUNCATION`) |
| Embedded NUL + `fs_mkdtemp` | `strnlen` stops at NUL → visible suffix is `ABCDEF` → FAIL/`INV`, no artifact | **V** `deep_fs_task5.cp:186-196` (`FS5_NUL_MKDTEMP_INVALID`) |
| Overlong path (`PATH_MAX + 127` = 1151 bytes) | all fallible calls FAIL/`IO` (`ENAMETOOLONG`/`ENOENT`); queries `0` | **V** `deep_fs_task5.cp:227-249` (`FS5_LONG_LENGTH`, `FS5_LONG_PATH`, `FS5_LONG_MKDTEMP`) |
| Regular file used as directory parent | mkdir/rename/mkdtemp FAIL/`IO`; parent stays a file; no artifact | **V** `deep_fs_task5.cp:147-169` (`FS5_FILE_PARENT_*`) |
| Missing path | queries `0`; `fs_size` `-1`/`IO`; remove/rename FAIL/`IO` | **V** `deep_fs_time.cp:94-110` (`fs.missing-*`) |
| Existing destination for `fs_mkdir` | FAIL/`IO` | **V** `deep_fs_time.cp:166-171` (`fs.mkdir-exists`) |
| `fs_rename` over existing file | `OK`; source gone; destination byte-exact replaced | **V** `deep_fs_time.cp:112-133` (size/existence), `deep_fs_task5.cp:305-315` (`FS5_RENAME_OVERWRITE_CONTENT`, 10-byte `"NEWCONTENT"` replaces 14-byte `"OLDLONGCONTENT"`) |
| `fs_rename` into non-existent parent | FAIL/`IO`; source intact | **V** `deep_fs_time.cp:135-141`, `deep_fs_task5.cp:159-162` |
| Bad `fs_mkdtemp` template (NULL, `size < 7`, missing `XXXXXX`) | FAIL/`INV` | **V** `deep_fs_time.cp:143-156` (`fs.mkdtemp-no-suffix`, `-small-size`, `-null`); `deep_fs_task5.cp:186-196` |
| `fs_mkdtemp` with absent parent | FAIL/`IO` | **V** `deep_fs_time.cp:157-164`; `deep_fs_task5.cp:323-325` |
| `fs_mkdtemp` template reuse after failure (consumed `XXXXXX`) | 2nd call FAIL/`INV` (suffix overwritten by libc); no artifact | **V** `deep_fs_task5.cp:318-331` (`FS5_MKDTEMP_FAILED_TEMPLATE_REUSE`) — **documented non-defect**, inherited from libc `mkdtemp` (`DEEP_FS_TASK5_CONTRACT.md:172-193`) |
| `size == strlen(template)` (instead of capacity) | FAIL/`INV`: `len >= size` guard at `stdlib/fs.hp:107` rejects it | **D** `DEEP_FS_TASK5_CONTRACT.md:195-204` (Finding 2); all final-suite calls pass `sizeof(buffer)` — see §7 |
| Permission-denied directory (`chmod 0000`) | `fopen` child `NULL`; nested `fs_mkdir` FAIL/`IO` | **V** `deep_fs_task5.cp:251-282` when `geteuid() != 0`; SKIP policy when root (§8) |
| Repeated remove (3× missing path, 1× real then 4× more) | always defined FAIL/`IO`, `fs_exists == 0`, no crash | **V** `deep_fs_task5.cp:284-295` (`FS5_MISSING_REMOVE_REPEAT`) and `368-378` (`FS5_REPEATED_REMOVE`) — fs analogue of repeated close/drop |
| Deterministic error-callback capture | exactly 1 call, `INV`/UNSUPPORTED(108) | **V** `deep_fs_time.cp:173-182` (`fs.error-callback`) |

No fs input in the suites produced a `cplus_fatal` code (101–109) or process
termination; completion of both suites is the evidence
(`DEEP_FS_TIME_CONTRACT.md:26-34`).

## 4. Interruption / timeout behavior

- `fs.hp` contains **no retry loop**: `stat`, `unlink`, `rename`, `mkdir` and
  `mkdtemp` are called once and any failure is reported immediately
  (`stdlib/fs.hp:39-117`). No contract promise of EINTR resilience exists.
- No fs: syscall is interrupt-injected in either suite. Interruption coverage
  belongs to `time_sleep_ms` (`tests/stdlib/deep_fs_time.cp:277-312`, a forked
  50 ms `SIGALRM` interrupting a 300 ms sleep with a 5 s parent watchdog and
  `SIGKILL` fallback) — see `time.hp.md`.
- No timeout parameter exists in the fs API. The task-5 suite bounds its own
  work with `timeout 30` wrappers and internal bounded wait loops; the prefix
  counting helpers use `opendir`/`readdir` only.
- **UNTESTED:** fs syscalls receiving `EINTR` (no injected fault); behaviour is
  "report FAIL/IO", which has not been exercised.

## 5. Cleanup evidence (VERIFY)

| Evidence | Location | Result |
|---|---|---|
| Every created file/dir removed; `fs_exists == 0` for all created paths | `deep_fs_time.cp:184-194` | asserted in-process |
| 90 forced failures (30× rename/remove/mkdtemp) then successful recovery; `/dev/fd` and `/tmp` PID-prefix counts unchanged | `deep_fs_task5.cp:317-366` (`FS5_RESOURCE_BASELINE`, `FS5_REPEAT_FAILURES`, `FS5_RECOVER_AFTER_FAILURES`, `FS5_RESOURCE_NO_LEAK`) | 1 in all three modes |
| Final no-leftovers check | `deep_fs_task5.cp:451-452` (`FS5_NO_LEFTOVERS`) | 1 in all three modes |
| External inspection | `DEEP_FS_TASK5_CONTRACT.md:214` | `ls -d /tmp/cplus_fs5_*` → `(none)`; `DEEP_FS_TIME_CONTRACT.md:17` reports no `/tmp/cplus_deep_fs_*` artifact |
| stderr | `tests/stdlib/deep_fs_time.stderr.expected`, `tests/stdlib/deep_fs_task5.stderr.expected` | empty in all modes |
| Permission fixture restored | `deep_fs_task5.cp:272-273` | `chmod 0755` then `rmdir` |

Cleanup is deliberately two-path: files via `fs_remove`, directories via
`rmdir()` because `fs_remove` is `unlink`-only.

## 6. Confirmed defects fixed (reproducer / regression)

### 6.1 Header defects

- **Open header defects found in this audit: 0.** Neither
  `DEEP_FS_TIME_CONTRACT.md:21-23` nor `DEEP_FS_TASK5_CONTRACT.md:206-207`
  confirmed a source defect in `fs.hp` or `time.hp`; no `.hp` header was
  modified for either suite.
- Historical git evidence (`git show df3bc7b -- stdlib/fs.hp`) shows
  `fs_mkdtemp` was *added* as a new API, not introduced as a defect fix. No
  historical `fs.hp` defect with a regression test was identified in the
  audited history (`74ae6a7`, `df3bc7b`, `558bc36`).

### 6.2 Test-side defects found while establishing the contract (fixed)

| Defect | Reproducer | Fix / regression |
|---|---|---|
| Fixture loop reused a failed `fs_mkdtemp` template without restoring `XXXXXX`, so later iterations reported INVALID instead of the intended IO failure. | `DEEP_FS_TASK5_CONTRACT.md:172-193` (bad parent × 2, second call flips INVALID) | Test fixture re-seeds `XXXXXX` each iteration (`deep_fs_task5.cp:340-354`) and the quirk is pinned as `FS5_MKDTEMP_FAILED_TEMPLATE_REUSE` (`deep_fs_task5.cp:318-331`). No header change. |
| An early draft passed `strlen(buffer)` instead of `sizeof(buffer)` to `fs_mkdtemp`, producing INVALID where IO was expected. | `DEEP_FS_TASK5_CONTRACT.md:195-204` (Finding 2) | Corrected on the **test** side; final suite passes buffer capacity throughout. Documented as a caller rule, not "fixed" in the header. |

### 6.3 Documented non-defects (not fixed by design)

1. Failed `fs_mkdtemp` consumes the `XXXXXX` suffix because libc `mkdtemp`
   overwrites it before `mkdir` fails (`DEEP_FS_TASK5_CONTRACT.md:172-193`).
   Retrying requires rebuilding the template.
2. `size == strlen(template)` is rejected by the `len >= size` guard
   (`stdlib/fs.hp:107`); callers must pass capacity
   (`DEEP_FS_TASK5_CONTRACT.md:195-204`).
3. `fs_remove` is unlink-only, so directories require `rmdir()`
   (`DEEP_FS_TIME_CONTRACT.md:63-67`).

**Defect counts (fs):** header defects fixed in audited version **0**;
historical header defects identified **0**; test-side defects fixed **2**;
open defects **0**; documented non-defects **3**.

## 7. Remaining untested paths (UNTESTED — not claimed verified)

1. `fs_mkdtemp` with `size == strlen(template)` as an executable check — the
   behaviour is documented (`DEEP_FS_TASK5_CONTRACT.md:195-204`) but no final
   suite tag asserts it.
2. `EINTR` delivered during any fs syscall — no injection; wrappers do not
   retry and the resulting category is unasserted.
3. `EACCES` on `stat`/`fs_size` (permission-denied *parent* directory); the
   permission case tested is create-denied only (`deep_fs_task5.cp:251-282`).
4. Symlink semantics: dangling symlinks, `stat` following links, no `lstat`
   wrapper; untested.
5. Cross-device rename (`EXDEV`), rename of directory over non-empty
   directory, and rename where destination is a directory — untested.
6. `fs_remove` on symlinks, device nodes, sockets or a directory-as-target
   beyond the documented `unlink`-only EPERM/EISDIR case — untested.
7. `fs_mkdir` mode behaviour under non-default `umask` (mode argument is
   hard-coded `0777` at `stdlib/fs.hp:89`) — untested.
8. Non-UTF-8 / multibyte path bytes and path components containing newlines —
   untested.
9. Concurrent `fs_mkdtemp` calls racing on one template, or two processes using
   the same PID-prefixed fixture namespace — untested.
10. Global error-callback re-entrancy/thread-safety while fs failures are
    reported concurrently (`stdlib/error.hp:194-222`) — untested.
11. `fs_size` with `st_size` exceeding `long long` (cast at
    `stdlib/fs.hp:51`) — untested, practically unreachable on this host.
12. `PATH_MAX` boundary exactly at 1024 bytes (suite tests 1151 and normal
    paths, not the exact limit) — untested.

## 8. SKIPs

- **Declared skip policy, not triggered:** `SKIP FS5_PERMISSIONS
  reason=euid=0-root-bypasses-DAC-nondeterministic` is emitted only when
  `geteuid() == 0` (`deep_fs_task5.cp:255-257`,
  `DEEP_FS_TASK5_CONTRACT.md:115-119`). The recorded runs had `euid = 501`, so
  the permission scenario executed for real and printed `FS5_PERM_*` records.
- No `SKIP` line appears in either fs suite's `.expected` file.

## 9. What this audit does and does not establish

VERIFIED: exact return values, error categories/codes and absence of leftover
`/tmp` artifacts for the matrix in §3; deterministic failure injection through
controlled filesystem setup and one error-callback capture; cleanup after 90
consecutive failures.

UNTESTED / NOT ESTABLISHED: the paths in §7; memory safety, absence of UB in
the headers or the C+ RAII lowering, and behaviour under signal interruption
of fs syscalls. Passing the suites is behavioural evidence only.

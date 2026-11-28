# Safety audit — `stdlib/file.hp`

Audit scope: `stdlib/file.hp` only, cross-referencing `stdlib/error.hp`,
`docs/stdlib/DEEP_FILE_CONTRACT.md`, `docs/stdlib/DEEP_FILE_TASK2_CONTRACT.md`,
`docs/stdlib/DEEP_CROSS_FAMILY_CONTRACT.md`, `docs/MODERN_STDLIB.md`, and the
committed fixtures `tests/stdlib/deep_file_lines.cp`,
`tests/stdlib/deep_file_lifecycle.cp`, `tests/stdlib/deep_file_task2.cp`,
`tests/stdlib/deep_cross_family.cp`. No file outside this audit document was
modified; no tests were executed to produce this report — all findings are cited
from committed source/contract/test evidence.

## Evidence vocabulary

- **[VERIFIED]** — reproduced by a committed fixture with exact stdout/exit
  status pinned in a contract document.
- **[DOCUMENTED-ONLY]** — promised by a header comment or contract but not
  reproduced by a committed fixture in this module's suite.
- **[UNTESTED]** — code path exists but no committed fixture exercises it.

> **Safety-claim boundary.** Passing fixtures, recoverable errors, telemetry and
> fd-count checks are evidence of *contract conformance*, **not proof of memory
> safety**. ASan is rejected by this host's driver
> (`docs/stdlib/DEEP_STRING_CONTRACT.md:29-32`,
> `docs/stdlib/DEEP_TESTS_HARNESS.md:186`). "No leaks" below means the
> `tmpfile()`/fd-baseline observations in `deep_file_lifecycle.cp` and
> `deep_file_task2.cp`, not a general memory-safety verdict.

---

## (1) Public API list

`class file` at `stdlib/file.hp:16-208` (lowered by C+ to a struct plus
`file_<method>` functions); two free factories follow.

| Symbol | Signature | Source | Return contract |
| --- | --- | --- | --- |
| `file` ctor | `file(const char *path, const char *mode)` (private; called as `file(path,mode)`) | `file.hp:20-23` | opens with `fopen` when both args non-NULL; `owns = (stream!=NULL)` |
| `is_open` | `int is_open(void)` | `file.hp:25-27` | `1` if handle non-NULL |
| `native` | `FILE *native(void)` | `file.hp:29-31` | borrowed raw handle (may be NULL) |
| `read` | `int read(char *buf, size_t size, error *err)` | `file.hp:33-53` | `STDLIB_OK` (≥1 byte, including short read), `STDLIB_EOF`, `STDLIB_FAIL` |
| `read_line` | `int read_line(char *buf, size_t size, error *err)` | `file.hp:55-131` | `STDLIB_OK`/`STDLIB_EOF`/`STDLIB_FAIL` with `error` |
| `write` | `int write(const char *text)` | `file.hp:133-137` | boolean `1`/`0`; no `error` out-param |
| `write_bytes` | `int write_bytes(const char *data, size_t size, error *err)` | `file.hp:139-154` | `STDLIB_OK`/`STDLIB_FAIL` |
| `seek` | `int seek(long offset, int whence, error *err)` | `file.hp:156-167` | `STDLIB_OK`/`STDLIB_FAIL` |
| `flush` | `int flush(error *err)` | `file.hp:169-180` | `STDLIB_OK`/`STDLIB_FAIL` |
| `close` | `int close(error *err)` | `file.hp:183-200` | `STDLIB_OK`/`STDLIB_FAIL`; idempotent against double close |
| `drop` | `void drop(void)` | `file.hp:202-207` | closes only when `owns`; idempotent |
| `file_borrow` | `file file_borrow(FILE *handle)` | `file.hp:211-216` | wraps without ownership |
| `file_temp` | `file file_temp(error *err)` | `file.hp:219-230` | `tmpfile()`-backed owning handle, unlinked on close |

Status constants: `stdlib/error.hp:39-41`.

---

## (2) Allocation paths

- **No `stdlib_alloc`/`stdlib_calloc`.** The header states this explicitly in
  the contract (`DEEP_FILE_CONTRACT.md:64-66`): "`file.hp` performs no
  `stdlib_alloc`/`stdlib_calloc`; `stdlib_alloc_fail_after(0)` does not affect
  open/read/write/seek/close." All storage is libc-managed by `fopen`/`tmpfile`
  and released by `fclose` (`file.hp:21,192,204,222`).
- **[VERIFIED]** Allocation-injection no-op is asserted by
  `deep_file_lifecycle.cp` ("allocation-injection no-op",
  `DEEP_FILE_CONTRACT.md:126`); `error_fail` only records telemetry
  (`stdlib/error.hp:192-222`).
- The temporary-file path (`file_temp`, `file.hp:219-230`) calls `tmpfile()`;
  failure is reported with `CPLUS_EXIT_IO` and `owns=0`. The file is
  unlinked by libc on close/drop (`file.hp:218` comment,
  `DEEP_FILE_CONTRACT.md:67-70`).

---

## (3) Input-validation matrix

| Function | Condition | Behaviour | Code / category | Class | Evidence |
| --- | --- | --- | --- | --- | --- |
| ctor | `path==NULL \|\| mode==NULL` | `stream=NULL`, no `fopen` | n/a (`!is_open()`, `native()==NULL`) | [DOCUMENTED-ONLY] | `file.hp:21`; non-NULL invalid cases covered by `badpath-open`,`badmode-open` (`DEEP_FILE_TASK2_CONTRACT.md:40`) |
| ctor | invalid path / invalid mode (`"q"`) | `fopen` fails, handle NULL | n/a | [VERIFIED] | `deep_file_task2.cp` `badpath-open`,`badmode-open`,`badpath-recoverable` (`DEEP_FILE_TASK2_CONTRACT.md:40,166-167`) |
| `read` | `buf==NULL \|\| size==0` | `STDLIB_FAIL` | `108` / `INVALID` (`error_fail`) | [DOCUMENTED-ONLY] | `file.hp:36-39`; pattern mirrored in contract §4 (`DEEP_FILE_CONTRACT.md:46-48`) |
| `read` | `stream==NULL` | `STDLIB_FAIL` | `109` / `IO` | [VERIFIED] | `file.hp:40-43`; `badpath-recoverable`; `readfail-*` |
| `read` | `fread` returns 0 + `ferror` | `STDLIB_FAIL` | `109` / `IO` | [VERIFIED] | `file.hp:45-49`; `readfail-status`,`readfail-ferror` on a `"w"` stream (`DEEP_FILE_TASK2_CONTRACT.md:38,161-162`) |
| `read` | EOF (0 bytes, `!ferror`) | `STDLIB_EOF`, no write | `0` / `OK` | [VERIFIED] | `file.hp:50`; `empty-read-eof` (`DEEP_FILE_TASK2_CONTRACT.md:34`) |
| `read` | short read (>0 bytes) | `STDLIB_OK` (no out-count) | `0` / `OK` | [VERIFIED] | `file.hp:44,52`; documented limitation (`DEEP_FILE_CONTRACT.md:53-57,138-139`) |
| `read_line` | `buf==NULL \|\| size==0` | `STDLIB_FAIL` | `108` / `INVALID` | [DOCUMENTED-ONLY (guard)] / [VERIFIED via `read` parity] | `file.hp:58-62`; contract §4 |
| `read_line` | `size > INT_MAX` | `STDLIB_FAIL`, **no stream access** | `101` / `BOUNDS` | [VERIFIED] | `file.hp:63-67`; bug #7 in `DEEP_FILE_CONTRACT.md:84`; fix `5fe387d` |
| `read_line` | `stream==NULL` | `STDLIB_FAIL` | `109` / `IO` | [VERIFIED] | `file.hp:68-71`; `badpath-recoverable` |
| `read_line` | `size==1`, empty line / `\r` | `STDLIB_OK`, only `buf[0]='\0'` | `0` / `OK` | [VERIFIED] | `file.hp:75-91`; `DEEP_FILE_CONTRACT.md:26-31`; fix `5fe387d` |
| `read_line` | `size==1`, non-empty line | drains, `STDLIB_FAIL` | `101` / `BOUNDS` | [VERIFIED] | `file.hp:92-96`; bug #4 (`DEEP_FILE_CONTRACT.md:81`) |
| `read_line` | exact fit (`\n`/`\r`/EOF just past buffer) | peek accepts, strips terminators, `STDLIB_OK` | `0` / `OK` | [VERIFIED] | `file.hp:112-127`; `exact-eof-*`,`exact-nl-*` (`DEEP_FILE_TASK2_CONTRACT.md:35-36`); `deep_cross_family.cp` CF4 (`DEEP_CROSS_FAMILY_CONTRACT.md:140-163`) |
| `read_line` | overlong line | drains, `buf[0]='\0'`, `STDLIB_FAIL` | `101` / `BOUNDS` | [VERIFIED] | `file.hp:119-126`; bugs #1-#3,#5-#6 (`DEEP_FILE_CONTRACT.md:78-83`); `reject-*` |
| `read_line` | EOF | `STDLIB_EOF`, error clear, buffer untouched | `0` / `OK` | [VERIFIED] | `file.hp:98-100`; `empty-readline-eof`,`empty-readline-again-eof` |
| `write` | `stream==NULL \|\| text==NULL` | returns `0` | n/a | [UNTESTED (NULL text)] / [VERIFIED (via stream cases)] | `file.hp:134-135`; `writefail-string-zero` covers read-only failure (`DEEP_FILE_TASK2_CONTRACT.md:39`) |
| `write_bytes` | `size>0 && data==NULL` | `STDLIB_FAIL` | `108` / `INVALID` | [UNTESTED] | `file.hp:141-144` |
| `write_bytes` | `size==0` with non-NULL data | `STDLIB_OK` (fwrite of 0) | `0` / `OK` | [VERIFIED] | `file.hp:149-153`; `writebytes-zero-nondata` (`DEEP_FILE_TASK2_CONTRACT.md:41`) |
| `write_bytes` | short write / bad stream | `STDLIB_FAIL` | `109` / `IO` | [VERIFIED] | `file.hp:149-152`; `writebytesfail-status` on `"r"` stream (`DEEP_FILE_TASK2_CONTRACT.md:39,163-164`) |
| `seek` | `stream==NULL` | `STDLIB_FAIL` | `109` / `IO` | [DOCUMENTED-ONLY] | `file.hp:158-161` |
| `seek` | invalid whence / past-EOF offset | `FAIL` for `999`; seek past EOF succeeds then `STDLIB_EOF` on read | `109` / `IO` | [VERIFIED] | `file.hp:162-165`; `seek-invalid-whence`,`seek-past-eof-eof` (`DEEP_FILE_TASK2_CONTRACT.md:41,167-169`) |
| `flush` | `stream==NULL` / read-only stream | `FAIL` for NULL; `STDLIB_OK` on `"r"` | `109` / `IO`, `0` / `OK` | [VERIFIED (read-only)] / [DOCUMENTED-ONLY (NULL)] | `file.hp:171-179`; `flush-readonly-ok` (`DEEP_FILE_TASK2_CONTRACT.md:41,165`) |
| `close` | `stream==NULL` (double close / closed handle) | `STDLIB_FAIL` | `109` / `LIFECYCLE` | [VERIFIED] | `file.hp:186-190`; `DEEP_FILE_CONTRACT.md:59-61`; `badpath-recoverable` |
| `close` | `fclose` returns nonzero | handle cleared regardless; `STDLIB_FAIL` | `109` / `IO` | [UNTESTED] | `file.hp:191-199`; known limitation `DEEP_FILE_CONTRACT.md:142-143` |
| `drop` | repeated / borrowed / NULL handle | idempotent, closes only if `owns` | n/a | [VERIFIED] | `file.hp:202-207`; `DEEP_FILE_CONTRACT.md:61,125` |
| `file_temp` | `tmpfile()` fails | `STDLIB_FAIL`, `owns=0`, NULL handle | `109` / `IO` | [UNTESTED] | `file.hp:219-230` |

---

## (4) Ownership and lifecycle analysis

- **Owned vs borrowed:** a constructed/temp `file` owns its `FILE*`;
  `file_borrow` never does (`file.hp:4-6,211-216`). `owns` is set only when
  `fopen`/`tmpfile` succeeded (`file.hp:22,225-228`). A zero-initialised `file`
  has `stream==NULL, owns==0`, so the compiler-inserted destructor closes
  nothing — the "destructor inert on zeroed handle" rule
  (`MODERN_STDLIB.md:53-58`).
- **[VERIFIED]** `close()` on a borrowed stream detaches without `fclose`; a
  second `close()` is a recoverable `FAIL/LIFECYCLE`; `drop()` is idempotent;
  `cplus_move` zeroes the source (`DEEP_FILE_CONTRACT.md:58-63,125-126`).
- **[VERIFIED, compiler-level]** Copying or assigning an owning `file` is
  rejected by `c+ check` in default and `--strict` modes with
  `error: resource 'b' cannot be copied from 'a'`; `cplus_move` is accepted.
  This is verified by *separate one-shot compiler checks*, not by an in-process
  deep fixture (`DEEP_FILE_CONTRACT.md:130-134`).
- **[VERIFIED]** Cleanup: `tmpfile()`-backed files are unlinked by libc; named
  fixtures are removed with `remove(path)` and absence is re-checked by
  reopening (`cleanup-no-leftovers`), and the open-fd count returns to baseline
  (`DEEP_FILE_CONTRACT.md:67-70`; `DEEP_FILE_TASK2_CONTRACT.md:96-111`).
  This is a resource-cleanup observation, **not** a general memory-safety proof.
- **Known lifecycle limitations [DOCUMENTED]:** `close()` clears the handle
  even when `fclose` fails, so a failed close cannot be retried; `read_line` is
  text-oriented and an embedded NUL truncates its view
  (`DEEP_FILE_CONTRACT.md:136-143`).

---

## (5) Error codes and failure atomicity

| Code | Category | Meaning in `file.hp` |
| --- | --- | --- |
| `CPLUS_EXIT_OK` (0) | `OK` (0) | success / EOF; error record clear |
| `CPLUS_EXIT_UNSUPPORTED` (108) | `INVALID` (1) | null buffer / zero size (`read`, `read_line`, `write_bytes`) |
| `CPLUS_EXIT_IO` (109) | `IO` (3) | null handle, stream error, short write, seek/flush failure, `tmpfile` failure |
| `CPLUS_EXIT_BOUNDS` (101) | `BOUNDS` (5) | `size > INT_MAX`, overlong line |
| `CPLUS_EXIT_IO` (109) | `LIFECYCLE` (7) | double close / already-closed handle (`error_fail_cat`) |

- `error_reset(err)` is first in every `error`-taking method
  (`file.hp:35,57,140,157,170,185,221`), so success leaves a clear record.
- **Failure atomicity [VERIFIED]:** on overlong input `read_line` empties the
  buffer (`buf[0]='\0'`, `file.hp:124`) and drains the stream so the next call
  reads the following line (`reject-next-line`, `DEEP_FILE_TASK2_CONTRACT.md:37`).
  On EOF it writes nothing. `write_bytes` writes either the full `size` or
  fails (`file.hp:149-152`); on failure the on-disk bytes are unchanged
  (`writefail-content-intact`, `DEEP_FILE_TASK2_CONTRACT.md:39`).
- `write()` has no `error` out-param and returns a plain boolean
  (`file.hp:133-137`); callers must check it (`writefail-string-zero`).

---

## (6) Confirmed defects fixed (with reproducer, fix, regression test)

**Defect count for `file.hp`: 8** — 7 reproduced/fixed in commit `5fe387d`
("Fix HIGH/MEDIUM safety findings from systematic audit", 2026-11-23 02:42 IST;
`git show 5fe387d -- stdlib/file.hp`) and 1 exact-fit classification defect
fixed in commit `1438bf2` ("Fix file::read_line exact-fit classification and add
uncovered Task 2 tests", 2026-11-23 04:11 IST;
`git show 1438bf2 -- stdlib/file.hp`). All are in `read_line`.

### D-FILE-1 .. D-FILE-7 — seven `read_line` defects (commit `5fe387d`)

Reproducer: `deep_file_lines.cp` with `deep_file_lines.stdin`, built
`build/c+ build tests/stdlib/deep_file_lines.cp`; 7 failures before the fix,
0 after (`DEEP_FILE_CONTRACT.md:72-87,119-120`).

| # | Case | Observed (buggy) | Expected / fixed | Source |
| --- | --- | --- | --- | --- |
| 1 | overlong line, `buf[8]` | `STDLIB_OK`, truncated `"ABCDEFG"` | `FAIL`, empty, `BOUNDS` | `DEEP_FILE_CONTRACT.md:78` |
| 2 | line after overlong | `"HIJKLMN"` (remainder) | `"beta"` (stream drained) | `:79` |
| 3 | EOF after overlong | `STDLIB_OK` (misaligned) | `STDLIB_EOF` | `:80` |
| 4 | one-byte buffer | 8 calls, no input consumed (hang risk) | 2 calls, `FAIL`/`BOUNDS`, then EOF | `:81` |
| 5 | overlong at physical EOF | `STDLIB_OK`, truncated `"0123456"` | `FAIL`/`BOUNDS` | `:82` |
| 6 | call after case 5 | `STDLIB_OK` | `STDLIB_EOF` | `:83` |
| 7 | `size = 1 << 31` | category `INVALID` (1) | category `BOUNDS` (5) | `:84` |

- **Fix (5fe387d):** split `size > INT_MAX` into its own `BOUNDS` guard; add the
  `size==1` byte-at-a-time path; add drain-and-`BOUNDS` for a full buffer with
  no in-buffer terminator and no `feof` (`file.hp:63-67,75-97,106-120`; diff in
  `git show 5fe387d -- stdlib/file.hp`).
- **Regression tests:** `tests/stdlib/deep_file_lines.cp` (14 checks,
  0 failures) and `tests/stdlib/deep_file_lifecycle.cp` (17 checks, 9 groups)
  — `DEEP_FILE_CONTRACT.md:11-17`; verified in default,
  `--strict --sanitize=none` and `--debug --sanitize=undefined` modes
  (`DEEP_FILE_CONTRACT.md:89-120`).

### D-FILE-8 — exact-fit line falsely reported `BOUNDS` (commit `1438bf2`)

- **Reproducer [VERIFIED]:** CF4 cross-family cases — `"abcd"` (no newline) in
  a 5-byte buffer and `"abcd\n"` in a 5-byte buffer. `io.hp::read_line`
  returned `OK/"abcd"`, while `file.hp::read_line` before the fix returned
  `FAIL`, empty buffer, `BOUNDS` (`DEEP_CROSS_FAMILY_CONTRACT.md:140-154`,
  table rows 1-2). Root cause: `fgets` does not set `feof` when it fills the
  buffer; the 5fe387d condition
  `n == size-1 && buf[n-1] != '\n' && !feof()` misclassified an exact fit as
  overlong (`DEEP_CROSS_FAMILY_CONTRACT.md:142-147`).
- **Fix (1438bf2):** port `io.hp`'s peek logic — `fgetc` the next byte; `\n`,
  `\r` (with paired LF consumed via `ungetc`) or EOF is accepted as an exact
  fit; any other byte triggers the existing drain-and-`BOUNDS` path
  (`file.hp:112-127`; `git show 1438bf2 -- stdlib/file.hp`).
- **Regression tests:** `tests/stdlib/deep_file_task2.cp` cases 2-4
  (`exact-eof-*`, `exact-nl-*`, `reject-*`, 38 checks total, `FILE_TASK2 38 0`;
  `DEEP_FILE_TASK2_CONTRACT.md:13-26,35-37,47-94`) and
  `tests/stdlib/deep_cross_family.cp` CF4 checks
  (`cf4_file_full_buffer_eof_aligned`, `cf4_file_exact_fit_aligned`,
  `cf4_cross_family_aligned`, `cf4_eof_aligned`;
  `DEEP_CROSS_FAMILY_CONTRACT.md:155-163`). Cross-family expectations were
  updated from "pinned discrepancy" to "aligned behavior" in commit `1438bf2`.
- **Note:** no header was changed by the Task-2 work itself
  (`DEEP_FILE_TASK2_CONTRACT.md:9-11,171-175`); the fix is the earlier commit.

### Non-defects explicitly excluded (not counted)

- `deep_file_task2.cp` initial parallel-run failures on named temp files were a
  PID-collision in the *test*, fixed with process-unique names
  (`DEEP_FILE_TASK2_CONTRACT.md:98-102`).
- `read()` short-read ambiguity is a documented API limitation, not a defect
  (`DEEP_FILE_CONTRACT.md:53-57,138-139`).

---

## (7) Remaining untested paths and risks

**[UNTESTED / DOCUMENTED-ONLY] — 7 items:**

1. **`file_temp()` failure.** `tmpfile()` returning NULL
   (`file.hp:222-226`) cannot be injected by the fixtures; no committed test
   covers it. *Risk:* untested `owns=0`/`IO`-error branch.
2. **`file_borrow(NULL)`.** The factory accepts any `FILE*` without validation
   (`file.hp:211-216`); no test passes NULL. *Risk:* a borrowed NULL handle
   yields a closed-but-invalid file; behaviour is defined by the guards but
   unpinned.
3. **`write(NULL, …)` / `write_bytes(NULL, size>0, …)`.** Guards at
   `file.hp:134-135,141-144` exist; Task 2 exercises a read-only stream and a
   zero-size call, not a NULL text/data argument. *Risk:* untested invalid-arg
   branches.
4. **Stream error inside `read_line` (not just `read`).** `ferror` paths at
   `file.hp:80-81,101-102` are not injected — `readfail-*` uses `read()`, not
   `read_line` (`DEEP_FILE_TASK2_CONTRACT.md:38`). *Risk:* `EOF` vs IO-error
   discrimination on a failed stream is unpinned for this method.
5. **`seek` on a NULL handle** (`file.hp:158-161`) and **`flush` on a NULL
   handle** (`file.hp:171-174`). Guards are documented but no named fixture
   calls them after `close()`. *Risk:* untested lifecycle-after-close branches.
6. **`close()` where `fclose` fails** (`file.hp:191-199`). Not injectable;
   documented limitation that the handle is cleared regardless
   (`DEEP_FILE_CONTRACT.md:142-143`). *Risk:* failed-close retry semantics
   unpinned.
7. **Embedded NUL in `read_line` data.** Text-oriented by contract
   (`DEEP_FILE_CONTRACT.md:140-141`); no binary-data fixture exists. *Risk:*
   callers must use `read()` for binary content.

No item above is claimed safe because a passing fixture exists.

---

## (8) Compiler dependencies

- **Class lowering and `this->` rewriting:** `file` is a `class` lowered to a
  struct plus functions; methods use `this->stream` / `this->owns`
  (`file.hp:17-207`). `MODERN_STDLIB.md:34-38` restricts source to `this->…`
  only (no `self`), and internal cross-object calls must use lowered names
  (here `error_reset`/`error_fail` free helpers — `error.hp:8-10`,
  `MODERN_STDLIB.md:38,82-84`).
- **Constructor rules:** the ctor assigns fields with no `return`
  (`file.hp:20-23`), satisfying "constructors never `return`" and "leave a
  valid object in the invalid state on failure" (`MODERN_STDLIB.md:59-62`).
  It avoids `->` on other objects, using native `fopen` directly
  (`MODERN_STDLIB.md:62-65`).
- **Resource/lifetime enforcement:** owning `file` is a resource class with
  `drop`; copy/assign rejection and `cplus_move` acceptance come from the C+
  RAII/borrow pass (`DEEP_FILE_CONTRACT.md:130-134`; `MODERN_STDLIB.md:148-152,
  168-171`). The compiler zero-initialises resource locals so `owns==0`
  protects fd 0/1/2 (`MODERN_STDLIB.md:53-58`).
- **`static inline` / multi-TU:** free factories are `static inline`
  (`file.hp:211,219`); header-member lowering emits per-TU copies
  (`MODERN_STDLIB.md:100-117`).
- **libc/platform dependencies:** `fopen/fgets/fgetc/fread/fwrite/fseek/
  fflush/fclose/tmpfile/feof/ferror/ungetc` and `<limits.h>` `INT_MAX`
  (`file.hp:11-14`). IO-failure expectations depend on host libc semantics
  probed on macOS/Darwin (`DEEP_FILE_TASK2_CONTRACT.md:155-169`) and are
  therefore platform-observable, not portable guarantees.
- **Safe-mode / sanitizer dependencies:** default builds add
  `-fsanitize=undefined,bounds,null,alignment -fno-sanitize-recover=all`
  and `-DCPLUS_SAFE=1` (`compiler/Driver/Driver.cpp:516,519`).
  `deep_file_lines.cp`/`deep_file_lifecycle.cp` pass in default,
  `--strict --sanitize=none` and `--debug --sanitize=undefined`
  (`DEEP_FILE_CONTRACT.md:16,89-120`); `deep_file_task2.cp` passes in four
  modes (`DEEP_FILE_TASK2_CONTRACT.md:19-25,139-153`). **ASan unavailable** on
  this host (`DEEP_STRING_CONTRACT.md:29-32`); no address-safety claim is made.
- **Harness note (out of scope, cited for completeness):**
  `tests/stdlib/run_deep_tests.sh` adds the yyjson include triplet only for
  `deep_json*` base names, so `deep_cross_family.cp` was verified via direct
  build commands rather than a full-runner invocation
  (`DEEP_CROSS_FAMILY_CONTRACT.md:204-208`).

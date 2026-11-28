# Safety audit — `stdlib/io.hp`

Audit scope: `stdlib/io.hp` only, cross-referencing `stdlib/error.hp`,
`docs/stdlib/DEEP_IO_CONTRACT.md`, `docs/stdlib/DEEP_IO_TASK2_CONTRACT.md`,
`docs/MODERN_STDLIB.md`, and the committed fixtures
`tests/stdlib/deep_io_1byte.cp`, `tests/stdlib/deep_io_token.cp`,
`tests/stdlib/deep_io_task2.cp`. No file outside this audit document was
modified; no tests were executed to produce this report (all findings are cited
from committed source/contract/test evidence).

## Evidence vocabulary (used throughout)

- **[VERIFIED]** — behaviour reproduced by a committed fixture and pinned in a
  contract document with exact stdout / exit status.
- **[DOCUMENTED-ONLY]** — promised by a header comment or contract but not
  reproduced by a committed fixture.
- **[UNTESTED]** — code path exists in the header but no committed fixture
  exercises it; it is reported as a risk, never as a safety guarantee.

> **Safety-claim boundary.** Passing fixtures, recoverable return codes and
> telemetry counters are evidence of *contract conformance*, **not proof of
> memory safety**. The C+ backend on this host rejects `--sanitize=address`
> (`docs/stdlib/DEEP_STRING_CONTRACT.md:29-32`,
> `docs/stdlib/DEEP_TESTS_HARNESS.md:186`), so ASan-class defects (overlapping
> `memcpy`, use-after-free, out-of-bounds) are **not** ruled out by these tests.
> No statement in this document should be read as a memory-safety proof.

---

## (1) Public API list

All symbols are `static inline` in `stdlib/io.hp` (lines 23-269); the header has
no class and no owned state.

| Symbol | Signature | Source | Return contract |
| --- | --- | --- | --- |
| `println` | `void println(void)` | `io.hp:23-25` | none; wraps runtime `print()` |
| `stdlib_io_token` | `int stdlib_io_token(char *buf, size_t size)` | `io.hp:30-50` | `0` ok, `1` EOF/no-token/`NULL`/zero size, `2` truncated (drains to delimiter) |
| `read_line` | `int read_line(char *buf, size_t size, error *err)` | `io.hp:52-123` | `STDLIB_OK`/`STDLIB_EOF`/`STDLIB_FAIL` with `error` record |
| `read_int` | `int read_int(int *out, error *err)` | `io.hp:125-159` | as above; `strtol` based |
| `read_long` | `int read_long(long *out, error *err)` | `io.hp:161-195` | as above; `strtoll` based |
| `read_float` | `int read_float(float *out, error *err)` | `io.hp:197-233` | as above; `strtof` based, rejects non-finite |
| `read_double` | `int read_double(double *out, error *err)` | `io.hp:235-269` | as above; `strtod` based, rejects non-finite |

Status constants are defined in `stdlib/error.hp:39-41`
(`STDLIB_OK=0`, `STDLIB_EOF=1`, `STDLIB_FAIL=-1`).

---

## (2) Allocation paths

**None.** `io.hp` contains no `stdlib_alloc`/`stdlib_calloc`/`malloc` call
(`io.hp:13-21` includes only libc and `cplus/cplus.h`/`error.hp`). Conversion is
delegated to libc `strtol`/`strtoll`/`strtof`/`strtod` (`io.hp:146,182,218,256`),
and no hand-written parser or buffer is allocated.

- **[VERIFIED]** Deterministic injection with `stdlib_alloc_fail_after(0)`
  (`stdlib/error.hp:234-239`) does not affect `read_int`: fixture check
  `s5_i_alloc_fail` passes (`docs/stdlib/DEEP_IO_CONTRACT.md:319-325`;
  `deep_io_token.cp`). The contract concludes "the I/O readers are
  allocation-free, so there is no allocation-failure path to test".
- `error_reset`/`error_fail_cat` only update telemetry/callback state and do
  not allocate (`stdlib/error.hp:192-222`). Telemetry is explicitly "diagnostic
  information, not proof of safety" (`error.hp:25-28`).

---

## (3) Input-validation matrix

| Function | Condition | Behaviour | Code / category | Class | Evidence |
| --- | --- | --- | --- | --- | --- |
| `stdlib_io_token` | `buf==NULL \|\| size==0` | returns `1` (no write) | n/a | [VERIFIED] | `io.hp:34-35`; contract §3.1; `deep_io_token.cp` |
| `stdlib_io_token` | token longer than `size-1` | stores `size-1` bytes + NUL, drains token, returns `2` | n/a | [VERIFIED] | `io.hp:41-49`; contract §3.3 / Scenario 3 (`s3_direct`) |
| `stdlib_io_token` | leading whitespace / EOF | skips whitespace; EOF → `1` | n/a | [VERIFIED] | `io.hp:36-40`; contract §3.1 |
| `read_line` | `buf==NULL \|\| size==0` | `STDLIB_FAIL`, message set | `109` / `INVALID` | [VERIFIED] | `io.hp:54-58`; `deep_io_1byte.cp` `s1_null`,`s1_zero`; contract §3.2 |
| `read_line` | `size > INT_MAX` | `STDLIB_FAIL`, **no stream access** | `101` / `BOUNDS` | [VERIFIED] | `io.hp:59-63`; contract Scenario 2; `deep_io_1byte.cp` `s2_reject`,`s2_aligned` |
| `read_line` | `size==1`, empty line / `\r` | `STDLIB_OK`, writes only `buf[0]='\0'` | `0` / `OK` | [VERIFIED] | `io.hp:64-87`; contract §3.2; `deep_io_1byte.cp` `s1_empty`,`s4_crlf` |
| `read_line` | `size==1`, non-empty line | drains to `\n`, `buf[0]='\0'`, `STDLIB_FAIL` | `101` / `BOUNDS` | [VERIFIED] | `io.hp:82-86`; contract Scenario 1; `s1_nonempty` |
| `read_line` | exact fit (delimiter just past buffer) | peek accepts `\n`/`\r`/EOF, strips terminators, `STDLIB_OK` | `0` / `OK` | [VERIFIED] | `io.hp:102-118`; `deep_io_1byte.cp` `s4_exact`,`s4_crlf`; `deep_io_task2.cp` `c1_*`,`c2_*`,`c3_*` |
| `read_line` | overlong line | drains remainder, `buf[0]='\0'`, `STDLIB_FAIL` | `101` / `BOUNDS` | [VERIFIED] | `io.hp:109-117`; contract §4; `s4_over`,`s4_after_over` |
| `read_line` | EOF | `STDLIB_EOF`, error clear, destination untouched | `0` / `OK` | [VERIFIED] | `io.hp:66-68,88-90`; `s4_eof`,`s1_eof`; `deep_io_task2.cp` §2 |
| all numeric readers | `out==NULL` | `STDLIB_FAIL` before token read | `109` / `INVALID` | [DOCUMENTED-ONLY] | `io.hp:131-135,167-171,203-207,241-245`; contract §3.3 (no committed fixture check lists this) |
| all numeric readers | truncated token (token return `2`) | `STDLIB_FAIL` | `101` / `RANGE` | [VERIFIED] | `io.hp:139-143` et al.; contract Scenario 3 |
| `read_int`/`read_long` | malformed text (`12abc`) | `STDLIB_FAIL`, out untouched | `108` / `INVALID` | [VERIFIED] | `io.hp:147-151,183-187`; `deep_io_token.cp` `s5_i_bad`; `deep_io_task2.cp` `c4_i_malformed` |
| `read_int`/`read_long` | overflow / `ERANGE` | `STDLIB_FAIL`, out untouched | `101` / `RANGE` | [VERIFIED] | `io.hp:152-156,188-192`; `s5_i_over`,`s5_l_over`; `c4_*_overflow` |
| `read_float`/`read_double` | malformed / non-finite / `ERANGE` | `STDLIB_FAIL`, out untouched | `108` / `INVALID` (malformed), `101` / `RANGE` (overflow/inf/nan) | [VERIFIED] | `io.hp:219-230,257-266`; `s5_f_bad`,`s5_d_inf`,`c4_f_overflow`,`c4_d_overflow` |
| `read_float`/`read_double` | subnormal | finite value accepted or `RANGE`; libc-dependent, never another category | `0` or `101` | [VERIFIED (category shape)] / [DOCUMENTED-ONLY (exact value)] | contract §3.3, Scenario 5 (`s5_f_sub`,`s5_d_sub`); `DEEP_IO_TASK2_CONTRACT.md:113-129` |
| all numeric readers | negative in-range value | parsed normally (e.g. `-77`) | `0` / `OK` | [VERIFIED] | `deep_io_task2.stdin:4` (`-77`), `c5_l_valid_after`; `strtol` family at `io.hp:146,182` |
| all readers | repeated call after failure | stream drained; next call reads next token/line | n/a | [VERIFIED] | `io.hp:41-47,111-112`; `s3_aligned`,`s4_after_over`,`c5_*_valid_after` |

---

## (4) Ownership and lifecycle analysis

- `io.hp` owns **no resources**. Callers own `buf`/`out`; the only stream is the
  process-global `stdin` (`io.hp:37,65,77,82,88,103-112`). No `drop`, no
  allocator, no destructor registration.
- `error *err` is an out-parameter owned by the caller. Every reader calls
  `error_reset(err)` first (`io.hp:53,130,166,202,240`), so a success or EOF
  leaves the record clear and a stale message cannot leak into a later call.
  **[VERIFIED]** `error_reset` hygiene is asserted in
  `docs/stdlib/DEEP_IO_CONTRACT.md:171-172` and by the `c2_newline_next_eof`
  / `c6_*_eof` checks (`DEEP_IO_TASK2_CONTRACT.md:38-39`).
- `stdlib_io_token` invalidates the caller's `buf` contents (always NUL
  terminates) but never frees it; EOF leaves `buf` untouched (`io.hp:39-40`),
  which callers must not interpret as data.
- Lifetime risk is confined to caller behaviour: passing a stack buffer that
  is out of scope, or ignoring token return `2`, is outside library control
  (`DEEP_IO_CONTRACT.md:85-86`).

---

## (5) Error codes and failure atomicity

Categories/codes in `stdlib/error.hp:39-53` and contract
`DEEP_IO_CONTRACT.md:17-21`:

| Code | Category | Meaning in `io.hp` |
| --- | --- | --- |
| `CPLUS_EXIT_OK` (0) | `OK` (0) | success or EOF; error record clear |
| `CPLUS_EXIT_IO` (109) | `INVALID` (1) | null destination / null or zero read_line buffer; stream error |
| `CPLUS_EXIT_BOUNDS` (101) | `BOUNDS` (5) | `size>INT_MAX`, overlong line |
| `CPLUS_EXIT_BOUNDS` (101) | `RANGE` (2) | token too long, numeric `ERANGE`, non-finite result |
| `CPLUS_EXIT_UNSUPPORTED` (108) | `INVALID` (1) | malformed numeric text |

- **Failure atomicity [VERIFIED]:** numeric readers write `*out` only on the
  success path (`io.hp:157,193,231,267`); every failure returns before the
  write. `deep_io_task2.cp` pins this byte-for-byte with sentinels for all four
  readers (`c4_{i,l,f,d}_malformed`, `c4_{i,l,f,d}_overflow`;
  `DEEP_IO_TASK2_CONTRACT.md:37,109-139`).
- `read_line` on overlong input deliberately sets `buf[0]='\0'`
  (`io.hp:115`), so the destination is *not* byte-preserved on that failure —
  this is documented and asserted (`DEEP_IO_CONTRACT.md:164`). EOF, by
  contrast, writes nothing (`io.hp:66-68,88-90`).
- No `cplus_fatal` path exists; all conditions are recoverable
  (`DEEP_IO_CONTRACT.md:15`, "there are no fatal cases in this suite").

---

## (6) Confirmed defects fixed (with reproducer, fix, regression test)

**Defect count for `io.hp`: 2** (both fixed in commit `5fe387d` —
"Fix HIGH/MEDIUM safety findings from systematic audit", 2026-11-23 02:42 IST,
`git show 5fe387d -- stdlib/io.hp`).

### D-IO-1 — exact-fit line falsely reported as overlong (`BOUNDS`)

- **Reproducer [VERIFIED]:** `repro_exact.cp`, 17-byte buffer (capacity 16)
  fed `0123456789abcdef\n` — before fix the call returned
  `exact -1 101 5` (spurious `FAIL/BOUNDS`) and `next 0 1`
  (`DEEP_IO_CONTRACT.md:88-120`).
- **Root cause:** `fgets(buf,size,…)` stops at `size-1` bytes *without
  consuming the delimiter* and without necessarily setting `feof`; the old
  test `n == size-1 && buf[n-1] != '\n' && !feof(stdin)` treated a full buffer
  as proof of truncation (`DEEP_IO_CONTRACT.md:117-120`; diff in `5fe387d`).
- **Fix:** peek the next byte with `fgetc`; `\n`, `\r` or EOF means exact fit
  (accept), any other byte means truncated (drain + `FAIL/BOUNDS`)
  (`io.hp:102-118`; `git show 5fe387d -- stdlib/io.hp`).
- **Regression test:** `tests/stdlib/deep_io_1byte.cp` checks
  `s4_exact`,`s4_after_exact`,`s4_crlf`,`s4_after_crlf`,`s4_last`
  (`deep_io_1byte.expected`); exact-fit-at-EOF / CRLF-boundary re-confirmed by
  `tests/stdlib/deep_io_task2.cp` `c1_*`,`c2_*`,`c3_*`
  (`DEEP_IO_TASK2_CONTRACT.md:34-36,201-207`).

### D-IO-2 — CRLF boundary leaked an `LF` into the next read

- **Reproducer [VERIFIED]:** `repro_crlf.cp`, 15-char CRLF line in a 17-byte
  buffer: `fgets` stopped right after `CR`, leaving `LF`; next `read_line`
  returned an empty line. Before: `crlf 0 1` / `after 0 0`; after:
  `crlf 0 1` / `after 0 1` (`DEEP_IO_CONTRACT.md:122-125`).
- **Fix:** when the peek finds `\r`, consume it and a following `\n` if present
  (mirroring the `size==1` path) with `ungetc` for a non-LF follower
  (`io.hp:104-108`; `git show 5fe387d -- stdlib/io.hp`).
- **Regression test:** `tests/stdlib/deep_io_1byte.cp` `s4_crlf`,
  `s4_after_crlf` (stdin fixture line `0123456789abcde<CR><LF>`,
  `DEEP_IO_CONTRACT.md:208-242`).

### Non-defects explicitly excluded (not counted)

- **Task-2 numeric-overflow expectation:** the task text's
  `999999999999999999999255…` token is valid float/double data (10^26), so
  `read_float`/`read_double` correctly returned success; the fixture was
  corrected to use `1e400`. This is a **test-expectation mistake**, not an
  `io.hp` defect (`DEEP_IO_TASK2_CONTRACT.md:109-139,201-207`). No header
  changed.

---

## (7) Remaining untested paths and risks

**[UNTESTED / DOCUMENTED-ONLY] — 5 items:**

1. **Forced `ferror(stdin)` mid-stream.** `read_line` has IO-error branches at
   `io.hp:69-71,91-93` and `read_line(size==1)` at `io.hp:69-71`, but no
   committed fixture injects an error on `stdin`; the only IO-failure evidence
   is `file.hp`'s write-only-stream test (`DEEP_FILE_TASK2_CONTRACT.md:38,
   155-162`). *Risk:* an error/EOF misclassification on a real failed stream
   is not exercised for this module.
2. **Numeric reader `out==NULL`.** Guards exist (`io.hp:131,167,203,241`) and
   the contract promises `FAIL/109/INVALID` (`DEEP_IO_CONTRACT.md:168`), but no
   committed check in `deep_io_token.cp`/`deep_io_task2.cp` calls a numeric
   reader with `NULL`. *Risk:* unverified out-parameter contract.
3. **Direct `stdlib_io_token` truncation misuse.** The contract states callers
   *must* check return `2` (`DEEP_IO_CONTRACT.md:85-86,157`); no fixture
   exercises a caller that ignores it. *Risk:* silent truncated-number
   consumption is a documented caller rule, not a tested library behaviour.
4. **`size==1` CR followed by a non-LF byte.** The `ungetc(next)` branch
   (`io.hp:78-79`) is documented ("optional paired LF",
   `DEEP_IO_CONTRACT.md:29-31`) but no fixture uses a lone `CR` at the boundary
   with a following non-newline byte. *Risk:* stream-alignment edge not pinned.
5. **Locale / platform numeric parsing.** `isspace` is correctly
   `(unsigned char)`-cast (`io.hp:38,41`), but no locale fixture exists and
   subnormal acceptance is explicitly libc-dependent
   (`DEEP_IO_CONTRACT.md:169-170`, `DEEP_IO_TASK2_CONTRACT.md:113-129`).
   *Risk:* cross-platform drift of accepted value sets; no ASan/UBSan issue
   is implied.

No item above is claimed safe by virtue of passing tests; each is a coverage
gap, not a proven defect.

---

## (8) Compiler dependencies

- **Class/method lowering and "internal cross-object calls":** `io.hp` has no
  classes, but it uses the free helpers `error_reset`, `error_fail`,
  `error_fail_cat` (`io.hp:55,69,84,…`) rather than `err.clear()`-style method
  sugar. C+ only rewrites `this->method()` inside class bodies; method sugar on
  parameters is not applied (`docs/MODERN_STDLIB.md:34-38`, `error.hp:8-10`).
- **`static inline` + multi-TU:** every function is `static inline`
  (`io.hp:23,30,52,125,161,197,235`); headers lowered with `headerMode` emit
  member/helper definitions per TU (`MODERN_STDLIB.md:100-117`). Evidence:
  `tests/stdlib/multi_tu/` includes `stdlib/io.hp` in two TUs and links
  (`MODERN_STDLIB.md:116`).
- **libc surface:** `fgetc/fgets/ungetc/feof/stdin`, `strtol/strtoll/strtof/
  strtod`, `isspace`, `errno`, `isfinite` (`io.hp:13-21`) — the runtime provides
  no custom parser; all numeric policy is libc `ERANGE` semantics
  (`DEEP_IO_CONTRACT.md:146-149`).
- **Safe-mode / sanitizer dependencies:** default builds add
  `-fsanitize=undefined,bounds,null,alignment -fno-sanitize-recover=all` and
  `-DCPLUS_SAFE=1` (`compiler/Driver/Driver.cpp:516,519`;
  `DEEP_STRING_CONTRACT.md:23-32`). `--strict --sanitize=none` and `--strict
  --sanitize=undefined` fixtures pass (`DEEP_IO_CONTRACT.md:285-308`,
  `DEEP_IO_TASK2_CONTRACT.md:146-181`). **ASan is unavailable on this host**
  (`DEEP_TESTS_HARNESS.md:186`), so no address-safety claim is made.
- **Compile-time pitfall (test-side):** driver flags must be separate argv
  operands; quoting `"--strict --sanitize=none"` as one string fails with
  `clang: error: unsupported option …` (`DEEP_IO_CONTRACT.md:285-289`,
  `DEEP_IO_TASK2_CONTRACT.md:143-144`).

# OS-facing `stdlib/*.hp` public API inventory and test mapping

Task 3 Phase 1 — branch `agent/modern-stdlib`, snapshot taken 2026-11-23.
This document is a **static inventory**. It makes no execution, coverage,
correctness or safety claim. It was produced by reading the listed headers and
by textual call-site searches in the test trees; no build, no test runner and no
instrumentation was run.

**File ownership:** this is the only file created/modified for this task. No
header, compiler, runtime, test, or git index change was made.

## 0. Headline counts

| Quantity | Value | How obtained |
|---|---:|---|
| Module headers inspected | 8 | `stdlib/io.hp`, `file.hp`, `fs.hp`, `json.hp`, `net.hp`, `error.hp`, `thread.hp`, `log.hp` |
| Native header inspected | 1 | `libc/install-include/cplus_libc.h` (only declarations actually called by the eight headers are recorded, §2) |
| Public callables enumerated | **151** | Every `static inline` free function, class constructor and class method defined in those eight headers (§3). Forward declarations at `error.hp:590-594` are the same three callables defined later, not extra entries. |
| Per-header callables | io 7, file 13, fs 8, json 30, net 14, error 42, thread 21, log 16 | sum = 151 |
| Callables with ≥1 literal call-site file in the searched test trees | **145** | §5 |
| Callables with no literal call-site file | **6** | §6, all internal `error.hp` helpers |
| Native `cplus_libc.h` declarations called directly by these headers | **1** | `cplus_realloc` (`cplus_libc.h:33`); see §2 for the other native deps, which are declared elsewhere |

These counts are counts of **callables** and of **files containing a literal
identifier**, not of test cases, executed paths, or assertions.

## 1. Method, evidence labels, caveats

### 1.1 Search actually performed

```
grep -rlw --include='*.[ch]*' <api-name> tests/stdlib tests/libc tests/crt/unit
```

Then, from the raw result list, `*.md` documentation, transient
`.cplus-tmp-*` compiler scratch files and `__pycache__` were removed. The
`--include` glob deliberately admits `.cp`, `.c`, `.h`, `.hp` and `.hpp`.
Word-boundary (`-w`), case-sensitive, raw-text matching. Comments and string
literals were **not** stripped, so a "call site" can be a token in a comment;
that is why OBSERVED is defined below as *textual presence*, not execution.

### 1.2 Labels used in the per-API tables

* **DOCUMENTED** — the contract (resource, ownership, nullability, failure,
  cleanup, thread-safety) is stated in header prose. `H1` = a comment attached
  to that callable; `H0` = only the header-level contract prose covers it;
  `I` = no prose, behaviour is evident only from the code body. All three are
  documentation, not proof of behaviour. Existing per-module contract prose in
  `docs/stdlib/DEEP_*_CONTRACT.md` and `tests/stdlib/agent2_audit/` was used as
  a cross-check only; it is not an independent observation.
* **OBSERVED** — at least one literal `<name>(` / typed constructor token in a
  source file under `tests/stdlib/`, `tests/libc/` or `tests/crt/unit/`. The
  table gives the number of matching files and up to three example paths. A
  bare identifier such as `size`, `at`, `read`, `write`, `ok`, `set`, `wait` or
  `signal` is shared with other modules; for those, a receiver-verified example
  is cited instead of relying on the raw file count.
* **UNTESTED** — zero matching source files in those three trees. This means
  "no literal call site found", **not** "never executed": e.g. the internal
  `stdlib_meta_*` helpers are reached through `stdlib_alloc`/`stdlib_release`.

### 1.3 ABI column shorthand

* `SIL` — `static inline` free function: defined in the header, no exported
  symbol, no object layout contribution; adding/removing/reordering parameters
  changes the source contract only, not a shipped object layout.
* `MEM` — class member function: the owning class layout is fixed and must not
  change. Where the layout is spelled out below it is the current in-header
  member list; any addition, removal, reorder or type change to those members,
  or to `STDLIB_ERROR_SIZE`, `stdlib_state_t`, `stdlib_meta_slot`,
  `stdlib_telemetry_stats`, changes ABI and is out of scope.
* All return types are existing scalars/pointers/`int` status codes. No new
  struct is introduced by any callable in this inventory; **no layout change is
  proposed or permitted**.

## 2. Native dependencies of these headers

### 2.1 Declarations in `libc/install-include/cplus_libc.h` that these headers call

| Caller | Call site | Declaration | Notes |
|---|---|---|---|
| `stdlib_realloc` | `error.hp:446` | `void *cplus_realloc(void*, size_t);` — `cplus_libc.h:33` | Same signature as the runtime declaration `runtime/include/cplus/cplus.h:105`. `cplus_libc.h:83` also defines a `realloc(p,sz)` macro alias under `CPLUS_MALLOC_TRACKING`; because `error.hp` calls the namespaced symbol directly, the macro does not affect it. ABI: pointer-in/pointer-out, `size_t` size; no struct layout involved. |

### 2.2 Native calls made by these headers whose declarations are **not** in `cplus_libc.h`

Recorded so the inventory is not misread as "only one native dependency".
`libc/install-include/cplus_libc.h` was checked for every name; absent names
are marked so.

| Header | Native call(s) | Declared where | Notes |
|---|---|---|---|
| `thread.hp` | `cplus_mutex_new`, `cplus_mutex_lock`, `cplus_mutex_unlock`, `cplus_mutex_free` (`thread.hp:33,42,47,56`) | `runtime/include/cplus/cplus.h:205-208` — **absent** from `cplus_libc.h` | Opaque `void*` handle stored in `mutex::handle`; layout `void*`. |
| `error.hp` | `cplus_alloc` (`:385`), `cplus_release` (`:587`) | `runtime/include/cplus/cplus.h:101,112` — **absent** from `cplus_libc.h` (which declares `cplus_malloc`/`cplus_calloc`/`cplus_free`, not these) | Managed allocator; `stdlib_alloc` blocks must go back through `stdlib_release`, never raw `free()`. |
| `io.hp` | `print()` (`:24`) | `runtime/include/cplus/cplus.h:244` — **absent** from `cplus_libc.h` | `println()` is a zero-argument wrapper. |
| `log.hp` | `time_now_ns()`, `time_format_iso8601()` (`:120`) | `stdlib/time.hp` (out of scope) | Timestamp only; failure is ignored and `stamp[0]` is cleared. |
| all eight | host libc/POSIX: `stat`, `unlink`, `rename`, `mkdir`, `mkdtemp`, `strerror`, `strnlen`, `strcmp`, `strcasecmp`, socket family (`socket`, `bind`, `listen`, `accept`, `getsockname`, `setsockopt`, `close`, `read`, `write`, `htonl`, `htons`, `ntohs`), stdio (`fopen`, `fclose`, `fread`, `fwrite`, `fgetc`, `fgets`, `fputc`, `fputs`, `fseek`, `fflush`, `flockfile`, `funlockfile`, `ferror`, `feof`, `ungetc`, `tmpfile`), number conversion (`strtol`, `strtoll`, `strtof`, `strtod`), allocator (`malloc`/`calloc`/`free`), pthread (`pthread_cond_*`, `pthread_create`, `pthread_join`), `yyjson_*` | system headers / vendored `yyjson.h` | `cplus_libc.h` provides namespaced `cplus_pthread_*` wrappers (`:184-186`) but **these headers call the raw `pthread_*` symbols**, not the wrappers. |

## 3. Per-module public API inventory

Every row below is one callable. `Loc` is `header:first-line` (ranges are in the
source). "Resource" is the OS/runtime object touched. "Ownership" states who
retains the resource after return. "Nullability" states the checked pointer
arguments. "Failure" and "Cleanup" describe what the body actually does. `TS`
is the header's thread-safety claim (or "none stated" where no claim exists).

### 3.1 `stdlib/io.hp` — 7 callables

| Callable / signature | Loc | Resource | Ownership / nullability | Failure | Cleanup | TS | ABI | Doc | Observed |
|---|---|---|---|---|---|---|---|---|---|
| `void println(void)` | `io.hp:23` | runtime `print()` | none; no args | none (`void`) | none | none stated | SIL | I | 1 file: `tests/stdlib/examples/io_demo.cp:23` |
| `int stdlib_io_token(char *buf, size_t size)` | `io.hp:30` | `stdin` stream position | caller owns `buf`; `buf==NULL \|\| size==0` → returns 1, no write | 0 ok, 1 EOF/empty, 2 truncated (drains to delimiter, writes `size-1` bytes + NUL) | stream left at delimiter | stdin shared; no internal lock claimed | SIL, no layout | H1 | 5 files, e.g. `tests/stdlib/deep_io_token.cp`, `regressions/adv_long_input.cp`, `regressions/adv_null_zero.cp` |
| `int read_line(char *buf, size_t size, error *err)` | `io.hp:57` | `stdin` | caller owns `buf`/`err`; null or `size==0` → `STDLIB_FAIL`; `size>INT_MAX` → `STDLIB_FAIL` | `STDLIB_OK` / `STDLIB_EOF` / `STDLIB_FAIL`; overlong line drains and clears `buf` | remainder of overlong line drained | stdin shared; no lock | SIL | H0 | 23 files, e.g. `deep_io_1byte.cp`, `deep_io_task2.cp`, `eof.cp` |
| `int read_int(int *out, error *err)` | `io.hp:149` | `stdin`, `errno` | `out` caller-owned; `out==NULL` → `STDLIB_FAIL`, `err` optional (`error_reset`/`error_fail_cat` accept NULL) | `STDLIB_EOF`, token-too-long, not-an-integer, `ERANGE`/range → `STDLIB_FAIL`; `out` untouched on failure | none | not thread-safe (mutates `stdin`, `errno`); no claim | SIL | H0 | 13 files, e.g. `multi_tu/part_a.cp`, `deep_io_token.cp`, `deep_io_task2.cp` |
| `int read_long(long *out, error *err)` | `io.hp:185` | `stdin`, `errno` | same as `read_int` (`strtoll`) | EOF / too-long / not-integer / range → `STDLIB_FAIL` | none | same | SIL | H0 | 8 files, e.g. `deep_io_token.cp`, `deep_io_task2.cp`, `foundation.cp` |
| `int read_float(float *out, error *err)` | `io.hp:221` | `stdin`, `errno` | same as `read_int` (`strtof`) | EOF / too-long / not-a-number / `ERANGE` or non-finite → `STDLIB_FAIL` (explicit policy: reject `inf`/`nan`) | none | same | SIL | H1 | 6 files, e.g. `deep_io_token.cp`, `deep_io_task2.cp`, `foundation.cp` |
| `int read_double(double *out, error *err)` | `io.hp:259` | `stdin`, `errno` | same as `read_int` (`strtod`) | EOF / too-long / not-a-number / `ERANGE` or non-finite → `STDLIB_FAIL` | none | same | SIL | H0 | 9 files, e.g. `deep_io_token.cp`, `deep_io_task2.cp`, `eof.cp` |

### 3.2 `stdlib/file.hp` — 13 callables

Class `file` layout (must not change): `FILE *stream; int owns;`.

| Callable / signature | Loc | Resource | Ownership / nullability | Failure | Cleanup | TS | ABI | Doc | Observed |
|---|---|---|---|---|---|---|---|---|---|
| `file(const char *path, const char *mode)` | `file.hp:20` | `FILE*` via `fopen` | owns iff open succeeds; `path==NULL \|\| mode==NULL` → null handle, owns=0 | no error record (constructor cannot fail-report) | `drop()` closes only if owned | none stated | MEM | H0 | verified `deep_file_lifecycle.cp:44,88-90`, `deep_file_lines.cp:184` |
| `int is_open(void)` | `file.hp:25` | none (reads `stream`) | none | n/a (`stream != NULL`) | none | none stated | MEM | I | 36 files, e.g. `deep_file_lifecycle.cp`, `deep_cross_family.cp`, `negative.cp` |
| `FILE *native(void)` | `file.hp:29` | borrowed `FILE*` returned | caller must not close an owned stream through this | n/a | none | none stated | MEM | I | 34 files, e.g. `deep_file_lifecycle.cp`, `deep_cross_family.cp` |
| `int read(char *buf, size_t size, error *err)` | `file.hp:33` | stream position | caller owns `buf`; `buf==NULL \|\| size==0` → FAIL; null handle → FAIL | `fread` 0 + `ferror` → FAIL; 0 + EOF → `STDLIB_EOF`; else OK | none | not thread-safe (stdio stream shared); no claim | MEM | H0 | name-ambiguous with `connection::read`; verified via `file` receivers, e.g. `deep_file_lifecycle.cp` |
| `int read_line(char *buf, size_t size, error *err)` | `file.hp:55` | stream position | caller owns `buf`; null/zero → FAIL; `size>INT_MAX` → FAIL; null handle → FAIL | EOF / stream error / overlong (buffer cleared) → `STDLIB_FAIL`/`STDLIB_EOF` | overlong remainder drained | no claim | MEM | I | 23 files, e.g. `deep_file_lines.cp`, `deep_file_lifecycle.cp`, `multi_tu/part_b.cp` |
| `int write(const char *text)` | `file.hp:156` | stream position | `text` borrowed; null handle or `text==NULL` → returns 0 | `fputs==EOF` → 0 | none | no claim | MEM | I | 112 files for name `write`; file receivers e.g. `apps/legacy_examples/file_demo.cp` |
| `int write_bytes(const char *data, size_t size, error *err)` | `file.hp:162` | stream position | `data` borrowed; `size>0 && data==NULL` → FAIL; null handle → FAIL | short `fwrite` → FAIL | none | no claim | MEM | I | 26 files, e.g. `deep_file_lifecycle.cp`, `safety_regressions.cp`, `negative.cp` |
| `int seek(long offset, int whence, error *err)` | `file.hp:179` | stream position | null handle → FAIL | `fseek != 0` → FAIL | none | no claim | MEM | I | 14 files, e.g. `deep_file_lifecycle.cp`, `deep_cross_family.cp` |
| `int flush(error *err)` | `file.hp:192` | stream buffers | null handle → FAIL | `fflush != 0` → FAIL | none | no claim | MEM | I | 17 files, e.g. `deep_file_lifecycle.cp`, `compile_time/file_valid.cp` |
| `int close(error *err)` | `file.hp:206` | owned `FILE*` | clears `stream`/`owns`; borrowed handle is detached without `fclose` | already-closed → FAIL; `fclose != 0` → FAIL | closes only if owned; double-close safe | no claim | MEM | H1 | 65 files for name `close`; verified e.g. `compile_time/file_double_close.cp` |
| `void drop(void)` | `file.hp:225` | owned `FILE*` | closes only if owned, then nulls fields | none (`void`) | idempotent | no claim | MEM | H0 | 140 files for name `drop`; class-token credit, compiler-inserted destructor |
| `file file_borrow(FILE *handle)` | `file.hp:234` | borrowed `FILE*` | `owns=0`; `drop()`/`close()` never close it; `handle` may be NULL | none | none | no claim | SIL | H1 | 11 files, e.g. `deep_file_lines.cp`, `deep_file_lifecycle.cp` |
| `file file_temp(error *err)` | `file.hp:242` | `tmpfile()` (unlinked on close) | owns on success, owns=0 on failure | `tmpfile()==NULL` → FAIL, invalid file | caller should `drop()`/`close()` | no claim | SIL | H1 | 24 files, e.g. `deep_file_lifecycle.cp`, `deep_cross_family.cp` |

### 3.3 `stdlib/fs.hp` — 8 callables

All eight are `static inline` and touch the filesystem named by `path` (POSIX
`stat`, `unlink`, `rename`, `mkdir`, `mkdtemp`). All error-returning variants
call `error_reset(err)` first; `err` is optional because
`error_fail_cat(NULL, ...)` is a no-op beyond telemetry/callback.

| Callable / signature | Loc | Resource | Ownership / nullability | Failure | Cleanup | TS | ABI | Doc | Observed |
|---|---|---|---|---|---|---|---|---|---|
| `int fs_exists(const char *path)` | `fs.hp:17` | path via `stat` | borrowed path; `NULL` → 0 | any `stat` failure → 0 | none | no claim | SIL | I | 10 files, e.g. `deep_fs_task5.cp`, `regressions/adv_json_fs_net.cp` |
| `int fs_is_file(const char *path)` | `fs.hp:24` | path via `stat` | borrowed; `NULL`/stat failure → 0 | 0 | none | no claim | SIL | I | 8 files, e.g. `deep_fs_task5.cp`, `fuzz/f100_fs_pathgen.cp` |
| `int fs_is_dir(const char *path)` | `fs.hp:31` | path via `stat` | borrowed; `NULL`/stat failure → 0 | 0 | none | no claim | SIL | I | 9 files, e.g. `containers_safety_test.cp`, `deep_fs_task5.cp` |
| `long long fs_size(const char *path, error *err)` | `fs.hp:39` | path via `stat` | borrowed; `NULL` → -1 + FAIL | stat failure → -1 + `STDLIB_CATEGORY_IO` | none | no claim | SIL | H1 | 8 files, e.g. `deep_fs_task5.cp`, `fuzz/f100_fs_cleanup.cp` |
| `int fs_remove(const char *path, error *err)` | `fs.hp:54` | path via `unlink` | borrowed; `NULL` → FAIL | `unlink != 0` → FAIL (`IO`) | file removed on success | no claim | SIL | I | 11 files, e.g. `deep_fs_task5.cp`, `fuzz/f100_fs_pathgen.cp` |
| `int fs_rename(const char *from, const char *to, error *err)` | `fs.hp:68` | two paths via `rename` | borrowed; either `NULL` → FAIL | `rename != 0` → FAIL (`IO`) | none | no claim | SIL | I | 7 files, e.g. `deep_fs_task5.cp`, `regressions/adv_json_fs_net.cp` |
| `int fs_mkdir(const char *path, error *err)` | `fs.hp:82` | path via `mkdir(..., 0777)` | borrowed; `NULL` → FAIL | `mkdir != 0` → FAIL (`IO`) | directory persists | no claim | SIL | I | 8 files, e.g. `deep_fs_task5.cp`, `fuzz/f100_fs_pathgen.cp` |
| `int fs_mkdtemp(char *tmpl, size_t size, error *err)` | `fs.hp:98` | mutable `tmpl` + created directory | caller owns buffer (mutated in place); `tmpl==NULL`, `size<7`, bad length/suffix → FAIL | `mkdtemp==NULL` → FAIL (`IO`) | created directory persists; caller removes it | no claim | SIL | H1 | 8 files, e.g. `containers_safety_test.cp`, `deep_fs_task5.cp` |

### 3.4 `stdlib/json.hp` — 30 callables

Layouts (must not change): `json_view { const yyjson_val *value; }`,
`json { yyjson_doc *document; }`,
`json_builder { yyjson_mut_doc *document; yyjson_mut_val *root_value; }`.
A `json_view` is borrowed and valid only while its owning `json`/`json_builder`
lives. `write()`-returned buffers are the caller's and are released by
`json_free()` (raw `free`).

| Callable / signature | Loc | Resource | Ownership / nullability | Failure | Cleanup | TS | ABI | Doc | Observed |
|---|---|---|---|---|---|---|---|---|---|
| `json_view(const yyjson_val *value)` | `json.hp:24` | borrowed `yyjson_val` | never frees | none | none | no claim | MEM | H0 | 5 files for name; direct `json_view(NULL)` at `stdlib_hardening_test.cp:96` |
| `int valid(void)` | `json.hp:28` | none | none | `value != NULL` | none | no claim | MEM | I | 162 files for name `valid` (ambiguous); JSON-specific e.g. `compile_time/json_valid.cp` |
| `int is_object(void)` | `json.hp:32` | none | none | `yyjson_is_obj` | none | no claim | MEM | I | 13 files, e.g. `deep_json_stress.cp`, `json_test.cp` |
| `int is_array(void)` | `json.hp:36` | none | none | `yyjson_is_arr` | none | no claim | MEM | I | 14 files, e.g. `deep_json_stress.cp`, `deep_json_task5.cp` |
| `int is_number(void)` | `json.hp:40` | none | none | `yyjson_is_num` | none | no claim | MEM | I | 10 files, e.g. `deep_json_task5.cp`, `deep_json_types.cp` |
| `int is_string(void)` | `json.hp:44` | none | none | `yyjson_is_str` | none | no claim | MEM | I | 9 files, e.g. `deep_cross_family.cp`, `json_test.cp` |
| `int is_bool(void)` | `json.hp:48` | none | none | `yyjson_is_bool` | none | no claim | MEM | I | 7 files, e.g. `deep_json_task5.cp`, `json_test.cp` |
| `int get_int(int *out, error *err)` | `json.hp:52` | pointed-to JSON value | `out` caller-owned; `out==NULL` or non-int → FAIL | wrong type → `INVALID`; sint/uint outside `int` → `RANGE`, out untouched | none | no claim | MEM | I | 23 files for name; JSON receivers e.g. `json_test.cp`, `deep_json_task5.cp` |
| `int get_double(double *out, error *err)` | `json.hp:79` | pointed-to JSON value | `out` caller-owned; null/non-number → FAIL | wrong type → `INVALID` | none | no claim | MEM | I | 10 files, e.g. `deep_json_task5.cp`, `json_test.cp` |
| `int get_bool(int *out, error *err)` | `json.hp:90` | pointed-to JSON value | same as `get_double` | wrong type → `INVALID` | none | no claim | MEM | I | 10 files, e.g. `deep_json_task5.cp`, `deep_json_types.cp` |
| `int get_string(const char **out, error *err)` | `json.hp:102` | borrowed string inside document | caller must not free; `out==NULL`/non-string → FAIL | wrong type → `INVALID` | none | no claim | MEM | H1 | 19 files, e.g. `deep_cross_family.cp`, `compile_time/json_dangling_ref.cp` |
| `size_t size(void)` | `json.hp:113` | none | none | non-array/object → 0 | none | no claim | MEM | I | 145 files for name (ambiguous); JSON receivers e.g. `json_test.cp` |
| `json_view at(size_t index)` | `json.hp:121` | borrowed child | result invalid if out of range or parent invalid | returns invalid view, no error record | none | no claim | MEM | I | receiver-verified: `deep_json_stress.cp:166`, `deep_json_types.cp:355` (`v.at`/`root.at`) |
| `json_view member(const char *key)` | `json.hp:126` | borrowed member | `key==NULL` or invalid parent → invalid view | returns invalid view, no error record | none | no claim | MEM | I | 34 files for name; verified `json_test.cp:20-24`, `deep_json_task5.cp:68` |
| `char *write(size_t *length)` | `json.hp:133` | `yyjson_val_write` allocation | returned buffer is caller-owned, release with `json_free` | invalid view → NULL | caller must `json_free` | no claim | MEM | I | receiver-verified e.g. `json_test.cp:56`, `deep_json_stress.cp:85` |
| `json(const char *text, error *err)` | `json.hp:145` | parsed document (owned) | `text` borrowed and not modified; `text==NULL` → invalid doc, no error | parse failure → invalid doc + `INVALID` with yyjson message | `drop()` frees document | no claim | MEM | H1 | verified `deep_cross_family.cp:471`, `deep_json_stress.cp:80` |
| `int valid(void)` | `json.hp:161` | none | none | `document != NULL` | none | no claim | MEM | I | covered by `valid` name above; JSON receivers e.g. `compile_time/json_valid.cp` |
| `json_view root(void)` | `json.hp:165` | borrowed root value | view invalid if document invalid | invalid view, no error record | none | no claim | MEM | I | 45 files for name; verified `json_test.cp:19`, `deep_json_stress.cp:166` |
| `void drop(void)` | `json.hp:172` | owned document | nulls `document`; idempotent | none | `yyjson_doc_free` | no claim | MEM | H0 | class-token credit, e.g. `fuzz/f100_json_drop.cp` |
| `json json_parse_file(const char *path, error *err)` | `json.hp:181` | file + parsed document (owned) | borrowed path; `NULL` → invalid json + FAIL | `yyjson_read_file` failure → invalid + `IO` | caller `drop()` | no claim | SIL | H1 | 5 files, e.g. `json_test.cp`, `deep_json_types.cp`, `regressions/adv_json_fs_net.cp` |
| `json_view json_view_of(const yyjson_val *value)` | `json.hp:197` | borrowed value | never frees | none | none | no claim | SIL | I | 5 files, e.g. `deep_json_task5.cp`, `stdlib_hardening_test.cp` |
| `void json_free(char *text)` | `json.hp:202` | caller-owned buffer | takes ownership by freeing; `text==NULL` is safe (`free(NULL)`) | none | frees buffer | host `free` threading rules | SIL | H0 | 17 files, e.g. `deep_cross_family.cp`, `fuzz/f100_json_drop.cp` |
| `json_builder(void)` | `json.hp:210` | new mutable document (owned) | owns on success | allocation failure → invalid builder (no error record) | `drop()` frees | no claim | MEM | H0 | verified `deep_json_stress.cp:219`, `deep_cross_family.cp:449` |
| `int valid(void)` | `json.hp:220` | none | none | doc and root non-NULL | none | no claim | MEM | I | covered by `valid` above |
| `int add_int(const char *key, int value, error *err)` | `json.hp:224` | owned mutable document | `key` borrowed; `!valid() \|\| key==NULL` → FAIL | yyjson failure → `MEMORY` | none | no claim | MEM | I | 14 files for name; e.g. `json_test.cp`, `deep_json_task5.cp` |
| `int add_double(const char *key, double value, error *err)` | `json.hp:239` | same | same + non-finite → `RANGE` | yyjson failure → `MEMORY` | none | no claim | MEM | I | 8 files, e.g. `json_test.cp`, `deep_json_types.cp` |
| `int add_bool(const char *key, int value, error *err)` | `json.hp:259` | same | `value != 0` coercion | yyjson failure → `MEMORY` | none | no claim | MEM | I | 8 files, e.g. `json_test.cp`, `deep_json_types.cp` |
| `int add_string(const char *key, const char *value, error *err)` | `json.hp:275` | same | `value` borrowed; yyjson copies it | yyjson failure → `MEMORY` | none | no claim | MEM | H1 | 10 files, e.g. `deep_cross_family.cp`, `json_test.cp`, `apps/legacy_examples/json_demo.cp` |
| `char *write(size_t *length)` | `json.hp:290` | `yyjson_mut_write` allocation | returned buffer caller-owned, `json_free` it | invalid builder → NULL | caller must free | no claim | MEM | I | receiver-verified `json_test.cp:65`, `deep_cross_family.cp:465` |
| `void drop(void)` | `json.hp:296` | owned document + root | nulls both; idempotent | none | `yyjson_mut_doc_free` | no claim | MEM | H0 | class-token credit, e.g. `fuzz/f100_json_drop.cp` |

### 3.5 `stdlib/net.hp` — 14 callables

Layouts: `connection { int fd; int owns; }`, `listener { int fd; int owns; }`.
`owns=1` means the handle closes the descriptor in `drop()`; borrowed handles
and moved-from handles (`fd=-1`) never close. `is_valid()` treats fds 0-2 as
invalid unless owned. No error record is taken by the constructors or
`accept()`; failure is signalled by an invalid handle.

| Callable / signature | Loc | Resource | Ownership / nullability | Failure | Cleanup | TS | ABI | Doc | Observed |
|---|---|---|---|---|---|---|---|---|---|
| `connection(int descriptor)` | `net.hp:24` | socket fd | owns iff `descriptor >= 0` | descriptor < 0 → inert handle | `drop()` | no claim | MEM | H0 | verified `safety_regressions.cp:95`, `stdlib_hardening_test.cp:40` |
| `int is_valid(void)` | `net.hp:31` | none | none | `fd >= 3 \|\| (owns && fd >= 0)` | none | no claim | MEM | H1 | 28 files for name; net receivers e.g. `deep_net.cp`, `net_test.cp` |
| `int descriptor(void)` | `net.hp:35` | none | none | returns raw fd, even -1 | none | no claim | MEM | I | 27 files, e.g. `deep_net.cp`, `net_test.cp`, `apps/legacy_examples/net_demo.cp` |
| `int read(char *buf, size_t capacity, error *err)` | `net.hp:41` | socket fd | `buf` caller-owned; invalid handle, `buf==NULL`, `capacity==0` → FAIL; capacity clamped to `INT_MAX` | socket error → FAIL; 0 bytes → `STDLIB_EOF`; retries `EINTR` | none | no claim | MEM | H1 | receiver-verified `net_valid.cp:13`, `apps/legacy_examples/net_demo.cp:43` |
| `int write(const char *data, size_t length, error *err)` | `net.hp:64` | socket fd | `data` borrowed; invalid handle or `data==NULL` → FAIL | send loop: EINTR retried, 0 progress/error → FAIL; partial sends retried | none | no claim | MEM | H1 | receiver-verified `net_valid.cp:15`, `apps/legacy_examples/net_demo.cp:51` |
| `void drop(void)` | `net.hp:93` | owned socket fd | closes only if `owns && fd>=0`, then sets fd=-1/owns=0 | none | idempotent | no claim | MEM | H0 | class-token credit, e.g. `compile_time/net_double_close.cp` |
| `listener(int port)` | `net.hp:106` | listening TCP socket on loopback | owns on success; invalid port → inert handle | create/bind/listen failure closes fd and leaves inert (`fd=-1`) | `drop()` | no claim | MEM | H1 | verified `deep_net.cp:182,206,215-216` |
| `int is_valid(void)` | `net.hp:132` | none | none | same fd rule as connection | none | no claim | MEM | H1 | covered by `is_valid` above |
| `int descriptor(void)` | `net.hp:136` | none | none | returns raw fd | none | no claim | MEM | I | covered by `descriptor` above |
| `int bound_port(void)` | `net.hp:140` | socket via `getsockname` | none | invalid handle or `getsockname` failure → 0 | none | no claim | MEM | I | 8 files, e.g. `deep_net.cp`, `net_test.cp`, `apps/legacy_examples/net_demo.cp` |
| `connection accept(void)` | `net.hp:150` | accepted socket fd | returned connection owns fd; invalid listener → inert connection; EINTR retried | no error record | caller `drop()` | no claim | MEM | I | receiver-verified `deep_net.cp`, `net_test.cp`, `apps/legacy_examples/net_demo.cp` |
| `void drop(void)` | `net.hp:161` | owned listening socket | closes only if owned, then nulls | none | idempotent | no claim | MEM | H0 | class-token credit |
| `listener listener_borrow(int fd)` | `net.hp:170` | borrowed fd | `owns=0`; `drop()` never closes it | none | none | no claim | SIL | H1 | 8 files, e.g. `deep_net.cp`, `net_test.cp`, `compile_time/net_valid_borrow.cp` |
| `connection connection_borrow(int fd)` | `net.hp:177` | borrowed fd | `owns=0`; `drop()` never closes it | none | none | no claim | SIL | H1 | 13 files, e.g. `deep_net.cp`, `deep_net_task5.cp`, `net_test.cp` |

Parser note: an older coverage script reports a 15th `net.hp` entry,
`listener.listen`, from the raw POSIX `listen(this->fd, 1)` call at
`net.hp:120`. There is no such member function; this inventory does not count
it.

### 3.6 `stdlib/error.hp` — 42 callables

ABI-critical types: `error { int failed; int kind; cplus_exit_code status; char text[STDLIB_ERROR_SIZE]; }` (256-byte text buffer);
`stdlib_state_t` (`:109-122`), `stdlib_meta_slot` (`:102-107`),
`stdlib_telemetry_stats` (`:83-94`). None of these layouts may change.
Telemetry counters are atomic (`__atomic_*`); `stdlib_state.meta_lock` is a
spinlock protecting the metadata side table; the per-thread `reporting` flag in
`stdlib_note` prevents callback recursion.

| Callable / signature | Loc | Resource | Ownership / nullability | Failure | Cleanup | TS | ABI | Doc | Observed |
|---|---|---|---|---|---|---|---|---|---|
| `void stdlib_tm_add(int index, unsigned long long amount)` | `error.hp:127` | telemetry counter | none; no args checked | no-op when telemetry disabled | none | atomic relaxed add | SIL | H0 | **UNTESTED (0 files)** |
| `void stdlib_telemetry_set_enabled(int enabled)` | `error.hp:133` | telemetry flag | none | none | none | atomic relaxed store | SIL | H0 | 32 files, e.g. `deep_vector_task4.cp`, `container_safety/cs_stress.cp` |
| `int stdlib_telemetry_is_enabled(void)` | `error.hp:138` | telemetry flag | none | none | none | atomic relaxed load | SIL | H0 | 2 files: `containers_safety_test.cp`, `system_safety/error_hub_core.cp` |
| `void stdlib_telemetry_reset(void)` | `error.hp:142` | all telemetry counters | none | none | zeroes counters | atomic stores | SIL | H0 | 32 files, e.g. `deep_vector_task4.cp`, `container_safety/cs_stress.cp` |
| `void stdlib_telemetry_snapshot(stdlib_telemetry_stats *out)` | `error.hp:148` | telemetry counters | `out` caller-owned; `NULL` → no-op | none | none | atomic relaxed loads (non-atomic snapshot) | SIL | H0 | 31 files, e.g. `deep_vector_task4.cp`, `container_safety/cs_alloc.cp` |
| `void stdlib_set_error_callback(stdlib_error_callback callback)` | `error.hp:163` | global callback | `callback` may be NULL (disables) | none | none | atomic release store; re-entrancy guarded in `stdlib_note` | SIL | H0 | 25 files, e.g. `deep_vector_task4.cp`, `deep_cross_family.cp` |
| `const char *stdlib_category_name(int category)` | `error.hp:167` | none | returns static string; caller must not free | unknown → `"unknown"` | none | pure | SIL | H0 | 6 files, e.g. `containers_safety_test.cp`, `fuzz/f100_error_obj.cp` |
| `void stdlib_note(int category, cplus_exit_code code, const char *message)` | `error.hp:194` | telemetry counters + callback | `message` borrowed; NULL → `""` passed to callback | none (`void`); drops re-entrant reports | none | per-thread `reporting` guard; atomic callback load | SIL | H1 | 6 files, e.g. `memory_ownership/mo_alloc_edges.cp`, `fuzz/f100_error_obj.cp` |
| `void stdlib_alloc_fail_at(long n)` | `error.hp:227` | injection counters | none; `n<=0` disables | none | resets fail_from/seq | atomic stores only (not CAS) | SIL | H0 | 21 files, e.g. `deep_vector_task4.cp`, `container_safety/cs_alloc.cp`, `libc/test_alloc_fault_matrix.c` |
| `void stdlib_alloc_fail_after(long n)` | `error.hp:234` | injection counters | none | none | resets fail_at/seq | atomic stores | SIL | H0 | 13 files, e.g. `deep_file_lifecycle.cp`, `deep_io_token.cp` |
| `void stdlib_alloc_fail_reset(void)` | `error.hp:241` | injection counters | none | none | zeroes fail_at/fail_from/seq | atomic stores | SIL | H0 | 38 files, e.g. `deep_vector_task4.cp`, `deep_file_lifecycle.cp` |
| `long stdlib_alloc_seq(void)` | `error.hp:249` | injection counter | none | none | none | atomic relaxed load | SIL | H0 | 8 files, e.g. `deep_vector_task4.cp`, `container_safety/cs_alloc.cp` |
| `void stdlib_meta_acquire(void)` | `error.hp:256` | metadata spinlock | none | spins until acquired | caller must `stdlib_meta_release_lock` | atomic exchange acquire | SIL | H0 | **UNTESTED (0 files)** |
| `void stdlib_meta_release_lock(void)` | `error.hp:261` | metadata spinlock | none | none | releases lock | atomic store release | SIL | H0 | **UNTESTED (0 files)** |
| `size_t stdlib_meta_hash(const void *p)` | `error.hp:265` | none | `p` borrowed | none | none | pure | SIL | H0 | **UNTESTED (0 files)** |
| `stdlib_meta_slot *stdlib_meta_find(const void *p)` | `error.hp:273` | metadata side table | returns borrowed slot; `NULL`/tomb → NULL | not found → NULL | none | **must be called under `meta_lock`** (callers acquire) | SIL | H0 | **UNTESTED (0 files)** |
| `void stdlib_meta_record(const void *p, size_t byte_size, size_t elem_size)` | `error.hp:289` | metadata side table (uses host `calloc`/`free`) | `p` borrowed; `NULL` → no-op; `elem_size==0` maps to 1 | if growth via `calloc` fails, the existing table is still used; if no table exists yet, no metadata is recorded (no error is reported) | acquires/releases lock internally | spinlock-protected | SIL | H0 | 1 file: `agent2/extra_error_metadata.cp:265` |
| `void stdlib_meta_forget(const void *p)` | `error.hp:348` | metadata side table | `p` borrowed; `NULL` → no-op; tombstones slot | none | acquires/releases lock internally | spinlock-protected | SIL | H0 | 1 file: `agent2/abi_safety_meta_integrity.cp:210,226,245` |
| `void *stdlib_alloc(size_t bytes)` | `error.hp:365` | managed heap + metadata | returned block caller-owned; release only via `stdlib_release` | `bytes==0` → 1; over limit/injected/refused → NULL + telemetry | caller `stdlib_release` | atomic seq, spinlock in metadata | SIL | H1 | 27 files, e.g. `deep_file_lifecycle.cp`, `container_safety/cs_generation.cp` |
| `void *stdlib_calloc(size_t count, size_t element_size)` | `error.hp:395` | managed heap + metadata | same as `stdlib_alloc` | product overflow or over limit → NULL; zeroed on success | `stdlib_release` | same | SIL | H1 | 9 files, e.g. `deep_cross_family.cp`, `deep_error_vector.cp` |
| `void *stdlib_realloc(void *ptr, size_t bytes)` | `error.hp:422` | managed heap + metadata (calls `cplus_realloc`, §2.1) | `ptr` must be a live stdlib block; NULL ptr delegates to `stdlib_alloc` | over limit or non-live ptr → NULL, original untouched; failure-atomic | `stdlib_release` on success | spinlock-protected metadata | SIL | H1 | 8 files, e.g. `container_safety/cs_generation.cp`, `libc/test_stdlib_realloc.cp` |
| `size_t stdlib_safe_div(size_t a, size_t b)` | `error.hp:462` | none | none | `b==0` → 0 | none | pure | SIL | H1 | **UNTESTED (0 files)** |
| `size_t stdlib_alloc_size(const void *ptr)` | `error.hp:477` | metadata | `ptr` borrowed | unknown ptr → 0 | none | spinlock-protected | SIL | H0 | 10 files, e.g. `container_safety/cs_generation.cp`, `containers_safety_test.cp` |
| `size_t stdlib_alloc_elem_size(const void *ptr)` | `error.hp:488` | metadata | `ptr` borrowed | unknown ptr → 0 | none | spinlock-protected | SIL | H0 | 4 files, e.g. `containers_safety_test.cp`, `memory_ownership/mo_alloc_edges.cp` |
| `size_t stdlib_alloc_length(const void *ptr)` | `error.hp:499` | metadata | `ptr` borrowed | unknown ptr → 0 | none | spinlock-protected | SIL | H0 | 6 files, e.g. `container_safety/cs_generation.cp`, `deep_cross_family.cp` |
| `unsigned long long stdlib_alloc_generation(const void *ptr)` | `error.hp:513` | metadata | `ptr` borrowed | unknown ptr → 0 | none | spinlock-protected | SIL | H0 | 14 files, e.g. `container_safety/cs_generation.cp`, `regressions/adv_stale_views.cp` |
| `int stdlib_alloc_check_generation(const void *ptr, unsigned long long gen)` | `error.hp:524` | metadata | `ptr` borrowed; `NULL`/`gen==0` → 0 | mismatch → 0 | none | spinlock-protected | SIL | H0 | 14 files, e.g. `container_safety/cs_generation.cp`, `fuzz/f100_mem_tokens.cp` |
| `int stdlib_alloc_query_length(const void *ptr, size_t elem_size, size_t *out_count)` | `error.hp:538` | metadata | `out_count` caller-owned (zeroed on entry); `NULL` ptr/out → -1 | unknown ptr → -1 | none | spinlock-protected | SIL | H0 | 6 files, e.g. `containers_safety_test.cp`, `memory_ownership/mo_alloc_edges.cp` |
| `int stdlib_transfer_ownership(void **dst, void **src)` | `error.hp:558` | metadata + two handles | `*src` moved to `*dst`, `*src` nulled; invalid handles/dst non-NULL/dst==src → -1 | non-live source → -1 + `OWNERSHIP` note | bumps generation | spinlock-protected | SIL | H0 | 6 files, e.g. `container_safety/cs_generation.cp`, `deep_cross_family.cp` |
| `void stdlib_release(void *ptr)` | `error.hp:582` | managed heap + metadata | consumes `ptr`; `NULL` → no-op | none | `stdlib_meta_forget` + `cplus_release` | spinlock-protected | SIL | H1 | 17 files, e.g. `container_safety/cs_generation.cp`, `deep_cross_family.cp` |
| `error(void)` constructor | `error.hp:602` | none | initializes fields | no failure | none | none stated | MEM (264-byte layout) | H0 | verified `deep_file_lines.cp:23`, `deep_net.cp:169` |
| `int ok(void)` | `error.hp:609` | none | none | `!failed` | none | pure | MEM | H0 | 252 files for name (ambiguous); receiver-verified `compile_time/error_valid.cp:9` |
| `int failed_now(void)` | `error.hp:613` | none | none | `failed` | none | pure | MEM | H0 | 52 files; receiver-verified `compile_time/error_valid.cp:9` |
| `int code(void)` | `error.hp:617` | none | none | `(int)status` | none | pure | MEM | H0 | 71 files; receiver-verified `compile_time/error_valid.cp:11` |
| `const char *what(void)` | `error.hp:621` | inline text buffer | returns internal pointer; valid while `error` lives; do not free | empty string initially | none | pure read | MEM | H0 | 48 files; receiver-verified `compile_time/error_valid.cp:21` |
| `int category(void)` | `error.hp:627` | none | none | OK category when not failed | none | pure | MEM | H0 | 75 files; receiver-verified `compile_time/error_valid.cp:11` |
| `void clear(void)` | `error.hp:631` | inline text buffer | none | resets via `error_reset` | clears message | same as `error_reset` | MEM | H0 | 60 files for name (ambiguous); receiver-verified `compile_time/error_valid_reset.cp` |
| `void set(cplus_exit_code code, const char *message)` | `error.hp:635` | inline text buffer + telemetry | `message` borrowed; NULL → empty text | derives category in `error_fail` | none | callback guard in `error_fail_cat` | MEM | H0 | 50 files for name (ambiguous); receiver-verified `compile_time/error_valid.cp:14` |
| `void set_category(int category, cplus_exit_code code, const char *message)` | `error.hp:639` | inline text buffer + telemetry | `message` borrowed | records explicit category | none | same | MEM | H0 | 6 files, e.g. `compile_time/error_valid.cp:24`, `deep_error_vector.cp` |
| `void error_reset(void *target)` | `error.hp:645` | caller's `error` object | `target` may be NULL (no-op); otherwise cast to `error*` | none | zeroes fields | none stated | SIL | H0 | 14 files, e.g. `negative.cp`, `compile_time/error_valid_reset.cp` |
| `void error_fail(void *target, cplus_exit_code code, const char *message)` | `error.hp:655` | caller's `error` + telemetry | `message` NULL → empty text; derives category from code | none | sets failed/status/text | callback guard | SIL | H0 | 7 files, e.g. `deep_error_vector.cp`, `regressions/adv_null_zero.cp` |
| `void error_fail_cat(void *target, int category, cplus_exit_code code, const char *message)` | `error.hp:671` | caller's `error` + telemetry | `message` NULL → empty text; truncates at 255 bytes + NUL | none | sets failed/kind/status/text | callback guard | SIL | H1 | 7 files, e.g. `deep_cross_family.cp`, `fuzz/f100_error_obj.cp` |

Note: `error_reset`/`error_fail`/`error_fail_cat` are forward-declared at
`error.hp:590-594` so class methods can call them; they are the same three
callables defined at `:645/:655/:671`, not additional API.

### 3.7 `stdlib/thread.hp` — 21 callables

Layouts: `mutex { void *handle; }`, `guard { mutex *owner; }`,
`condvar { pthread_cond_t *cv; }`, `thread { pthread_t handle; int joinable; }`.
`guard` borrows its mutex and clears `owner` on `unlock()` so
`unlock()` + `drop()` cannot double-unlock. `thread` is move-only; `drop()`
joins if still joinable.

| Callable / signature | Loc | Resource | Ownership / nullability | Failure | Cleanup | TS | ABI | Doc | Observed |
|---|---|---|---|---|---|---|---|---|---|
| `mutex(void)` ctor | `thread.hp:32` | `cplus_mutex_new()` handle | owns handle | `NULL` handle → invalid mutex | `drop()` | n/a | MEM | H0 | verified `deep_thread_guard.cp:44`, `deep_thread_task5.cp:114` |
| `int is_valid(void)` | `thread.hp:36` | none | none | `handle != NULL` | none | pure | MEM | H0 | covered by `is_valid` above; e.g. `fuzz/f100_thread_condvar.cp:113` |
| `void lock(void)` | `thread.hp:40` | mutex handle | no-op if `handle==NULL` | none (`void`) | `unlock()` | serialises callers | MEM | H0 | 23 files; verified `deep_thread_guard.cp:58`, `m.lock()` |
| `void unlock(void)` | `thread.hp:45` | mutex handle | no-op if `handle==NULL` | none | none | serialises callers | MEM | H0 | 23 files; verified `deep_thread_guard.cp:59` |
| `void *native(void)` | `thread.hp:50` | borrowed `pthread_mutex_t*` | caller must not destroy it | n/a | none | no claim | MEM | H0 | 34 files for name; thread receivers e.g. `deep_thread_guard.cp` |
| `void drop(void)` | `thread.hp:54` | owned mutex handle | `cplus_mutex_free`, then NULL; idempotent | none | frees | no claim | MEM | H0 | class-token credit, e.g. `deep_thread_guard.cp` |
| `guard(mutex *owner)` ctor | `thread.hp:66` | borrowed mutex | `NULL` owner → no-op guard | none | `drop()` unlocks | single-thread scope-bound | MEM | H1 | verified `deep_thread_guard.cp:53,94` |
| `int holds(void)` | `thread.hp:72` | none | none | `owner != NULL` | none | pure | MEM | H0 | verified `deep_thread_guard.cp:54-56` (`g1.holds()`) |
| `void unlock(void)` | `thread.hp:76` | borrowed mutex | clears `owner` after `mutex_unlock` | none | idempotent | single-thread | MEM | H0 | verified `deep_thread_guard.cp:55,65-66` |
| `void drop(void)` | `thread.hp:83` | borrowed mutex | nulls `owner` after unlock | none | idempotent | single-thread | MEM | H0 | class-token credit |
| `condvar(void)` ctor | `thread.hp:94` | `pthread_cond_t` via `stdlib_alloc` | owns cv; init failure → `stdlib_release`, cv=NULL | allocation/init failure → invalid | `drop()` | n/a | MEM | H1 | verified `containers_safety_test.cp:303`, `fuzz/f100_thread_condvar.cp:96` |
| `int is_valid(void)` | `thread.hp:102` | none | none | `cv != NULL` | none | pure | MEM | H0 | covered by `is_valid`; verified `fuzz/f100_thread_condvar.cp:113` |
| `int wait(mutex *m)` | `thread.hp:106` | cv + borrowed mutex | `cv==NULL \|\| m==NULL \|\| m->handle==NULL` → -1 | `pthread_cond_wait != 0` → -1 | none | **caller must hold `m`** (header contract) | MEM | H0 | verified `regressions/adv_thread_contention.cp:244`, `fuzz/f100_thread_condvar.cp:241` |
| `int signal(void)` | `thread.hp:114` | cv | invalid cv → -1 | `pthread_cond_signal != 0` → -1 | none | callable without holding mutex | MEM | H0 | verified `regressions/adv_thread_contention.cp:247`, `fuzz/f100_thread_condvar.cp:193` |
| `int broadcast(void)` | `thread.hp:120` | cv | invalid cv → -1 | `pthread_cond_broadcast != 0` → -1 | none | callable without holding mutex | MEM | H0 | 7 files, e.g. `containers_safety_test.cp`, `agent2/extra_thread_condvar.cp:223` |
| `void drop(void)` | `thread.hp:126` | owned cv | destroy + `stdlib_release`, null cv; idempotent | none | frees | no claim | MEM | H0 | class-token credit, e.g. `fuzz/f100_thread_condvar.cp:240` |
| `thread(void)` ctor | `thread.hp:140` | none (`joinable=0`) | none | no failure | none | n/a | MEM | H0 | verified `deep_thread_task5.cp:119` |
| `int is_joinable(void)` | `thread.hp:144` | none | none | `joinable != 0` | none | pure | MEM | H0 | 6 files, e.g. `deep_thread_task5.cp`, `regressions/adv_lifecycle.cp` |
| `int start(void *(*fn)(void *), void *arg, error *err)` | `thread.hp:148` | new pthread | `fn` required; `arg` opaque caller-owned (uintptr_t round-trip) | null fn / already running / `pthread_create!=0` → FAIL | `drop()`/`join()` joins | move-only; one active start per object | MEM | H1 | verified `deep_thread_task5.cp:120`, `agent2/extra_thread_condvar.cp:169` |
| `int join(void **out, error *err)` | `thread.hp:174` | joinable pthread | `out` caller-owned, may be NULL | not joinable / `pthread_join!=0` → FAIL; clears joinable either way | consumes joinability | caller synchronisation | MEM | H0 | verified `deep_thread_task5.cp:126,133`, `agent2/extra_thread_condvar.cp:177` |
| `void drop(void)` | `thread.hp:191` | joinable pthread | joins if still joinable, clears flag; idempotent | none | joins | no claim | MEM | H0 | class-token credit, e.g. `agent1_handoff/cfg_ownership/conditional_drop.cp` |

### 3.8 `stdlib/log.hp` — 16 callables

Class `log_sink` layout: `FILE *stream; int owns; int threshold;`. Owned sinks
close the stream in `drop()`; borrowed sinks (`log_stderr`/`log_stdout`, or any
sink constructed with `owns=0`) never close process streams. `write()` holds
`flockfile()` across the whole line so concurrent threads cannot interleave a
line (header claim at `log.hp:5-6,95-97`).

| Callable / signature | Loc | Resource | Ownership / nullability | Failure | Cleanup | TS | ABI | Doc | Observed |
|---|---|---|---|---|---|---|---|---|---|
| `const char *log_level_name(int level)` | `log.hp:28` | none | returns static string | unknown → `"UNKNOWN"` | none | pure | SIL | I | 4 files, e.g. `deep_log_task5.cp`, `log_test.cp` |
| `int log_level_parse(const char *text, error *err)` | `log.hp:47` | none | `text` borrowed; NULL → -1 + `INVALID` | unknown level → -1 + `INVALID` | none | uses `strcasecmp` (locale) | SIL | I | 4 files, e.g. `deep_log_task5.cp`, `log_test.cp` |
| `log_sink(FILE *stream, int owns, int threshold)` ctor | `log.hp:69` | `FILE*` | `owns` forced to 0 when `stream==NULL` | invalid sink on NULL stream | `drop()` | no claim | MEM | H0 | verified `deep_cross_family.cp:523-524`, `stdlib_hardening_test.cp:51` |
| `int valid(void)` | `log.hp:75` | none | none | `stream != NULL` | none | pure | MEM | I | 34 files for name; verified `log_test.cp:26`, `fuzz/f100_log_lines.cp:191` |
| `FILE *native(void)` | `log.hp:79` | borrowed `FILE*` | caller must not close an owned stream through it | n/a | none | no claim | MEM | I | 34 files for name; verified `log_test.cp:39` |
| `int level(void)` | `log.hp:83` | none | none | returns threshold | none | pure | MEM | I | 18 files for name; verified `log_test.cp:26,39` |
| `void set_level(int level)` | `log.hp:87` | none | none | none | none | no synchronisation | MEM | I | 4 files, e.g. `deep_log_task5.cp`, `log_test.cp` |
| `int enabled(int severity)` | `log.hp:91` | none | none | false when sink invalid | none | pure | MEM | I | 21 files; verified `fuzz/f100_log_lines.cp:217` |
| `int write(int severity, const char *message, const char *const *keys, const long long *values, size_t field_count, const char *file, int line, error *err)` | `log.hp:98` | `FILE*` stream + timestamp helpers | `message`/`keys`/`values`/`file` borrowed; `field_count>0` with NULL arrays → FAIL | invalid sink → FAIL; null arrays → FAIL; truncation → `BOUNDS`; `fwrite` short → `IO` | `funlockfile` on every exit path | flockfile serialises a whole line | MEM | H1 | verified `log_test.cp:30-36`, `fuzz/f100_log_lines.cp:226` |
| `int info(const char *message, error *err)` | `log.hp:179` | delegates to `write` | borrowed message | delegates | delegates | inherits `write` | MEM | I | 8 files, e.g. `deep_log_task5.cp`, `log_test.cp` |
| `int warn(const char *message, error *err)` | `log.hp:183` | delegates to `write` | borrowed message | delegates | delegates | inherits `write` | MEM | I | 5 files, e.g. `deep_log_task5.cp`, `log_test.cp` |
| `int failure(const char *message, error *err)` | `log.hp:187` | delegates to `write` | borrowed message | delegates | delegates | inherits `write` | MEM | I | 96 files for name (ambiguous); verified `log_test.cp:41` |
| `void drop(void)` | `log.hp:192` | owned `FILE*` | closes only if `owns`, then nulls; idempotent | none | closes owned stream only | no claim | MEM | H1 | class-token credit, e.g. `fuzz/f100_log_lines.cp` |
| `log_sink log_open_file(const char *path, int threshold, error *err)` | `log.hp:201` | `fopen(path, "a")` | returned sink owns the stream on success; borrowed path; NULL path → FAIL | `fopen==NULL` → FAIL + invalid sink | caller `drop()` | no claim | SIL | H1 | 5 files, e.g. `deep_log_task5.cp`, `log_test.cp`, `stdlib_hardening_test.cp` |
| `log_sink log_stderr(int threshold)` | `log.hp:221` | borrowed `stderr` | `owns=0`; `drop()` never closes stderr | none | none | inherits `write` for lines | SIL | H1 | 3 files, e.g. `log_test.cp`, `fuzz/f100_log_lines.cp` |
| `log_sink log_stdout(int threshold)` | `log.hp:226` | borrowed `stdout` | `owns=0`; `drop()` never closes stdout | none | none | inherits `write` for lines | SIL | H1 | 3 files, e.g. `log_test.cp`, `fuzz/f100_log_lines.cp` |

## 4. DOCUMENTED (contract prose found in the headers)

All 151 callables are documented by header prose at some level. The
distinction that matters for downstream work is:

* **H1 — callable-specific comment** (e.g. `io.hp:27-29` for `stdlib_io_token`,
  `file.hp:205` for `close`, `json.hp:101` for `get_string`, `json.hp:274` for
  `add_string`, `net.hp:29-30` for `is_valid`, `thread.hp:62` for `guard`,
  `thread.hp:160-163` for `start`, `log.hp:95-97` for `write`,
  `error.hp:416-421` for `stdlib_realloc`, `error.hp:680-683` for
  `error_fail_cat`).
* **H0 — covered only by module-level prose** (e.g. the ownership paragraph at
  `file.hp:1-7`, the "every reader returns..." paragraph at `io.hp:6-8`, the
  checked-allocator/telemetry contract at `error.hp:1-29`, the RAII and
  thread-safety paragraph at `thread.hp:1-20`).
* **I — no prose; behaviour visible only in the body** (trivial type probes and
  accessors such as `is_open`, `native`, `level`, `set_level`, `enabled`,
  `log_level_name`, `log_level_parse`, `fs_exists`/`fs_is_file`/`fs_is_dir`,
  most `json_view` type predicates and getters, `json_view::at`/`size`, and
  several `file` methods).

DOCUMENTED is a **claim about text**, not about correctness. For example,
`log_sink::write` is documented to serialise a whole line with `flockfile()`;
that is what the source says, not a measured concurrency result.

## 5. OBSERVED (literal call sites in `tests/stdlib/`, `tests/libc/`, `tests/crt/unit/`)

145 of the 151 callables have at least one matching source file. Representative
files for every callable are in the last column of §3. Concentration of
evidence:

* Deep fixtures carry most API calls: `tests/stdlib/deep_io_token.cp`,
  `deep_io_1byte.cp`, `deep_io_task2.cp`, `deep_file_lines.cp`,
  `deep_file_lifecycle.cp`, `deep_file_task2.cp`, `deep_fs_task5.cp`,
  `deep_json_task5.cp`, `deep_json_stress.cp`, `deep_json_types.cp`,
  `deep_net.cp`, `deep_net_task5.cp`, `deep_thread_guard.cp`,
  `deep_thread_task5.cp`, `deep_log_task5.cp`, `deep_error_task5.cp`,
  `deep_error_vector.cp`, `deep_cross_family.cp`.
* Compile-time/negative fixtures exercise lifecycle rules:
  `tests/stdlib/compile_time/file_double_close.cp`,
  `net_double_close.cp`, `net_use_after_close.cp`, `thread_double_unlock.cp`,
  `json_dangling_ref.cp`, `error_use_after_reset.cp` and siblings.
* Fuzz/regression corpora supply volume: `tests/stdlib/fuzz/f100_*.cp`,
  `tests/stdlib/regressions/adv_*.cp`, `tests/stdlib/agent2/extra_*.cp`,
  `tests/stdlib/system_safety/*.cp`.
* `tests/libc` contributes notably `validate_contracts.cp`,
  `test_contract_integrity.cp`, `test_contract_consumption.cp`,
  `test_compiler_contract_alignment.cp`, `test_stdlib_realloc.cp` and
  `test_alloc_fault_matrix.c`. `tests/crt/unit/` contributes almost nothing to
  these eight headers — its matches are generic identifiers (`failure`,
  `signal`, `file_temp`-style names) whose receivers belong to the C CRT tests,
  not to these `.hp` APIs.

**Important limitation.** A literal token is not proof of execution, of the
correct overload, or of the behaviour asserted. Generic names are biased
upward: `size` (145 files), `at` (162), `valid` (162), `drop` (140), `ok` (252),
`write` (112), `read` (115) include calls on `vector`, `string`, `collections`,
`std::`-like types and plain C locals. For those APIs the §3 table cites a
receiver-verified example rather than relying on the raw file count.

## 6. UNTESTED (no literal call-site file found)

All six are internal `stdlib/error.hp` helpers. They are reached indirectly by
other APIs (notably `stdlib_alloc`/`stdlib_realloc`/`stdlib_release`), so this
is "no direct literal call site", not "unreachable":

| Callable | Loc | Why it is still reached indirectly |
|---|---|---|
| `stdlib_tm_add` | `error.hp:127` | called from `stdlib_note`, `stdlib_alloc`, `stdlib_calloc`, `stdlib_realloc`, `stdlib_release` |
| `stdlib_meta_acquire` | `error.hp:256` | called from `stdlib_meta_record`, `stdlib_realloc`, the `stdlib_alloc_*` query helpers, `stdlib_transfer_ownership` |
| `stdlib_meta_release_lock` | `error.hp:261` | same callers, on every exit path |
| `stdlib_meta_hash` | `error.hp:265` | called from `stdlib_meta_find`, `stdlib_meta_record` |
| `stdlib_meta_find` | `error.hp:273` | called from `stdlib_meta_record`, `stdlib_meta_forget`, `stdlib_realloc`, all `stdlib_alloc_*` queries, `stdlib_transfer_ownership` |
| `stdlib_safe_div` | `error.hp:462` | called from `stdlib_alloc_length`, `stdlib_alloc_query_length` |

The two metadata mutators with direct call sites are
`stdlib_meta_record` (`tests/stdlib/agent2/extra_error_metadata.cp:265`) and
`stdlib_meta_forget` (`tests/stdlib/agent2/abi_safety_meta_integrity.cp:210,226,245`);
they are OBSERVED, not UNTESTED.

## 7. ABI guardrails (no layout changes)

1. Do not add, remove or reorder members of `file`, `connection`, `listener`,
   `json_view`, `json`, `json_builder`, `mutex`, `guard`, `condvar`, `thread`,
   `log_sink`, `error`, `stdlib_state_t`, `stdlib_meta_slot` or
   `stdlib_telemetry_stats`.
2. Do not change `STDLIB_ERROR_SIZE` (256) — it is part of `error`'s layout. Do
   not change the `STDLIB_CATEGORY_*`, `STDLIB_TM_*`, `LOG_*` numeric values or
   the `STDLIB_OK`/`STDLIB_EOF`/`STDLIB_FAIL` return codes; callers compare
   against these literals.
3. Keep every free helper `static inline` and keep its signature: these
   headers define no exported symbol of their own. Changing a parameter or
   return type is a source/ABI break for every translation unit that includes
   the header.
4. Keep the two declarations of `cplus_realloc` compatible
   (`cplus_libc.h:33` and `runtime/include/cplus/cplus.h:105`). Do not replace
   the raw `pthread_*` calls in `thread.hp` with the namespaced
   `cplus_pthread_*` wrappers without a separate ABI decision: that changes
   which symbol is linked, even though it does not change a struct layout.
5. Keep the managed-heap discipline: `stdlib_alloc`/`stdlib_calloc`/
   `stdlib_realloc` blocks must be released with `stdlib_release`, and
   `json_view`/`file_borrow`/`listener_borrow`/`connection_borrow` must remain
   non-owning. Changing an `owns` policy is an ownership-contract break even if
   no byte of layout changes.

## 8. Limitations of this inventory

* Static text only. No test was executed; no claim about pass/fail, branch
  coverage, or runtime behaviour is made here.
* The search corpus is exactly `tests/stdlib/`, `tests/libc/` and
  `tests/crt/unit/`, as requested. Tests elsewhere (e.g. `tests/cases/`,
  `tests/runtime_fail/`, `tests/native/`, `apps/legacy_examples/` outside `tests/`) were not
  searched; an API marked UNTESTED is untested **within these three trees**.
* `tests/stdlib/agent2_audit/**/*.md` was excluded from call-site counting
  because it is documentation. Transient `.cplus-tmp-*` files produced by other
  concurrent work were also excluded.
* The counts in §0 are derived from the enumeration in §3 and the searches in
  §5-§6. No percentage, sampling ratio or estimated coverage figure is
  reported.

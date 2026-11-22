# Deep contract for `stdlib/file.hp` — Task 2 uncovered cases

Scope: an **independent** self-checking test for only the cases from Task 2 that
were not already covered by `tests/stdlib/deep_file_lines.cp` and
`tests/stdlib/deep_file_lifecycle.cp`. The test is
`tests/stdlib/deep_file_task2.cp`; this document records its exact stdout, the
verification commands, the cleanup proof, and the observed results.

The header `stdlib/file.hp` was **not** modified by this work. All checks run
against the current header, including its just-fixed exact-fit peek logic in
`file::read_line`.

## Status

| Item | Result |
| --- | --- |
| Checks in `deep_file_task2.cp` | 38 (each prints `<tag> <1|0>`) |
| Defects reproduced against the current header | **0** |
| Default mode | PASS — `FILE_TASK2 38 0`, exit 0 |
| `--strict --sanitize=none` | PASS — `FILE_TASK2 38 0`, exit 0 |
| `--sanitize=undefined` | PASS — `FILE_TASK2 38 0`, exit 0 |
| `--strict --sanitize=undefined` (harness ubsan mode) | PASS — `FILE_TASK2 38 0`, exit 0 |
| Deep harness (`run_deep_tests.sh deep_file_task2`) | 3 passed, 0 failed, 0 skipped |
| stdout identical across all four modes | yes (byte-for-byte) |
| stderr output in any mode | empty |
| Leftover temp files after runs | 0 |

Every binary invocation was bounded by `timeout 30`; no run timed out.

## Coverage (only the uncovered Task 2 cases)

| # | Tags | Contract asserted |
| --- | --- | --- |
| 1 | `empty-open`, `empty-readline-eof`, `empty-read-eof`, `empty-readline-again-eof` | On an empty temp file: `read_line` returns `STDLIB_EOF` with the error clear and every buffer byte still at its `'Z'` sentinel; `read` returns `STDLIB_EOF`; a second `read_line` returns `STDLIB_EOF` with no stale error. |
| 2 | `exact-eof-setup`, `exact-eof-status`, `exact-eof-tell`, `exact-eof-next-eof` | Regression for the just-fixed defect. Content `abcd` in a 5-byte buffer: `STDLIB_OK` / `"abcd"`, error clear, `ftell == 4` after the fit-checking peek consumes EOF, next read `STDLIB_EOF`. |
| 3 | `exact-nl-setup`, `exact-nl-status`, `exact-nl-tell`, `exact-nl-next-eof` | Content `abcd\n` in a 5-byte buffer: `STDLIB_OK` / `"abcd"`, `ftell == 5` (the newline was consumed by the peek), next read `STDLIB_EOF` with no phantom empty line. |
| 4 | `reject-setup`, `reject-status`, `reject-tell`, `reject-next-line` | Content `abcdef\nNEXT\n` in a 5-byte buffer: `STDLIB_FAIL` / `STDLIB_CATEGORY_BOUNDS` / `CPLUS_EXIT_BOUNDS` with `buf[0] == '\0'`; `ftell == 7` (start of `NEXT`, verified rather than assumed); next `read_line` returns `"NEXT"`. |
| 5 | `readfail-open`, `readfail-status`, `readfail-ferror`, `readfail-cleanup` | `read()` on a write-only (`"w"`) named file: `STDLIB_FAIL`, `CPLUS_EXIT_IO`, `STDLIB_CATEGORY_IO`, and `ferror(stream)` set. No crash. |
| 6 | `writefail-open`, `writebytesfail-status`, `writefail-string-zero`, `writefail-content-intact`, `writefail-cleanup` | `write_bytes()` and `write()` on a read-only (`"r"`) named file fail (`STDLIB_FAIL` / `CPLUS_EXIT_IO` / IO category; `write` returns 0). Reopening the file still reads `"KEEP\n"` — content unchanged. |
| 7 | `badpath-open`, `badmode-open`, `badpath-recoverable`, `validpath-cleanup` | `file("/definitely/not/a/real/dir/x", "r")` and `file(valid_path, "q")` are `!is_open()` with `native() == NULL`; `read` then returns `STDLIB_FAIL`/IO and `close` returns `STDLIB_FAIL`/LIFECYCLE; `drop()` is safe. |
| 8 | `bound-open`, `writebytes-zero-nondata`, `seek-cur-roundtrip`, `seek-past-eof-eof`, `seek-invalid-whence`, `flushro-open`, `flush-readonly-ok`, `flushpath-cleanup` | `write_bytes("abc", 0, ...)` is `STDLIB_OK`; `seek(0, SEEK_CUR, ...)` preserves position (`ftell == 2` before and after); `seek(1000, SEEK_SET, ...)` then `read_line` is `STDLIB_EOF`; `flush()` on a read-only stream is `STDLIB_OK`; `seek(0, 999, ...)` is `STDLIB_FAIL` / IO. |
| — | `cleanup-no-leftovers` | Final proof that all four named fixtures were removed. |

`empty-open`, `*-setup`, `*-open`, and `*-cleanup` tags are fixture assertions
included in the same 38-check count; they are not duplicate library cases.

## Exact stdout

Identical in all four build modes. There is no stdin input and no stderr output.

```
empty-open 1
empty-readline-eof 1
empty-read-eof 1
empty-readline-again-eof 1
exact-eof-setup 1
exact-eof-status 1
exact-eof-tell 1
exact-eof-next-eof 1
exact-nl-setup 1
exact-nl-status 1
exact-nl-tell 1
exact-nl-next-eof 1
reject-setup 1
reject-status 1
reject-tell 1
reject-next-line 1
readfail-open 1
readfail-status 1
readfail-ferror 1
readfail-cleanup 1
writefail-open 1
writebytesfail-status 1
writefail-string-zero 1
writefail-content-intact 1
writefail-cleanup 1
badpath-open 1
badmode-open 1
badpath-recoverable 1
validpath-cleanup 1
bound-open 1
writebytes-zero-nondata 1
seek-cur-roundtrip 1
seek-past-eof-eof 1
seek-invalid-whence 1
flushro-open 1
flush-readonly-ok 1
flushpath-cleanup 1
cleanup-no-leftovers 1
FILE_TASK2 38 0
```

`1` means the check passed. The test exits 0 only when the final failure count
is 0; any failing check prints `0` and makes the process exit 1.

## Cleanup proof

- Four named fixtures are created with process-unique names derived from
  `(int)getpid()` via `snprintf` (e.g. `cplus_deep_file_task2_writeonly_<pid>.tmp`).
  PID-unique names let the three harness modes run in parallel without racing
  on each other's files; an early parallel run without them produced spurious
  named-file failures, which disappeared once the names became unique.
- Each named fixture is removed with `remove(path)` and the return value is
  asserted (`readfail-cleanup`, `writefail-cleanup`, `validpath-cleanup`,
  `flushpath-cleanup`).
- The final `cleanup-no-leftovers` check re-opens all four paths with `fopen`
  and requires every open to fail (helper `t2_path_absent`); any survivor sets
  the check to 0.
- `tmpfile()`-backed fixtures (cases 1–4 and the boundary file) are anonymous
  and unlinked by libc on close/drop, so they need no explicit removal.
- Observed after all runs: `ls -1 cplus_deep_file_task2_*.tmp | wc -l` → `0`.

## Commands and per-mode results

Run from the repository root. `<out>` below is an absolute path in the
approved temporary directory so no build artifact is added to the repo.

```sh
# Required: strict, sanitizers off
./build/c+ build tests/stdlib/deep_file_task2.cp --strict --sanitize=none -o <out_strict_none>
timeout 30 <out_strict_none>

# Default
./build/c+ build tests/stdlib/deep_file_task2.cp -o <out_default>
timeout 30 <out_default>

# Undefined-behavior sanitizer
./build/c+ build tests/stdlib/deep_file_task2.cp --sanitize=undefined -o <out_ubsan>
timeout 30 <out_ubsan>

# Harness-equivalent UBSan mode (also verified)
./build/c+ build tests/stdlib/deep_file_task2.cp --strict --sanitize=undefined -o <out_strict_ubsan>
timeout 30 <out_strict_ubsan>

# Harness integration
sh tests/stdlib/run_deep_tests.sh deep_file_task2
```

| Build invocation | Build rc | Run rc | Final stdout line | stderr |
| --- | --- | --- | --- | --- |
| default | 0 | 0 | `FILE_TASK2 38 0` | empty |
| `--strict --sanitize=none` | 0 | 0 | `FILE_TASK2 38 0` | empty |
| `--sanitize=undefined` | 0 | 0 | `FILE_TASK2 38 0` | empty |
| `--strict --sanitize=undefined` | 0 | 0 | `FILE_TASK2 38 0` | empty |

Harness summary:

```
ok       deep_file_task2 (default): FILE_TASK2 38 0
ok       deep_file_task2 (strict-none): FILE_TASK2 38 0
ok       deep_file_task2 (ubsan): FILE_TASK2 38 0
deep stdlib tests: 3 passed, 0 failed, 0 skipped (1 file(s) x 3 modes)
```

## Observed libc behaviour (evidence, not assumptions)

A standalone probe on this host (macOS/Darwin libc) confirmed the platform
semantics the failure cases rely on, and every corresponding library assertion
held:

- `fread` on a `"w"` stream → 0 bytes, `ferror` set, `feof` clear (errno 9):
  `file::read` reports `STDLIB_FAIL` / IO rather than `STDLIB_EOF`.
- `fwrite`/`fputs` on an `"r"` stream → 0 / `EOF`, `ferror` set; the on-disk
  bytes are unchanged: `file::write_bytes` fails and `file::write` returns 0.
- `fflush` on an `"r"` stream → 0: `file::flush` returns `STDLIB_OK`.
- `fopen(path, "q")` → `NULL` (errno 22): invalid-mode construction is closed.
- `fseek(stream, 0, 999)` → -1 (errno 22): `file::seek` reports `STDLIB_FAIL`/IO.
- `fseek(1000, SEEK_SET)` on a short file succeeds; the following read sets
  `feof` and yields `STDLIB_EOF`.

## Defects / contradictions

None. No check contradicts the contract described above, and the exact-fit
regression (case 2) passes against the current `stdlib/file.hp`. No header or
existing test file was changed to obtain a pass; no `file.hp` edit was needed.

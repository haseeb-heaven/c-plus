# Deep contract for `stdlib/file.hp`

Scope: the RAII `file` wrapper (owning vs borrowed `FILE*`), with emphasis on
`file::read_line`. Evidence is from `tests/stdlib/deep_file_lines.cp` (with
`deep_file_lines.stdin`) and `tests/stdlib/deep_file_lifecycle.cp`.

## Status

| Item | Result |
| --- | --- |
| Defects reproduced before fix | **7** (all in `read_line`) |
| Defects fixed | **7** |
| Lifecycle defects found | **0** |
| Checks in `deep_file_lines.cp` | 14 positive/negative assertions |
| Checks in `deep_file_lifecycle.cp` | 17 assertions across 9 groups |
| Default / `--strict --sanitize=none` / `--debug --sanitize=undefined` | PASS / PASS / PASS |
| Stdlib harness regressions | 9 related targets PASS, 0 FAIL |

## Contract decisions

These decisions make `file::read_line` agree with the already-hardened
`stdlib/io.hp::read_line`. A `file` is an owning C+ type (it has `drop()`), so
all failures are recoverable `STDLIB_FAIL` / `STDLIB_EOF` results with an
`error` record; no method calls `cplus_fatal`.

1. **One-byte buffer makes progress.** `fgets(buf, 1, ...)` writes only the NUL
   and consumes nothing, so an unguarded call returns `STDLIB_OK` forever. A
   size-1 buffer now reads one byte at a time: an empty line (`\n` or `\r`, with
   an optional paired `\n`/`\r`) returns `STDLIB_OK` with `buf[0] == '\0'`; a
   line with content is drained and returns `STDLIB_FAIL` / `BOUNDS`; EOF
   returns `STDLIB_EOF`; a stream error returns `STDLIB_FAIL` / `IO`.
2. **No hidden truncation.** If `fgets` fills `size - 1` bytes with no `\n`/`\r`
   and the stream is not at EOF, the line does not fit. The remainder is
   drained up to and including the next newline, `buf[0]` is set to `'\0'`, and
   `STDLIB_FAIL` with `STDLIB_CATEGORY_BOUNDS` / `CPLUS_EXIT_BOUNDS` is
   returned. The stream stays aligned to the following line. End-of-file with
   an unterminated full line is treated the same way (drain reaches EOF).
3. **Oversized length is a bounds error.** `size > INT_MAX` cannot be cast to
   the `int` parameter of `fgets`; it now returns `STDLIB_FAIL` with
   `STDLIB_CATEGORY_BOUNDS` (previously it was lumped into the generic
   "invalid buffer or length" `INVALID` category).
4. **Null/zero buffer stays `UNSUPPORTED`.** `buf == NULL || size == 0` returns
   `STDLIB_FAIL` with `CPLUS_EXIT_UNSUPPORTED` (category `INVALID`), matching
   `file::read` and `file::write_bytes`.
5. **EOF is not an error.** Exhausted input returns `STDLIB_EOF` with the
   `error` record left clear. `ferror` distinguishes a real stream error.
6. **Line terminators.** Trailing `\n` and `\r` are stripped from a successful
   line; a CRLF pair is stripped as one terminator.
7. **Partial `read` is `STDLIB_OK`.** `read` returns `STDLIB_OK` for any
   `fread` of at least one byte, including a short read; EOF is reported by the
   *next* call. This is unchanged. The API intentionally has no out-count:
   callers must request a known number of bytes or use the native handle. This
   is documented as a limitation, not treated as a defect.
8. **Ownership.** A constructed/temp file owns its `FILE*`; `file_borrow`
   never does. `close()` on a borrowed stream detaches without `fclose`; a
   second `close()` is a recoverable `STDLIB_FAIL` with
   `STDLIB_CATEGORY_LIFECYCLE`. `drop()` is idempotent. `cplus_move` zeroes the
   source so its implicit destructor is a no-op. Copying or assigning an
   owning `file` is a compile-time error (verified separately, see Fatal).
9. **Allocation-failure injection is a no-op.** `file.hp` performs no
   `stdlib_alloc`/`stdlib_calloc`; `stdlib_alloc_fail_after(0)` does not
   affect open/read/write/seek/close. `error_fail` only records telemetry.
10. **No leaks.** `tmpfile()`-backed files are unlinked by libc; named files are
    closed but not removed (caller controls the path). The lifecycle test
    removes its named temp file and verifies the open-fd count returns to its
    baseline.

## Defects reproduced (before the fix)

`build/deep-file/lines_def` run with the `.stdin` fixture produced 7 failures:

| # | Case | Observed (buggy) | Expected (contract) |
| --- | --- | --- | --- |
| 1 | overlong line, `buf[8]` | `STDLIB_OK`, truncated `"ABCDEFG"`, category `OK` | `STDLIB_FAIL`, buffer empty, `BOUNDS` |
| 2 | line after overlong | `"HIJKLMN"` (remainder) | `"beta"` (stream drained) |
| 3 | EOF after overlong | `STDLIB_OK` (stream misaligned) | `STDLIB_EOF` |
| 4 | one-byte buffer | 8 calls, no input consumed (hang risk) | 2 calls, both `FAIL`/`BOUNDS`, then EOF |
| 5 | overlong at physical EOF | `STDLIB_OK`, truncated `"0123456"` | `STDLIB_FAIL` / `BOUNDS` |
| 6 | call after case 5 | `STDLIB_OK` | `STDLIB_EOF` |
| 7 | `size = 1 << 31` | category `INVALID` (1) | category `BOUNDS` (5) |

All 7 disappeared after the single `read_line` change. No other `file.hp`
method was modified.

## Test layout and commands

```sh
# build (default / strict / UBSan)
build/c+ build tests/stdlib/deep_file_lines.cp     -o build/deep-file/lines_def
build/c+ build --strict --sanitize=none tests/stdlib/deep_file_lines.cp -o build/deep-file/lines_strict
build/c+ build --debug --sanitize=undefined tests/stdlib/deep_file_lines.cp -o build/deep-file/lines_ubsan
build/c+ build tests/stdlib/deep_file_lifecycle.cp -o build/deep-file/life_def
# ... same flag sets for deep_file_lifecycle.cp

# run (stdin fixture for lines; bounded loop plus `timeout 15` guards hangs)
timeout 15 build/deep-file/lines_def     < tests/stdlib/deep_file_lines.stdin
timeout 15 build/deep-file/lines_strict  < tests/stdlib/deep_file_lines.stdin
timeout 15 build/deep-file/lines_ubsan   < tests/stdlib/deep_file_lines.stdin
timeout 15 build/deep-file/life_def
timeout 15 build/deep-file/life_strict
timeout 15 build/deep-file/life_ubsan

# standard harness integration / regression
tests/stdlib/run_stdlib_tests.sh deep_file
tests/stdlib/run_stdlib_tests.sh foundation
tests/stdlib/run_stdlib_tests.sh negative
tests/stdlib/run_stdlib_tests.sh eof
tests/stdlib/run_stdlib_tests.sh io_edge_cases
tests/stdlib/run_stdlib_tests.sh stdlib_hardening
tests/stdlib/run_stdlib_tests.sh adversarial
tests/stdlib/run_stdlib_tests.sh safety_regressions
tests/stdlib/run_stdlib_tests.sh file_demo
```

All commands exited 0 after the fix. Before the fix, `deep_file_lines.cp`
exited 1 (7 failures) in all three modes; `deep_file_lifecycle.cp` exited 0.

## Success / recoverable / fatal separation

- **Success** (in-process, exit 0): normal lines, exact-fit lines, CRLF,
  short-then-EOF reads, borrowed detaching, move, fd baseline restoration,
  allocation-injection no-op.
- **Recoverable** (in-process, still exit 0 when correctly reported): overlong
  lines, one-byte buffers, null/zero/oversized arguments, null handles, double
  close, closed-handle operations, failed seek.
- **Fatal** (separate one-shot compiler checks; not run in the in-process
  tests): copying an owning `file` and assigning one owning `file` to another
  are rejected by `c+ check` in both default and `--strict` modes with
  `error: resource 'b' cannot be copied from 'a'`. `cplus_move` is accepted.
  `file.hp` itself has no runtime `cplus_fatal` path.

## Known limitations (not changed)

- `read()` does not report the number of bytes actually read; a short read is
  indistinguishable from a full one except by the caller's expectations.
- `read_line` is text-oriented: an embedded NUL byte terminates its view of
  the line, and binary data should be handled with `read`.
- `close()` clears the handle even when `fclose` fails, so a failed close
  cannot be retried; the handle is invalid at that point regardless.

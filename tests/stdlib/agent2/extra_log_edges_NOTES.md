# extra_log_edges — uncovered `stdlib/log.hp` paths

## Owned files

- `tests/stdlib/agent2/extra_log_edges.cp` — the self-checking test (72 checks).
- `tests/stdlib/agent2/extra_log_edges_NOTES.md` — this document.

No other file was created or modified. No headers were edited. Nothing was
staged, committed, or added to any runner's discovery list.

## Provenance

| Item | Value |
|---|---|
| Branch / HEAD | `agent/modern-stdlib` / `e1ce162` |
| Host | macOS arm64 (`uname -m = arm64`), `Darwin ... RELEASE_ARM64_T81` |
| Driver | `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+` |
| Backend | `CPLUS_CLANG=/usr/bin/clang` — Apple clang 16.0.0 (clang-1600.0.26.6), target `arm64-apple-darwin27.2.0` |
| Output dir | `/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/extralog` (outside the repo) |
| Time bound | `timeout 30` on **every** build and **every** run; no timeout occurred |
| Stdin | `/dev/null` (test reads no stdin) |

## Coverage added

The checked-in `log_test.cp` and `deep_log_task5.cp` already cover level
parsing/naming, threshold filtering, open failures, 255/256/4095-byte messages,
enable/disable parity, and 32 open/write/drop cycles. This test adds the paths
that remained untested:

1. **NULL field arrays with `field_count > 0`** — `keys==NULL`, `values==NULL`,
   and both NULL. All three are *not* allowed: `write()` returns `STDLIB_FAIL`
   with `STDLIB_CATEGORY_INVALID` / `CPLUS_EXIT_UNSUPPORTED` and a
   `"null field arrays"` message, and never touches the file (no crash, no
   partial line). Validation happens **before** severity filtering: the check
   is performed with a FATAL threshold and an INFO message and still fails.
   With `field_count == 0` the same NULL arrays are ignored and the write
   succeeds (both NULL and live arrays tested).
2. **NULL key entries / extreme values** — `keys[i]==NULL` is substituted by
   `"?"`, so `NULL/42` and `NULL/LLONG_MIN` produce `?=42` and
   `?=-9223372036854775808`; no literal `(null)` is ever emitted.
   `LLONG_MAX`/`LLONG_MIN` round-trip exactly, and a `file` argument is rendered
   as `(fk.c:11)`.
3. **Long field name / field list / file name** — a 699-byte key, 128 repeated
   fields, and a 699-byte `file` name each trigger the three distinct
   `snprintf` truncation branches: `STDLIB_FAIL`,
   `STDLIB_CATEGORY_BOUNDS` / `CPLUS_EXIT_BOUNDS`, message contains
   `"truncated"`. The stream stays usable afterwards, and **no partial line is
   flushed** (only the surrounding `before`/`after` lines are in the file).
4. **Empty / boundary messages** — `""` and `NULL` messages are safe and emit
   an otherwise empty line; 255, 256 and 483 bytes are accepted; 484 and 4095
   bytes are rejected with the BOUNDS truncation marker. 483 is the exact
   largest accepted length: the 20-byte ISO-8601 stamp + `' '` + `%-5s` + `' '`
   is 27 bytes, leaving `512 - 27 - 1 ('\n') - 1 (NUL) = 483` bytes for the
   message. The rejected strings are absent from the file; a subsequent write
   succeeds.
5. **Invalid levels** — `log_level_parse("nonsense")`/`parse(NULL)` return
   `-1` with INVALID/UNSUPPORTED; `log_level_name(99|-1|LOG_LEVELS)` is
   `"UNKNOWN"`. `write(99, ...)` is emitted with the `UNKNOWN` level (99 >=
   threshold 0); `write(-1, ...)` returns `STDLIB_OK` but is filtered out
   (-1 < 0), so it never reaches the file.
6. **1000 writes to one owned file sink** — one extra fd while open
   (`before+1`), no fd growth during the 1000 writes, fd back to baseline after
   `drop()`, and exactly 1000 newline-terminated lines, all shorter than
   `LOG_LINE_SIZE`.
7. **`log_stdout()` / `log_stderr()`** — both borrowed sinks are `valid()`,
   report the correct `native()` stream and level, and double-`drop()` without
   closing the process streams (`fcntl(fd, F_GETFD)` still succeeds) or leaking
   an fd. A write to a dropped borrowed sink fails safely with INVALID.
   **No message is written to stdout/stderr** (their output contains a live
   timestamp and is therefore non-deterministic), so the harness output remains
   byte-exact; this follows the "assert valid() and no crash" option in the
   task.
8. **Enable/disable parity** — for `write`, `info`, `warn` and `failure`, a
   suppressed call and an emitted call return the same `STDLIB_OK`, leave the
   C+ error object in the OK state with identical category/code, and leave
   libc `errno` at 0; only the enabled calls reach the file (4 lines).
9. **Cleanup** — the PID-unique path
   `/tmp/cplus_extra_log_edges_<pid>.log` is unlinked before each section and
   unlinked and asserted absent at the end. No leftovers were found
   (`ls /tmp/cplus_extra_log_edges_*` returns nothing after the runs).

## Exact commands and per-mode results

Run from the repository root. `OUT` is the session temp directory above.

```sh
# 1. STRICT / no sanitizer (run FIRST)
CPLUS_CLANG=/usr/bin/clang timeout 30 ./build/c+ build \
  tests/stdlib/agent2/extra_log_edges.cp --strict --sanitize=none \
  -o "$OUT/extra_log_edges__strict_none"
timeout 30 "$OUT/extra_log_edges__strict_none" < /dev/null

# 2. DEFAULT mode
CPLUS_CLANG=/usr/bin/clang timeout 30 ./build/c+ build \
  tests/stdlib/agent2/extra_log_edges.cp --default --sanitize=none \
  -o "$OUT/extra_log_edges__default"
timeout 30 "$OUT/extra_log_edges__default" < /dev/null

# 3. STRICT / UBSan
CPLUS_CLANG=/usr/bin/clang timeout 30 ./build/c+ build \
  tests/stdlib/agent2/extra_log_edges.cp --strict --sanitize=undefined \
  -o "$OUT/extra_log_edges__ubsan"
timeout 30 "$OUT/extra_log_edges__ubsan" < /dev/null
```

A fourth, implicit-default build (`./build/c+ build <file> -o ...`, no mode and
no sanitizer flag) was also run as a control.

| # | Mode | Build exit | Run exit | Checks | Failures | Final stdout line | Stderr |
|---|---|---|---|---|---|---|---|
| 1 | `--strict --sanitize=none` | 0 | 0 | 72 | 0 | `EXTRA_LOG_EDGES 72 0` | empty |
| 2 | `--default --sanitize=none` | 0 | 0 | 72 | 0 | `EXTRA_LOG_EDGES 72 0` | empty |
| 3 | `--strict --sanitize=undefined` | 0 | 0 | 72 | 0 | `EXTRA_LOG_EDGES 72 0` | empty |
| 4 | implicit default (control) | 0 | 0 | 72 | 0 | `EXTRA_LOG_EDGES 72 0` | empty |

All four builds produced empty build stdout/stderr. All four runs produced
byte-identical 1679-byte stdout (73 lines) and empty stderr. A second
consecutive run of mode 1 was byte-identical to the first
(`STDOUT_DETERMINISTIC=yes`), and all four stdout files diff clean against each
other. Exit code is 0 only because `ex_failures == 0`.

## Exact stdout (all modes, byte-identical; 73 lines, 1679 bytes)

```
ex_nullarr_open 1
ex_nullarr_keys_null 1
ex_nullarr_values_null 1
ex_nullarr_both_null 1
ex_nullarr_zero_fields_null_arrays 1
ex_nullarr_zero_fields_live_arrays 1
ex_nullarr_drop 1
ex_nullarr_only_successes_flushed 1
ex_nullarr_success_content 1
ex_nullarr_failed_content_absent 1
ex_field_open 1
ex_field_null_key_write 1
ex_field_extreme_write 1
ex_field_question_substitution 1
ex_field_no_null_literal 1
ex_field_extreme_values 1
ex_field_location 1
ex_field_lines 1
ex_trunc_open 1
ex_trunc_before 1
ex_trunc_long_key_rejected 1
ex_trunc_many_fields_rejected 1
ex_trunc_long_file_rejected 1
ex_trunc_after_recovery 1
ex_trunc_no_partial_lines 1
ex_trunc_content 1
ex_len_open 1
ex_len_empty_string 1
ex_len_null_message 1
ex_len_255 1
ex_len_256 1
ex_len_483_accepted 1
ex_len_484_rejected 1
ex_len_4095_rejected 1
ex_len_truncation_marker 1
ex_len_post_overflow_write 1
ex_len_lines_wellformed 1
ex_len_small_present 1
ex_len_oversize_absent 1
ex_level_parse_invalid 1
ex_level_parse_null 1
ex_level_name_invalid 1
ex_level_sink_open 1
ex_level_write_99 1
ex_level_write_negative 1
ex_level_unknown_in_file 1
ex_level_negative_filtered 1
ex_level_lines 1
ex_bulk_open 1
ex_bulk_one_fd 1
ex_bulk_1000_writes 1
ex_bulk_no_write_fd_leak 1
ex_bulk_drop 1
ex_bulk_fd_released 1
ex_bulk_exact_line_count 1
ex_stdout_valid 1
ex_stdout_enabled 1
ex_stdout_drop 1
ex_stdout_post_drop_write 1
ex_stdout_stream_open 1
ex_stderr_valid 1
ex_stderr_enabled 1
ex_stderr_drop 1
ex_stderr_stream_open 1
ex_borrowed_fd_stable 1
ex_parity_open 1
ex_parity_write 1
ex_parity_info 1
ex_parity_warn 1
ex_parity_failure 1
ex_parity_only_enabled_written 1
ex_cleanup_absent 1
EXTRA_LOG_EDGES 72 0
```

## Defects

**No defect was reproduced.** Every check passed under all four modes, no
sanitizer diagnostic was emitted, and no fd or temp-file leak was observed, so
there is no minimal defect repro to report.

One *non-reproducible* code observation (not a finding, not counted as a
defect): `log_sink::write` clears `stamp` when `time_format_iso8601` fails but
does not call `error_reset(err)` again before returning `STDLIB_OK`. With the
fixed 32-byte stamp buffer and a realtime clock this path cannot be triggered
from the test, so it was not asserted and no header was changed.

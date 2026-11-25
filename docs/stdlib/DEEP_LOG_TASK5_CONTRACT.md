# Deep log Task 5 contract (genuine remaining gaps)

Scope: `stdlib/log.hp` only. This contract is defined by one new self-checking
program, `tests/stdlib/deep_log_task5.cp`. It deliberately contains **no** case
already covered by `log_test.cp`: basic level filtering, structured fields,
RAII/borrowed sinks and the single invalid-path open are excluded here. The
`log_test.cp` spot checks for `log_level_name`/`log_level_parse` (`WARN`,
`UNKNOWN`, `debug`, `nonsense`) are not repeated — the inputs below are a
broader matrix with exact error codes/categories.

Driver: `build/c+` (the binary in this checkout, built from the sibling
`c-plus-core` tree which supplies native `--strict` and `--sanitize=none`).

## 1. New cases and contract decisions

| Case | Contract exercised | Observed tags |
| --- | --- | --- |
| 1. `log_level_parse` | All six valid names accepted case-insensitively with a clear error (`err.ok()`). Numeric strings (`"0"`, `"3"`, `"5"`, `"-1"`), empty string and `NULL` are rejected with return `-1`, `STDLIB_CATEGORY_INVALID` (1) and `CPLUS_EXIT_UNSUPPORTED` (108) — the header compares names only, so numeric levels are **not** parsed. | `t5_parse_valid_mixed_case`, `t5_parse_numeric_*`, `t5_parse_empty`, `t5_parse_null` |
| 2. `log_level_name` | Levels 0..5 map to `TRACE/DEBUG/INFO/WARN/ERROR/FATAL`; out-of-range `-1`, `LOG_LEVELS` (6) and `99` map to `UNKNOWN`. | `t5_name_all_levels`, `t5_name_out_of_range` |
| 3. Invalid / NULL `log_open_file` | `NULL` path -> invalid sink (`native()==NULL`), `STDLIB_CATEGORY_INVALID` / `CPLUS_EXIT_UNSUPPORTED`; unopenable path -> invalid sink, `STDLIB_CATEGORY_IO` (3) / `CPLUS_EXIT_IO` (109). On a dead sink `write/info/warn/failure` all return `STDLIB_FAIL` with INVALID/UNSUPPORTED, `enabled()` is false, `level()` still reports the requested threshold, and `drop()` is idempotent. | `t5_open_null_*`, `t5_open_bad_*` |
| 4. Threshold boundary | With threshold `LOG_WARN`, writes at `TRACE/DEBUG/INFO` are suppressed but still return `STDLIB_OK` with a clear error; writes at `WARN/ERROR/FATAL` are emitted. File read-back: exactly 3 lines, none containing `below_`, all three at/above markers present. | `t5_threshold_*` |
| 5. Message-length boundaries | Empty, 255-byte and 256-byte messages are written (`STDLIB_OK`); a 4095-byte message is rejected with `STDLIB_FAIL`, `STDLIB_CATEGORY_BOUNDS` (5), `CPLUS_EXIT_BOUNDS` (101) and an error string containing `truncated` (the truncation marker — no partial line is flushed). A following write still succeeds. Read-back: 4 well-formed newline-terminated lines, each shorter than `LOG_LINE_SIZE` (512), markers `M255-`/`M256-` present, `M4095-` absent. | `t5_len_*` |
| 6. Enable/disable parity | For each of the four sink methods (`write`, `info`, `warn`, `failure`), the disabled call (threshold `FATAL`) and enabled call (threshold `TRACE`) have identical return values (`STDLIB_OK`), identical C+ error state (`ok()` true, `code()==CPLUS_EXIT_OK`, `category()==STDLIB_CATEGORY_OK`) and identical libc `errno` (reset to 0 before each call, stays 0). Only enabled calls emit a line (4 lines total). | `t5_parity_write/info/warn/failure`, `t5_parity_lines` |
| 7. Repeated open/write/drop | 32 `log_open_file` -> `info` -> `drop` cycles produce 32 lines and leave the open-fd count unchanged (no fd leak). | `t5_cycles_complete`, `t5_cycles_fd_stable`, `t5_cycles_lines` |

### Implementation notes

- All temp files are named `cplus_deep_log_task5_<pid>.log` (PID-unique so the
  three verification modes can run in parallel), and are `unlink`ed before each
  case and once more at the end; `t5_cleanup_no_leftovers` asserts the final
  path is absent.
- "errno parity" is checked literally with libc `errno` (`<errno.h>`): it is
  set to 0 immediately before each disabled/enabled call and compared after.
  The C+ `error` object (`code()`/`category()`/`ok()`) is compared alongside it.
- The 4095-byte rejection is a deliberate design behaviour: `log_sink::write`
  detects `snprintf` truncation before `fwrite`, so the oversized message never
  reaches the file; the on-disk truncation marker is the BOUNDS error, not a
  literal string in the log.
- No `.expected` sidecar is needed: every check prints `<tag> <1|0>` and the
  final line is `LOG_TASK5 <checks> <failures>`; exit is 0 only when
  `failures == 0`.

## 2. Exact expected stdout

`tests/stdlib/deep_log_task5.cp` — exit 0, empty stderr:

```
t5_parse_valid_mixed_case 1
t5_parse_numeric_0 1
t5_parse_numeric_3 1
t5_parse_numeric_5 1
t5_parse_numeric_neg 1
t5_parse_empty 1
t5_parse_null 1
t5_name_all_levels 1
t5_name_out_of_range 1
t5_open_null_invalid 1
t5_open_null_error 1
t5_open_null_write 1
t5_open_null_write_direct 1
t5_open_null_warn 1
t5_open_null_failure 1
t5_open_null_disabled 1
t5_open_null_level 1
t5_open_bad_invalid 1
t5_open_bad_error 1
t5_open_bad_disabled 1
t5_open_bad_level 1
t5_open_bad_write 1
t5_threshold_open 1
t5_threshold_below_ok 1
t5_threshold_above_ok 1
t5_threshold_lines 1
t5_threshold_no_below 1
t5_threshold_has_above 1
t5_len_open 1
t5_len_empty 1
t5_len_255 1
t5_len_256 1
t5_len_4095_rejected 1
t5_len_4095_bounds 1
t5_len_truncation_marker 1
t5_len_after_overflow 1
t5_len_lines_wellformed 1
t5_len_has_255_256 1
t5_len_no_oversized 1
t5_parity_open 1
t5_parity_write 1
t5_parity_info 1
t5_parity_warn 1
t5_parity_failure 1
t5_parity_lines 1
t5_cycles_complete 1
t5_cycles_fd_stable 1
t5_cycles_lines 1
t5_cleanup_no_leftovers 1
LOG_TASK5 49 0
```

## 3. Verification commands and results

Run from the repository root. STRICT `--sanitize=none` was run **first**, then
default and UBSan; every compiler and binary invocation is bounded by
`timeout 30`.

```sh
# 1) STRICT, sanitizer off (run first)
timeout 30 ./build/c+ build tests/stdlib/deep_log_task5.cp \
    --strict --sanitize=none -o /tmp/deep_log_task5_strict_none
timeout 30 /tmp/deep_log_task5_strict_none

# 2) Default mode
timeout 30 ./build/c+ build tests/stdlib/deep_log_task5.cp \
    -o /tmp/dlt5_default
timeout 30 /tmp/dlt5_default

# 3) UBSan (and the harness's strict+UBSan combination)
timeout 30 ./build/c+ build tests/stdlib/deep_log_task5.cp \
    --sanitize=undefined -o /tmp/dlt5_ubsan
timeout 30 /tmp/dlt5_ubsan
timeout 30 ./build/c+ build tests/stdlib/deep_log_task5.cp \
    --strict --sanitize=undefined -o /tmp/dlt5_strict_ubsan
timeout 30 /tmp/dlt5_strict_ubsan

# Integration (build<=60s, run<=30s per invocation)
timeout 120 sh tests/stdlib/run_deep_tests.sh deep_log_task5
```

Results:

| Mode | Build rc | Run rc | Summary line | stdout vs strict-none |
| --- | --- | --- | --- | --- |
| `--strict --sanitize=none` (first) | 0 | 0 | `LOG_TASK5 49 0` | baseline |
| default | 0 | 0 | `LOG_TASK5 49 0` | identical |
| `--sanitize=undefined` | 0 | 0 | `LOG_TASK5 49 0` | identical |
| `--strict --sanitize=undefined` | 0 | 0 | `LOG_TASK5 49 0` | identical |
| harness default / strict-none / ubsan | 0 | 0 | `LOG_TASK5 49 0` (x3) | identical |

Harness line (both Task 5 patterns selected): `deep stdlib tests: 6 passed, 0
failed, 0 skipped (2 file(s) x 3 modes)`; for this test alone it is
`3 passed, 0 failed, 0 skipped`.

**Totals:** 4 direct build+run invocations + 3 harness invocations = 7 passing
runs; 49 check assertions per run, 0 failures, 0 timeouts, empty stderr, byte
identical stdout in all modes, no leftover temp files.

## 4. Findings

No real defect was reproduced in `stdlib/log.hp`. Every new case passed in all
modes:

- `log_level_parse` rejects numeric strings with INVALID/UNSUPPORTED — this is
  the documented name-only contract, not a defect.
- Dead sinks (NULL and unopenable path) are recoverable and return the exact
  category/code from the header.
- Threshold suppression returns `STDLIB_OK` (suppression is not an error).
- Oversized messages are rejected atomically with BOUNDS/CPLUS_EXIT_BOUNDS and
  leave the file usable.
- Enabled/disabled return values, C+ error state and libc `errno` are identical
  for all four sink methods.
- 32 open/write/drop cycles leak no file descriptors.

**Classification:** all runs are **success**. No recoverable or fatal defect
was found, so no minimal repro is reported.

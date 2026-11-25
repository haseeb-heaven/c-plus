# Deep contract: `stdlib/fs.hp` and `stdlib/time.hp`

Evidence-driven behavioural contract for the two stateless stdlib headers,
established by `tests/stdlib/deep_fs_time.cp`.  The test is independent and
self-checking (asserts exact return values, error categories and exit codes in
process; exits non-zero on any mismatch).  It is *not* a `.expected`-file test.

## Result summary

| Build mode | Command | Result |
|---|---|---|
| Default (Safe, UBSan on) | `c+ build tests/stdlib/deep_fs_time.cp -o …` | 54 checks, 0 failures, rc 0 |
| Strict, sanitizers off | `c+ build --strict --sanitize=none …` | 54 checks, 0 failures, rc 0 |
| Explicit UBSan | `c+ build --sanitize=undefined …` | 54 checks, 0 failures, rc 0 |

Check breakdown: 37 filesystem, 16 time, 1 interrupted-sleep.  No UBSan
diagnostic, no fatal exit, and no `/tmp/cplus_deep_fs_*` artifact survived any
run.  The standard runner also passes: `sh tests/stdlib/run_stdlib_tests.sh
deep_fs_time` → `ok deep_fs_time`.

**No source defect was confirmed in `fs.hp` or `time.hp`; no source change was
made.**  Behaviours that look surprising below are consequences of the stated
API types and are documented, not "fixed" to satisfy the test.

## Classification vocabulary

- **Success**: return `STDLIB_OK` (0); error record is clear (`err.ok()` true,
  category `STDLIB_CATEGORY_OK` 0, code `CPLUS_EXIT_OK` 0).
- **Recoverable**: return `STDLIB_FAIL` (-1) with a populated error record;
  category ∈ {INVALID 1, RANGE 2, IO 3, BOUNDS 5} and code ∈ {UNSUPPORTED 108,
  IO 109, BOUNDS 101}.  The caller continues; no process termination.
- **Fatal**: a `cplus_fatal` exit code (101 BOUNDS, 102 NULL_POINTER, … 109 IO).
  None of the tested inputs are permitted to be fatal — completion of the test
  is the evidence.

## `stdlib/fs.hp` contract

All fallible functions call `error_reset(err)` first, accept `err == NULL`
(no-op in `error_reset`/`error_fail_cat`), and set the record before returning.

| Function | Input | Result | Category / code |
|---|---|---|---|
| `fs_exists`, `fs_is_file`, `fs_is_dir` | `NULL` or missing path | `0` | n/a (never consults `err`) |
| `fs_size` | `NULL` path | `-1` | INVALID / UNSUPPORTED(108) |
| `fs_size` | `stat` failure (missing path) | `-1` | IO / IO(109) |
| `fs_remove` | `NULL` path | FAIL | INVALID / UNSUPPORTED(108) |
| `fs_remove` | `unlink` failure (missing path) | FAIL | IO / IO(109) |
| `fs_rename` | either path `NULL` | FAIL | INVALID / UNSUPPORTED(108) |
| `fs_rename` | `rename` failure (missing src, bad parent) | FAIL | IO / IO(109) |
| `fs_mkdir` | `NULL` path | FAIL | INVALID / UNSUPPORTED(108) |
| `fs_mkdir` | `mkdir` failure (path exists) | FAIL | IO / IO(109) |
| `fs_mkdtemp` | `NULL` template, size < 7, no `XXXXXX` suffix | FAIL | INVALID / UNSUPPORTED(108) |
| `fs_mkdtemp` | `mkdtemp` failure (bad parent) | FAIL | IO / IO(109) |

Additional verified properties:

- `fs_rename` over an **existing destination file** succeeds and atomically
  replaces it: source disappears, destination keeps the source's byte size.
  A second rename of the now-missing source is a recoverable IO failure and
  leaves the destination unchanged.
- `fs_mkdtemp` mutates the caller's template into the created path on success.
  A failed `fs_mkdtemp` creates no directory.
- `fs_remove` is **`unlink`-only**.  It cannot remove a directory (POSIX
  `unlink` → EPERM/EISDIR): applying it to a directory created by `fs_mkdir` or
  `fs_mkdtemp` returns FAIL/IO and the directory remains.  Cleanup of directories
  must use `rmdir()`.  The test does exactly that and then asserts `fs_exists`
  is 0 for every path it created — proving **no leftover temp artifacts**.
- Deterministic failure injection is exercised two ways: (a) controlled setup
  (missing source / non-existent parent directory) forces the syscall to fail
  with a stable category/code; (b) `stdlib_set_error_callback` captures the
  reported category and code (INVALID/UNSUPPORTED for a null path) — one call,
  exact values.

## `stdlib/time.hp` contract

`time_format_iso8601(unsigned long long unix_ns, char *buf, size_t size, error *err)`
formats whole seconds since the epoch (fractional nanoseconds are truncated,
not rounded); the output is always the fixed 20-character form
`YYYY-MM-DDTHH:MM:SSZ` plus a NUL (21 bytes).  It accepts `err == NULL`.

| Input | Result | Exact output / error |
|---|---|---|
| `0`, `999999999` ns | OK | `"1970-01-01T00:00:00Z"` |
| `1000000000` ns | OK | `"1970-01-01T00:00:01Z"` |
| `1700000000000000000` ns | OK | `"2023-11-14T22:13:20Z"` |
| `ULLONG_MAX` ns | OK | well-formed ISO string (e.g. `2554-07-21T23:34:33Z` on this 64-bit host) |
| `(unsigned long long)(-1)` ("negative") | OK | **wraps to a large positive**; formatted as a future date, not rejected.  Documented consequence of the unsigned-nanoseconds API: on this host the full `unsigned long long` nanosecond range spans ~1970–2554, so the `secs_u > LONG_MAX` and `gmtime_r` range guards are effectively unreachable and a wrapped negative is indistinguishable from a legitimate far-future timestamp. |
| `buf == NULL` or `size == 0` | FAIL | INVALID(1) / UNSUPPORTED(108); buffer not written |
| `size` 1, 4, 20 (truncated) | FAIL | BOUNDS(5) / BOUNDS(101); `buf[0] = '\0'` |
| `size == 21` (exact) | OK | exact 20-char string, NUL at index 20, no write past it |
| `size == 32` (roomy) | OK | exact string; a sentinel at index 21 is untouched |

Note the deliberate category split: `size == 0` is INVALID/"null buffer", while
`size` in `1..20` is BOUNDS/"buffer too small".  This asymmetry is preserved as
implemented.

Other verified properties:

- `time_now_ns/ms` and `time_monotonic_ns/ms` are positive; `time_monotonic_ns`
  is non-decreasing and advances by at least the requested 20 ms sleep.
- `time_sleep_ms` resumes after `EINTR`: in a forked child a one-shot 50 ms
  `SIGALRM` interrupts a 300 ms sleep; measured monotonic elapsed is ~301 ms
  (≥ 200 ms threshold), i.e. the `nanosleep`/`rem` loop completes the full
  duration.  A 5-second parent watchdog (`waitpid(WNOHANG)` + `SIGKILL` fallback)
  bounds the test.  The child `fflush`es before `_exit` and writes nothing, so
  output is deterministic.

## Reproduction commands

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
C=build/c+; T=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode
$C build tests/stdlib/deep_fs_time.cp -o $T/dft_default && $T/dft_default
$C build --strict --sanitize=none tests/stdlib/deep_fs_time.cp -o $T/dft_strict && $T/dft_strict
$C build --sanitize=undefined tests/stdlib/deep_fs_time.cp -o $T/dft_ubsan && $T/dft_ubsan
sh tests/stdlib/run_stdlib_tests.sh deep_fs_time
```

Pass criteria for every invocation: build rc 0, run rc 0, stdout
`deep_fs_time: 54 checks, 0 failures`, empty stderr.

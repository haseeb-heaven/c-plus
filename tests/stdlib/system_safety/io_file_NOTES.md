# `io_file_*` — Task 3 Phase 2: I/O and file lifecycle verification notes

Branch `agent/modern-stdlib`, 2026-11-23. Host macOS arm64, Apple clang 16,
driver `build/c+` (built 2026-11-23 08:07). Every build and every run used
`timeout 30`. `CPLUS_CLANG=/usr/bin/clang` was exported for all invocations.
All build/run artifacts lived outside the repository, in
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/io_file_run`;
no fixture file remained in any run cwd.

## Files created (only these)

| File | Purpose |
| --- | --- |
| `tests/stdlib/system_safety/io_file_empty_large.cp` | empty files; 1-byte, 64 KiB and 64 KiB-1 files |
| `tests/stdlib/system_safety/io_file_lines.cp` | exact-fit buffers, overlong-line drain, EOF before/after data, CRLF boundary, io.hp stdin readers |
| `tests/stdlib/system_safety/io_file_partial_modes.cp` | partial reads/writes, invalid handles, read-only vs write-only vs `r+` |
| `tests/stdlib/system_safety/io_file_lifecycle.cp` | seek/flush failures, repeated close/drop, borrowed `FILE*` ownership, cleanup after failed init, fd counts |
| `tests/stdlib/system_safety/io_file_fd_exhaustion.cp` | fd exhaustion in a forked child with SIGALRM watchdog |
| `tests/stdlib/system_safety/io_file_NOTES.md` | this file |

One library file was modified, only after a defect was reproduced (see below):
`stdlib/file.hp` (one-line change). `stdlib/io.hp` was **not** changed. No
compiler/runtime file, no other test, no expectation fixture and no git
index was touched. No `git add` / `git commit` was performed.

All test sources obey the stricter-build constraints: no variable
division/modulo; no pointer-parameter subscripts (`__builtin_memcpy` used to
inspect/write through pointer parameters); every local and out-parameter
initialised; unique `ifel_`/`ifln_`/`ifpm_`/`iflc_`/`ifde_` prefixes per TU.
Each test is self-checking: it prints `<tag> <0|1>` per check, a final
`IO_FILE_* <checks> <failures>` line, and exits 0 only when failures == 0.

## Coverage matrix

| Required scenario | File / tags |
| --- | --- |
| empty files | `io_file_empty_large`: `empty-*` (named file: create/close/reopen/read EOF/read_line EOF/size 0), `emptytemp-*` (anonymous `file_temp`) |
| large files (<=64 KiB) | `io_file_empty_large`: `one-*`, `large-*` (exactly 65536 bytes in 256-byte chunks, `ftell` size check, byte-for-byte readback, EOF after), `near-*` (65535 bytes, non-block-aligned tail) |
| exact-fit line buffers | `io_file_lines`: `exact-lf-*` (7 chars + LF into `buf[8]`, LF is the peeked byte), `exact-eof-*` (7 chars at physical EOF), `iostream-exact` (io.hp) |
| overlong lines + drain verification | `io_file_lines`: `overlong-lf-status`, `overlong-drain-next`, `overlong-drain-eof`, `overlong-eof-*`, `iostream-overlong`, `iostream-drain`; token truncation `token-truncated`, `token-drained`, `token-eof` |
| EOF before / after data | `io_file_lines`: `eof-after-data-*`, `eof-repeat-line`, `eof-before-data`, `iostream-eof`, `token-eof`; `io_file_empty_large` EOF checks |
| partial reads / writes | `io_file_partial_modes`: `partial-*` (full 4, partial 4-of-8, EOF, repeated EOF); short write via read-only file `ro-write-bytes` |
| invalid handles | `io_file_partial_modes`: `null-*` (null path/mode: all ops return defined failures, double drop safe), `closed-*` (after explicit close); `arg-*` (null/zero-length/huge argument guards) |
| read-only vs write-only | `io_file_partial_modes`: `ro-*` (read OK, both write forms fail, on-disk content intact), `wo-*` (write OK, `read`/`read_line` fail), `rw-*` (`r+` both directions) |
| seek / flush failures | `io_file_lifecycle`: `seek-fail-neg`, `seek-fail-whence`, `seek-fail-dropped`, `flush-fail-closed`, `flush-fail-null`; success paths `seek-end*`, `seek-cur*`, `flush-ok` |
| repeated close / drop | `io_file_lifecycle`: `close-first/second/third`, `drop-repeat` (3x), `close-then-drop`, `drop-then-close` |
| borrowed `FILE*` never closed (observable) | `io_file_lifecycle`: `borrow-after-drop`, `borrow-after-destructor`, `borrow-after-wrapper-close`, `borrow-raw-write`, `borrow-raw-verify`, `borrow-manual-close` — raw handle probed with `fgetc`/`fputs`/`fclose` after wrapper `drop()`, scope destruction and explicit `close()` |
| cleanup after failed init | `io_file_lifecycle`: `failedinit-*` (32 failed opens + double drop, `/dev/fd` count unchanged); `io_file_fd_exhaustion`: `exh-temp-*`, `exh-drop-safe` |
| fd exhaustion, forked child, watchdog | `io_file_fd_exhaustion`: child lowers `RLIMIT_NOFILE`, holds `/dev/null` streams until `fopen` fails, drives all wrapper failure paths; 8 s `SIGALRM` watchdog (`_exit(99)`), fixed 64-slot array, parent `waitpid()`, outer `timeout 30` |

`stdout` is flushed before `fork()` so the child's buffered output is not
duplicated. The child emits `IO_FILE_FD_EXHAUSTION 15 0` (its 15 checks); the
parent then adds 3 checks (`child-normal-exit`, `child-no-watchdog`,
`child-zero-failures`) — **18 checks total**, all passing.

## Exact commands and results

```sh
export CPLUS_CLANG=/usr/bin/clang
CPLUS=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+
OUT=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/io_file_run
mkdir -p "$OUT"
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib

# 1) STRICT, NO SANITIZER (run FIRST)
for T in empty_large lines partial_modes lifecycle fd_exhaustion; do
  timeout 30 "$CPLUS" build "tests/stdlib/system_safety/io_file_${T}.cp" \
      --strict --sanitize=none -o "$OUT/${T}__strict_none"
done
cd "$OUT"
for T in empty_large lines partial_modes lifecycle fd_exhaustion; do
  timeout 30 "./${T}__strict_none" < /dev/null > "${T}.out" 2> "${T}.err"
done
```

### Strict / no-sanitizer results (final, after fix)

| Test | Build exit | Run exit | stderr | Final line |
| --- | --- | --- | --- | --- |
| `io_file_empty_large` | 0 | 0 | empty | `IO_FILE_EMPTY_LARGE 35 0` |
| `io_file_lines` | 0 | 0 | empty | `IO_FILE_LINES 36 0` |
| `io_file_partial_modes` | 0 | 0 | empty | `IO_FILE_PARTIAL_MODES 53 0` |
| `io_file_lifecycle` | 0 | 0 | empty | `IO_FILE_LIFECYCLE 39 0` |
| `io_file_fd_exhaustion` | 0 | 0 | empty | `IO_FILE_FD_EXHAUSTION 15 0` (+3 parent checks pass) |

Total: **183 checks, 0 failures** (35+36+53+39+18).

### UBSan

The driver's native sanitizer mode could **not** be used: `--strict
--sanitize=undefined` recompiles an instrumented runtime and fails in an
out-of-ownership file:

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23: error:
  use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
error: failed to compile instrumented C+ runtime
build_exit=1
```

The driver compiles that runtime with
`-I /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/runtime/include`; this
repo's `runtime/include/cplus/cplus.h` defines exit codes only through
`CPLUS_EXIT_IO = 109`, while the external `cplus_runtime.c` now references
`CPLUS_EXIT_ARITHMETIC = 110`. This is a cross-repo integration issue outside
my ownership (and compiler/runtime edits are forbidden), so it is reported,
not fixed. It was re-checked after all test work and was still present.

UBSan coverage was therefore obtained with a backend-passthrough fallback,
which instruments the lowered test translation unit (including the `io.hp`
and `file.hp` code under test) and links the UBSan runtime:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
for T in empty_large lines partial_modes lifecycle fd_exhaustion; do
  timeout 30 "$CPLUS" build "tests/stdlib/system_safety/io_file_${T}.cp" \
      --strict --sanitize=none -o "$OUT/${T}__ubsan" -- -fsanitize=undefined
done
cd "$OUT"
for T in empty_large lines partial_modes lifecycle fd_exhaustion; do
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
      timeout 30 "./${T}__ubsan" < /dev/null > "${T}.ubsan.out" 2> "${T}.ubsan.err"
done
```

`nm -g` confirms UBSan handlers are linked in every fallback binary (4–6
`___ubsan_handle_*` symbols each). `UBSAN_OPTIONS=halt_on_error=1` was set, so
any diagnostic would abort the run.

| Test | Build exit | Run exit | stderr | Final line |
| --- | --- | --- | --- | --- |
| `io_file_empty_large` | 0 | 0 | empty | `IO_FILE_EMPTY_LARGE 35 0` |
| `io_file_lines` | 0 | 0 | empty | `IO_FILE_LINES 36 0` |
| `io_file_partial_modes` | 0 | 0 | empty | `IO_FILE_PARTIAL_MODES 53 0` |
| `io_file_lifecycle` | 0 | 0 | empty | `IO_FILE_LIFECYCLE 39 0` |
| `io_file_fd_exhaustion` | 0 | 0 | empty | `IO_FILE_FD_EXHAUSTION 15 0` (+3 parent checks pass) |

No UBSan diagnostic was emitted in any mode.

### Determinism

Three consecutive strict/no-sanitizer runs of each test produced
byte-identical stdout (SHA-1) and empty stderr:

| Test | SHA-1 of stdout (all 3 runs) |
| --- | --- |
| `io_file_empty_large` | `fcd9d9bc95a4449fa91891ba0ca76ab1c8574ff7` |
| `io_file_lines` | `c6562bf4eb3cb659ebcf1043125d0ba41fc48937` |
| `io_file_partial_modes` | `7daa4b3092354f0a476d9ebd9d5bf29c679ab9c5` |
| `io_file_lifecycle` | `dea45d4ebf5b4cef884905a220192b89b6024048` |
| `io_file_fd_exhaustion` | `2a3aa424baa4b3a1a1218d76b062fa4228172bee` |

No `cplus_ifel_*` / `cplus_ifpm_*` / `cplus_no_such_file_zzzz` artifact
remained in any run cwd.

## Defect reproduced and fixed

**Defect:** `file::read_line` in `stdlib/file.hp` failed to consume the LF of
a CRLF terminator when `fgets` filled the buffer exactly through the CR.

**Reproduction (strict/no-sanitizer, before fix):** `io_file_lines` reported
`IO_FILE_LINES 36 2` with, specifically:

```
crlf-edge-next-no-leading-lf 0
crlf-edge-eof 0
```

Fixture `buf[8]` + `"123456\r\nXYZ\n"`. `fgets(buf, 8)` reads `"123456\r"`
(7 bytes = size-1). The old truncation-peek guard was
`if (last != '\n' && last != '\r' && !feof(...))`; with `last == '\r'` the peek
was skipped, leaving `\n` in the stream. The next `read_line` then returned an
empty line (its `fgets` stopped at that leading `\n`), and the following call
returned `"XYZ"` instead of EOF. `io.hp::read_line` already had the correct
guard (`last != '\n'` only), so this was specific to `file.hp`.

**Minimal fix (one line, `stdlib/file.hp`):**

```diff
-            if (last != '\n' && last != '\r' && !feof(this->stream)) {
+            if (last != '\n' && !feof(this->stream)) {
```

This mirrors `io.hp::read_line` exactly. The existing peek body already
handles `c == '\n'` (consume it), `c == '\r'` (consume optional following LF),
`c == EOF` (exact fit at EOF) and any other byte (drain + `BOUNDS` failure),
so no other change was needed. After the fix `io_file_lines` reports
`IO_FILE_LINES 36 0` in both strict and UBSan modes.

**Regression checks on existing file tests** (strict/no-sanitizer, after fix,
all exit 0, empty stderr):

| Test | Result |
| --- | --- |
| `tests/stdlib/deep_file_lines.cp` (with its `.stdin` fixture) | `deep-file-lines PASS 0` |
| `tests/stdlib/deep_file_lifecycle.cp` | `deep-file-lifecycle PASS 0` |
| `tests/stdlib/agent2/extra_file_binary.cp` | `EXTRA_FILE_BINARY 81 0` |

## Known limitations

* **Native driver UBSan (`--sanitize=undefined`) is blocked** by the external
  `CPLUS_EXIT_ARITHMETIC` header/runtime mismatch described above; the
  fallback instruments the program TU and the standard-library headers under
  test, but not the prebuilt C+ runtime.
* **Physical flush failure** (e.g. `ENOSPC`) is not deterministic on this host.
  The wrapper's flush failure branch is exercised deterministically through
  the null-handle and closed-handle guards (`flush-fail-null`,
  `flush-fail-closed`); the success branch is covered by `flush-ok`.
* No test was weakened to accommodate the defect: the CRLF-boundary assertions
  remain strict, and the library was fixed instead.

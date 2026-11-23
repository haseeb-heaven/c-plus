# RUN_NOTES — file/io/fs per-method tests

All commands below were run from the repository root:
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` (branch `agent/modern-stdlib`).

## Deliverables

| File | Purpose |
| --- | --- |
| `test_file.cp` | 13 public methods of `stdlib/file.hp`, one verified case each |
| `test_io.cp` | 7 public functions of `stdlib/io.hp`, one verified case each |
| `test_fs.cp` | 8 public functions of `stdlib/fs.hp`, one verified case each |
| `methods.txt` | per-method manifest with result |
| `RUN_NOTES.md` | this file |
| `bin/` | binaries and captured build/run logs from the exact commands below |

No headers, source files, or files outside `tests/stdlib/method_tests/file_io/`
were created or modified. No git add/commit was performed.

## Method enumeration

- `stdlib/file.hp` — 13: `file` (constructor), `is_open`, `native`, `read`,
  `read_line`, `write`, `write_bytes`, `seek`, `flush`, `close`, `drop`,
  `file_borrow`, `file_temp`.
- `stdlib/io.hp` — 7 public `static inline` functions: `println`,
  `stdlib_io_token`, `read_line`, `read_int`, `read_long`, `read_float`,
  `read_double`.
- `stdlib/fs.hp` — 8: `fs_exists`, `fs_is_file`, `fs_is_dir`, `fs_size`,
  `fs_remove`, `fs_rename`, `fs_mkdir`, `fs_mkdtemp`.

**Count reconciliation.** The parent task listed `file.hp (13), io.hp (6),
fs.hp (8)` = 27. `file.hp` and `fs.hp` match exactly. `io.hp` actually exposes
7 public `static inline` functions; the repository's own audit
(`tests/stdlib/agent2_audit/module_audits/io.hp.md`, section "Public API list")
lists all 7. The likely reason for the stated 6 is that `stdlib_io_token`
carries an internal-looking `stdlib_` prefix and was excluded from the parent
enumeration. It is nevertheless public, callable, and fully invoked and
verified in `test_io.cp`, so it is counted here: 13 + 7 + 8 = 28. If the parent
prefers its original 13/6/8 classification, the equivalent headline is
`METHOD_TEST file_io 27 27 0 0`; the only difference is whether the exercised
`stdlib_io_token` helper is included in the total.

## Exact commands and results

Build (each exit code 0, empty build log):

```sh
timeout 30 ./build/c+ build tests/stdlib/method_tests/file_io/test_file.cp --strict --sanitize=none -o tests/stdlib/method_tests/file_io/bin/file > tests/stdlib/method_tests/file_io/bin/file.build.log 2>&1
timeout 30 ./build/c+ build tests/stdlib/method_tests/file_io/test_io.cp   --strict --sanitize=none -o tests/stdlib/method_tests/file_io/bin/io   > tests/stdlib/method_tests/file_io/bin/io.build.log 2>&1
timeout 30 ./build/c+ build tests/stdlib/method_tests/file_io/test_fs.cp   --strict --sanitize=none -o tests/stdlib/method_tests/file_io/bin/fs   > tests/stdlib/method_tests/file_io/bin/fs.build.log 2>&1
```

Build results: `BUILD file rc=0`, `BUILD io rc=0`, `BUILD fs rc=0`.
`bin/*.build.log` are empty (no warnings/errors in `--strict` mode).
No `-Ithird_party/yyjson -Lbuild/ext -lyyjson` flags were needed: none of
`file.hp`, `io.hp`, or `fs.hp` depend on yyjson.

Run (each exit code 0; every run under the 30s timeout):

```sh
timeout 30 tests/stdlib/method_tests/file_io/bin/file > tests/stdlib/method_tests/file_io/bin/file.run.log 2>&1
timeout 30 tests/stdlib/method_tests/file_io/bin/io   > tests/stdlib/method_tests/file_io/bin/io.run.log 2>&1
timeout 30 tests/stdlib/method_tests/file_io/bin/fs   > tests/stdlib/method_tests/file_io/bin/fs.run.log 2>&1
```

`bin/file.run.log`:

```
file file PASS
file is_open PASS
file native PASS
file read PASS
file read_line PASS
file write PASS
file write_bytes PASS
file seek PASS
file flush PASS
file close PASS
file drop PASS
file file_borrow PASS
file file_temp PASS
file TOTAL 13 / 13 PASS
```

`bin/io.run.log`:

```
io println PASS
io stdlib_io_token PASS
io read_line PASS
io read_int PASS
io read_long PASS
io read_float PASS
io read_double PASS
io TOTAL 7 / 7 PASS
```

`bin/fs.run.log`:

```
fs fs_exists PASS
fs fs_is_file PASS
fs fs_is_dir PASS
fs fs_size PASS
fs fs_remove PASS
fs fs_rename PASS
fs fs_mkdir PASS
fs fs_mkdtemp PASS
fs TOTAL 8 / 8 PASS
```

Run results: `RUN file rc=0`, `RUN io rc=0`, `RUN fs rc=0`.

## Determinism, coverage and cleanup

- Each test creates one scratch directory with `mkdtemp` from a template
  `"$TMPDIR/cplus_method_<family>_XXXXXX"` (fallback `/tmp`); no hard-coded
  user or machine paths. All files/dirs are removed before exit.
  Post-run check: `ls -d "${TMPDIR:-/tmp}"/cplus_method_*` found 0 leftovers.
- `test_io.cp` redirects `stdin` to a generated fixture and briefly redirects
  `stdout` to a capture file for the void `println()`, then restores both, so
  results do not depend on the caller's terminal.
- Coverage per method includes the success path plus at least one failure,
  bounds, or validation path where the header defines one: e.g. null buffer /
  null handle / zero length (`read`, `read_line`, `write_bytes`), partial read
  and EOF, invalid `seek`, double `close`/`drop`, non-owning `file_borrow`,
  integer/float range and null-out failures for the numeric readers, and
  null-path / missing-path / duplicate-entry failures for the fs helpers.
- Each test exits nonzero if any check fails; all three exited 0.

## Totals

- total: 28
- passed: 28
- failed: 0
- skipped: 0

METHOD_TEST file_io 28 28 0 0

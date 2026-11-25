# Native Agent 2 regression runner

`tests/stdlib/agent2/run_all_native.sh` + `tests/stdlib/agent2/run_all.cp`
replace `run_all.sh` + `run_all.py` with a native C+ executable. The Python
file is intentionally left in place (not deleted or modified) so the old
summary can still be reproduced for comparison.

## Files

| File | Role |
|------|------|
| `tests/stdlib/agent2/run_all.cp` | Native runner. Discovery, builds, runs, classification, JSON + text summary. |
| `tests/stdlib/agent2/run_all_native.sh` | Resolves repo root, builds the runner, forwards the nine positional arguments. |
| `docs/stdlib/NATIVE_AGENT2_RUNNER.md` | This document. |

## Usage

```sh
sh tests/stdlib/agent2/run_all_native.sh [options]
```

Options mirror `run_all.sh`: `--test NAME`, `--module NAME`, `--clean`,
`--keep-logs`, `--timeout N` (default 30), `--build-timeout N` (default 90),
`--help`.

The wrapper builds the runner only when the binary is missing or older than
the source, into `build/agent2/run_all_native`. The runner binary is built with:

```sh
./build/c+ build tests/stdlib/agent2/run_all.cp --strict --sanitize=none \
  -o build/agent2/run_all_native -- build/libc/libcplus_hardened.a
```

The archive is required because `cplus_system()` lives in the hardened libc;
without it the link fails with `Undefined symbols ... _cplus_system`.

## What is ported

- **Discovery**: `tests/stdlib/deep_*.cp`, `tests/stdlib/agent2/*.cp` (both
  `LC_ALL=C` sorted), then `existing_tests.txt` in file order.
- **Discovery self-exclusion**: `agent2/run_all.cp` is skipped. The Python
  runner predates this file; without the guard the native runner would
  discover and recursively execute itself. This is the only intentional
  deviation from the Python discovery rules and is required to keep the test
  count at 56.
- **Builds**: exactly `[c+, build, src, --strict, --sanitize=none, (json flags),
  -o, exe]`; the JSON flags are added when the source contains
  `stdlib/json.hp` or `yyjson.h`. The archive is **not** appended to per-test
  builds (the Python runner did not append it), so build stderr stays
  byte-identical.
- **Timeout / stdin / expectations / classification**: `/usr/bin/timeout Ns`
  wraps each build and run; stdin fixtures are read from STD then AG and fed
  through a private `.native_stdin` file; `.expected`, `.stderr.expected`, and
  `.exit` fixtures are searched in `(source dir, STD, AG)`; classification is
  `PASS / FAIL / BUILD_FAILURE / TIMEOUT / SKIPPED / BLOCKED` with the Python
  precedence rules. `system()`'s wait status is normalised via `WEXITSTATUS`
  semantics so exit codes match `subprocess.run().returncode`.
- **Outputs**: per-test `.build.log`, `.stdout`, `.stderr`, `.diff`,
  `.commands`, `results/summary.json`, `results/summary.txt`, and the terminal
  table. `summary.json` is written manually with `json.dumps(indent=2)`
  compatible layout (including `null` for absent fields and `\\u00xx`
  escaping).

## Verification (exact commands and results)

Build command used for verification (the official wrapper adds the same
archive after `--`):

```sh
./build/c+ build tests/stdlib/agent2/run_all.cp --strict --sanitize=none \
  -Ithird_party/yyjson -Lbuild/ext -lyyjson -o /tmp/native_run_all \
  -- build/libc/libcplus_hardened.a
# BUILD_EXIT=0; warnings only:
# clang: warning: -lyyjson: 'linker' input unused [-Wunused-command-line-argument]
# clang: warning: build/libc/libcplus_hardened.a: 'linker' input unused [-Wunused-command-line-argument]
# clang: warning: argument unused during compilation: '-Lbuild/ext' [-Wunused-command-line-argument]
```

Single-test filter (the parent-requested smoke check):

```sh
sh tests/stdlib/agent2/run_all_native.sh --test deep_string_append
# Agent 2 aggregate regression summary (9a061ae)
# ...
# deep_string_append  deep  string  PASS  0 0
# SUMMARY PASS=1 FAIL=0 BUILD_FAILURE=0 TIMEOUT=0 SKIPPED=0 BLOCKED=0 TOTAL=1
# exit 0
```

Full run:

```sh
sh tests/stdlib/agent2/run_all_native.sh
# SUMMARY PASS=56 FAIL=0 BUILD_FAILURE=0 TIMEOUT=0 SKIPPED=0 BLOCKED=0 TOTAL=56
# RUN_EXIT=0
```

Transient helpers (`.native_*.manifest`, `.native_build_out`,
`.native_build_err`, `.native_stdin`) are deleted on exit, so `results/`
contains the same file set the Python runner produced.

Other option checks:

```sh
sh tests/stdlib/agent2/run_all_native.sh --module collections
# SUMMARY PASS=4 ... TOTAL=4

sh tests/stdlib/agent2/run_all_native.sh --test eof --clean --timeout 5 --build-timeout 30
# SUMMARY PASS=1 ... TOTAL=1, header shows build_timeout=30s run_timeout=5s
```

Field-by-field comparison of `results/summary.json` against the previously
saved Python summary (`python3` loaded both and compared every key and every
one of the 56 result records):

- `schema_version`, `compiler`, `build_timeout`, `run_timeout`, `counts`,
  `total`, key order, result order, and **all 56 records** match exactly,
  including `stdout`, `stderr`, `build_stdout`, `build_stderr`, `build_command`,
  `runtime_command`, exits, expectation paths, classifications and reasons.
- `generated_at` differs (new run time): `2026-11-23T07:18:03.788651` →
  `2026-11-23T07:34:06.784507`.
- `head` differs because parallel work added commits after the Python summary
  was captured: `ed2ac0f` → `9a061ae`.
- `summary.txt` matches the Python text exactly after normalising the HEAD
  token (`diff -u` clean).

## Issues found and fixed during verification

1. **`*count` on an `int`**: the curated-test loop mistakenly dereferenced
   `count` (an `int` in `main`) instead of using it directly; strict build
   reported `indirection requires pointer operand ('int' invalid)` at lines
   449/466/474. Fixed by using `count`, not `*count`.
2. **Double `.expected` suffix**: the expected-stdout path was rebuilt from an
   already-relative `expectation` field by appending `.expected` again, so
   every test with a stdout fixture was misclassified `FAIL`. Fixed by using
   the relative path verbatim.
3. **`exp_path` reuse**: the stderr fixture path was overwritten by the
   stdout comparison, so `deep_string_append`, `deep_string_view`, and
   `deep_thread_guard` were misclassified with
   `stderr differs from <root>/...expected`. Fixed with a dedicated
   `exp_err_rel` buffer.
4. **`cplus_test_native.hp::native_run_to`** builds `{cmd}` without the
   required spaces/semicolon, which `sh` rejects (`{printf: command not
   found`). The runner therefore declares `cplus_system()` from that header
   but uses its own correctly quoted `run_capture()` wrapper; it still uses
   `native_write` / JSON-escape helpers from the header.
5. `existing_tests.txt` lines are now trimmed on both ends (Python's
   `strip()`), so trailing whitespace cannot break discovery.

No checks were weakened: the build stays `--strict --sanitize=none`, and the
runner itself is compiled with the hardened libc archive.

## Known limitations

- Timeout is implemented with `/usr/bin/timeout`; a timeout yields exit 124
  and the same classification as Python's `subprocess.TimeoutExpired`.
- The native runner is self-excluded from discovery (see above).
- `KEEP_LOGS` is accepted for CLI parity; result logs are always kept, which
  matches the default `run_all.sh` behaviour.
- The runner binary lives in `build/agent2/` so `--clean` cannot delete the
  executable while it is running.

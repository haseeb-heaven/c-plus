# Agent 2 aggregate regression runner

`run_all.sh` is the single entry point for the complete standard-library
regression corpus. It is a correctness runner, not a coverage tool: it checks
build exit codes, runtime exit codes, and byte-exact expected output.

## Quick start (from the repository root)

```sh
sh tests/stdlib/agent2/run_all.sh --clean
```

The final line is machine-readable:

```
SUMMARY PASS=.. FAIL=0 BUILD_FAILURE=0 TIMEOUT=0 SKIPPED=0 BLOCKED=0 TOTAL=..
```

The script exits non-zero if any test is not PASS.

## Selecting tests

```sh
sh tests/stdlib/agent2/run_all.sh --test deep_net           # one test by name
sh tests/stdlib/agent2/run_all.sh --module collections      # one module family
sh tests/stdlib/agent2/run_all.sh --module agent2           # Agent 2 new tests only
sh tests/stdlib/agent2/run_all.sh --module existing         # curated existing tests
sh tests/stdlib/agent2/run_all.sh --timeout 10 --build-timeout 60
```

`--clean` removes prior per-test result files before running. `--keep-logs`
is reserved for future isolated work directories; per-test logs are always
retained in `results/`.

## Discovery

1. `tests/stdlib/deep_*.cp` — all deep regression tests (auto-discovered).
2. `tests/stdlib/agent2/*.cp` — new Agent 2 regression tests (auto-discovered).
3. The curated list in `tests/stdlib/agent2/existing_tests.txt` — existing
   non-deep stdlib tests that are deterministic and build in strict mode
   without unavailable external development files.

Tests that are compile-time NEGATIVE cases (`adversarial.cp`) or that require
an unavailable dependency (`safety_regressions.cp` needs `<uv.h>`) are
deliberately not in the runtime corpus; the exclusions and reasons are
recorded in `FINAL_AUDIT.md` and `../agent2_audit/known_gaps.md`.

## Fixtures and expectations

For a test named `NAME` the runner looks for:

- `NAME.stdin` in `tests/stdlib/` then `tests/stdlib/agent2/` — passed on stdin
  (otherwise stdin is `/dev/null`).
- `NAME.expected` — byte-exact expected stdout.
- `NAME.stderr.expected` — byte-exact expected stderr.
- `NAME.exit` — expected process exit code (default `0`).

A test without an expectation fixture still has a defined success contract
(build success, runtime exit `0`), and its captured output is preserved, but
it is **not** treated as byte-exact verified.

Headers that include `stdlib/json.hp` or `yyjson.h` are compiled with
`-Ithird_party/yyjson -Lbuild/ext -lyyjson` automatically.

## Timeouts and classification

Every build uses `timeout` with `--build-timeout` (default 90 s) and every
run with `--timeout` (default 30 s). Results are classified as:

| Class | Meaning |
|---|---|
| `PASS` | build 0, run 0, all expectation fixtures matched |
| `FAIL` | ran but exit code or output differed |
| `BUILD_FAILURE` | compiler rejected the test |
| `TIMEOUT` | build or run exceeded its bound |
| `SKIPPED` | not run for a documented reason (not a pass) |
| `BLOCKED` | external dependency missing (not a pass) |

No failure is masked with `|| true`, and no failing test is removed to obtain
a green result.

## Results and reproducibility

All artifacts are written to `tests/stdlib/agent2/results/`:

- `summary.txt` — human-readable table.
- `summary.json` — machine-readable record with HEAD SHA, compiler path,
  timeouts, per-test build/run commands, exit codes, stdout/stderr, and
  classification.
- `<test>.build.log`, `<test>.stdout`, `<test>.stderr`, `<test>.diff`,
  `<test>.commands` — per-test evidence.

## Adding a new regression test

1. Add `tests/stdlib/agent2/<name>.cp`, self-checking with a final
   `<NAME> <checks> <failures>` line and a non-zero exit on failure.
2. Add `<name>.stdin` if the test reads stdin.
3. Add `<name>.expected` / `<name>.stderr.expected` captured from a verified
   run (must be deterministic across two consecutive runs).
4. Run `sh tests/stdlib/agent2/run_all.sh --test <name> --clean`.
5. Use unique owned-variable names — the C+ lowering tracks ownership by
   identifier within a function.

## Current limitations

- Sanitizer results are reported separately in `SANITIZER_MATRIX.md`; ASan is
  unsupported by the host toolchain for most builds. A passing UBSan run is
  not a memory-safety proof.
- `coverage.py` is a static API-name heuristic and `coverage_exec.sh` ignores
  runtime exit status (`|| true`); neither is a correctness gate. See
  `API_COVERAGE_REPORT.md` and `../agent2_audit/known_gaps.md`.
- Remote Linux CI and SonarCloud gates remain unverified locally.

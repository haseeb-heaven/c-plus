# Round-4 blue integration verifier

Run by the parent **after** rebuilding `build/c+`. Owns only new files in this
directory; it does not edit suites, rebuild, commit, or merge.

```sh
# full battery (17 steps, fixed order)
./tests/compiler/round4_integration/run.sh

# fast iteration: only core, agent1 --full, safety_matrix, safety_breakers
./tests/compiler/round4_integration/run.sh --quick

# inspect order / adjust hard timeouts / run one step / stop at first failure
./tests/compiler/round4_integration/run.sh --list
./tests/compiler/round4_integration/run.sh --timeout-scale 2
./tests/compiler/round4_integration/run.sh --step-timeout api_coverage=3600
./tests/compiler/round4_integration/run.sh --only ctest,api_coverage
./tests/compiler/round4_integration/run.sh --stop-on-fail
```

Reports (written here, never to a suite):

- `summary.txt` — per-step status, rc, duration, command, final summary line, counts
- `summary.json` — same data, structured, plus aggregate counts and `overall_rc`
- `logs/<step>.stdout.log`, `logs/<step>.stderr.log` — complete raw evidence

Guarantees:

- Exact order; every step is attempted (use `--stop-on-fail` to override).
- Hard per-step wall-clock timeout with process-group kill; timeouts are failures.
- No `|| true`; a step's exact rc is recorded and a non-zero rc makes the run fail.
- `CPLUS_BIN` is pinned to the freshly built `<repo>/build/c+` for every step.
- `api_coverage` regenerates its 2415 scenarios rather than trusting stale files.
- `rust_compare` is optional: if no runner exists in the tree it is recorded as
  `SKIP` (not a failure). Candidates checked: `tests/core/rust_compare_suite.py`,
  `tests/compiler/rust_compare/run_suite.py`, `tests/compiler/rust_compare/run.py`.

Full step order: `repo_tests`, `core`, `agent1_full`, `ctest`, `safety_matrix`,
`full_coverage`, `api_coverage`, `stdlib_api_coverage`, `compile_time_oob`,
`exceptions_suite`, `exceptions_fuzz100`, `safety_breakers`, `real_apps`,
`abi_mixed`, `rust_proof`, `rust_compare` (optional), `benchmarks`.

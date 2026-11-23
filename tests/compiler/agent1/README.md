# Agent 1 compiler safety suite

Run everything (discovers compile-fail, runtime-fail, positive, strict-positive,
native harnesses, strict40, stress, legacy and sanitizer categories):

```sh
CPLUS_BIN=$PWD/build/c+ sh tests/compiler/agent1/run_all.sh --full
```

Run one test / category / mode:

```sh
sh tests/compiler/agent1/run_all.sh --test cf_classic_bug1_oob
sh tests/compiler/agent1/run_all.sh --category compile_fail
sh tests/compiler/agent1/run_all.sh --mode strict --category strict_positive
sh tests/compiler/agent1/run_all.sh --timeout 120 --category sanitizer
```

Inspect generated C: every compile writes `results/generated/<name>.c`; the
source `--emit-c` argument is used explicitly.

Expected failures: compile-fail fixtures require an exact `.err` diagnostic;
runtime-fail fixtures are executed and require the `.exit` code and `.stderr`
substring. SKIPPED cases are documented contract exclusions (helper TUs linked
from their main TU; `uaf_conditional.c` is not in the strict safe subset).

Classification: PASS, FAIL, BUILD_FAILURE, RUNTIME_FAILURE,
DIAGNOSTIC_MISMATCH, TIMEOUT, SKIPPED, BLOCKED. Results in `results/summary.txt`
and `results/summary.json`; logs in `results/logs/`; generated C in
`results/generated/`.

Adding a regression: for a negative compiler case add `tests/compile_fail/x.c`
plus `x.err`; for a runtime case add `tests/runtime_fail/x.c`, `x.exit`,
`x.stderr`; for exact multi-mode cases add to
`tests/native/cplus_safety_tests.c`. Never remove a failing test to make the
suite green.

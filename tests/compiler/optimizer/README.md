# Native optimizer / safety-mode equivalence suite

This directory owns only **new** files. It does not build the compiler, does not
touch any other test tree, and makes no commits.

The suite compiles the same small, well-defined C+ programs under six native
build configurations and asserts that observable behaviour (exact stdout and
exit code) is **identical** across all of them. It also inspects the lowered C
emitted by `--emit-c` and asserts that native safety guards are present in
every Safe-mode build and absent in the Legacy-C build.

## Build configurations

Every configuration passes `--sanitize=none` so that no backend sanitizer is
involved and the only safety instrumentation is the C+ **native guards**.

| Name | Exact flags | Purpose |
| --- | --- | --- |
| `debug` | `--debug --sanitize=none` | -O0 plus debug info (compiler default) |
| `release` | `--release --sanitize=none` | -O2, native guards must stay on |
| `o0` | `-O0 --sanitize=none` | -O0 passed through as a C+ flag |
| `o2` | `-O2 --sanitize=none` | -O2 passed through as a C+ flag |
| `guards_on` | `--default --sanitize=none` | DEFAULT mode, native guards present |
| `guards_off` | `--legacy-c --sanitize=none` | Legacy-C mode, native guards disabled |

`-O0` / `-O2` are appended after the driver's own optimization flag, so with
clang the passed-through value wins. This makes `o0` and `o2` a clean pair that
differs only in the optimization level requested through C+ flags.

## What is asserted

For each case and each of the six configurations:

* `c+ build <src> <flags> --emit-c <out.c> -o <bin>` must succeed (exit 0).
* Running `<bin>` must produce the exact declared stdout and exit code.
* The emitted C must contain every declared native-guard token in the five
  Safe-mode configs.
* The emitted C must contain **none** of those tokens in `guards_off`.
* Tokens listed as `always` (RAII drop calls) must be present in every config,
  confirming deterministic cleanup survives both optimization and mode changes.

Finally, all six `(stdout, exit)` pairs for a case must be equal.

## Covered families

arithmetic (checked add/sub/mul/div), stack bounds, heap bounds / allocation,
pointer formation, RAII scope cleanup, RAII early-return cleanup, allocation
(malloc/free, calloc/realloc/free), and one mixed case combining all five.

## Run

```sh
cd <repo-root>
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/optimizer/run_suite.py
```

Optional environment knobs: `OPT_TIMEOUT` (per-subprocess hard timeout, default
60s), `OPT_WORKERS` (parallel workers, default 4). The runner exits 0 only when
every expectation matches, otherwise 1.

## Results

Written under `results/`:

* `summary.txt` — counts, exact commands, and every difference with case and config.
* `summary.json` — full machine-readable record including every argv/command.
* `logs/<case>.<config>.build.txt` — build command, return code and full output.
* `logs/<case>.<config>.run.txt` — run command, return code and full output.
* `emitted/<case>.<config>.c` — the lowered C inspected for guard tokens.

Case sources in `cases/` are regenerated from the manifest in `run_suite.py`
on every run.

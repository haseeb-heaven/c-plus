# stdlib safe-C API coverage suite (generated, differential)

Permanent, deterministic suite that calls **every exported function and macro**
of the 13 project-owned headers `stdlib/safe/*.h` in the sibling
`c-plus-stdlib` repo, one API at a time, with edge cases. Every scenario
asserts an exact return value (emitted canonically), exact stdout and exact
child exit code. The same generated C is built with two compilers and the
per-scenario stdout/exit must be byte-identical.

## Layout

| Path | Purpose |
| --- | --- |
| `generate.py` | Deterministic generator (bakes expected values from `exp.json`). |
| `exp.json` | Exact expected stdout/exit per scenario, measured once from the reference compiler. |
| `harness.h` | Fork + stdout-pipe capture runner; prints `SCENARIO <name> PASS/FAIL ...`. |
| `generated/*.c` | 9 generated C files (not one compile per scenario). |
| `generated/manifest.json` | APIs discovered, scenario counts, file list. |
| `run_suite.py` | Builds with clang and c+, runs both, compares per scenario, writes `summary.*`. |
| `summary.txt`, `summary.json` | Latest measured counts. |

## Run

```sh
STDLIB_ROOT=/path/to/c-plus-stdlib python3 run_suite.py
```

`STDLIB_ROOT` is read from the environment; no path is hard-coded in the
runner or in generated C. If unset, a sibling-repo path relative to this
script is used only as a local convenience. The linked archives must be
pre-built in `$STDLIB_ROOT/build/safe/libcplus_safecrt.a` and
`$STDLIB_ROOT/build/libc/libcplus_hardened.a`.

To re-measure and re-freeze expectations (runs the reference twice and aborts
if it is nondeterministic):

```sh
STDLIB_ROOT=/path/to/c-plus-stdlib python3 run_suite.py --bake
```

## Method

- Reference: `/usr/bin/clang -std=c11 -O2`.
- Under test: `c+ build --legacy-c --sanitize=none`.
- Same generated C files, headers and archives; both get the same
  `-Drestrict=` because c+'s legacy-C lowering cannot parse the `restrict`
  qualifier used by `cplus_libc.h`. This is a preprocess-level workaround only.
- Each scenario runs in a forked child with stdout captured through a pipe and
  stderr redirected to `/dev/null`. The parent records the child's exact
  stdout (hex), exit code, and whether it completed normally. Intentional
  traps (unguarded `cplus_runtime_violation`, format violations with the trap
  enabled) are expected to exit **70** and emit no stdout.
- Edge cases: null pointers, zero / one / exact capacity / capacity-1,
  overflow and `RSIZE_MAX_*` sizes, invalid pointers, overlap, embedded NULs,
  missing/extra state, repeated calls, and allocator fail injection.
- Every base scenario has a `|repeat2` twin that invokes the same API twice
  with identical arguments (the repeated-calls edge case).
- Expected values were calibrated once against the reference and are now
  literal constants in `generated/*.c`; a differential failure is reported if
  the c+ build emits different stdout or a different exit for any scenario.

## Measured result (last run, `STDLIB_ROOT` set to the sibling repo)

| Metric | Count |
| --- | --- |
| Headers inventoried | 13 |
| Exported APIs discovered (functions + documented macros) | 209 |
| Base scenarios generated | 741 |
| Scenarios generated / executed | 1482 / 1482 |
| Scenarios passed (identical under clang and c+) | 1482 |
| Scenarios failed | 0 |
| APIs with zero scenarios | 0 |
| APIs excluded for external state | 0 |

No API was excluded: I/O APIs were driven with `tmpfile()`, `/dev/null` and
`setenv()` inside the isolated child; stdin-driven scan APIs were fed from a
pipe created by the scenario. The `c+` harness path (no background threads)
was used throughout.

## Coverage honesty

**No line coverage claim is made.** This suite measures API and scenario
coverage only. No gcov instrumentation is used, so no line-coverage percentage
or uncovered-line list is reported. Add gcov instrumentation and re-run before
claiming source-line coverage.

# Native C+ check regression matrix

Runner: `tests/compiler/agent1/run_native_matrix.sh` (see `../run_native_matrix.sh`).

```sh
CPLUS_BIN=$PWD/build/c+ sh tests/compiler/agent1/run_native_matrix.sh --timeout 90
```

Six cases x four configurations = 24 cells.  Each cell records the exact
compile rc/stdout/stderr, the generated C, and (for `build`) the executable's
rc/stdout/stderr, then classifies PASS / FAIL / BLOCKED with an explicit
reason in `results/summary.txt` and `results/summary.json`.

## Contract used for classification

- Proven OOB (array decay, pointer arithmetic beyond a known array): every
  configuration must reject at compile time with the asserted
  `CPLUS-BOUNDS` / pointer-arithmetic diagnostic.
- Runtime overflow, zero divisor, invalid shift: a runtime-only value must
  trap with the native guard (`CPLUS_EXIT_ARITHMETIC = 110`) or with UBSan
  (rc 134 + `runtime error:`).  A silent rc-0 result is a FAIL.
- Narrowing of a value-changing constant: DEFAULT must warn (rc 0 + asserted
  `changes value` diagnostic + deterministic run rc 0); STRICT must reject.
- A cell is PASS only when an asserted diagnostic substring is present or an
  exact expected exit code is observed.  Timeouts and missing UBSan
  prerequisites are BLOCKED, never PASS or silently skipped.

## Configurations

1. `check --default --sanitize=none`
2. `check --strict --sanitize=none`
3. `build --default --sanitize=none` + execute
4. `build --strict --sanitize=undefined` with
   `CPLUS_CLANG=/opt/homebrew/opt/llvm/bin/clang` and
   `-- -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`

The compiler binary's mtime and SHA-256 are re-read at the start and end of
each run in `results/environment.txt`; a concurrent rebuild is reported as a
warning because results may then mix binary revisions.

## Files

- `cases/*.c` — the six fixtures; `inputs/*.txt` — stdin for runtime probes.
- `results/cells/<case>__<config>.txt` — per-cell expectations, commands, rcs
  and paths to the exact logs.
- `results/logs/` — verbatim compile/run stdout and stderr.
- `results/generated/` — generated C (`*.NOT_EMITTED` when the compiler
  rejected the source before emitting C).

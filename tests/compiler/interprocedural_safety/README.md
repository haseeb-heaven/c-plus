# Interprocedural safety regression suite

Native (compiler-binary) regression suite for C+ interprocedural safety.  It
owns only files in this directory; it does not build the compiler and does not
touch any other test tree.

## Run

```sh
cd <repo-root>
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/interprocedural_safety/run_suite.py
```

Environment knobs (optional): `IPS_TIMEOUT` (per-subprocess seconds, default
90), `IPS_WORKERS` (parallel workers, default 8).  The runner exits 0 only if
every declared expectation matches; otherwise it exits 1 and prints the exact
failing case names and modes.

No shell `|| true` is used: every subprocess return code is classified, and
every subprocess has a hard timeout.  Timeouts and compiler crashes are hard
failures.

## What is checked

Each case is a small self-contained program stored in `cases/<name>.c`
(regenerated deterministically on every run from the manifest in
`run_suite.py`).  For every case and every mode, the runner records:

* expected compile result (accept or reject),
* expected diagnostic as one or more **exact substrings** of the normalised
  compiler output (absolute source path is replaced by `<SRC>`),
* actual compile result, return code and full normalised output,
* runtime result (stdout substring and exit status) for accepted, safe cases.

Modes: `--default` (report proven errors, warn on unverified), `--strict`
(fail closed on unverified operations) and `--legacy-c` (compatibility mode;
deliberately **not** memory safe).  Negative cases therefore expect rejection
in DEFAULT and STRICT but acceptance in LEGACY.  "strict-only" cases expect a
DEFAULT warning plus acceptance, and a STRICT error.

Families covered (each with positive and negative cases):

| Family | Cases |
| --- | --- |
| `call_shape` | direct, 2/3-level nested, recursive, mutually recursive calls |
| `array_decay` | `int first(int *p){return p[4];}` with `int a[4]`, stack and heap extents |
| `param_bounds` | pointer-parameter extents, byte counts, conflicting/untracked extents |
| `ret_ownership` | borrowed/owned returns, returned-extent propagation, returned stack address |
| `nullability` | null-returning functions, null arguments crossing call boundaries, guards |
| `ownership` | release inside a callee, use-after-transfer, double release |
| `effects` | read and write effects through borrowed pointers |
| `globals` | global scalar writes, global array bounds, global pointer bounds |
| `alias` | local aliases and pointer-to-pointer (`int **`) accesses |
| `structs` | fixed array fields, struct pointer fields |
| `reassignment` | extent after reassignment, in-bounds and out-of-bounds |
| `dataflow` | branches, `for`/`while` loops, conditional index selection |
| `static_arith` | constant overflow, division by zero, allocation-size wrap, shift width |

Cases whose `polarity` is `soundness-gap` encode what a sound checker *should*
reject; the current compiler accepts them.  They are expected failures and are
reported by name with the reason and actual diagnostic.  They are kept in the
suite so a future fix turns a red case green (at which point flip the case to a
normal `unsafe(...)` with the emitted exact substring).

## Results

Written to `results/`:

* `summary.txt` — counts (cases, mode checks, runtime, per-family) and every
  failure with case name, mode, reason and expected/actual diagnostic.
* `summary.json` — full machine-readable record including all diagnostics,
  per-mode expectations, runtime results and the case sources.
* `logs/<case>.<mode>.txt` — complete normalised compiler output per check.
* `logs/<case>.build.txt` / `logs/<case>.run.txt` — build/run output.

A count of `cases failed` greater than zero is expected while soundness-gap
cases remain open; those case names are always listed explicitly in
`summary.txt` and on stdout.

# Deterministic C+ differential fuzzer

`fuzz.py` generates plain C programs from a fixed seed, builds each one with
the C+ compiler in **legacy C** mode and with a **reference host compiler**
(clang by default, gcc as fallback), runs both executables, and compares
stdout and process exit status.  It is a black-box differential test: it does
not assume any particular internal representation and never rebuilds the
compiler.

## What is generated

Every case is a self-contained C program with a deterministic `main()` that
prints labelled lines.  The generator (`ProgramGen`) is a small grammar driven
by `random.Random(seed)` and covers:

| Area | Examples |
|---|---|
| Expressions | arithmetic, bitwise, shifts, comparisons, logical ops, ternary, casts, `sizeof` |
| Declarations / types | scalars of every basic type, arrays, `enum`, `typedef`, globals/locals, `const`-free but bounded values |
| Pointers | address-of, dereference, array decay, `pi[i]`, `pi - g_arr`, `char*` views, casts |
| Structs / unions | tagged `struct`/`union`, nested members, positional and designated initializers, member assignment |
| Control flow | `if`/`else`, `for`, `while`, `do`/`while`, `switch`, `break`, `continue`, nested loops |
| Function calls | static helpers, recursion (bounded), function pointers, array-summing helpers |
| Initializers | scalar, array, struct, designated struct, string literals |

Generated values are small and in range so the programs avoid undefined
behaviour; any divergence therefore points at a toolchain difference rather
than at an ambiguous C construct.  The generator uses only per-instance RNG
and never `hash()`, the clock or the module-level RNG, so a given seed always
produces byte-identical sources.

## Determinism and seed policy

* `cases[i]` is generated with `case_seed = FUZZ_SEED + i`.
* A failure stores its `case_seed`.  To reproduce one case exactly:

  ```sh
  CPLUS_BIN=$PWD/build/c+ FUZZ_SEED=<case_seed> FUZZ_CASES=1 \
      python3 tests/compiler/fuzz/fuzz.py
  ```

* With the same `FUZZ_SEED` and `FUZZ_CASES`, the generated program sources are
  identical on every run (the summary text file lists the full seed list).

## Classification

A divergence is classified as:

* **parser** — one toolchain fails to compile and the diagnostic is a lex/parse
  error (`syntax error`, `expected ...`, `stray ...`, …).
* **sema** — one toolchain fails to compile on semantic/type grounds, or the two
  toolchains disagree on whether the program is a valid translation unit.
* **codegen** — both compile and exit normally, but stdout or exit status differ.
* **runtime** — one binary crashes, aborts or times out while the other runs.
* **optimizer** — a codegen/runtime mismatch that disappears when both
  toolchains are rebuilt at the opposite optimization level (the fuzzer
  re-tests mismatches at `-O0`/`--debug` when the primary run used `-O2`).

## Minimization

Each mismatch is minimized before it is reported.  The minimizer is a ddmin
style delta debugger over source lines: it repeatedly tries deleting chunks
of lines (shrinking the chunk as progress is made), then removes individual
lines, keeping a deletion only when the **same mismatch class** still
reproduces under the same toolchains.  The attempt is bounded by
`FUZZ_MIN_BUDGET` predicate checks; a converged flag in the metadata records
whether the budget was exhausted.

## Outputs

All artefacts are written under `tests/compiler/fuzz/results/`:

```
results/
  summary.txt                     human-readable counts + seed list
  summary.json                    structured counts + one record per failure
  failures/
    case_seed_<HEX>.c             exact original source that diverged
    case_seed_<HEX>.min.c         minimized source (when attempted)
    case_seed_<HEX>.json          seed, classification, both toolchains' outputs,
                                  minimization stats and a repro command
```

## Usage

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/fuzz/fuzz.py
# or
CPLUS_BIN=$PWD/build/c+ ./tests/compiler/fuzz/run.sh
```

Options (environment variables):

| Variable | Default | Meaning |
|---|---|---|
| `CPLUS_BIN` | `<repo>/build/c+` | C+ driver to test |
| `REF_CC` | `clang` then `gcc`/`cc` | Reference compiler |
| `FUZZ_SEED` | `0x5EED1234` | Base seed (`int(x, 0)`) |
| `FUZZ_CASES` | `200` | Number of programs |
| `FUZZ_WORKERS` | `8` | Parallel comparison workers |
| `FUZZ_TIMEOUT` | `30` | Per compile/run timeout (seconds) |
| `FUZZ_OPT` | `O2` | Primary optimization level (`O0` or `O2`) |
| `FUZZ_MIN_BUDGET` | `90` | Max minimizer checks per failure |
| `FUZZ_MAX_MINIMIZED` | `12` | Max failures to attempt minimizing |
| `FUZZ_RESULTS_DIR` | `<here>/results` | Alternate output directory (used by self-tests) |
| `CPLUS_FUZZ_SDK` | `xcrun --show-sdk-path` | Override the reference compiler SDK sysroot |

The runner never writes to `build/`; it only executes `CPLUS_BIN`.  This is a
differential measurement, not a proof of compiler correctness.

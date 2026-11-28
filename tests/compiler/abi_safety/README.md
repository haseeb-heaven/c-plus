# C+ native ABI regression suite

Native (compiler-binary) regression suite for the ABI emitted by the C+
compiler.  It owns only new files in this directory; it never rebuilds the
compiler, never touches another test tree and never edits existing sources.

## Run

```sh
cd <repo-root>
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/abi_safety/run_suite.py
```

Optional environment knobs: `ABI_TIMEOUT` (per-subprocess seconds, default
90), `ABI_WORKERS` (parallel workers, default 4).

The runner exits 0 only when every check passes.  Every subprocess is subject
to a hard timeout and every return code is classified explicitly; there is no
`|| true` anywhere.  Compiler crashes, timeouts, missing output files and
stdout mismatches are hard failures.

## What is checked

All 11 fixtures are compiled and executed in three modes: `--legacy-c`,
`--default` and `--strict` (33 checks).  Builds pass `--sanitize=none`
deliberately: sanitizer instrumentation and the accompanying runtime
recompilation are orthogonal to emitted data layout and calling convention,
and would perturb the ABI under measurement.  The C+ front-end mode flags are
still applied in full.  Expected stdout is compared **exactly** (full-string
equality, plus exact return code 0), never by substring.

| Family | Checks |
| --- | --- |
| `pointer_size` | `sizeof(void*)` vs function/object pointer and `uintptr_t`; exact widths of all fundamental scalar types, union and enum |
| `struct_layout` | `sizeof` and `offsetof` for a mixed struct, a `#pragma pack(1)` struct, a nested struct and a struct with an array field |
| `signature` | int/long/double/pointer arguments and returns; small and large structs passed and returned by value |
| `calling_convention` | typed callbacks (int, double, struct-returning) and a `qsort` comparison callback |
| `function_pointer` | function-pointer equality, byte-representation equality via `uintptr_t`, and function pointers stored in / returned from structs |
| `symbol_names` | exact C symbol names on a generated object and executable via `nm`: exported globals, absence of a `static` function, and the undefined `printf` symbol |
| `library_interop` | a static archive built by the same compiler from a separate translation unit (exact exported names via `nm`, then linked and called), and a call into the compiler's shipped precompiled `libcplus_runtime.a` |

The platform table in `run_suite.py` (`LP64_ABI`, `SUPPORTED`) pins the exact
expected numbers.  An unrecognised platform is a hard configuration error, not
a silent skip.  Mach-O symbol names are normalised by stripping the single
leading `_`; ELF names are used verbatim.

"Precompiled-library interop where available" is honoured explicitly: if `nm`
or `ar` is absent from `PATH`, the affected checks are recorded as `skip` with
a reason in both summaries.  They are never counted as passes.

## Results

Written under `results/`:

* `summary.txt` — totals, per-mode and per-family breakdown, and every failure
  with case name, mode and exact reason.
* `summary.json` — full machine-readable record: binary/version/platform, all
  build/run/nm logs, expected vs actual stdout, symbol sets and sources.
* `logs/<case>.<tag>.*.txt` — complete build, run, object/archive `nm` and
  linker-tool output for every check (`tag` is `legacy_c`, `default` or
  `strict`).
* `cases/` — fixtures regenerated deterministically on every run.

Fixtures deliberately avoid language constructs unrelated to ABI (long
chained arithmetic, variable-length stack arrays) that the current Safe-mode
rewriter handles conservatively; this keeps failures attributable to ABI
regressions rather than to unrelated analysis diagnostics.

# C+ real-world-style native ABI suite

Native ABI regression suite for the C+ compiler binary. It owns only new
files in this directory: it never rebuilds the compiler, never touches
another test tree and never edits existing sources.

## Run

```sh
cd <repo-root>
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/abi_real_world/run_suite.py
```

Optional environment knobs: `ABI_TIMEOUT` (per-subprocess seconds, default
90), `ABI_WORKERS` (parallel workers, default 4) and `ABI_CLANG` (reference
compiler, default `/usr/bin/clang`).

The runner exits 0 only when the clang oracle and every C+ check pass. Every
subprocess has a hard timeout and every return code is classified explicitly;
there is no `|| true` anywhere. Build failures, crashes, timeouts, missing
output files, stdout mismatches and symbol-set mismatches are hard failures.

## What is checked

Every fixture is idiomatic C with declarations before statements and no
C99-only for-loop declarations, so the same source builds under clang
`-std=gnu17` / `-std=c99` and under C+ `--legacy-c` (lowered with `-std=gnu89`).

Clang is the reference ABI implementation. For each fixture it is compiled
and run twice (`-std=gnu17`, `-std=c99`); both oracle runs must produce the
pinned exact stdout, and the two standards must agree. Then C+ compiles and
runs the same fixture in `--legacy-c`, `--default` and `--strict`; output must
match the reference byte-for-byte, with exact return code 0. All builds pass
`--sanitize=none` deliberately: sanitizer instrumentation and its runtime
recompilation are orthogonal to emitted data layout and calling convention,
and would perturb the ABI under measurement. The front-end mode flags are
still applied in full.

| Family | Cases | Coverage |
| --- | --- | --- |
| `pointer_size` | 1 | `sizeof(void*)`, object/function pointers, `uintptr_t`, `size_t`, null and call |
| `struct_layout` | 3 | packed/nested/array struct `sizeof`/`offsetof`; union raw bytes; bit-field packing incl. zero-width separator |
| `enum` | 1 | enum sizes and raw representation for small, full-range and flag enums |
| `function_pointer` | 1 | equality, byte representation via `uintptr_t`, struct round-trip |
| `callback` | 2 | int/double/struct-returning callbacks and `qsort` comparators |
| `variadic` | 1 | `va_arg` for int/double/long/pointer plus a libc variadic call |
| `extern_signature` | 1 | extern int/long/double/pointer/struct signatures across TUs |
| `symbol_names` | 1 | exact `nm` symbol sets on object/binary; exported globals, absence of a static helper, undefined `printf` |
| `library_interop` | 1 | static archive built by the same compiler: exact exported names, then linked and called |

That is 12 cases x 3 modes = **36 C+ checks**, plus 12 x 2 = **24 clang
oracle runs**. Symbol sets are normalised by stripping the platform's single
leading underscore on Mach-O; the expected values are pinned constants in
`run_suite.py`, not read back from either compiler. An unsupported host is a
hard configuration error, not a silent skip. `nm`/`ar` absence is recorded as
`skip` (never as a pass) in both summaries.

## Results

Written under `results/`:

* `summary.txt` — totals, per-mode/per-family breakdown, oracle table and
  every failure with case name, mode and exact reason.
* `summary.json` — full machine-readable record: binary/version/platform,
  both oracle runs, all build/run/nm/ar logs, expected vs actual stdout,
  symbol sets and sources.
* `logs/<case>.<tag>.*.txt` — complete subprocess logs; `tag` is
  `legacy_c`, `default`, `strict` or `oracle_std_gnu17` / `oracle_std_c99`.
* `cases/` — fixtures, executables, objects and archives regenerated
  deterministically on every run.

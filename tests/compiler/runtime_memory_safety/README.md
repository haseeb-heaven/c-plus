# Runtime memory-safety regression suite

Native (compiler-binary) suite for C+ **runtime** memory safety. It owns only
new files in this directory; it does not build the compiler, does not touch
other test trees, and makes no commits.

## Run

```sh
cd <repo-root>
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/runtime_memory_safety/run_suite.py
```

Optional environment knobs: `RMS_TIMEOUT` (per-subprocess hard timeout,
default 60s), `RMS_WORKERS` (parallel workers, default 2), `CPLUS_ASAN_CLANG`
(default `/opt/homebrew/opt/llvm/bin/clang`), `CPLUS_SDKROOT` (default the
Command Line Tools macOS SDK), `CPLUS_CHECK_CLANG` (default `/usr/bin/clang`).

The runner exits 0 only when every declared expectation matches, otherwise 1.

## Two modes per case

Every one of the 28 cases is compiled **and executed** in two independent
modes:

| Mode | Command | Role |
| --- | --- | --- |
| primary | `c+ build --strict --sanitize=none` | authoritative; C+ runtime checks only |
| supplementary | `c+ build --strict --sanitize=address` | independent ASan witness |

Sanitizer builds recompile the C+ runtime. Once per run the runner copies the
live `runtime/` sources into `results/root/` and points `CPLUS_ROOT` there, so
the instrumented runtime matches the compiler under test and is isolated from
edits made while the suite runs. No existing source file is modified.

## Exact assertions (no bare nonzero)

Nonzero exits are recorded but are never proof on their own. Each case kind
declares exact expectations:

* `safe` — run exit code **0**, no diagnostic marker in output, and the exact
  stdout substring(s).
* `runtime_abort` — exact exit code (**-6** / shell 134 for `abort`, **101**
  for a `_Exit` fatal), the exact `C+ error:` / `C+ runtime error:` message
  substring, and a C+ diagnostic prefix.
* `asan_only` — primary run exit **0** with no diagnostic marker (the OOB lands
  in accessible memory), supplementary run exit **-6** with both
  `ERROR: AddressSanitizer: <kind>` and `SUMMARY: AddressSanitizer: <kind>`.
* `static_reject` — compile exit code **1**, the exact named `[CPLUS-*]`
  diagnostic substring, and no binary produced.

The primary mode additionally runs `c+ check --strict --sanitize=none` for
every case and asserts acceptance (`check: <SRC> ok`) or the exact rejection
diagnostic. Every subprocess has a hard timeout; timeouts and compiler
crashes (negative return codes) are hard failures.

Diagnostics are normalised by replacing the source path with `<SRC>` so
expectations are checkout-independent.

## Covered families

Each family has positive controls and negative cases:

| Family | Cases |
| --- | --- |
| stack bounds | in-range index; `C+ error: index 7 ... 'a'` abort |
| heap bounds | in-range index; ASan `heap-buffer-overflow` |
| global bounds | in-range index; `C+ error: index 8 ... 'g'` abort |
| array decay | in-range `first(int*)`; ASan `stack-buffer-overflow` |
| interior pointers | `&a[2]` in range; heap interior OOB (ASan) |
| pointer arithmetic | in-range `p += n`; `out-of-bounds pointer formation` exit 101 |
| pointer-to-pointer | in-range `(*pp)[i]`; ASan write overflow |
| casts | in-range `void*` round-trip; ASan overflow after cast |
| function params / returns | borrowed return; stack and heap parameter OOB (ASan) |
| struct pointer fields | in-range `b->p[i]`; heap overflow through field (ASan) |
| malloc/calloc/realloc/free consistency | clean calloc/malloc/free; double-free, UAF, stale realloc, interior free (static rejects) |
| allocation consistency | allocation-size wrap and zero-size allocation (static rejects) |

## Results

Written to `results/`:

* `summary.txt` — counts (cases, mode checks, per-family, per-kind) and every
  failure with exact case name, mode, reason and expected/actual diagnostic.
* `summary.json` — full machine-readable record: all diagnostics, per-mode
  results, return codes, sources, commands and failure list.
* `logs/<case>.<mode>.<step>.txt` — complete normalised output and return code
  for each `check` / `build` / `run` subprocess.

Case sources in `cases/` are regenerated from the manifest in `run_suite.py`
on every run.

## Last verified run

`28 cases / 28 passed, 0 failed` (56 mode checks, 0 failure entries) against
`build/c+` (C+ 0.1.0) with `--strict --sanitize=none` (primary) and
`--strict --sanitize=address` (supplementary).

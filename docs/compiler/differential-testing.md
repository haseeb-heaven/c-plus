# Differential compiler testing

Purpose: detect incorrect parsing, type checking, code generation and runtime
behaviour by comparing C+ against established C compilers on compatible C
programs. Safety claims are never derived from a differential match.

## Reference compilers (verified on this host, 2026-11-23)

| Binary | Version | Notes |
| --- | --- | --- |
| `/usr/bin/clang` | Apple clang 16.0.0 (clang-1600.0.26.6) | xcrun shim → CommandLineTools; finds `MacOSX.sdk` automatically |
| `/usr/bin/gcc` | same Apple clang 16 | **not** an independent reference |
| `/opt/homebrew/opt/llvm/bin/clang` | Homebrew clang 23.1.6 | needs `-isysroot $(xcrun --show-sdk-path)` or headers are not found |
| `/opt/homebrew/bin/gcc-14` | GCC 14.2.0 | independent non-LLVM frontend; broken ASan on this host |
| `/opt/homebrew/bin/i686-w64-mingw32-gcc` | GCC 16.2.0 | 32-bit Windows cross target; not runnable on macOS directly |

C+ version: `C+ 0.1.0`, backend host Clang/LLVM. Legacy mode uses `-std=gnu89`;
Safe mode uses `-std=gnu17` (`compiler/Driver/Driver.cpp:735`). Pin `-std=gnu89`
for legacy differential compiles.

## Harness contract

`tools/compiler_differential/` + `tests/compiler/differential/`:

1. Discover reference compilers, record exact `--version`.
2. For each fixture: compile with reference (`-std=gnu89 -O0`) and with
   `c+ --legacy-c --sanitize=none`.
3. Execute both binaries with a hard timeout; compare exit status and exact
   stdout/stderr, normalising only absolute paths to `<SRC>`.
4. Save every mismatch with source, both commands, both outputs and a generated
   case name. No `|| true`; timeouts and crashes are hard failures.
5. `summary.txt` / `summary.json` record expected vs actual for every case.

## Exclusions

Exclude undefined behaviour and implementation-defined output from equality
assertions. Intentional C+ semantic differences (.hp headers, `countof`,
`cplus_*` runtime APIs, managed/resource extensions) are not legacy C and are
not part of this corpus. Any exclusion must be documented in
`docs/compiler/compatibility-matrix.md`.

## Verified results (this branch)

```
tools/compiler_differential/run_differential.py  -> 125 comparisons, 125 passed, 0 mismatches (5 compilers)
tests/compiler/differential/probe/run_probes.py   -> 25 probes, 0 mismatches
tests/compiler/compatibility/run_compat.py        -> 21/21 passed
tests/compiler/optimizer/run_suite.py             -> 11 cases x 6 configs, 66 builds/runs, 0 differences; 174 guard checks passed
tests/compiler/fuzz/fuzz.py (seed 0x5EED1234)     -> 200 cases compared, 0 mismatches
```

One early differential failure (`array_struct`) was traced to a **harness flag
skew**: the reference used `-std=c11` while C+ legacy defaulted to `-std=gnu89`,
so `for (int i = ...)` diverged. The harness now passes `-- -std=c11` to C+ so
both tracks use the same dialect, and the original two-loop fixture was
restored as a permanent regression. No compiler defect was involved; the
generated C was byte-identical to the input.

## Integration with other suites

- `tests/compiler/compatibility/`: language-feature coverage in LEGACY mode.
- `tests/compiler/optimizer/`: debug vs release, `-O0` vs `-O2`, instrumentation
  on vs off; asserts identical observable output and checks emitted C for
  expected guard presence/absence.
- `tests/compiler/fuzz/`: deterministic seeds, minimisation and permanent
  regressions for any mismatch.
- `tests/compiler/agent1/run_all.sh` and `run_native_matrix.sh`: safety-specific
  diagnostics and runtime traps; they are not differential oracles.

## Upstream test shapes (design only, no source copied)

Clang `clang/test/...` with `RUN:` + `FileCheck`/`-verify`, GCC `testsuite`
DejaGnu expected-output files, C2Rust cross-check event-stream comparison and
Clang SA `clang/test/Analysis/` are established shapes for recording expected
compiler behaviour. C+ uses its own native runners with exact stdout assertions
rather than importing any upstream test file.

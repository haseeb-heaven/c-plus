# API coverage scenario suite

A permanent, generated suite that exercises **every exported function, macro
and exported variable** in `runtime/include/cplus/cplus.h` one-by-one, and
measures the resulting line coverage of `runtime/cplus_runtime.c` with `gcov`.

## Files

| File | Purpose |
| --- | --- |
| `run_suite.py` | Generator **and** runner. Emits `generated_scenarios.c`, compiles, runs, collects `.gcda`, invokes `gcov`, writes `summary.txt` / `summary.json`. |
| `harness.h`, `harness.c` | Hand-written support: PASS/FAIL table, fork-trap runner with hard 5 s timeout and exact exit-code/stderr assertions, fatal-hook interception, stdout/stdin capture, exception round-trip, drop counter. |
| `generated_scenarios.c` | Generated, checked-in C source: `2,415` named scenario functions and a `const scenario_t g_scenarios[]` table. Do not edit by hand. |
| `summary.txt`, `summary.json` | Generated measurement report. |
| `.scratch/` | Scratch build directory (objects, binary, `.gcda`, `.gcov`, run log). Never touches the repository `build/` directory. |

## API inventory

`run_suite.py` carries an explicit manifest of **68 exported APIs**: all
function declarations in `cplus.h` (diagnostics, checked arithmetic,
conversion, managed memory, registered-allocation ABI, introspection, bounds
checks, scope guards, resources, mutex/TLS, print/scan, exceptions) plus the
public macros (`cplus_new`, `cplus_new_array`, `cplus_move`, `countof`,
`cplus_rotl32`, `cplus_rotr32`, `cplus_print_arg`, `cplus_scan_one`, `print`,
`scan`, `cplus_log`, `CPLUS_LOG_*`) and exported variables
(`cplus_fatal_hook`, `cplus_runtime_log_level`). `cplus_fatal_hook` has its own
scenarios that install a capturing hook, so the hook contract is tested rather
than bypassed.

## Scenario design

* Every scenario calls the **real** runtime (`runtime/cplus_runtime.c`), never a
  mock. Nothing is stubbed or `#define`d away.
* Parameters vary deterministically: sizes `0/1/4/…/large`, `SIZE_MAX`,
  `INT64_MIN/MAX`, null and non-null pointers, live/stale/freed/interior
  pointers, zero/negative/overflow counts, `bits` of `0/8/32/64/65`, signed and
  unsigned, repeated calls, nested scopes, two-thread mutex/TLS contention.
* Each scenario returns `PASS` only when an **exact** expected value, exact
  captured stdout string, or exact expected child status is observed. There is
  no `|| true`, no skipping, and no XFAIL list.
* Intentionally trapping scenarios (`invalid free`, `double free`, bounds
  abort, arithmetic overflow, out-of-range conversion, scan input errors,
  unhandled exceptions, …) run in a **forked child**. The parent asserts the
  exact `WEXITSTATUS` (e.g. `101`–`111`) or `128 + SIGABRT`, reads the child's
  stderr and asserts an exact diagnostic substring. A polling `waitpid` loop
  enforces a hard 5-second timeout (`SIGKILL` + reap on expiry).
* **Child coverage is not lost.** Before `fn()` runs, the child installs
  `cplus_fatal_hook = child_fatal_gcov_hook`, which calls `__gcov_dump()` at
  the point where `cplus_print_fatal()` would otherwise `_Exit`. `abort()`
  paths (notably `__cplus_bounds_check`) never reach that hook, so the child
  also installs a `SIGABRT` handler that dumps and then restores the default
  disposition and re-raises, preserving the exact `128 + SIGABRT` status. All
  dumps write to the same `.scratch/obj/*.gcda` as the parent (the absolute
  object path is baked into the `.gcno`, and the child inherits the cwd), so
  libgcov merges the counters when the parent exits. `GCOV_PREFIX` /
  `GCOV_PREFIX_STRIP` were considered but are deliberately **not** set: every
  child already resolves to the parent's gcda directory, and adding a prefix
  would redirect them away from it.
* A small number of `cplus_fatal_hook` scenarios use the public hook to capture
  the fatal code/kind and `longjmp` back before `_Exit`; these do **not** trap
  and are used to verify the exported hook API. Their forked counterparts
  still verify real process termination.
* The whole suite binary is executed by the runner under a hard 900-second
  timeout (`--timeout` overrides it); any timeout is a hard failure.

## Running

```sh
python3 tests/compiler/api_coverage/run_suite.py
# optional: CPLUS_CLANG=/opt/homebrew/opt/llvm/bin/clang ... --timeout 900
```

The command compiles `-std=gnu17 -g -O0 -fprofile-arcs -ftest-coverage` with
`runtime/include` and this directory on the include path, links `-lpthread
-lm`, runs the binary, then runs `/usr/bin/gcov -o .scratch/obj -p
runtime/cplus_runtime.c` from inside `.scratch/` (a relative symlink to
`runtime/` keeps gcov's embedded source path resolvable while all output stays
inside the scratch directory).

## Measured result (latest run)

* Scenarios discovered / executed / passed / failed: **2415 / 2415 / 2415 / 0**
* APIs inventoried / with at least one scenario / with zero scenarios:
  **68 / 68 / 0**
* `gcov` line coverage of `runtime/cplus_runtime.c`:
  **814 executed / 917 executable lines = 88.77 %**
  (before child-side `__gcov_dump()` instrumentation: 710 / 917 = 77.43 %;
  no scenario was added, removed or weakened — both runs had 2,415 scenarios).

This is **not** 100 %. `summary.txt` and `summary.json` enumerate every one of
the 103 remaining uncovered executable lines with its source text and a reason.
The dominant reasons are:

1. unreachable failure branches (malloc / `pthread_mutex_init` / `pthread_key_create`
   OOM and OS-failure paths, `uint64_t` generation wrap, a registry extent larger
   than the platform's usable malloc size);
2. opt-in `CPLUS_QUARANTINE` paths (disabled by default);
3. the retired-address-ledger saturation path (requires 1,048,576 retirements);
4. `pthread_atfork` child hook lines (fork-child only, and the child's own
   counters cannot be merged back into the parent);
5. a small set of scan width branches and stale-registration diagnostic lines
   that are not exercised by the current scenario tables.

Only the measured percentage above is reported; no coverage is inferred from
build success or from the number of scenarios.

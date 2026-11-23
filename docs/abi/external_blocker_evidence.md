# External compiler blocker — final evidence (Task 4)

**Branch:** `agent/modern-stdlib`
**Runs executed (UTC):** 2026-11-23T03:57Z — 03:59Z
**Host:** Darwin / arm64, macOS 27.2.1 (build 26B131); Apple clang 16.0.0 (`/usr/bin/clang`)
**Compiler under test:** `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+`
  - `C+ 0.1.0`, `Backend: host Clang/LLVM`
  - SHA-256: `b8b4be0e95533f4ea91b2756ded2a50d12192b3cbf622d761ab4eed92fa111a4`

All five scripts were run **serially, one at a time, each wrapped in an external
`timeout` bound** (300–600 s). No script result was retried until green, no
output was masked, and no exit code was rewritten. The numbers below are the
literal final lines and the literal shell exit codes observed.

## 1. Script results (exact final lines and exit codes)

| # | Command (serialized) | Exit code | Exact final line(s) |
|---|----------------------|-----------|---------------------|
| 1 | `sh tests/run_tests.sh` | `1` | `C+ tests: 99 passed, 51 failed` |
| 2 | `sh tests/stdlib/run_deep_tests.sh` | `1` | `deep stdlib tests: 28 passed, 58 failed, 1 skipped (29 file(s) x 3 modes)` |
| 3 | `sh tests/abi/run_abi_compat.sh` | `0` | `abi_compat: 69 passed, 0 failed` |
| 4 | `sh tests/abi/run_abi_layouts.sh` | `1` | `fatal error: 'errno.h' file not found` … `1 error generated.` |
| 5 | `sh tests/native/build_native_tools.sh` | `0` | `native tools: 8 built, 0 failed, 0 missing` |

### Per-mode breakdown — deep stdlib (script 2)

```
MODE           PASSED   FAILED  SKIPPED  BUILD-FAIL  RUN-FAIL  OUT-FAIL
default             0       29        0          29         0         0
strict-none        28        0        1           0         0         0
ubsan               0       29        0          29         0         0
```

The single skip is a bounded-run abort in `strict-none`:
`TIMEOUT  deep_collections_alloc (strict-none): run exceeded 30s (rc=134)`
(the harness classifies `rc >= 128` as SKIPPED, never as PASSED).

## 2. The exact external diagnostic

Verbatim, as emitted into `tests/out/<case>.build.log` for **all 51** failing
cases of script 1 and **all 58** failing build invocations of script 2
(e.g. `tests/out/hello.build.log`):

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
  129 |     cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", NULL, 0,
      |                       ^
1 error generated.
error: failed to compile instrumented C+ runtime
```

The relative-path form requested for tracking is exactly:

```
c-plus-core/runtime/cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
```

### Why this is an external blocker, not a change in this repo

- The failing source is in a **different git repository**:
  `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core` (its own `rev-parse
  --show-toplevel` returns that path), *not* this `c-plus-stdlib` checkout.
- The symbol **is** declared in the external repo's
  `runtime/include/cplus/cplus.h:85` (`CPLUS_EXIT_ARITHMETIC = 110`), but the
  external driver's "compile instrumented C+ runtime" step does not see that
  declaration; the resulting error string and that compile step live in the
  external repo (`c-plus-core/compiler/Driver/Driver.cpp:861`).
- The failure is deterministic and occurs only on the **instrumented** runtime
  path. Scripts that pass explicitly use `--sanitize=none`:
  - deep stdlib `strict-none` mode (`--strict --sanitize=none`) — 28 passed.
  - `run_abi_compat.sh` T3a/T3b c+ builds — all passed.
  - `build_native_tools.sh` (all 8 tools built with `--strict --sanitize=none`).
  Default and `--sanitize=undefined` modes both reproduce the diagnostic.
- `run_abi_layouts.sh` (script 4) fails **for a different, also external,
  environment reason**: its c+ driver invocation resolves a clang with a broken
  default SDK (`/Library/Developer/CommandLineTools/SDKs/MacOSX27.sdk` does not
  exist; only up to `MacOSX15.2.sdk` is installed), so `libc/log/log.c` cannot
  find `<errno.h>`. This is toolchain-selection/sysroot state, not the
  `CPLUS_EXIT_ARITHMETIC` issue and not a `compiler/` or `runtime/` edit here.
  Diagnostic-only probe (pinned `CPLUS_CLANG=/usr/bin/clang`,
  `PATH=/usr/bin:/bin`): exit `0`, final line `abi_layouts: 70/70 layouts match`.
  This probe is recorded for diagnosis only and does **not** change the official
  script-4 result above.

## 3. Agent 2 file-ownership check

```
$ git status --short compiler runtime
<empty>
```

The `compiler/` and `runtime/` trees in this repo are clean: no staged or
unstaged changes, confirming Agent 2 introduced no compiler/runtime edits in
this checkout. (Other untracked paths from parallel agents are outside the
scope of this check and were neither created nor modified by this task.)

## 4. Scope and honesty statements

- **ABI compatibility results are valid only for the exact current binary.**
  The `abi_compat: 69 passed, 0 failed` result was produced by
  `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+` with SHA-256
  `b8b4be0e95533f4ea91b2756ded2a50d12192b3cbf622d761ab4eed92fa111a4`, using
  the pinned toolchain recorded in `build/abi/compat/results.log`
  (`PATH=/usr/bin:/bin`, `CPLUS_CLANG=/usr/bin/clang`, Apple clang 16.0.0,
  `arch=arm64 os=Darwin`). It says nothing about a rebuilt compiler, a different
  SDK/clang, or any future binary.
- **No remote CI result is claimed.** Everything in this document comes from
  local, serialized execution in this workspace; no GitHub Actions / remote
  runner status is referenced, implied, or relied upon.
- **No fabricated green results.** Scripts 1, 2 and 4 exited non-zero and are
  reported as failures with their real counts. The only green results reported
  (scripts 3 and 5, and the explicitly labelled diagnostic probe) are the exact
  lines actually observed. Timeouts are reported as skipped, not passed.

## 5. Bottom line

The external `c-plus-core` instrumented-runtime compile error
(`c-plus-core/runtime/cplus_runtime.c:129:23: ... 'CPLUS_EXIT_ARITHMETIC'`) is
an **unresolved external blocker**. It prevents the default and UBSan modes of
`run_tests.sh` and `run_deep_tests.sh` from going green and is outside this
repo's `compiler/` / `runtime/` ownership. It must be fixed upstream in
`c-plus-core` before a full green run can be claimed. Tests that exercise the
non-instrumented (`--sanitize=none`) path — ABI compatibility (69/69) and the 8
native tools — are green for the exact binary and toolchain stated above only.

# Real-world C project compatibility (report scaffold)

> **Status: scaffold, not results.** This document defines the shape of the
> real-world C compatibility report. It was drafted before the parallel
> execution workstream produced its harnesses or data. Toolchain metadata in
> section 1 was captured locally on this host; **every campaign result cell is a
> `PENDING` placeholder and no project has been recorded as built or passing
> here.** Do not quote a placeholder as a measurement. Fill the cells only from
> executed runs, then remove this banner.

## Scope and claims

- Purpose: record what happens when **unmodified, externally sourced C
  projects** are fed to `c+`, across modes, compared with a reference compiler
  on the same host and flags.
- This is a **behavioural compatibility report**, not a conformance proof and
  not a memory-safety proof. A match means "the observed build and run matched
  on the pinned toolchain for the pinned project revision"; nothing more.
- Companion documents:
  - `docs/compiler/compatibility-matrix.md` — feature-level implementation and
    differential coverage.
  - `docs/compiler/differential-testing.md` — harness contract and exclusions
    for small differential fixtures.
  - `docs/UPSTREAM_AUDIT.md` — prior single-file real-C evidence (`kilo.c`,
    `2048.c`, `tetris.c`); those results are **not** re-counted in this report.
- Execution workstream owns the harness directories `tests/compiler/real_world_c/`,
  `tests/compiler/c_standards/` and `tests/compiler/abi_real_world/`; this
  document only consumes their outputs. Integration and any claim-making remain
  parent-owned.

## 1. Toolchain / version snapshot

Captured on this host by the scaffold author; **re-capture immediately before
publishing** and record the capture date and the exact `c+` build used.

| Item | Value | How captured |
| --- | --- | --- |
| Capture date | 2026-11-23 | — |
| Branch / HEAD at capture | `agent/core-safety` / `d71db23` | `git branch --show-current`, `git rev-parse --short HEAD` |
| Host | macOS 27.2.1, Darwin 27.2.0, arm64 | `sw_vers`, `uname -a` |
| C+ under test | `C+ 0.1.0`, backend host Clang/LLVM | `<CPLUS_BIN> --version` |
| C+ binary path | `<set CPLUS_BIN to the built binary>` | env var, never a hard-coded user path |
| Reference 1 | Apple clang 16.0.0 (clang-1600.0.26.6) | `/usr/bin/clang --version` |
| Reference 2 | Homebrew clang 23.1.6 | `/opt/homebrew/opt/llvm/bin/clang --version` |
| Reference 3 | GCC 14.2.0 (Homebrew) | `/opt/homebrew/bin/gcc-14 --version` |
| SDK | `<PENDING: xcrun --show-sdk-path>` | `xcrun --show-sdk-path` |
| C+ source dialect flags | `-std=gnu89` LEGACY, `-std=gnu17` otherwise | `compiler/Driver/Driver.cpp:740` (code fact, not a measurement) |

Notes for the final report:

- Record `CPLUS_CLANG` state (set/unset and value) for every run; the differential
  suites unset it. Backend skew invalidates comparisons.
- State explicitly whether `/usr/bin/gcc` was used: on this host it is the same
  Apple clang 16 shim and is **not** an independent reference.
- The Homebrew clang needs `-isysroot $(xcrun --show-sdk-path)`; GCC 14 is the
  independent non-LLVM frontend. ASan is not usable in this environment (runtime
  initialisation failure), so sanitizer comparisons use `--sanitize=none` unless
  a working configuration is separately evidenced.

## 2. Corpus selection and candidate shortlist

Selection rules (apply before any build, apply uniformly to all candidates):

1. Real upstream C source, downloaded at a **pinned commit**, not vendored into
   this repository. Store only URL + commit + license metadata here.
2. Unmodified source: no C+ syntax, no `.hp` headers, no source edits to make a
   project compile. A required edit disqualifies the project from the
   "unmodified" column and must be reported as a failure with the diff.
3. License must permit local build/test. Record the licence **as found in the
   pinned tree**, not from memory; if it is incompatible with this MIT project,
   record it as rejected for redistribution (no code inspected/copied).
4. Project must have a deterministic, scriptable build and a runnable command
   with observable stdout/stderr and exit status. Nondeterministic output
   (timestamps, addresses, random seeds, temp paths) must be normalised or
   excluded **before** running, with the exclusion written here.
5. Record the exact configure/build flags and the exact test command for both
   the reference and the C+ track; the only intended difference is the compiler.

Candidate shortlist — **URLs are canonical upstream pointers; commits, licences
and all statuses are placeholders to be filled from the pinned trees. No
candidate below has been built in this document.**

| Project | Upstream URL | Pinned commit | License (verify at pin) | C surface exercised | Build / test entry point | Status |
| --- | --- | --- | --- | --- | --- | --- |
| SQLite | `https://github.com/sqlite/sqlite` | `<PENDING: commit>` | `<PENDING: verify>` | large amalgamation, C89/C99, macros, `goto`, libc/POSIX | `<PENDING: configure + test command>` | `<PENDING: not built>` |
| zlib | `https://github.com/madler/zlib` | `<PENDING: commit>` | `<PENDING: verify>` | portable C89, pointers, bit ops, `sizeof`/struct layout | `<PENDING: make + self-test>` | `<PENDING: not built>` |
| curl | `https://github.com/curl/curl` | `<PENDING: commit>` | `<PENDING: verify>` | large multi-TU, configure, callbacks, sockets | `<PENDING: configure + make test>` | `<PENDING: not built>` |
| Lua | `https://github.com/lua/lua` | `<PENDING: commit>` | `<PENDING: verify>` | compact C89, `setjmp`/`longjmp`, varargs, libc | `<PENDING: make + test suite>` | `<PENDING: not built>` |
| libpng | `https://github.com/pnggroup/libpng` | `<PENDING: commit>` | `<PENDING: verify>` | structs, `setjmp`, malloc/free, pointers | `<PENDING: cmake + ctest>` | `<PENDING: not built>` |
| `<PENDING: add project>` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING: not built>` |

Prior single-file evidence already recorded elsewhere (do not merge into these
counts): `kilo.c`, `2048.c`, `tetris.c` — see `docs/UPSTREAM_AUDIT.md`.

## 3. Build flags, modes and standards

Code facts from the driver, stated so the result tables can name exact
configurations. These are not measurements.

| Mode / config | Invocation | Backend standard | Optimisation / instrumentation | Notes |
| --- | --- | --- | --- | --- |
| LEGACY | `--legacy-c` (alias `--legacy`) | `-std=gnu89` | per build config | behavioural oracle for unchanged C; no C+ safety checks |
| DEFAULT | no mode flag | `-std=gnu17` | per build config | safe checks active |
| STRICT | `--strict` | `-std=gnu17` | per build config | additional rejections; `-Werror` promoted diagnostics (`compiler/Driver/Driver.cpp:778`) |
| Release | `--release` | as above | `-O2` | `compiler/Driver/Driver.cpp:745` |
| Debug | `--debug` | as above | `-O0 -g` | `compiler/Driver/Driver.cpp:747` |
| Sanitizers off | `--sanitize=none` | as above | none | use for stdout/exit equality; sanitizer messages are not C output |
| Dialect override | `-- -std=c11` (passthrough) | caller-specified | — | pin the same dialect on both tracks; a prior skew here produced a false mismatch (see `differential-testing.md`) |

Standards statement to carry into the final report verbatim in substance:

- The driver emits `-std=gnu89` in LEGACY and `-std=gnu17` in Safe mode.
  C99/C11/C17 features accepted by the host Clang are available through the
  backend, but C+ does **not** claim a separate C23 implementation.
- Strict ISO `-pedantic-errors` is **not supported**: generated C uses GNU
  extensions (statement expressions, `__typeof__`, `_Static_assert`).
- C standard coverage (C89 → C23) is tracked separately in
  `tests/compiler/c_standards/`; results there are not a blanket guarantee that
  every conforming program in a standard is accepted.

## 4. How each signal is measured

| Signal | Procedure | Pass condition | Raw evidence |
| --- | --- | --- | --- |
| Build / acceptance | compile pinned project with C+ in each mode; reference track uses same flags minus C+ | C+ result matches the recorded expectation (accept, or reject with named diagnostic) | command + full log per project/mode |
| stdout / exit match | run both binaries with identical input, env, cwd and timeout; normalise absolute paths to `<SRC>` only | exact stdout and exact exit status; stderr compared with documented exclusions; timeouts/crashes are hard failures | both outputs, diff, exit codes |
| Source preservation | for plain C, compare generated C from `--emit-c` with the input after removing the single mandatory leading `#line` directive | byte-identical; re-use shape of `tests/compiler/source_preservation/run.py` | fixture, generated C, diff |
| ABI checks | compare pointer sizes, struct `sizeof`/`offsetof`, signatures, callback shapes, exported symbol names and precompiled-library interop across DEFAULT/STRICT/LEGACY | re-use shape of `tests/compiler/abi_safety/run_suite.py`; all checks pass | per-check results |
| Failures | capture stage, diagnostic, and whether it is a C+ defect, a documented safe-mode rule, a harness issue or an unsupported feature | classified and, if a defect, a permanent regression is filed | triage table below |

Do not count a bare nonzero exit as a safety success, and do not count a
reference build failure as a C+ success. Every "pass" needs the named signal.

## 5. Results (placeholders — fill from executed runs)

Legend: `` `<PENDING: ...>` `` = no data recorded yet; `N/A` = not applicable and
must be explained in a footnote; `EXPECTED-REJECT` = rejection required by a
documented safe-mode rule, not a compatibility failure by itself.

### 5.1 Build / acceptance matrix

| Project | Mode | Standard passed | Build flags | C+ build result | Reference build result | Classification | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `<project>` | LEGACY | gnu89 | `--legacy-c --release --sanitize=none` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` |
| `<project>` | DEFAULT | gnu17 | `--release --sanitize=none` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` |
| `<project>` | STRICT | gnu17 | `--strict --release --sanitize=none` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` |

### 5.2 Runtime stdout / exit match

| Project | Mode | Test command | stdout exact match | exit code match | stderr match / exclusions | Runs | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `<project>` | `<mode>` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` |

### 5.3 Source preservation

| Project / fixture | Mode | Byte-identical after `#line` | Diff summary | Notes |
| --- | --- | --- | --- | --- |
| `<project or plain-C fixture>` | LEGACY | `<PENDING>` | `<PENDING>` | `<PENDING>` |

### 5.4 ABI checks

| Project | Mode | Pointer sizes | Struct `sizeof`/`offsetof` | Signatures / callbacks | Symbol names | Precompiled-library interop | Checks passed |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `<project>` | `<mode>` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` |

### 5.5 Aggregate counts

| Metric | Value |
| --- | --- |
| Projects shortlisted / pinned | `<PENDING>` / `<PENDING>` |
| Project × mode builds attempted | `<PENDING>` |
| Accepted in LEGACY | `<PENDING>` |
| Accepted in DEFAULT / STRICT | `<PENDING>` / `<PENDING>` |
| stdout + exit matches (of comparable runs) | `<PENDING>` |
| Source-preservation passes | `<PENDING>` |
| ABI checks passed | `<PENDING>` |
| Failures by class (defect / expected-safe-rule / harness / unsupported) | `<PENDING>` |

## 6. Failures and triage

Classification taxonomy:

- **C+ defect** — valid unchanged C rejected, or behaviour differs with no
  documented reason. Requires a permanent regression and a follow-up fix.
- **Expected safe-mode rule** — rejection caused by a documented Safe/STRICT
  rule on otherwise valid C (e.g. raw `realloc`, overlapping `memcpy`, plain-char
  `ctype`, `printf` in a signal handler, `setjmp`/`longjmp` over live resources;
  see `docs/compiler/compatibility-matrix.md`). LEGACY must still compile it.
- **Harness issue** — flag skew, missing sysroot, missing system library, path
  normalisation, nondeterministic output. Fix the harness, then re-run; do not
  silently exclude.
- **Unsupported feature / out of scope** — project requires a build model, C
  dialect, target or external dependency this environment cannot exercise.
  Record precisely; never count as a pass.

Triage table template:

| Project | Mode | Stage (configure/build/run/ABI) | Exact diagnostic or diff | Class | Regression / follow-up | Resolved? |
| --- | --- | --- | --- | --- | --- | --- |
| `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` | `<PENDING>` |

## 7. Honest limitations

Carry these into the final report; they are constraints, not caveats to bury.

- **Not all C standards are guaranteed.** LEGACY emits `-std=gnu89`, Safe emits
  `-std=gnu17`; later C features arrive only through the host Clang backend.
  There is no separate C23 implementation and no strict ISO `-pedantic-errors`
  mode because generated C uses GNU extensions. A green C89/C17 fixture does
  not imply C99/C11/C23 conformance.
- **Not all projects are supported.** Only the pinned candidates in section 2
  are in scope, and only on this host/toolchain. Large multi-TU build systems,
  cross-compilation, 32-bit targets, projects needing unavailable system
  libraries, and programs with nondeterministic output may be out of scope and
  must be listed as such rather than quietly dropped.
- **LEGACY is not memory safe.** It is the behavioural oracle for unchanged C;
  it disables the safety checks. A LEGACY stdout match says nothing about
  safety, and a Safe-mode rejection says nothing about parser conformance.
- **No general safety proof.** Differential or stdout/exit matches do not
  establish absence of undefined behaviour, full pointer-provenance tracking,
  general temporal safety, or data-race freedom. See `docs/LIMITATIONS.md` and
  `docs/compiler/runtime-memory-safety.md`.
- **Local, small-sample evidence.** All numbers are a single host snapshot with
  a small corpus; no CI claim, no universal acceptance-rate claim, no blended
  score, and no claim that results transfer to other compilers or platforms.
- **No source vendored.** Upstream projects are referenced by URL + pinned
  commit only; their source is not added to this repository (see
  `docs/UPSTREAM_AUDIT.md` for the licence-rejection precedent).

## 8. Reproduce

Fill in the runner commands once the parallel harnesses land; keep paths
relative or environment-supplied.

```sh
cd <repo root>
unset CPLUS_CLANG
export CPLUS_BIN="$PWD/build/c+"        # or path supplied by caller
export REAL_C_ROOT="<downloaded projects root, not in this repo>"

# toolchain snapshot
"$CPLUS_BIN" --version
/usr/bin/clang --version
/opt/homebrew/opt/llvm/bin/clang --version
/opt/homebrew/bin/gcc-14 --version

# execution harnesses (owned by the parallel workstream; paths to confirm)
# python3 tests/compiler/real_world_c/run.py
# python3 tests/compiler/c_standards/run.py
# python3 tests/compiler/abi_real_world/run_suite.py

# supporting suites already in the tree
# python3 tools/compiler_differential/run_differential.py
# CPLUS_BIN="$CPLUS_BIN" python3 tests/compiler/source_preservation/run.py
# python3 tests/compiler/abi_safety/run_suite.py
```

## 9. Evidence layout / index

| Artifact | Path | Status |
| --- | --- | --- |
| This report | `docs/compiler/real-world-compatibility.md` | scaffold — results pending |
| Real-project harness | `tests/compiler/real_world_c/` | `<PENDING: created by workstream>` |
| C standards harness | `tests/compiler/c_standards/` | `<PENDING: created by workstream>` |
| Real-world ABI harness | `tests/compiler/abi_real_world/` | `<PENDING: created by workstream>` |
| Raw logs / summaries | `<PENDING: per-harness results directory>` | `<PENDING>` |
| Prior single-file evidence | `docs/UPSTREAM_AUDIT.md` | exists; not re-counted here |

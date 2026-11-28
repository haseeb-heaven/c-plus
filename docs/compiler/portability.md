# Compiler toolchain portability (issue #32)

This document records which compiler/runtime/libc/platform combinations are
actually exercised by CI. **A green job on one OS never declares support for
another OS or architecture**; every row has its own CI job as evidence. Exact
tool versions for each run are printed to that job's log by the
`Toolchain versions and feature probes` step, which executes
`sh tools/verify_toolchain.sh`.

## Evidence policy

- `Supported` requires a dedicated CI job for that exact runner/architecture
  that configures, builds, runs the ABI gate, installs into a clean SDK prefix,
  and compiles + runs both an unchanged-C and a C+ smoke program from that
  prefix.
- `Experimental` means a job exists in this change but support is provisional
  until a green run is observed in that named job.
- `Unsupported` means there is no CI job and the toolchain path is undefined;
  neither a local build nor a neighbouring platform's success counts.

## Compatibility matrix

| Platform / runner label | Arch | libc / runtime | Compiler (exact version in CI log) | C+ runtime | Status | Evidence (CI job name) |
| --- | --- | --- | --- | --- | --- | --- |
| macOS 15 (`macos-15`) | arm64 (Apple silicon) | Darwin libSystem; C runtime from brew LLVM clang | Homebrew LLVM clang + Apple clang; `llvm-config` from `$(brew --prefix llvm)` | `libcplus_runtime.a` installed to `<prefix>/lib` | Supported | `macos-arm64-cplus` |
| Ubuntu 24.04 (`ubuntu-24.04`) | x86-64 | glibc 2.39 (Ubuntu 24.04 base image) | apt `clang` / `llvm-config` (`/usr/lib/llvm-<major>`) | `libcplus_runtime.a` installed to `<prefix>/lib` | Supported | `linux-x86-64-cplus` |
| Ubuntu 24.04 arm (`ubuntu-24.04-arm`) | arm64 (Arm Neoverse N2 / Ampere Cobalt 100) | glibc 2.39 (Ubuntu 24.04 arm image) | apt `clang` / `llvm-config` (`/usr/lib/llvm-<major>`) | `libcplus_runtime.a` installed to `<prefix>/lib` | Experimental — job added in this change; promote to Supported only after a green run | `linux-arm64-cplus` |

Runner label validity: `ubuntu-24.04-arm` is a valid public GitHub-hosted
label (GitHub changelog, 2025-08-07, "arm64 hosted runners for public
repositories are now generally available"; it also works in private repos
since 2026-01-29). `macos-14` was retired on 2026-11-02, so macOS arm64 uses
`macos-15`.

## Sanitizer feature status

Sanitizer support is probed at CI runtime, never assumed. For each name in
`SANITIZERS` (default `address,undefined`), the probe compiles AND runs a tiny
program with `-fsanitize=<name>`. The log shows either
`PROBE sanitizer=<name>: SUPPORTED (compiled and ran)` or
`PROBE sanitizer=<name>: UNSUPPORTED (skip=record)` followed by the compiler or
runtime diagnostic. A skip is recorded explicitly and applies only to that
sanitizer on that platform. `memory`, `thread`, `leak`, `cfi` and `integer` are
driver-selectable but **not** presumed portable; the driver documents `memory`
as Linux x86-64 only. Baseline C compilation and static/shared library linking
are hard requirements and fail the job if broken.

## Experimental and unsupported targets

- **Windows (x64 and arm64): UNSUPPORTED as a host compiler.** No CI job is
  defined. The toolchain path is undefined: CMake generator, LLVM/Clang
  discovery, MSVC vs clang-cl ABI, install layout (`bin`/`lib`/`include`/
  `share`), path separators, runtime linker and sanitizer runtimes have not
  been validated on any Windows runner. The public label `windows-11-arm`
  exists but is deliberately not added here.
- **Windows x86-64 target smoke: EXPERIMENTAL (local evidence).** A C+ source
  was analysed with host `c+ --strict`, lowered to C, cross-compiled with
  MinGW-w64 GCC 16.2.0 to a PE32+ executable and run under Highball 0.10.22 /
  its Wine engine with exact stdout and exit 0. This does **not** make Windows
  a supported host or validate the runtime library, CRT, sanitizers or CI.
  Evidence: `docs/compiler/windows_wine_highball_evidence.md`.
- **macOS x86-64 (`macos-15-intel`): experimental / unvalidated.** No job;
  success of `macos-arm64-cplus` does not imply Intel macOS support.
- **Ubuntu 22.04 and other glibc/musl distributions: unvalidated.** No job.
- **Linux arm64 (`linux-arm64-cplus`): experimental** as noted in the matrix.

## Reproducing a row locally

From the repository root (no build under `build/` is required for the probe):

```sh
CC=clang sh tools/verify_toolchain.sh
```

The CI SDK step additionally configures a fresh `build-sdk` tree, installs to
`$RUNNER_TEMP/cplus-sdk`, then builds and runs an unchanged-C program
(`c+ build --legacy-c`, cross-checked with host clang) and a C+ program using
`.len` and `cplus_new_array` from that prefix. Static and shared library link
smokes link through the installed `c+` driver. Each of these is a separate,
non-suppressed CI step, so a failure is attributed to a named job and step.

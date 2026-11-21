# C+ roadmap — two library workstreams, one safer C compiler

> **Goal:** improve existing C rather than replace it: Cake-style static ownership + C++-style deterministic RAII + Apple-style bounds safety + Clang-optimized native execution, preserving C syntax and the native C ABI.

**Two library workstreams only:**
1. **Existing libc/CRT, made safer by the C+ compiler.** Keep `malloc`, `free`, `strcpy`, `memcpy`, `fopen`, etc. Study/reuse Apple bounds-safety and libc, Microsoft Secure CRT, LLVM/Clang fortification and sanitizer infrastructure. **No separate C+ Safe CRT package, no invented mandatory `malloc_s/free_s` family, no glibc fork.**
2. **One optional Modern C+ Library** for String, Vector, File, Result, collections, GUI, logging, JSON, graphics, input, networking, HTTP, async, audio, images and video. Use small RAII/checked-lifetime `.hp` wrappers over native APIs; do not reinvent existing parsers or graphics engines.

Compiler ownership, bounds, RAII and diagnostics support both workstreams; they are a compiler responsibility, **not a third library**. See [README](../README.md), [STATUS](STATUS.md), [LIMITATIONS](LIMITATIONS.md). Claims below are **goals until verified**.

## Architecture

```text
Existing C source (.c/.h)          Modern C+ source (.cp/.hp)
   malloc, free, strcpy             String, Vector, GUI, JSON,
   memcpy, fopen, etc.              Logger, HTTP, Audio, etc.
             \                         /
              \                       /
                    C+ compiler
        Clang AST/CFG + ownership/borrow analysis
        RAII + bounds + provenance + safe FFI
                           |
                      Clang / LLVM
                           |
                 native code + existing
               libc/CRT and native libraries
```

### Agent 1 progress already implemented in PR #16

- [~] Use-after-free: ASan remains broken on this host; an opt-in bounded
      quarantine (`CPLUS_QUARANTINE=1`) delays managed frees so stale aliases
      after address reuse are detected. Not enabled by default; raw C pointer
      dereferences remain unchecked.
- [~] Borrow checking covers locals, escapes, moves, stack-to-thread args and
      null out-parameters; fields/containers/general function-argument
      lifetimes remain open.
- [~] `goto`/`longjmp` produce documented warnings in the safe subset;
      rejecting them outright would break real C programs and is not enabled.
- [~] Adversarial fuzzing: deterministic checker fuzz (120 inputs, 0 crashes)
      and allocator churn test added; libFuzzer target still planned.
- [ ] Concurrency safety: data-race detection or a checked lock discipline;
      currently mutex RAII is only a primitive, marked experimental.
- [ ] Verify Linux x86-64 and ARM64 targets in CI; add Windows x86-64 later.

## Development issues

| Priority | Issue | Owner / PR |
| --- | --- | --- |
| P0 | [#1 — Compiler core safety: ownership, borrowing, RAII, bounds, concurrency](https://github.com/haseeb-heaven/c-plus/issues/1) | Agent 1 · [PR #16](https://github.com/haseeb-heaven/c-plus/pull/16) |
| P0 | [#19 — **Workstream 1: safer existing libc/CRT usage**](https://github.com/haseeb-heaven/c-plus/issues/19) | Agent 1 · same branch, compiler instrumentation |
| P0 | [#3 — **Workstream 2: modern C+ library + native GUI/log/JSON/etc.**](https://github.com/haseeb-heaven/c-plus/issues/3) | Agent 2 · [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17) |
| P0 | [#4 — Multi-TU class/RAII linkage](https://github.com/haseeb-heaven/c-plus/issues/4) | Agent 1 with Agent 2 integration tests |
| P1 | [#8 — CI, fuzzing, diagnostics, benchmarks and portability](https://github.com/haseeb-heaven/c-plus/issues/8) | Both agents |
| P2 | [#18 — Optional legacy C → modern C+ modernizer](https://github.com/haseeb-heaven/c-plus/issues/18) | Future |
| Consolidated | [#20 — former third-party wrapper ticket](https://github.com/haseeb-heaven/c-plus/issues/20) | **Closed into #3**, not a separate library |

## Phase 0 — experimental foundation (already present in some form)

- Native C compilation through Clang, familiar syntax and libc interoperability.
- Limited `class`, `resource struct`, `managed`, `auto`, RAII lowering and compile-time/running bounds diagnostics.
- Core and external C library `.hp` wrappers being developed in PR #17.
- Partial ownership/borrow checking, managed allocation bookkeeping, tests and benchmarks.
- **Not yet proven:** comprehensive raw-pointer lifetime safety, full data-race prevention, every RAII control-flow path, or complete multi-TU library link safety.

## Phase 1 — Agent 1: improve existing libc usage and compiler enforcement (P0)

### Existing libc + native hardening — [#19](https://github.com/haseeb-heaven/c-plus/issues/19)

- [ ] Track ordinary `malloc`, `calloc`, `realloc`, `free`, `memcpy`, `memmove`, `strcpy`, `strcat`, `strlen`, `fopen`, `fclose`, `fread` and `fwrite` using Clang AST/CFG and object-size/ownership information.
- [ ] Statically reject provable buffer overflow, freed alias dereference, duplicate free, invalid realloc and ownership violations in the defined safe subset; enforce dynamic checks when supported, otherwise report unknown/unsafe boundary accurately.
- [ ] Evaluate [Apple Libc](https://github.com/apple-oss-distributions/Libc), [Apple Bounds Safety Clang](https://github.com/swiftlang/llvm-project) ([docs](https://clang.llvm.org/docs/BoundsSafety/BoundsSafety.html)), [Microsoft Secure CRT](https://learn.microsoft.com/en-us/cpp/c-runtime-library/security-features-in-the-crt), [LLVM/Clang](https://github.com/llvm/llvm-project), [glibc](https://sourceware.org/git/?p=glibc.git), [musl](https://git.musl-libc.org/cgit/musl); reuse supported compiler features and system CRT functions rather than copying entire implementations.
- [ ] Preserve C source and ABI, valid `malloc` null returns, `free(NULL)`, `realloc` failure and native I/O errors. **No mandatory new `_s` family**; existing platform `strcpy_s` etc are optional with their native signature, not a forked C+ runtime.
- [ ] Prove with **unchanged C source** comparisons: ordinary Clang vs C+ safe mode, positive cases and compile-fail/runtime-fail negative cases, plus fair performance tests.

### Compiler language guarantees — [#1](https://github.com/haseeb-heaven/c-plus/issues/1), [#4](https://github.com/haseeb-heaven/c-plus/issues/4)

- [ ] Ownership and borrowing on locals, fields, function arguments, returns, aliases, moves and escapes; explicit safe vs unsafe FFI.
- [ ] Exactly-once RAII under all supported exits and partial initialization; reject or clearly handle `goto`, `longjmp`, other unsafe exits.
- [ ] Spatial/temporal pointer checking, nullability, definite initialization, provenance and concurrency guarantees for a precisely specified safe subset.
- [ ] Correct `.hp` multi-TU linkage, constructors/destructors and generated metadata.
- [ ] Avoid handwritten memcpy/strcpy/malloc replacements in the minimal compiler runtime.

## Phase 2 — Agent 2: one Modern C+ Library (P0/P1) — [#3](https://github.com/haseeb-heaven/c-plus/issues/3)

- [ ] **Core:** String/StringView, Vector/Slice, File, Result/Error, paths, time, collections, threads, networking. Checked sizes and ownership/borrowing, deterministic RAII, recoverable errors. Native libc/POSIX/OS implementations underneath.
- [ ] **GUI, graphics, window and input:** [SDL3](https://github.com/libsdl-org/SDL) and [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear); own window/context/renderer handles safely.
- [ ] **Logging:** practical C+ Logger with severity, formatting, sinks, error reporting, cleanup, optional structured fields and thread safety; reuse libc/platform or vetted C implementation.
- [ ] **JSON:** [yyjson](https://github.com/ibireme/yyjson), with owned doc and borrowed value lifetime.
- [ ] **HTTP / async:** [libcurl](https://github.com/curl/curl), [libuv](https://github.com/libuv/libuv), with checked response arithmetic, borrowed header lifetime and async close callbacks.
- [ ] **Audio / image / video:** [miniaudio](https://github.com/mackron/miniaudio), [stb](https://github.com/nothings/stb), [FFmpeg](https://github.com/FFmpeg/FFmpeg).
- [ ] Prioritize *existing PR #17 correctness*: String self-append after realloc; vector overflow; file buffer sizes; partial socket I/O; async handle storage lifetime; libcurl multiplication/response growth; move/zero/double-drop; GUI callback and teardown order.
- [ ] Pin dependency versions/licenses, verify multi-TU, adversarial tests and native equivalent benchmarks. **Do not implement a Safe CRT layer or edit compiler/runtime behind Agent 1.**

## Phase 3 — testing, integration, native speed (P1) — [#8](https://github.com/haseeb-heaven/c-plus/issues/8)

- [ ] Regression/negative/fuzz coverage for compiler, libc-boundary analysis and C+ libraries.
- [ ] CI and targeted macOS/Linux/Windows compatibility, SonarCloud reliability issues and dependency provenance.
- [ ] Fair `-O2` comparison on same workload: Clang native C vs C+ compatibility/safe modes; direct third-party C API vs C+ RAII wrapper. Record performance, peak memory, compile time, binary size, sanitizer parity.
- [ ] Solve multi-TU conflicts and run combined compiler + library suites.

## Phase 4 — optional source modernizer (P2) — [#18](https://github.com/haseeb-heaven/c-plus/issues/18)

- [ ] AST-based migration of legacy C to `class`, owned types, RAII and modern library APIs only when semantic equivalence is supported by tests.
- [ ] Do not automatically replace existing `malloc` with invented `malloc_s`, or `strcpy` with `strcpy_s`, without sound size and error-behavior evidence.

## Development and merges

- **Agent 1:** `agent/core-safety` → [PR #16](https://github.com/haseeb-heaven/c-plus/pull/16) → `develop`. Owns `compiler/**`, minimal `runtime/**`, `tests/core/**` and `tests/safety/**`; issues **#1, #19, #4**.
- **Agent 2:** `agent/modern-stdlib` → [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17) → `develop`. Owns `stdlib/**`, `third_party/**`, `tests/stdlib/**`; issue **#3**, including former #20.
- Both own CI evidence scoped to their modules under **#8**. No uncoordinated overlapping compiler edits or automatic merges.
- **Order:** finish/review PR #16 → merge to `develop`; Agent 2 syncs `develop` and retests → finish/review PR #17 → merge to `develop`; fast-forward `main` only from a validated `develop` snapshot.

## Explicit non-goals

- A new glibc, duplicate `malloc_s/free_s` library, wholesale Microsoft/Apple CRT copy or mandatory migration away from existing libc.
- New JSON parser, GUI framework, logger engine or multimedia codec implementation when established C code can be wrapped.
- Claiming zero overhead or complete memory/thread safety without reproducible evidence.
- Replacing ordinary C with Rust, Zig, Odin, or an incompatible C++ class/runtime model.

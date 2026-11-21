# C+ roadmap — three safer library pillars + compiler safety

> **Goal:** Cake-style static ownership + C++-style deterministic RAII + Apple-style bounds safety + Clang native optimization, while preserving existing C source and ABI interoperability.

C+ should provide **all three** developer-facing library layers, not just new syntax or `managed` allocation. It is **not** a glibc fork, full C++ implementation, or replacement for native libc, Clang or third-party implementations. Current support is experimental; this document describes **planned acceptance criteria**, not completed safety guarantees. See [STATUS.md](STATUS.md), [LIMITATIONS.md](LIMITATIONS.md), [README](../README.md).

## Deliverables

| Pillar | Developer-facing result | Implementation strategy | Tracking |
| --- | --- | --- | --- |
| **1. C+ Safe CRT** | `malloc_s`/`calloc_s`/`realloc_s`/`free_s`; checked `strcpy_s`, `memcpy_s`, `memmove_s`, safe input, formatting, file I/O and conversions | Reuse platform libc/CRT/Clang builtins; explicitly specify API/ABI, buffer sizes, ownership and errors; inspect ordinary libc calls in compiler | [#19](https://github.com/haseeb-heaven/c-plus/issues/19) + [#1](https://github.com/haseeb-heaven/c-plus/issues/1) |
| **2. C+ modern core standard library** | Owned String/Vector/File and borrowed views, Result/error, slices/collections, threads, paths/time/network basics | `class` / `.hp` facade, deterministic RAII; reuse native C libraries | [#3](https://github.com/haseeb-heaven/c-plus/issues/3), [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17) |
| **3. Third-party safe libraries** | Stable RAII adapters for yyjson, SDL3, Nuklear, libcurl, libuv, miniaudio, stb and FFmpeg | Link native libraries / vendor licensed narrow subsets; track object ownership, borrowed resources, async callbacks and destruction order | [#20](https://github.com/haseeb-heaven/c-plus/issues/20), [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17) |

### Architecture / upstream references

- **Compiler safety and analysis:** [LLVM/Clang](https://github.com/llvm/llvm-project), [Cake](https://github.com/thradams/cake), [Checked C](https://github.com/checkedc/checkedc-clang), [Apple Clang bounds safety](https://github.com/swiftlang/llvm-project).
- **Resource lifetime:** [Cforall](https://github.com/cforall/cforall), [Fil-C](https://github.com/pizlonator/fil-c) for understanding tradeoffs; do not copy Fil-C's GC or claim equivalent guarantees without it.
- **Existing CRT implementations:** [Microsoft Secure CRT documentation](https://learn.microsoft.com/en-us/cpp/c-runtime-library/security-features-in-the-crt), [Apple Libc](https://github.com/apple-oss-distributions/Libc), [glibc](https://sourceware.org/git/?p=glibc.git), [musl](https://git.musl-libc.org/cgit/musl).
- **Third-party libraries:** [yyjson](https://github.com/ibireme/yyjson), [SDL3](https://github.com/libsdl-org/SDL), [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear), [libcurl](https://github.com/curl/curl), [libuv](https://github.com/libuv/libuv), [miniaudio](https://github.com/mackron/miniaudio), [stb](https://github.com/nothings/stb), [FFmpeg](https://github.com/FFmpeg/FFmpeg).

## Phase 0 — existing experimental foundation

- C source compilation through Clang, native C ABI and `--legacy-c`.
- Existing `class`/methods, `resource struct`/destructors, managed allocations, `auto`, `print`/`scan`, selected array checks.
- Limited ownership/borrow analysis, runtime allocation metadata and native library integration.
- Modern String/Vector/File/JSON/GUI/etc. wrappers exist or are being developed in unmerged [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17); not all lifecycle and runtime checks are complete.

**Do not conflate implemented syntax with complete safety guarantees.** See [STATUS.md](STATUS.md) and [LIMITATIONS.md](LIMITATIONS.md).

## Phase 1 — P0: compiler safety, Safe CRT, core libraries and multi-TU

### Compiler foundations — Agent 1 ([#1](https://github.com/haseeb-heaven/c-plus/issues/1), [#4](https://github.com/haseeb-heaven/c-plus/issues/4), [PR #16](https://github.com/haseeb-heaven/c-plus/pull/16))
- [ ] AST/CFG-based ownership and borrow checking across locals, calls, return values and escaped aliases.
- [ ] Exactly-once RAII on supported exit paths; diagnose unsupported `goto`, `longjmp` and nonlocal exits.
- [ ] Spatial bounds, pointer provenance, definite initialization, null and temporal/UAF behavior for a **defined safe subset**; explicit unsafe FFI boundaries.
- [ ] Analyze ordinary `malloc`, `calloc`, `realloc`, `free`, `memcpy`, `strcpy` patterns without requiring library replacement; warn/reject where soundly provable.
- [ ] Correct `.hp` class methods and generated destructor linkage in multiple translation units.
- [ ] Document the guarantees actually enforced versus excluded cases.

### Pillar 1 — Safe CRT ([#19](https://github.com/haseeb-heaven/c-plus/issues/19))
- [ ] Design explicit `_s` contracts: `malloc_s`, `free_s`, checked allocation, strings, memory operations, formatting/scan and files.
- [ ] Reuse system libc/CRT calls and Clang builtins; `malloc_s`/`free_s` are **C+ inventions**, not standard Microsoft Secure CRT.
- [ ] Prefer platform-native Microsoft `strcpy_s`/`memcpy_s` semantics when names collide. Where signatures/semantics conflict, use a distinct name.
- [ ] Make buffer/source-size limitations explicit; test overflow, aliasing, truncation, memory lifetime, release failure, EOF and error handling.
- [ ] Retain ordinary C ABI and optional use; no libc fork or mandatory auto-migration.

### Pillar 2 — modern core stdlib ([#3](https://github.com/haseeb-heaven/c-plus/issues/3), [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17))
- [ ] Harden String self-append, borrowed views, vector growth overflow, File and socket validity/zero states.
- [ ] Safe typed input with recoverable errors/EOF; sound native handle ownership and RAII.
- [ ] Clean headers across translation units; document compatibility and surface API.

## Phase 2 — P1: third-party wrappers + verification

### Pillar 3 — third-party adapters ([#20](https://github.com/haseeb-heaven/c-plus/issues/20))
- [ ] Audit and fix libuv async close/loop lifetime, libcurl response arithmetic/header borrowing, JSON borrowed value lifetime, SDL3/Nuklear/FFmpeg/miniaudio handle teardown.
- [ ] Pin source revisions and licenses; link/vendoring policy; compile existing C libraries once, no reinvented algorithms.
- [ ] Run test suites and sanitizers where available; callback/reentrancy, errors, failure-path and multi-TU stress tests.

### Toolchain / speed ([#8](https://github.com/haseeb-heaven/c-plus/issues/8))
- [ ] Cross-platform macOS/Linux CI and Windows target validation.
- [ ] Adversarial compiler/CRT/wrapper fuzzing, sanitizer-equivalent baselines, linker and native compatibility tests.
- [ ] Reproducible `-O2` comparisons: Clang/libc vs checked CRT vs C+ classes vs direct third-party C API. Track compile-time, peak memory, runtime and executable size.
- [ ] Resolve required GitHub checks and static analysis findings; source-range diagnostics and incremental builds as warranted.

## Phase 3 — P2: optional legacy C modernizer ([#18](https://github.com/haseeb-heaven/c-plus/issues/18))

- [ ] Clang AST-driven `c+ migrate legacy.c -o modern.cp` with conservative/idiomatic/safe modes.
- [ ] Optional conversion to classes, RAII, safe CRT calls and core/third-party wrappers **only when semantics and resource lifetime can be established**.
- [ ] No blindly rewriting `malloc` to `malloc_s` or `strcpy` to `strcpy_s` without adequate buffer/owner information.
- [ ] Require compile/regression/differential tests and a human-readable migration report.

## Responsibility and merges

| Workstream | Assigned working role | Change locations |
| --- | --- | --- |
| Safety and Clang integration | Agent 1 ([PR #16](https://github.com/haseeb-heaven/c-plus/pull/16)) | `compiler/**`, minimal compiler-required `runtime/**`, `tests/core/**`, `tests/safety/**` |
| Core + third-party library fixes | Agent 2 ([PR #17](https://github.com/haseeb-heaven/c-plus/pull/17)) | `stdlib/**`, `tests/stdlib/**`, relevant `third_party/**` |
| Safe CRT surface | Later coordinated sub-workstream in [#19](https://github.com/haseeb-heaven/c-plus/issues/19) | Separate optional checked-C API layer; Agent 1 supplies compiler contracts, Agent 2 implements library layer after current stabilization |
| Validation | Both, scoped to owned modules | CI/benchmarks/fuzzing in [#8](https://github.com/haseeb-heaven/c-plus/issues/8) |

**Merge sequence:** Agent 1 finishes/reviews PR #16 → integrate into `develop`; Agent 2 updates/retests PR #17 on new `develop` → integrate. Then create focused Safe CRT milestones if needed. `main` receives reviewed `develop` snapshots. No agent may merge automatically.

## Non-goals and safety limitations

- No full rewrite or replacement of glibc, Apple Libc, Microsoft CRT, LLVM, or upstream JSON/GUI/audio/video libraries.
- No mandatory GC, new C++ class object model, or incompatible ABI as an accidental side effect.
- No claim that `malloc_s`, `free_s` or a `_s` suffix alone prevents arbitrary UAF or buffer errors.
- No false claim of full memory safety, zero runtime overhead, portability or source compatibility without reproducible evidence.

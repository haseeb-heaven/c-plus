# C+ roadmap — Rust-grade safe subset, existing libc, modern C+ libraries

> **Goal:** improve existing C rather than replace it: Cake-style static ownership + C++-style deterministic RAII + Apple-style bounds safety + Clang-optimized native execution, preserving C syntax and the native C ABI.

**Two library workstreams only:**
1. **Existing libc/CRT, made safer by the C+ compiler.** Keep `malloc`, `free`, `strcpy`, `memcpy`, `fopen`, etc. Study/reuse Apple bounds-safety and libc, Microsoft Secure CRT, LLVM/Clang fortification and sanitizer infrastructure. **No separate C+ Safe CRT package, no invented mandatory `malloc_s/free_s` family, no glibc fork.**
2. **One optional Modern C+ Library** for String, Vector, File, Result, collections, GUI, logging, JSON, graphics, input, networking, HTTP, async, audio, images and video. Use small RAII/checked-lifetime `.hp` wrappers over native APIs; do not reinvent existing parsers or graphics engines.

Compiler ownership, bounds, RAII and diagnostics support both workstreams; they are a compiler responsibility, **not a third library**. See [README](../README.md), [STATUS](STATUS.md), [LIMITATIONS](LIMITATIONS.md). Claims below are **goals until verified**.

## NON-NEGOTIABLE P0: Rust-grade compile-time memory safety for the verified C+ safe subset (2026-11-21)

**Goal (not achieved today):** Safe-mode C+ must provide Rust-comparable *temporal safety, ownership/borrowing and race-freedom* statically for every accepted program **within the explicitly defined safe subset**. Dynamic bounds/index checks are permitted where runtime sizes cannot be known (Rust does this too). No declaration of success until the contract is validated by exhaustive negative fixtures, fuzzing, differential/sanitizer evidence and independent review. Legacy unrestricted C remains a separate **unsafe/compatibility** mode. Preserving syntax and C ABI does NOT mean declaring arbitrary raw-pointer operations safe.

**Fail closed, rather than only warn.** A program that cannot satisfy the ownership/lifetime/provenance/exclusivity rules must: (1) fail safe-mode compilation, or (2) require an explicit, reviewed unsafe/FFI boundary with checked contracts; never silently downgrade to "UBSan will hopefully catch it". Unsafe blocks/FFI are trusted obligations, just as unsafe Rust is, and must not be misrepresented as statically proven safe. If raw memory access cannot be statically justified, require checked pointer/view types or a proven runtime bounds contract. No false claims that simply retaining the traditional C ABI guarantees safety.

**Compiler requirements (Agent 1):** move beyond token/name heuristic sets. Build sound flow-sensitive and interprocedural ownership/lifetime/alias analysis over Clang AST/CFG, ideally a suitable lowered IR; unique owners, move-only ownership, mutable XOR shared borrow, non-escaping borrows, partial moves, structs/fields/arrays/containers, branches/loops/recursion, callbacks, thread transfer and synchronized sharing, dynamic allocation invalidation and correct realloc failure semantics. Model destructors across all safe exits; forbid or isolate unsafe goto/longjmp paths. Explicit provenance/nullability/bounds contracts, including libc calls, callbacks and FFI. Avoid saying the analysis is sound before proving coverage of the declared subset.

**Agent 2:** develop adversarial acceptance corpus and portable existing libc contracts/adapters: `malloc/calloc/realloc/free`, `memcpy/memmove`, `strcpy/strcat/strlen`, `printf/scanf`, `FILE*`, platform-native fortification; verify the compiler rejects UAF through aliases/functions/containers, outliving borrows, double-free, invalid invalidation, data races and insufficient buffer extents. Track false negatives, false positives, real ABI and sanitizer parity. Maintain modern library work but freeze feature expansion; unsafe FFI adapters must be individually documented and bounded.

**Milestone acceptance:** A published safe-mode specification; an explicit table of which C operations are permitted and which are rejected/unsafe; positive and negative integration tests *that actually execute in CI*; multi-TU/interprocedural tests; negative tests for aliases/loops/fields/FFI and thread races; deterministic cleanup; measurable overhead vs equivalent C/Rust builds; no remaining known counterexample inside the declared safe subset. A testcase that should be rejected but passes is a blocking defect, not "partial support." Keep PRs unmerged until their relevant gate is met. Do not claim unrestricted C is 100% memory-safe or Rust-equivalent.

**Design references:** Rust safe/unsafe boundaries https://doc.rust-lang.org/book/ch20-01-unsafe-rust.html ; Xr0 contracts https://xr0.dev/learn ; Fil-C runtime tradeoffs https://fil-c.org/how ; Cforall RAII https://cforall.uwaterloo.ca/ ; C3 contracts/runtime checks https://c3-lang.org/language-common/contracts/ . Xr0's subset is incomplete; Fil-C uses dynamic checking + GC/different ABI; Cforall and C3 are not demonstrated Rust-complete compile-time borrow-checking solutions. Adopt techniques, not unsupported equivalence claims.

## CURRENT P0 — shared safer existing libc/CRT milestone (both agents)

**This milestone takes precedence over modern-library feature expansion.** Both agents work on issue [#19](https://github.com/haseeb-heaven/c-plus/issues/19), independently on separate branches and PRs:

| Agent | Immediate deliverable | Ownership boundary |
| --- | --- | --- |
| **1 — PR #16** | Clang AST/CFG analysis and diagnostics for existing `malloc/calloc/realloc/free`, `memcpy/memmove`, `strcpy/strcat` and I/O; fix `realloc` success/failure semantics; ownership/bounds/lifetime checks; resolve conflicts and SonarCloud | `compiler/**`, minimal `runtime/**`, core/safety tests |
| **2 — PR #17** | **Same libc safety milestone**, from the library/testing side: portable `tests/crt/**` matrix, native Apple/Microsoft/glibc/musl API availability probes, checked/fortified call examples, error/ABI conformance, failure-path and performance tests. Maintain existing `stdlib/**` but **freeze new modern features**. No new libc fork or mandatory `malloc_s/free_s`. | `tests/crt/**`, `stdlib/**` test/adapters/docs; no independent compiler/runtime edits |

**Milestone exit criteria:** For each libc API family, document (1) unchanged C source test, (2) invalid case, (3) compile-time rejection vs runtime check vs unprotected/unknown, (4) semantics/ABI compatibility, (5) matching Clang and C+ sanitizer/optimization flags, (6) real test results. Resolve PR #16 conflicts; coordinate Agent 2 against its updated compiler; pass required CI and review prior to merging.

**After the shared milestone:** review/merge Agent 1 first; Agent 2 updates its branch and passes combined tests; then finish/merge Agent 2's modern wrappers. Advanced GUI/JSON/logging expansion is **P1, not the current P0**.

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

## Development issues

| Priority | Issue | Owner / PR |
| --- | --- | --- |
| P0 | [#1 — Compiler core safety: ownership, borrowing, RAII, bounds, concurrency](https://github.com/haseeb-heaven/c-plus/issues/1) | Agent 1 · [PR #16](https://github.com/haseeb-heaven/c-plus/pull/16) |
| P0 | [#19 — **Workstream 1: safer existing libc/CRT usage**](https://github.com/haseeb-heaven/c-plus/issues/19) | **BOTH agents** · Agent 1 compiler; Agent 2 libc verification and native adapters |
| P1 | [#3 — **Workstream 2: modern C+ library + native GUI/log/JSON/etc.**](https://github.com/haseeb-heaven/c-plus/issues/3) | Agent 2 · [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17) |
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

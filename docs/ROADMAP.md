# C+ roadmap — familiar C, compile-time-first safety, bundled libc+

> **Product goal, not yet achieved:** Make ordinary C substantially safer without making developers write Rust-style ownership syntax. Ship **one toolchain**: the C+ compiler **plus an installable hardened libc+** with shared machine-readable safety contracts. Claim Rust-comparable guarantees **only for an explicitly verified strict-safe subset**. Neither arbitrary unchanged C nor zero runtime overhead can be guaranteed in general.

## Developer experience — non-negotiable

- **C stays C:** unchanged ordinary `.c` and `.h` syntax; familiar pointers, structs, aliases, `malloc/free` and calls. Do not require `defer`, `move`, ownership attributes, explicit lifetime annotations, `unsafe` keywords or `*_s` APIs in normal source.
- **Automatic inference first:** accept valid, demonstrably safe C aliasing and resource patterns; do not copy Rust's borrow-checker *developer experience* or reject a safe pattern simply because Rust's particular borrow rules would reject it.
- **Two documented safety modes:** **default compatibility/diagnostic mode** accepts standard C syntax, diagnoses proven defects and warns about unverified operations; **strict safe mode** rejects unverified operations unless a sound check or explicitly defined trusted/unsafe boundary applies. **Default mode is not guaranteed memory-safe.**
- **Compile-time first, performance measured:** avoid mandatory GC/runtime *ownership* instrumentation for proven code. Dynamic bounds checks may still be necessary. Preserve native C ABI and existing defined C behavior.
- **No magical cleanup:** compiler-generated RAII is opt-in or applies only under clearly defined resource contracts; never silently change legacy C cleanup semantics.
- **No misleading claims:** Rust-class memory safety for the defined safe subset is a research/engineering target; publish limitations and evidence before using that description as an achieved result.

## Ship two tightly integrated components

| Area | Owner | Deliverable | Existing work |
| --- | --- | --- | --- |
| **C+ compiler** | Agent 1 | Clang AST/CFG/dataflow checks for ownership, lifetime, aliasing, UAF, double-free, bounds and resource effects; low-friction diagnostics; safe-mode/compatibility-mode distinction; consumption of libc+ contracts; RAII integration | [#21](https://github.com/haseeb-heaven/c-plus/issues/21), [PR #16](https://github.com/haseeb-heaven/c-plus/pull/16), `agent/core-safety` |
| **Hardened libc+** | Agent 2 | **Actual compiled and packaged** C standard-library implementations, standard headers, ABI/platform adapters, optional checked `*_s` APIs, shared contracts, logging, unit/fuzz tests, examples, installable SDK | [#22](https://github.com/haseeb-heaven/c-plus/issues/22), [PR #17](https://github.com/haseeb-heaven/c-plus/pull/17), `agent/modern-stdlib` |
| **Combined verification** | Both | Same contract schema; cross-TU real-application tests, correct C ABI/allocator/linking, negative and positive safety tests, CI/fuzzing/sanitizers and honest performance metrics | [#23](https://github.com/haseeb-heaven/c-plus/issues/23) |

## Delivery sequence

1. **P0 — narrow end-to-end proof:** Agent 1 automatically proves ownership/lifetimes for simple unchanged C and rejects demonstrable alias UAF/double-free; Agent 2 ships a **real selectable libc+ allocation, memory and string subset**, plus source-compatible headers/contracts. Clean-prefix install and link tests on each claimed platform.
2. **P0 — validation:** test safe and unsafe aliases, returns/branches/loops, cross-call and cross-TU lifetimes, `realloc` success/failure, buffer sizes and libc errors. Keep minimized bugs as regressions; run CI, relevant sanitizers, fuzzing and independent review. Record false positives and false negatives. No claim of complete soundness based on a finite test suite.
3. **P1 — higher coverage:** richer function summaries, struct/field/container ownership, concurrency, opt-in deterministic RAII and audited FFI contracts; grow libc+ to stdio then remaining ISO C families with an API-by-API coverage matrix. Optional checked variants do not replace standard names.
4. **P2 — real-world adoption:** compile unmodified real C projects, measure acceptance rate, diagnostics, false rejections, compatibility, compile time, executable size and runtime cost versus optimized Clang and comparable Rust programs. Expand macOS/Linux/Windows only after ABI and allocator tests pass.
5. **Release:** the installable product includes compiler, headers, supported libc+ symbols, contract metadata, build integration and tests. Review PR #16 then PR #17 and their integration into `develop`; promote to `main` only after reviewed gates. **No automatic merging.**

## Reuse-first engineering

**Do not reinvent mature implementations.** Use [LLVM/Clang](https://github.com/llvm/llvm-project) AST/CFG/dataflow infrastructure; study [Cake](https://github.com/thradams/cake), [CORAL](https://github.com/tiagodusilva/coral), [Checked C](https://github.com/checkedc/checkedc-clang), [Apple Bounds Safety](https://clang.llvm.org/docs/BoundsSafety/BoundsSafety.html) and [Xr0](https://xr0.dev/learn) for analysis. Use licensed implementation files from [LLVM libc](https://github.com/llvm/llvm-project/tree/main/libc), [musl](https://git.musl-libc.org/cgit/musl) and [Intel safestringlib](https://github.com/intel/safestringlib) as appropriate; verify per-file license, upstream SHA, dependencies and attribution first. **Cake has GPLv3 plus additional restrictions; do not copy its implementation without verified permission.**

### Agent 1 progress already implemented in PR #16

- [~] Use-after-free: ASan remains broken on this host; an opt-in bounded
      quarantine (`CPLUS_QUARANTINE=1`) delays managed frees so stale aliases
      after address reuse are detected. Not enabled by default; raw C pointer
      dereferences remain unchecked.
- [~] Borrow checking covers locals, escapes, moves, stack-to-thread args,
      field conflicts and null out-parameters; containers/general function
      argument lifetimes remain open.
- [~] `goto`/`longjmp` produce documented warnings in the safe subset;
      rejecting them outright would break real C programs and is not enabled.
- [~] Adversarial fuzzing: deterministic checker fuzz (120 inputs, 0 crashes)
      and allocator churn test added; libFuzzer target still planned.
- [ ] Concurrency safety: data-race detection or a checked lock discipline;
      currently mutex RAII is only a primitive, marked experimental.
- [ ] Verify Linux x86-64 and ARM64 targets in CI; add Windows x86-64 later.

**Agent 2 — bundled libc/verification:** Build and ship actual compiled implementations, checked APIs, contract definitions, opt-in logging, installable headers and SDK components for the **same** libc APIs (`malloc/calloc/realloc/free`, `memcpy/memmove`, `strcpy/strcat/strlen`, formatting, `FILE*` I/O). Use existing Apple/Microsoft/glibc/musl libc and supported fortification; preserve C ABI, error semantics and existing function names. Write `tests/crt/**` positive and adversarial negative fixtures covering aliases, UAF, double-free, unknown sizes, realloc errors, thread ownership and FFI lifetime. Do not add compiler changes or copy whole libc. Freeze new modern-library features.

**Platform scope:** libc+ is the portable, selectively linked hardened library layer; glibc is GNU/Linux-specific. Use explicit glibc integration on Linux and platform-native Darwin libc/UCRT integration on macOS/Windows. Do not overwrite system CRTs or mix allocators. An existing `strcpy(dst, src)` call lacks capacity in its signature; provide a compiler proof, a sound check or strict-mode rejection rather than pretend the libc alone makes it safe.

## Definition of done per feature

Actual implementation + compatible API + compiler contract + good/bad/edge-case tests + fuzz/sanitizer results where relevant + ABI/portability evidence + measurable performance + provenance documentation. Report **implemented / partial / unsupported**, never an invented safety percentage. Iterate build → test → fuzz → fix → retest until the claimed acceptance scope passes or a genuine blocker is documented.

## Status and prior work

**Experimental.** The complete strict-safe guarantee and full ISO C libc+ are **not yet demonstrated**. Existing modern C+ classes, String/Vector, GUI, JSON, HTTP and media work is preserved but expansion is deferred until P0. Superseded issues #1, #3, #4, #8, #18 and #19 were closed as reorganized work, **not completed**. See [STATUS](STATUS.md), [LIMITATIONS](LIMITATIONS.md) and the [README](../README.md).

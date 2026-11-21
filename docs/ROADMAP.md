# C+ roadmap — Rust-grade safe subset first

> **P0 goal, NOT YET ACHIEVED:** C+ safe mode must offer Rust-comparable static ownership, borrowing/lifetime and safe concurrency for its **explicitly defined supported safe subset**. Unknown unsafe operations **fail closed** or require marked, audited unsafe/FFI boundaries. Dynamic bounds checks may be used when they guarantee safety, as in Rust. Legacy unrestricted C remains a separate compatibility/unsafe mode. Keep familiar C syntax and the native C ABI without claiming arbitrary legacy C is memory-safe.

## Active issues — only three

| Priority | Work | Owner / branch | Pull request |
| --- | --- | --- | --- |
| **P0** | [#21 — Sound fail-closed ownership, borrowing, lifetimes and race prevention](https://github.com/haseeb-heaven/c-plus/issues/21) | **Agent 1**, `agent/core-safety` | [#16](https://github.com/haseeb-heaven/c-plus/pull/16) |
| **P0** | [#22 — Safe existing libc/CRT contracts, portable checks and adversarial fixtures](https://github.com/haseeb-heaven/c-plus/issues/22) | **Agent 2**, `agent/modern-stdlib` | [#17](https://github.com/haseeb-heaven/c-plus/pull/17) |
| **P0** | [#23 — CI, negative-test evidence, cross-TU integration and release gates](https://github.com/haseeb-heaven/c-plus/issues/23) | **Both agents** | Both PRs |

**Previous issues #1, #3, #4, #8, #18 and #19 are closed as superseded, not completed.** Existing work remains on its branches. Do not close or auto-merge PR #16 or #17.

## One safety mission, different engineering ownership

**Agent 1 — compiler:** Clang AST/CFG/dataflow/IR; owner and borrow semantics, alias/provenance/lifetime tracking; moving/borrowing across fields, loops, branches, functions, containers and threads; reliable static error diagnostics, safe RAII and sound unsafe/FFI boundaries. No shallow heuristics advertised as safety. Fix realloc failure/success rules. Resolve PR #16 conflicts and SonarCloud first.

**Agent 2 — libc/verification:** Build testable contracts and portable native adapters for the **same** libc APIs (`malloc/calloc/realloc/free`, `memcpy/memmove`, `strcpy/strcat/strlen`, formatting, `FILE*` I/O). Use existing Apple/Microsoft/glibc/musl libc and supported fortification; preserve C ABI, error semantics and existing function names. Write `tests/crt/**` positive and adversarial negative fixtures covering aliases, UAF, double-free, unknown sizes, realloc errors, thread ownership and FFI lifetime. Do not add compiler changes or copy whole libc. Freeze new modern-library features.

**Modern C+ libraries (P1, deferred):** Existing String, Vector, collections, File, GUI, logging, JSON, HTTP, async, media wrappers remain under PR #17 for preservation/bug fixes; **no feature expansion** until the P0 safety gate is met.

## Milestone gates

1. **Safe subset spec:** operations accepted, compile-time rejected, guaranteed runtime-bounds checked, and explicitly unsafe; rules must be sound.
2. **Compiler safety:** catch UAF across aliases/functions, mutable/shared borrow conflicts, invalid frees and realloc invalidation, escaping pointers, field/container escapes and thread-race hazards within declared subset; no silent downgrade to a warning or UBSan.
3. **CRT conformance:** each native function gets positive + negative tests, platform capabilities, ABI/error checks and correct safe/unsafe classification.
4. **Independent verification:** every negative test must run and produce a verified rejection or intentional unsafe designation. Fuzz and sanitizers provide corroboration, not proof by themselves. Document false positives/negatives and exclusions.
5. **Integration/quality:** resolve PR #16 conflicts, pass required CI/SonarCloud, demonstrate multi-TU correctness and publish fair Clang C / C+ / Rust benchmarks with matched relevant flags.
6. **Merge order:** review Agent 1 PR #16 into develop; sync Agent 2 PR #17 and run combined tests; review Agent 2; validate develop before syncing main. No merge until gates pass.

## References

- [LLVM/Clang](https://github.com/llvm/llvm-project), [Apple Clang Bounds Safety](https://clang.llvm.org/docs/BoundsSafety/BoundsSafety.html)
- [Rust unsafe boundary](https://doc.rust-lang.org/book/ch20-01-unsafe-rust.html), [Xr0](https://github.com/xr0-org/xr0), [Fil-C](https://fil-c.org/how), [Cforall](https://github.com/cforall/cforall), [C3](https://c3-lang.org/)
- [Apple libc](https://github.com/apple-oss-distributions/Libc), [Microsoft Secure CRT](https://learn.microsoft.com/en-us/cpp/c-runtime-library/security-features-in-the-crt), [glibc](https://sourceware.org/git/?p=glibc.git), [musl](https://git.musl-libc.org/cgit/musl)

These are research and native-platform references, **not** claims of license clearance for source copying or demonstrated parity with Rust.

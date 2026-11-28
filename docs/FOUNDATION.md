# Compiler foundation selection (evidence)

The specification requires inspecting candidate foundations, checking licenses
and choosing one coherent base instead of combining repositories blindly.
This page records the decision actually made for this repository.

## Candidates examined

| project | role considered | license / notes | decision |
| --- | --- | --- | --- |
| [Fil-C](https://fil-c.org) (pizlonator/llvm-project) | memory-safe C/C++ via InvisiCaps + concurrent GC | Apache 2 compiler, BSD 2 runtime, musl/glibc stdlibs | Not adopted as the base. It is a full Clang fork plus GC runtime; no build of it exists in this environment, and adopting it would replace, not extend, the existing toolchain. Its guarantees are broader than this MVP and come with GC/FFI restrictions. Details in [RELATED.md](RELATED.md). |
| [Clang/LLVM](https://github.com/llvm/llvm-project) | C parser, type checker, optimiser, codegen, linker driver | Apache 2 with LLVM exceptions | **Chosen as the backend.** The host `clang` driver parses and compiles the lowered C; LLVM `Support` libraries are linked into the `c+` driver itself. |
| [Goose](https://github.com/aardappel/goose) | allocation architecture, data stacks, no-GC design | Apache 2 | Not adopted. It is a new whole-program language with its own compiler and type system, not C-compatible. One idea is shared: deterministic scope-exit frees. Details in [RELATED.md](RELATED.md). |
| [Rust](https://github.com/rust-lang/rust) | ownership/borrowing model | MIT/Apache 2 | Not adopted as a base. Its lifetime/type system is deliberately out of scope ("C+, not Rust"). A small targeted borrow check is implemented for C+ locals only. |
| [C3](https://github.com/c3lang/c3c) | C-like syntax, fast compiler | BSD 3-clause | Not adopted; C+ must compile ordinary `.c`, not a new source language. |
| [Checked C](https://github.com/checkedc/checkedc) | bounds-aware pointers | MIT | Not adopted; its checked-pointer annotations change C source types, while C+ keeps ordinary C arrays and adds a token-level bounds pass. |
| [Zig](https://github.com/ziglang/zig) | C ABI/build ideas | MIT | Not adopted; C+ uses the existing C toolchain and linker directly. |
| [D](https://github.com/dlang/dmd) | scope cleanup ideas | Boost 1.0 | Not adopted; same reason as above. |

## Decision

**C+ is a source-lowering frontend over the existing open-source C toolchain.**

```text
.c (C + small C+ extensions)
  -> c+ lexer/rewriter (Sema/)
  -> deterministic cleanup insertion (Sema/RaiiLowering.cpp)
  -> plain C + #line directives
  -> host clang: parse / type-check / optimise / codegen / link
  -> native executable linked with a small C runtime archive
```

This is the smallest maintainable path that satisfies the non-negotiable rule:
C+ must feel like C and must not reimplement C. The host C compiler remains
the authority for C syntax, types, diagnostics and optimisation.

## What is explicitly NOT claimed

- No Clang AST/IR plugin is used: the current implementation lowers at the
  source level. This is an engineering simplification, not a claim that source
  lowering is equivalent to AST-based analysis.
- No GC is used. Managed memory uses an allocator side table; resource
  lifetime is deterministic RAII.
- No merge of any of the listed repositories was performed.
- No mathematical proof of safety is claimed. Guarantees are bounded by the
  documented threat model and capability table.

## Concrete blocker for the preferred strategy

The specification preferred evaluating Fil-C as the foundation. The concrete
blocker is not licensing but **integration and availability**: adopting Fil-C
means replacing the host compiler and its runtime with a large, separate
toolchain that is not present or buildable in this environment. That would
violate "reuse what exists here" and "smallest extension of C". If a Fil-C
toolchain becomes available, it can be evaluated as an optional backend for
the same lowered C source; no source-language change would be required.

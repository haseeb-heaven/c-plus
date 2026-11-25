# Compiler architecture (10-stage audit)

C+ is a source-to-source compiler: it lowers a small set of C+ extensions to
annotated plain C, then delegates parsing, type checking, optimisation, code
generation and linking to host Clang.

## Pipeline

1. **Source parsing / lowering**: `compiler/Main.cpp:120` -> `Driver.cpp:786`
   `runDriver()`; per-TU `Driver.cpp:397` `lowerUnit()`. Lexer
   `CPlusLexer.cpp:28`; rewriter `CPlusRewriter.cpp:1603`
   `rewriteCPlusSource()` -> `RewrittenSource {text, resourceTypes,
   classTypes, arrayLengths}`.
2. **Semantic analysis**: Clang AST built in-memory by
   `AstSafetyCheck.cpp:3858` `buildASTFromCodeWithArgs()`; backend Clang
   compile at `Driver.cpp:985`.
3. **Type checking**: Clang Sema (`-Werror=*` promotions in
   `Driver.cpp:747-762`); C+-specific `TypeChecker.cpp` is not on the live
   driver path.
4. **Static analysis**: token pass `StaticBoundsCheck.cpp:2909`
   `checkStaticBounds()` (original source + arrayLengths) plus AST pass
   `AstSafetyCheck.cpp:3801` `checkAstSafety()` (lowered C, two collect rounds
   then check).
5. **Bounds analysis**: static extent tracking in both passes; dynamic
   `__cplus_bounds_check()` injection in `CPlusRewriter.cpp:1628`
   `instrumentDynamicArrayBounds()` after all static analysis.
6. **Pointer analysis**: token checks in `StaticBoundsCheck.cpp` plus AST
   alias/release/one-past/NUL tracking in `AstSafetyCheck.cpp`.
7. **Arithmetic**: `AstSafetyCheck.cpp` `knownConst()/arithmeticOverflow()`
   and token-level allocation checks in `StaticBoundsCheck.cpp`; optional
   sanitizer instrumentation in `Driver.cpp:589-619`.
8. **Lifetime/ownership**: `RaiiLowering.cpp:1583` `lowerRaii()` plus CFG
   definite-init `OwnershipAnalysis.cpp:351`; runtime table in
   `runtime/cplus_runtime.c`.
9. **C generation**: textual result of rewriting + RAII + optional bounds
   instrumentation; `--emit-c` at `Driver.cpp:913-920`.
10. **Runtime checking**: `runtime/cplus_runtime.c` (`__cplus_bounds_check`,
    allocator bookkeeping, scope guards) + sanitizers (runtime recompiled
    instrumented when selected).

Placeholder stubs (no logic): `Analysis/BoundsAnalysis.cpp`,
`Sema/RaiiAnalyzer.cpp`, `Ast/ClassLowering.cpp`, `Ast/ResourceLowering.cpp`,
`runtime/CPlusRuntime.cpp`.

Full detail was gathered by the architecture audit; see
`docs/compiler/agent1/compiler_architecture.md` equivalent content preserved
here. Key invariants: `check` is pure static analysis (no probe, no
instrumentation, no link); sanitizer flags reach both compile and link via
`backendArgs()`; bounds instrumentation runs after static checks.

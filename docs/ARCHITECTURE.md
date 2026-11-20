# Architecture

## Guiding idea

C+ does not implement a C compiler. It starts from an existing open-source C
toolchain, adds a small source-to-source lowering step for the new constructs,
and uses the host Clang driver for everything that is already C:

| concern | owner |
| --- | --- |
| C parsing, type checking, diagnostics | host Clang |
| optimisation, code generation, linking | host Clang / LLVM |
| C ABI, libc and POSIX interoperability | host C runtime |
| `resource` / `class` / `managed` / `owned` syntax | C+ rewriter |
| deterministic cleanup insertion | C+ RAII lowering |
| managed memory bookkeeping, safety exits | small C runtime library |

The result is a native executable with no custom runtime dependency beyond a
static C archive, and ordinary C files compile unchanged.

## Pipeline

```text
file.c
  |
  |  src/Main.cpp        CLI parsing (build/run/check/test/bench/demos)
  v
DriverOptions
  |
  |  src/Driver/Driver.cpp
  v
  1. read source
  2. rewriteCPlusSource()          src/Sema/CPlusRewriter.cpp
     - CPlusLexer.cpp tokenises without losing offsets
     - resource struct -> struct + typedef
     - class -> struct + T_method(T *self, ...) functions
     - p.m() / p->m() -> T_m(&p, ...) / T_m(p, ...)
     - managed/owned -> plain declaration + machine-readable marker comment
     - #line directives map results back to the original file
  3. checkStaticBounds()           src/Sema/StaticBoundsCheck.cpp
     - uses array extents collected by the rewriter
     - rejects provable constant and counted-loop out-of-bounds accesses
  4. lowerRaii()                   src/Sema/RaiiLowering.cpp
     - tracks lexical scopes and owning locals
     - inserts drops at scope exit (reverse declaration order)
     - wraps return/break/continue so cleanup cannot break if/else binding
     - rejects copies, assignments, managed-pointer escapes, borrow escapes
       and use-after-move (a small borrow checker)
  5. write a temporary .c beside the source, add -include cplus/cplus.h,
     invoke the C compiler with the selected flags and the C+ runtime archive
  6. for `run`, execute the produced binary; for `check`, stop at -fsyntax-only
```

The lowered C is auditable: `c+ build --emit-c out.c --keep-temp` prints the
exact translation unit. The markers are comments such as
`/*cplus$own:f:File_drop*/` and `/*cplus$mem:b*/`; they are consumed by stage 2
and removed from the final output.

## Diagnostic flow

`DiagnosticEngine` collects structured diagnostics from both lowering stages.
`printDiagnostics` renders them as `file:line:column: error: ...` with optional
`help:` notes. Compiler errors from the C backend already point at the
original source because the generated C carries `#line` directives.

## Runtime ABI

`include/cplus/cplus.h` is the stable boundary between generated code and
`src/Runtime/cplus_runtime.c`:

- `cplus_alloc`, `cplus_zalloc`, `cplus_realloc`, `cplus_release`
- `cplus_new(T)`, `cplus_new_array(T, N)`, `cplus_move(x)`
- `cplus_check_index`, `cplus_check_bounds`, fatal reporting with fixed exit
  codes (`CPLUS_EXIT_DOUBLE_FREE`, `CPLUS_EXIT_BOUNDS`, ...)
- mutex primitives used by the concurrency primitives in demos/tests

The managed allocator uses an open-addressed hash table of live allocations
and a bounded direct-mapped cache of recently freed addresses. Lookups are
O(1); metadata is never placed in front of the user pointer, so detection does
not read freed memory.

The cleanup that stage 2 emits is ordinary C source calls; the runtime scope
guard API exists for future use but is not required by the current lowering.
Everything is C-only and links with `libc` (plus `pthread` in safe mode).

## Safety flags

Safe mode passes to the host C compiler:

```text
-Werror=array-bounds                  # static: known-bad constant subscripts
-fsanitize=undefined,bounds,null,alignment
-fno-sanitize-recover=all
-DCPLUS_SAFE=1  -DCPLUS=1  -pthread
-O0 -g        (or -O2 with --release)
```

`-Werror=array-bounds` turns a statically provable bad subscript into a
compile error. `-fno-sanitize-recover=all` is what makes a run-time detected
failure abort instead of continuing. ASan is deliberately not enabled on this host: its runtime aborts
during initialisation with a `sanitizer_malloc_mac.inc` check failure, so
enabling it would produce false claims. See the limitations document.

## Build notes

The driver's own support code uses LLVM `Support` helpers (`FileSystem`,
`Path`, `Program`, `MemoryBuffer`, `InitLLVM`, `raw_ostream`). Some unused
skeleton sources also include Clang AST headers, which is why the build finds
LLVM/Clang CMake packages. On macOS, Homebrew's shared `libLLVM.dylib` can be
an empty stub, so `CMakeLists.txt` forces static component archives and
patches the `LLVM::LLVM` target when the dylib is unusable.

The backend C compiler is resolved at run time: `CPLUS_CLANG` wins, then a
non-Homebrew `clang` on `PATH`, then known system paths, then the value baked
in at configure time. Headers and the runtime archive can similarly be
overridden with `CPLUS_INCLUDE` and `CPLUS_RUNTIME_LIB`.

## Verification

- `tests/run_tests.sh` — 18 tests: positive programs, compile-fail cases,
  safe runtime aborts, and legacy C mode.
- `demos/run_demos.sh` — nine programs covering C interop, bounds safety,
  resource cleanup, classes, sockets, threads, borrowing, managed allocation
  length and RAII across loops/early returns.
- `benchmarks/run_bench.sh` — builds each benchmark three ways and reports
  medians, separating lowering cost from sanitizer cost.

`c+ test`, `c+ demos` and `c+ bench` are wrappers around those scripts.

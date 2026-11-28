# Repository layout

The repository separates three concerns: the compiler, the C runtime, and the
C+ standard library written in `.hp`.

```text
c+_lang/
├── compiler/            # the c+ compiler frontend (C++20 + LLVM Support)
│   ├── Main.cpp         # CLI entry point
│   ├── Driver/          # lowering pipeline and backend invocation
│   ├── Sema/            # lexer, syntax lowering, bounds/borrow checks, RAII
│   ├── Diagnostics.*    # diagnostic engine
│   ├── LangFeatures.h   # keyword/feature declarations
│   └── SourceManager.*  # source bookkeeping
├── runtime/             # small C runtime linked into every C+ program
│   ├── cplus_runtime.c  # allocator, managed memory, guards, mutex, TLS, I/O
│   └── runtime/include/cplus/cplus.h  # stable C ABI used by generated C
├── stdlib/              # C+ standard library, written in .hp
│   ├── string.hp
│   ├── vector.hp
│   ├── file.hp
│   ├── thread.hp
│   ├── io.hp
│   └── net.hp
├── tests/               # positive, compile-fail, runtime-fail, known gaps
├── apps/legacy_demos/               # A..P demonstration programs (.c and .cp)
├── benchmarks/          # reproducible comparisons against clang -O2
├── docs/                # language, architecture, audit, status, roadmap
└── tools/               # build/verification scripts
```

## Rules

- Compiler sources live under `compiler/`; no standard-library headers there.
- Runtime C code and the public ABI header live under `runtime/`.
- All `.hp` standard-library headers live under `stdlib/` and are included as
  `#include "stdlib/string.hp"` etc.
- `.c`/`.h` legacy C remains fully supported; `.cp`/`.hp` are the modern
  recommended extensions.
- Generated C is an intermediate artifact, never checked in.

The build files and driver resolve these directories relative to the source
root, so the layout does not require installed copies during development.

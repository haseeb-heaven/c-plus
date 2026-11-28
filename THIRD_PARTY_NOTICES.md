# Third-party notices

## Vendored source in this repository

- `third_party/clog/` — MIT licence, see `third_party/clog/LICENSE`. Header notice
  retained in `runtime/include/cplus/clog.h`.

## Upstream implementations linked and invoked (no source text copied)

C+ links the host Clang/LLVM libraries and calls their implementations directly.
No upstream compiler source file has been copied into `compiler/` or `runtime/`.

| Component | Upstream | Pin / version | Licence | C+ use |
| --- | --- | --- | --- | --- |
| `clang::runUninitializedVariablesAnalysis`, `UninitVariablesHandler`, `AnalysisDeclContext` | `llvm/llvm-project`, `clang/include/clang/Analysis/Analyses/UninitializedValues.h`, `clang/lib/Analysis/UninitializedValues.cpp`, `clang/include/clang/Analysis/AnalysisDeclContext.h` | installed Clang/LLVM 23.1.6 (`llvmorg-23.1.6` = `5b10d2d74929eae78912771222c9855af97496f4`) | Apache-2.0 WITH LLVM-exception | `compiler/Analysis/OwnershipAnalysis.cpp`: definite-initialization dataflow |
| `tooling::buildASTFromCodeWithArgs`, `CFG::buildCFG`, Clang AST visitors | `llvm/llvm-project` | same | Apache-2.0 WITH LLVM-exception | `compiler/Sema/AstSafetyCheck.cpp`, `compiler/Analysis/OwnershipAnalysis.cpp` |
| Host Clang backend, sanitizer flags, C runtime linking | `llvm/llvm-project` | Apple clang 16.0.0 / Homebrew clang 23.1.6 | Apache-2.0 WITH LLVM-exception | `compiler/Driver/Driver.cpp` |

The LLVM exception text and Apache-2.0 licence are available at
`https://llvm.org/LICENSE.txt`. Preserve the SPDX notice in any future source
port:

```
Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
```

## Design/predicate references (no code integrated)

Checked C (`checkedc/checkedc-clang` @ `a2843eae17d96cbfad2cf386dd60de0b849cfb6d`,
Apache-2.0 WITH LLVM-exception; tests `checkedc/checkedc` @
`46fb63401882a1fc46557a192874fcdaecf7931d`, MIT), Apple Bounds Safety
(`swiftlang/llvm-project` @ `ca74e48841e588074fba77543e4e0fc643edc52c`,
Apache-2.0 WITH LLVM-exception), SoftBoundCETS (`santoshn/softboundcets-34` @
`9a9c09f04e16f2d1ef3a906fd138a7b89df44996`, BSD-3-Clause plus per-file
MIT-style headers) and Fil-C (`pizlonator/fil-c` @
`d3f9f822c276b446c8687ce70605b71aefc19beb`, LLVM Apache + libpas BSD) were
inspected at the pinned commits. Their AST types, wide-pointer representations,
shadow stacks, GC and runtime layouts are incompatible with C+'s ordinary-C ABI;
only designs and predicates were adapted. No source from these projects was
copied. TrapC has no verifiable upstream implementation; GCC, Cake and Cyclone
are GPL/AI-restricted and were research-only.

Test shapes in `tests/compiler/upstream_ports/` are independently authored C
fixtures with recorded upstream origin; no upstream test source was copied.

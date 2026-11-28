# Upstream design analysis and licence audit (Agent 1)

Pinned clones live outside the repository in `../c-plus-upstreams`. No upstream
implementation source is committed to C+.

| project | pinned revision | licence | exact paths studied | integration decision |
| --- | --- | --- | --- | --- |
| Cake (`thradams/cake`) | `2da2afd1759a` | GPLv3 + AI-training restriction | `README.md`, `ownership.md`, `flow.md`, docs only | **design research only**. The addendum forbids using the source as AI input/reference; no source copied. |
| CORAL (`tiagodusilva/coral`) | `b047d6dab621` | no licence file found | `README.md`, `docs/`, `src/` layout | design research only; no source copied. Non-lexical lifetime and borrow concepts reimplemented with Clang AST/CFG. |
| Checked C | `a2843eae17d9` | Apache-2.0 WITH LLVM exceptions | `clang/docs/checkedc/`, AST/Sema docs | bounds/nullability contract design reference; no source copied, no compiler fork. |
| Safe C++ (`safecpp.org`) | website (no revision) | website documentation | public lifetime/borrow documentation | semantic reference; no source copied. |
| Rust (`rustc` installed) | system toolchain | MIT/Apache-2.0 | language semantics and benchmark | semantics reference; no source copied. |
| LLVM/Clang (installed Apple clang 16) | system toolchain | Apache-2.0 WITH LLVM exceptions | `clang/include/clang/AST`, `Analysis/CFG.h`, `Support/JSON.h` | linked directly; APIs used, no `.cpp` copied. |
| Apple Bounds Safety | `ca74e48841e5` | Apache-2.0 WITH LLVM exceptions | public docs + `SemaBoundsSafety.cpp` paths | `-fbounds-safety` unsupported on installed clang 16; not claimed. |
| Microsoft Secure CRT | docs only | proprietary platform API docs | public docs | native API reference; no source copied. |
| Intel safestringlib | `f4491915cc68` | MIT | headers/tests paths | Agent 2 owns library adaptation; compiler only recognises contracts. |

## Cake design notes (not copied)

- ownership states: UNINITIALIZED, OWNED, BORROWED_SHARED, BORROWED_MUTABLE,
  MOVED, FREED, NULL, UNKNOWN;
- flow-sensitive transfer across assignments, branches, loops, returns and
  calls;
- standard-library ownership contracts encoded outside user source;
- clear diagnostics naming allocation, alias creation, invalidation and access.

C+ implements an independent subset of these ideas with Clang AST/CFG:
`compiler/Sema/AstSafetyCheck.cpp` and `compiler/Analysis/OwnershipAnalysis.cpp`.
The abstract state is intentionally conservative; unknown states fail closed in
the specified safe subset.

## CORAL / Rust design notes (not copied)

- non-lexical lifetimes: borrows end at last use, not necessarily at lexical
  scope end;
- unique mutable vs shared immutable borrows;
- moves invalidate the source and all aliases derived from it;
- function-effect summaries propagate ownership/invalidation interprocedurally.

C+ currently implements lexical/statement-level release and borrow state; true
NLL and full MIR-style borrow checking are **PARTIAL/UNSUPPORTED**.

## Checked C design notes (not copied)

- checked pointers carry bounds; nullability is explicit;
- bounds-safe scopes and dynamic checks where static proof is unavailable;
- gradual migration from unchecked pointers at explicit boundaries.

C+ uses the machine-readable JSON contract schema instead of new pointer types;
no mandatory qualifiers or annotations are required in user source.

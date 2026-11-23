# Static-analysis upstream reuse audit (interprocedural, bounds, dataflow)

Status: evidence-gathering document. This is a focused companion to
`docs/compiler/upstream-reuse.md`. It records only static interprocedural,
bounds and dataflow sources and is the place to look before proposing a new
analysis pass. **No upstream implementation source was copied into C+**;
"adapted" means a rule or worklist shape was re-expressed in C+'s own AST
visitors. "Design-only" means C+ has no equivalent implementation today.

## Pins and licences (LICENSE files at each pin)

| Project | Repository / pin | Licence | Disposition |
| --- | --- | --- | --- |
| Checked C compiler | `checkedc/checkedc-clang` @ `a2843eae17d96cbfad2cf386dd60de0b849cfb6d` | Apache-2.0 WITH LLVM-exception (`clang/LICENSE.TXT`) | Algorithm/design reference; no fork |
| Apple Bounds Safety | `swiftlang/llvm-project` @ `ca74e48841e588074fba77543e4e0fc643edc52c` (local pin; earlier audit also `f63645674df4…`) | Apache-2.0 WITH LLVM-exception (`LICENSE.TXT`) | Semantic rules adapted; no code copied |
| Clang Static Analyzer | `llvm/llvm-project` @ `0c41652846ac0771141157712a31b6ff29e78a0c` | Apache-2.0 WITH LLVM-exception | Engine design studied; Clang APIs linked |
| GCC analyzer | `gcc-mirror/gcc` @ `4cb25d3a3d609a2a291716bf99503a64789bde86` | GPL-3.0-or-later (`COPYING3`) | Research only; no source copied |
| Cake | `thradams/cake` @ `2da2afd1759a7f3a62024ac781af65c8832bbe5f` | GPLv3 plus AI-training-use addendum | Design docs only; implementation not opened |
| Cyclone | `pippijn/cyclone` @ `72c6ef2584b335be8e67ce72f392ff98c2175502` | GPL-2.0 / LGPL-2.1 | Research only; no source copied |

Pin caveat: `docs/CORE_SAFETY.md` pins LLVM at `14fee62eb543…` (LLVM
24.0.0git) while the linked toolchain is 23.1.6 (`llvmorg-23.1.6` =
`5b10d2d74929eae78912771222c9855af97496f4`). Do not claim API parity until
that is reconciled. Corrections to the earlier audit: the Apple transform
`TransformDynamicCountWithFunctionArgument` is implemented at the bottom of
`DynamicCountPointerAssignmentAnalysis.cpp` (`:3693`, `:3698`), not in a
separate file. At both Apple pins the header lives in `clang/lib/Sema/` (not
`clang/include/clang/Sema/`); a separate
`DynamicCountPointerAssignmentAnalysisExported.h` does live in
`clang/include/clang/Sema/` with its `.cpp` in `clang/lib/Sema/`. 3C's solver entry is `graphBasedSolve` at
`clang/lib/3C/Constraints.cpp:307` with the worklist `doSolve` at `:160`.
The Apple pin `f63645674df40e2ffa4e30a4b49110ace071c1a3` is a real
`swiftlang/llvm-project` commit; `git ls-tree -r --name-only f6364567…`
confirms its tree carries the full bounds-safety implementation. The design
docs there are `clang/docs/BoundsSafety.rst`, `BoundsSafetyAdoptionGuide.rst`
and `BoundsSafetyImplPlans.rst`, whereas at local pin `ca74e488…` the design
doc is `clang/docs/BoundsSafety/BoundsSafety.md`. All line numbers in this
document were read at `ca74e488…` and should be re-verified if the project
standardises on `f6364567…`. Method note: on blobless partial clones use
`git ls-tree -r --name-only <pin>` for path existence — `git cat-file -e
<pin>:<path>` can report a false MISS when a promisor fetch disconnects;
re-issue a failed read rather than concluding the path is absent.

## Source evidence actually inspected

- **Checked C**: `clang/lib/Sema/SemaBounds.cpp` — `AbstractForFunctionType:129`,
  `ConcretizeFromFunctionTypeWithArgs:264`, `ProveBoundsDeclValidity:1986`,
  `CheckBoundsDeclAtCallArg:2480`, call-site loop `:3645-3694`;
  `clang/lib/AST/CanonBounds.cpp` — `CompareExprSemantically:268`,
  `GetExprIntDiff:323`; `clang/lib/3C/Constraints.cpp` — `doSolve:160`,
  `graphBasedSolve:307`. Tests: `clang/test/CheckedC/inferred-bounds/calls.c`,
  `equiv-expr-sets.c`, `dynamic-checks/array-read-code-gen.c`.
- **Apple Bounds Safety**: `clang/lib/Sema/DynamicCountPointerAssignmentAnalysis.cpp`
  — `computeMemberExprKey:44`, `DepGroup:1096`,
  `TransformDynamicCountWithFunctionArgument:3693-3709`; header in
  `clang/lib/Sema/`, not `include/`. Tests: `clang/test/BoundsSafety-legacy-checks/`.
- **Clang SA**: `CoreEngine.cpp:87,488`; `ProgramState.cpp:308,352`;
  `MallocChecker.cpp:174,257,392` (`RefState`); `DivZeroChecker.cpp:82,105`;
  `ArrayBoundChecker.cpp:421,544`. Tests: `clang/test/Analysis/div-zero.cpp`,
  `malloc-interprocedural.c`, `malloc-failure.c`.
- **GCC analyzer (GPL research only)**: `call-summary.cc` `call_summary_replay`
  parameter/region remapping; `region-model.cc` `get_gassign_result`,
  `div_by_zero_diagnostic`. Tests: `gcc/testsuite/gcc.dg/analyzer/call-summaries-*.c`.
- **Cake (docs only)**: `flow.md` Chapter 4 object-state map, branch fork/join,
  two-pass loops. **Cyclone (docs only)**: `doc/cyclone/{regions,pointers}.tex`
  region capabilities, outlives, pointer qualifiers.

## Reuse matrix

| Feature | Upstream project | Source files | Reuse approach | C+ implementation | Tests |
| --- | --- | --- | --- | --- | --- |
| Abstract/concretize bounds at calls; argument substitution | Checked C | `SemaBounds.cpp:129,264,2480,3645-3694` | Algorithm adapted (type-level representation declined) | Name-keyed `callParamExtents_`, `returnedParam_`, `returnedAllocExtent_`, `callReturnExtent_` in `AstSafetyCheck.cpp` | `tests/compiler/interprocedural_safety/` decay/param-bounds cases; `tests/native/strict40_runner.c` |
| Semantic base equivalence / integer offsets | Checked C | `CanonBounds.cpp:268,323` | Design-only | Not implemented; name/key equality only | gap (no fixture) |
| Constraint-graph fixed point with conflict→unknown | Checked C 3C | `Constraints.cpp:160,307` | Rule adapted (conflict → unknown) | Three collect rounds plus `conflictedParams_` fail-closed in STRICT | `call_conflict` in `run_bounds_matrix.sh` |
| Dynamic-count ↔ pointer pairing / base-qualified keys | Apple | `DynamicCountPointerAssignmentAnalysis.cpp:44,1096,3693` | Keying idea adapted; attribute syntax declined | Flat name-keyed extents; field/FAM pairing design-only | `interproc_heap_extent.c` |
| Path-sensitive branch splitting | Clang SA | `CoreEngine.cpp:87,488`, `ProgramState.cpp:308` | Reference only | Clang CFG used only for definite-init; no `assumeDual` | `tests/safety/branch_uaf.c` |
| Resource-state machine / interproc zero diagnostics | Clang SA + GCC (GPL) | `MallocChecker.cpp:174,257`; `DivZeroChecker.cpp:82` | Rules adapted independently | `releasedVars_`, `derefParams_`, compile-time zero checks; no full state machine | `uaf_evidence_run.py`, `interproc_double_free.c` |
| Bounds only when extent provable | Clang SA / LLVM | `ArrayBoundChecker.cpp:421,544`; `BoundsChecking.cpp` | Rule adapted | `heapAllocs_`, `stackArrayExtents_`, native `__cplus_bounds_check` | `run_bounds_matrix.sh`, `tests/runtime_fail/bounds.c` |
| Callee summary remapping with fresh heap region | GCC (GPL) | `call-summary.cc` | Declined as code; used as specification | Name-keyed effect/extent tables; no region replay | interproc suite |
| Flow-state lattice / contracts between functions | Cake, Cyclone (docs) | `flow.md` Ch.4; `regions.tex`, `pointers.tex` | Independent reimplementation with Clang AST/CFG | Conservative flow-sensitive ownership; no region types | `tests/cases/*.cp`, native harness |

## Honest roll-up

Implemented independently: fixed-point extent collection and call-argument
substitution; ownership/effect tables; extent-carrying array decay and one-past
handling; native compile-time zero/shift/bounds diagnostics; interprocedural
nullability into dereferencing callees with null-guard awareness.

Partial: Clang-CFG definite-init worklist with intersection meet; name-keyed
interprocedural facts; conservative flow-sensitive ownership.

Design-only or unsupported: function-type bounds abstractions, semantic
expression proofs, constraint graphs, field/FAM-aware pairing, symbol/region
keys, `assumeDual`, summary replay, region capabilities, and full cross-TU
analysis. GPL/copyleft and AI-restricted projects contributed no code.

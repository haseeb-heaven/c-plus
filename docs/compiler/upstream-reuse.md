# Upstream reuse audit — Agent 1 compiler safety

Status: evidence-gathering document. This is the **second** upstream pass for
this repository and deliberately extends, rather than replaces, the existing
audits:

- `docs/UPSTREAM_AUDIT.md` (branch baseline; six pinned projects)
- `docs/UPSTREAM_ANALYSIS.md` (licence audit and design notes)

The second pass was subsequently extended with four further research reports —
(a) division/remainder/shift, (b) implicit conversions, (c) pointer arithmetic
and (d) interprocedural reasoning. Their findings are folded into the
provenance table (§1), the feature matrix (§2) and the addendum (§7). Every
upstream file and line cited in those reports is evidence and *predicates
only*: no UBSan handler, Clang SA checker, GPL analyzer source or runtime code
was copied into C+.

**Pin discrepancy to resolve before any API migration.** `docs/CORE_SAFETY.md:155`
and `docs/UPSTREAM_AUDIT.md:14` pin `llvm/llvm-project` at `14fee62eb543…`,
which is LLVM 24.0.0git, while the linked toolchain is Homebrew LLVM/Clang
23.1.6. The correct provenance pin for APIs actually in use is tag
`llvmorg-23.1.6` = `5b10d2d74929eae78912771222c9855af97496f4`. The APIs cited
below are present in both revisions, but the pin must be corrected before any
code change claims API parity.

Both earlier audits concluded that **no upstream implementation source has
been copied into C+**. That conclusion still holds after this deeper pass.
What this document adds is first-hand inspection of the *actual upstream
implementation files and tests* (not only READMEs), pinned commit SHAs, and a
feature-by-feature decision on whether a concrete implementation can be
reused, adapted, or must be declined.

## 0. Method and scope

For each upstream project this audit: (1) pinned a commit via the GitHub API,
(2) read its licence file, (3) located the actual implementation source and
tests via the repository tree and file contents, (4) compared the algorithm
with the live C+ implementation in `compiler/` and `runtime/`, and (5) recorded
an integration decision. No source from an incompatible licence was copied,
quoted into C+, or used as code-generation input.

## 1. Provenance and licences reviewed

| Project | Pinned revision | Licence (source inspected) | Disposition |
| --- | --- | --- | --- |
| Checked C (`checkedc/checkedc-clang`) | `a2843eae17d96cbfad2cf386dd60de0b849cfb6d` | Apache-2.0 WITH LLVM-exception (per-file SPDX in `clang/lib/3C/ArrayBoundsInferenceConsumer.cpp`) | Design reference; no fork, no code copied |
| Checked C samples/tests (`checkedc/checkedc`) | `46fb63401882a1fc46557a192874fcdaecf7931d` | MIT (`LICENSE.TXT`, "Checked C Test and Sample Code", Microsoft) | Test ideas only |
| Apple Bounds Safety (`swiftlang/llvm-project`) | `f63645674df40e2ffa4e30a4b49110ace071c1a3` (full implementation branch; repo local pin `ca74e48841e588074fba77543e4e0fc643edc52c`) | Apache-2.0 WITH LLVM-exception (`LICENSE.TXT`) | Semantic rules adapted; no code copied |
| LLVM/Clang (`llvm/llvm-project`) | `ff97e97e01a80965504abaab3bf76b6e6ffdacd7` | Apache-2.0 WITH LLVM-exception (`LICENSE.TXT`) | APIs linked directly; no `.cpp` copied |
| LLVM/Clang arithmetic + conversion study (`llvm/llvm-project`, incl. `compiler-rt`) | `c3b40cbb3379e837f4dde28bd40f00ed1966989a` | Apache-2.0 WITH LLVM-exception (`clang` and `compiler-rt` `LICENSE.TXT`) | Predicates/diagnostics studied; UBSan handlers and implicit-conversion predicates not ported |
| SoftBoundCETS (`santoshn/softboundcets-34`) | `9a9c09f04e16f2d1ef3a906fd138a7b89df44996` | BSD-3-Clause root `LICENSE` + per-file MIT-style UPenn/Rutgers headers | Propagation discipline adapted; runtime not ported |
| Fil-C (`pizlonator/fil-c`) | `d3f9f822c276b446c8687ce70605b71aefc19beb` | LLVM Apache-2.0 WITH LLVM-exception (`LLVM-LICENSE.txt`) + PAS BSD-2/3 (`libpas/LICENSE.txt`) | Declined: ABI/runtime model incompatible |
| Cake (`thradams/cake`) | `2da2afd1759a7f3a62024ac781af65c8832bbe5f` | GPLv3 **plus AI-training-use addendum** | Rejected; design docs only, no source code read |
| Cyclone (`pippijn/cyclone`) | `72c6ef2584b335be8e67ce72f392ff98c2175502` | GPL-2.0 (`COPYING`), LGPL-2.1 (`COPYING.LIB`) | Research only; no GPL source copied |
| CHERI LLVM (`CTSRD-CHERI/llvm-project`) | `7e122876ee01a7a82585f7674c94f12fc1cc7689` | Apache-2.0 WITH LLVM-exception (`LICENSE.TXT`); MIT CHERI headers | Reference architecture; no port |
| GCC (`gcc-mirror/gcc`) | `4cb25d3a3d609a2a291716bf99503a64789bde86` | GPL-3.0-or-later per analyzer sources (`COPYING3`) | Research only; no source copied |
| GCC analyzer arithmetic/call summaries (`gcc-mirror/gcc`) | `4cb25d3a3d609a2a291716bf99503a64789bde86` (same pin as above) | GPL-3.0-or-later (`COPYING3`) | Research only: `region-model.cc` `get_gassign_result` and `call-summary.cc` `call_summary_replay` read for shape, no source copied |
| Clang Static Analyzer (`llvm/llvm-project`) | `0c41652846ac0771141157712a31b6ff29e78a0c` | Apache-2.0 WITH LLVM-exception | APIs + algorithms studied; no source copied |
| C2Rust (`immunant/c2rust`) | `049202967ef127c8abda676ad6863db55e8e86d3` | BSD-3-Clause + dual Apache/MIT vendored crate | Design ideas only |
| Microsoft GSL | `a4651c0f995d3309432f78e977058d9d4760f9fa` | MIT (+ BSD-3 test-only notices) | Contract vocabulary adapted to plain C |
| CORAL (`tiagodusilva/coral`) | `b047d6dab62147acd4293b2f4c7f8a3cbeb1a2d1` | **No licence file** — all rights reserved | Rejected; design ideas only |
| Xr0 (`xr0-org/xr0`) | `cd306c23def69cf3cf1d6a7ba8d49c13dbea6709` | Apache-2.0 | Design ideas only |
| CCured (mirror `CTSRD-CHERI/ccured`) | `8e6824ae70cfe109390866748ae58d4fb1c87511` | BSD-3-Clause | Design ideas only (original `ucb-bar/ccured` is 404) |
| Cforall (`cforall/cforall`) | `a726980cfaa56c233ce37f8b5529856a0e3d9c4e` | BSD-3-Clause | Design ideas only |

The Cake licence is the hard constraint: its addendum forbids using the
software "as input or reference" for AI work, so only its public design
documentation was read and no implementation file was opened. The earlier
audits' "REJECT" decision is authoritative over older notes that called Cake
a permissive design reference.

The four additional reports map onto the rows above as follows: div/mod/shift →
Checked C, LLVM/Clang arithmetic + conversion study, Clang SA, GCC analyzer;
conversions → LLVM/Clang arithmetic + conversion study, Microsoft GSL,
Checked C; pointer arithmetic → Apple Bounds Safety, Checked C, LLVM/Clang,
Clang SA, SoftBoundCETS, CHERI; interprocedural → Checked C, Apple Bounds
Safety, GCC analyzer, Clang SA. The only new pin introduced is `c3b40cbb…`
(LLVM/Clang plus compiler-rt); every other project was already pinned above.
Licences are Apache-2.0 WITH LLVM-exception for the Clang/LLVM/compiler-rt
files, MIT for Microsoft GSL, BSD-3/MIT for SoftBoundCETS, and
GPL-3.0-or-later for the GCC analyzer files. GPL material is research only:
no GPL file was copied, quoted into C+, or used as code-generation input.

## 2. Feature reuse matrix

| Feature | Upstream project | Source files | Reuse approach | C+ implementation | Tests |
| --- | --- | --- | --- | --- | --- |
| Array-to-pointer decay carries bounds | Apple `-fbounds-safety`; Checked C | `clang/docs/BoundsSafety/BoundsSafety.md` §1.2.4; `clang/test/CheckedC/inferred-bounds/calls.c` | **Algorithm adapted**: decayed array expression inherits array extent; function prototypes decay to parameter-count bounds | `AstSafetyCheck.cpp` `stackArrayExtents_`, `callParamExtents_`, return summaries; `tests/34_array_decay_bounds.cp` | `tests/native/cplus_safety_tests.c` decay cases; `strict40_runner.c`; `compile_fail/*.err` |
| Bounds inference / propagation at calls | Checked C 3C | `clang/lib/3C/ArrayBoundsInferenceConsumer.cpp` (905 lines), `clang/lib/3C/ConstraintResolver.cpp` (885 lines), `clang/test/CheckedC/inferred-bounds/{calls,basic}.c` | **Algorithm adapted**: fixed-point collection then checking; call-site argument extents substituted into callee | Three collect rounds at `AstSafetyCheck.cpp:3865-3869`, seeding at `:3873-3889` | `decay_nested_bad`, `decay_returned_ptr_bad`, `decay_direct_bad` |
| Pointer metadata travels with the value | SoftBoundCETS | `softboundcets-lib/softboundcets.h`, `softboundcets-llvm-clang34/lib/Transforms/SoftBoundCETS/SoftBoundCETS.cpp:1469-1539` (`propagateMetadata`), `:3744-3805` (`handleAlloca`), `:4021-4024` (`handleGEP`) | **Design adapted, not ported**: extent should be keyed by expression as well as by name | Currently name-keyed (`heapAllocs_`, `stackArrayExtents_`); expression-keyed propagation is an identified improvement | Candidate cases listed in §6; not yet implemented |
| Call/return metadata across function boundaries | SoftBoundCETS | `softboundcets.h` shadow stack, `SoftBoundCETS.cpp:1595,1675,1724,4079,4155,4441` | **Simplified**: C+ keeps compile-time summary tables; no shadow stack, no ABI change | `callParamExtents_`, `returnedParam_`, `returnedFixedExtent_`, `callReturnExtent_` | `strict40_runner.c`, field/return decay cases |
| Runtime bounds validation | LLVM `BoundsChecking.cpp`; Checked C dynamic checks | `llvm/lib/Transforms/Instrumentation/BoundsChecking.cpp`; `clang/test/CheckedC/dynamic-checks/array-read-code-gen.c` | **Separate implementation**: C+ injects a source-level `__cplus_bounds_check` call; LLVM uses IR checks and `ObjectSizeOffsetEvaluator` | `CPlusRewriter.cpp:1628-1710`, `runtime/cplus_runtime.c:21` | `tests/runtime_fail/bounds.c`; `tests/compiler/agent1/run_all.sh` runtime-fail category |
| Static bounds / typed bounds declarations | Checked C, Apple | `clang/lib/3C/ArrayBoundsInferenceConsumer.cpp`, `clang/test/CheckedC/ast-dump-bounds.c` | **Declined as syntax**: would require new pointer types/annotations; C+ must accept ordinary C | Token checker `StaticBoundsCheck.cpp` + AST checker with JSON contracts instead | `tests/compile_fail/*.err` (58 fixtures) |
| Ownership / move / borrow / defer | Cake (docs), CORAL, Cyclone | Cake `flow.md`, `rules.md`, `manual.md`, `diagnostics.md` (docs only); Cyclone docs | **Independent reimplementation** with Clang AST/CFG due licence and syntax constraints | `RaiiLowering.cpp`, `OwnershipAnalysis.cpp`, `AstSafetyCheck.cpp` | native 44-case harness; `tests/cases/*.cp`; `tests/core/positive/*` |
| Definite initialization dataflow | Clang CFG APIs; Cake flow docs | `clang/include/clang/Analysis/CFG.h`; `clang/lib/Analysis/CFG.cpp` | **Reused Clang API** (linked library, no code copied) | `OwnershipAnalysis.cpp:351-431` worklist with intersection meet | `tests/compile_fail/uninitialized.err`, branch/loop cases |
| Temporal safety (UAF after free) | SoftBoundCETS, Fil-C | `softboundcets.h` key/lock trie; Fil-C LLVM pass | **Declined**: shadow trie/fat pointers break native ABI and add GC/runtime cost | C+ uses compile-time release state + managed allocator side table | `tests/runtime_fail/*`, `tests/native/uaf*` |
| Capability pointers / provenance | CHERI LLVM | CHERI LLVM pass and tests | **Reference only**: no capability hardware target | Not implemented; documented gap | none |
| Path-sensitive static analysis | Clang Static Analyzer, GCC analyzer | `clang/lib/StaticAnalyzer/Core`, GCC analyzer source | **Reference only**; C+ currently uses Clang CFG + targeted visitors | `OwnershipAnalysis.cpp` definite-init; AST visitors | documented gap for general races |
| Division/remainder zero and signed div overflow | Checked C; LLVM/Clang + compiler-rt; Clang SA; GCC analyzer (GPL) | `clang/lib/Sema/SemaExpr.cpp` `DiagnoseBadDivideOrRemainderValues:10824`; `clang/lib/CodeGen/CGExprScalar.cpp` `EmitUndefinedBehaviorIntegerDivAndRemCheck:4289`; `compiler-rt/lib/ubsan/ubsan_handlers.cpp` `handleDivremOverflowImpl:325`; `clang/lib/StaticAnalyzer/Checkers/DivZeroChecker.cpp`; `gcc/analyzer/region-model.cc` `get_gassign_result:953` (GPL, research only) | **Predicates only; UBSan runtime declined**: reuse the zero-divisor and `INT_MIN / -1` conditions as compile-time facts, not the handler/trap path | **Implemented** in `compiler/Sema/AstSafetyCheck.cpp` `VisitBinaryOperator`: `division by zero`, `remainder by zero`, `cannot be excluded` variants, `signed integer overflow in division/remainder`; runtime values wrapped by native `__cplus_checked_div_i64` / `__cplus_checked_rem_i64` guards emitted into generated C by `CPlusRewriter.cpp`, defined in `runtime/cplus_runtime.c` — not UBSan | `tests/native/cplus_safety_tests.c` `arith_div_zero`, `arith_mod_zero`, `arith_div_unknown`; `tests/core/arithmetic_safety_suite.py` `const_div_zero`, `const_mod_zero`, `runtime_div_zero`; `tests/core/rust_compare_suite.py` |
| Shift-count range | Checked C; LLVM/Clang | `clang/lib/Sema/SemaExpr.cpp` `DiagnoseBadShiftValues:11407`; `clang/lib/CodeGen/CGExprScalar.cpp` `EmitShl:5108` | **Predicates adapted**: count in `[0, promoted-left-width)` as an AST check; no UBSan shift handler | **Implemented** in `VisitBinaryOperator`: `negative shift count`, `shift count ... is not less than the width ...`, `shift count cannot be proven within the left operand's width`; native guards `__cplus_checked_shl_i64` / `__cplus_checked_shr_i64` in generated C | `tests/native/cplus_safety_tests.c` `arith_shift_negative`, `arith_shift_wide`; `tests/compile_fail/classic_bug19_shift.c` |
| Implicit conversions: sign/round-trip predicates | LLVM/Clang; Microsoft GSL (MIT); Checked C | `clang/lib/Sema/SemaChecking.cpp` `CheckImplicitConversion ~13657`, `IntRange ~11961`; `clang/lib/CodeGen/CGExprScalar.cpp` implicit-conversion predicates `~1142`; `include/gsl/narrow` (MIT); `clang/lib/Sema/SemaCast.cpp` `CheckBoundsCast ~3081` | **Round-trip + sign predicates only**: GSL `narrow`/`narrow_cast` vocabulary is not C+ syntax; UBSan conversion predicates not ported | **Partial, pointer provenance only**: `VisitCastExpr` records `(uintptr_t)p` and tagged `u & ~1` as verified round-trips and rejects integer-forged pointers (`pointer created from integer value has unverifiable provenance`). **Design-only**: no general implicit-narrowing / sign-change / integer round-trip checker; `implicit-integer-truncation` in `Driver.cpp` is opt-in backend sanitizer, not a native guard | `tests/native/cplus_safety_tests.c` `int_to_ptr`; no narrowing fixture today (documented gap) |
| Pointer arithmetic and array bounds | Apple Bounds Safety; Checked C; LLVM; Clang SA; SoftBoundCETS; CHERI (reference) | Apple `clang/lib/Sema/SemaExpr.cpp` pointer restrictions; `clang/lib/Sema/BoundsUtils.cpp` `ExpandToRange:109`; `llvm/lib/Transforms/Instrumentation/BoundsChecking.cpp` `ObjectSizeOffsetEvaluator`; `clang/lib/StaticAnalyzer/Checkers/ArrayBoundChecker.cpp`; SoftBoundCETS `propagateMetadata`; CHERI subobject bounds (reference) | **Rules adapted, representation declined**: decay to `[base, base+count)` and "check only when object size is provable" mirrored; wide pointers/capabilities and IR-level checks declined; SoftBound expression-keyed propagation is a follow-up | **Implemented**: `stackArrayExtents_`, `heapAllocs_`, `onePastEnd_`, `pointer arithmetic outside array`, `pointer subtraction`/`relational comparison ... of pointers to distinct objects is undefined`, native `__cplus_bounds_check` guard emitted into generated C. **Design-only**: expression-keyed extents through casts/arithmetic/subobjects and CHERI-style subobject bounds | `tests/native/cplus_safety_tests.c` `const_oob`, `fam_oob`, `compound_oob`; `tests/34_array_decay_bounds.cp`; `tests/runtime_fail/bounds.c`; `tests/safety/fnptr_bounds/`; `tests/compile_fail/classic_bug26_ptr_arith.c` |
| Interprocedural bounds declarations and call arguments | Checked C; 3C; Apple Bounds Safety | `clang/lib/Sema/SemaBounds.cpp` `AbstractForFunctionType:129`, `ConcretizeFromFunctionTypeWithArgs:264`, `CheckBoundsDeclAtCallArg:2480`; `clang/include/clang/AST/CanonBounds.h` and `clang/lib/AST/CanonBounds.cpp` `CompareExprSemantically:268`, `GetExprIntDiff:323`, `PreorderAST.h`; `clang/lib/3C/Constraints.cpp` `graphBasedSolve ~150-236`; Apple `DynamicCountPointerAssignmentAnalysis.cpp` `computeMemberExprKey:44`, `TransformDynamicCountWithFunctionArgument.cpp:3698` | **Simplified algorithm adapted**: collect to fixed point then substitute call arguments into callee extents; `graphBasedSolve`, semantic base equivalence, `PreorderAST` and Apple dynamic-count pairing are design references only | **Implemented**: `callParamExtents_`, `returnedParam_`, `returnedFixedExtent_`, `callReturnExtent_` in `AstSafetyCheck.cpp`. **Design-only**: abstract/concretize bounds in function types, `CompareExprSemantically`/`GetExprIntDiff` proofs, constraint graphs, dynamic-count transforms, field/container-aware interprocedural extents | `tests/native/strict40_runner.c` decay/return cases; `decay_nested_bad`, `decay_returned_ptr_bad`, `decay_direct_bad`; `tests/safety/interproc_heap_extent` |
| Interprocedural effect/call summaries | GCC analyzer (GPL); Clang SA (negative result) | `gcc/analyzer/call-summary.cc` `call_summary_replay` (GPL, research only); `clang/include/clang/StaticAnalyzer/Core/PathSensitive/FunctionSummary.h` (checked and rejected) | **Declined**: `FunctionSummary.h` is inlining heuristics, not a callee summary API; GCC replay is GPL research only | **Implemented independently**: effect table (`allocates`, `frees(arg)`, `borrows`, `writes`, `returnsOwned`) in `OwnershipAnalysis.cpp` plus simple user `frees(arg)`/`returnsOwned` collection in `AstSafetyCheck.cpp`; arith helper-effect inference for a single `return a OP b;` on direct parameters. No GCC replay, no `FunctionSummary.h` | `tests/safety/interproc_double_free`, `tests/safety/interproc_return_uaf`, `tests/core/positive/interproc_return_ok`; `helper_add_overflow`, `helper_sub_overflow`, `helper_mul_overflow` |

## 3. Findings by priority source

### 3.1 Checked C (highest priority) — first-hand evidence

Pinned `checkedc-clang` at `a2843eae17d96cbfad2cf386dd60de0b849cfb6d`.
The bounds problem C+ faces is solved in Checked C by *type-level* bounds:
`_Array_ptr<T>` carries a `BoundsExpr`, function parameters declare bounds
such as `count(n)`, and the 3C conversion pass (`clang/lib/3C/`) infers and
propagates them. The inference pipeline in `3C.cpp:596-638` is:

1. seed constant array bounds (`addConstantArrayBounds`);
2. infer lower bounds (`inferLowerBounds`);
3. flow analysis (`performFlowAnalysis`) — repeated to a fixed point;
4. allocator-based inference (`AllocBasedBoundsInference`) for `malloc`/`calloc`;
5. repeated flow analysis;
6. rewrite (`RewriteConsumer`).

`ArrayBoundsInferenceConsumer.cpp` uses Clang CFG dominators plus name-based
heuristics (`LengthVarNamesPrefixes = {len, count, size, num, siz}`) to pair
pointers with length variables — conceptually the same problem C+ solves with
`arrayLengths` and `callParamExtents_`. The test
`clang/test/CheckedC/inferred-bounds/calls.c` specifically checks **bounds
substitution at call boundaries** (a function returning `_Array_ptr` with
bounds expressed in terms of an argument), which is exactly the C+ defect
fixed by commits `2a8ab84` and `4ce140e`.

**Decision.** Checked C cannot be reused as code: it requires a forked Clang
AST and checked-pointer types, breaking "ordinary C, native ABI". Its
*fixed-point collection + call-substitution* algorithm is already mirrored in
C+ (`AstSafetyCheck.cpp:3865-3889`). The remaining difference is that C+ keys
extents by variable name rather than by expression node; §6 records the
concrete follow-up.

### 3.2 Apple Bounds Safety — first-hand evidence

Read `clang/docs/BoundsSafety/BoundsSafety.md` at pinned LLVM `main`. The
design note at §1.2.4 is directly applicable:

- an array parameter `void f(int n, int arr[n])` decays to
  `int *__counted_by(n)` — C+ already records the same relationship in
  `callParamExtents_` and uses it to reject `f(a, 11)`;
- an array *reference* decays to an implicit wide pointer with lower bound
  `&arr[0]` and upper bound `&arr[size]` — C+ records the byte extent in
  `stackArrayExtents_` and injects `__cplus_bounds_check` for dynamic indices;
- `__single` is the default for ABI-visible pointers (function parameters) and
  pointer arithmetic on it is an error unless overridden — C+ implements the
  fail-closed analogue: an unknown pointer extent in STRICT is a diagnostic;
- local pointers are implicitly wide (bounds carried internally) — C+ keeps
  ordinary pointer layout and carries extents in the compiler, preserving ABI.

**Decision.** Adapt the *semantic rules*, not the pointer representation. The
wide-pointer annotations change layout and cannot be used in C+.

### 3.3 SoftBoundCETS — first-hand evidence

Pinned `9a9c09f04e16f2d1ef3a906fd138a7b89df44996`, BSD-3-Clause (with
per-file MIT headers). The main pass is 5,164 lines. Its central rule — a
pointer's base/bound/key/lock metadata is copied on every operation that
changes the pointer value (`propagateMetadata` in `SoftBoundCETS.cpp:1469-1539`,
called from `handleGEP`, `handleBitCast`, PHI, select, and store handlers) — is
the property C+ approximates for the special case of array decay.

Function-boundary metadata uses a runtime shadow stack (`softboundcets.h`,
`SoftBoundCETS.cpp:4079-4096, 4155-4211, 4441-4500`). The trie is large
(2^23 primary entries plus secondary tables, 64M temporal lock slots) and the
runtime depends on POSIX `mmap`/`execinfo`.

**Decision.** Do not port the trie, shadow stack, allocator interposition, or
`abort()` semantics. Adapt only the expression-keyed propagation discipline.

### 3.4 LLVM/Clang infrastructure

`llvm/lib/Transforms/Instrumentation/BoundsChecking.cpp` (pinned
`ff97e97e01a80965504abaab3bf76b6e6ffdacd7`) reuses `DataLayout`,
`TargetLibraryInfo`, `ObjectSizeOffsetEvaluator` and `ScalarEvolution` to
decide which loads/stores can be checked and to avoid duplicate checks. C+
currently generates source-level checks before Clang sees the IR, so this
pass is not directly applicable, but it validates the design choice of
checking only when an object size is provable and skipping otherwise.

Clang CFG APIs are already linked and used by
`compiler/Analysis/OwnershipAnalysis.cpp:351-431`; no custom CFG engine was
written. This satisfies the "reuse LLVM infrastructure" requirement.

### 3.5 Declined upstreams

- **Fil-C** (pinned `d3f9f822c276b446c8687ce70605b71aefc19beb`): custom LLVM
  fork, capability-style pointer representation, and concurrent GC. It cannot
  be integrated without replacing C+'s source-to-source architecture and
  native ABI.
- **Cake** (pinned `2da2afd1759a7f3a62024ac781af65c8832bbe5f`): GPLv3 plus an
  AI-training-use addendum; documentation only. Its flow-state and
  `defer`-at-every-exit rules are already approximated independently.
- **Cyclone** (GPL-2.0) and **GCC analyzer** (GPL): research-only licences;
  no source copied. Region and analyzer ideas are already approximated by
  resource types and Clang CFG dataflow.
- **CHERI**: hardware capability model; reference architecture only.

### 3.6 Evidence that implementation code and tests were inspected

Each row below is an actual source/test file read at the pinned commit, not a
README claim:

- **Checked C**: `clang/include/clang/AST/CanonBounds.h`, `clang/lib/AST/CanonBounds.cpp`,
  `clang/lib/Sema/BoundsUtils.cpp` (`CreateBoundsForArrayType`, `ArrayExprBounds`,
  `ExpandToRange`), `clang/lib/Sema/SemaBounds.cpp` (`CheckBoundsDeclarations`,
  `ConcretizeFromFunctionTypeWithArgs`, `ProveBoundsDeclValidity`,
  `CheckBoundsDeclAtCallArg`, `CheckIsNonModifying`), `clang/lib/CodeGen/CGDynamicCheck.cpp`
  (`lower <= ptr < upper` checks, `llvm.trap` failure block), and tests
  `clang/test/CheckedC/inferred-bounds/calls.c`,
  `clang/test/CheckedC/dynamic-checks/array-read-code-gen.c`,
  `tests/dynamic_checking/bounds/subscript.c`. The compiler-side licence is
  Apache-2.0-with-exception; `checkedc/checkedc`'s `LICENSE.TXT` is MIT.
  Notable: **there is no Checked C runtime library to port** — dynamic checks
  are inline IR.
- **Apple Bounds Safety**: the design doc `clang/docs/BoundsSafety/BoundsSafety.md`
  §1.2.4 and, at pins `f63645674df4…` / `ca74e48841e5…`, the **actual
  implementation** `clang/lib/Sema/SemaBoundsSafety.cpp` (1,089 lines),
  `clang/lib/Sema/DynamicCountPointerAssignmentAnalysis.cpp` (CFG-block-based
  paired-assignment groups), `clang/lib/Parse/ParseDecl.cpp` (late-parsed bounds
  attrs), `clang/lib/Sema/SemaExpr.cpp` (pointer-arithmetic restrictions),
  `clang/lib/CodeGen/BoundsSafetyTraps.cpp`, `clang/lib/Headers/ptrcheck.h`,
  and ~1,050 tests under `clang/test/BoundsSafety/` including
  `AST/array-to-pointer-decay.c`. Earlier local docs claimed only docs were
  inspected; this audit inspected implementation and tests. The public page is
  still marked "not available for users yet"; the full implementation lives on
  Apple/upstreaming branches.
- **SoftBoundCETS**: `softboundcets-lib/softboundcets.h` (base/bound/key/lock
  trie), `softboundcets-lib/softboundcets.c`, `softboundcets-wrappers.c`, and
  `softboundcets-llvm-clang34/lib/Transforms/SoftBoundCETS/SoftBoundCETS.cpp`
  (`propagateMetadata`, `handleAlloca`, `handleGEP`, call-site shadow stack).
- **LLVM/Clang**: `llvm/lib/Transforms/Instrumentation/BoundsChecking.cpp`
  (`ObjectSizeOffsetEvaluator`, `ScalarEvolution`), `clang/include/clang/Analysis/CFG.h`,
  `clang/include/clang/Analysis/FlowSensitive/DataflowAnalysis.h`,
  `clang/lib/StaticAnalyzer/Core/CoreEngine.cpp` (`HandleBranch`, `ProgramState::assume`),
  `clang/lib/StaticAnalyzer/Checkers/MallocChecker.cpp` (resource state machine),
  `clang/lib/StaticAnalyzer/Checkers/ArrayBoundChecker.cpp`.
- **Fil-C**: `llvm/lib/Transforms/Instrumentation/FilPizlonator.cpp`
  (17,703 lines; check merging and `filc_free_with_checks` binding),
  `libpas/src/libpas/filc_runtime.{c,h}` (free flag, aux capability slots,
  origins), `gimso_semantics.md`, and test manifests under `filc/tests/`.
- **Cake** (docs only, per licence): `flow.md`, `rules.md`, `manual.md`,
  `diagnostics.md`; no `.c`/`.h` opened.
- **Cyclone**: `doc/cyclone/{regions,pointers,aliasfree,definiteassignment}.tex`,
  `testsuite/compiler/testaqualfail.cyc` and `.ref`, `testsuite/tests/dynregion.cyc`.
- **CHERI LLVM**: `clang/lib/Headers/cheri.h`, `cheriintrin.h`,
  `clang/test/CodeGen/cheri/cap-provenance.c`, `subobject-bounds-addrof.c`,
  `clang/test/Sema/cheri/cap-provenance-attr.c`.
- **GCC** (research only): `gcc/doc/analyzer.texi`, `gcc/analyzer/sm-malloc.dot`,
  `gcc/analyzer/{supergraph,engine,call-summary,region-model}.cc`, and
  `gcc/testsuite/gcc.dg/analyzer/` test names.
- **Secondary**: C2Rust `c2rust-analyze/src/type_desc.rs` permission sets; GSL
  `include/gsl/{pointers,span,assert,narrow}`; CORAL `coral/borrowck/Regionck.js`;
  Xr0 `src/verifier/include/{path,segment}.h`; CCured `src/{ptrnode,cure,markptr}.ml`;
  Cforall `src/InitTweak/GenInit.cpp`.

### 3.7 Concrete algorithm findings worth carrying forward

1. **Checked C's decay rule** (`BoundsUtils.cpp:39-83`): create bounds from the
   array type, then `CK_ArrayToPointerDecay` followed by `ExpandToRange` yields
   `[base, base+count)`. Call arguments are checked by substituting arguments
   into callee parameter bounds (`ConcretizeFromFunctionTypeWithArgs`) and
   proving 3-valued containment (True/False/Maybe). C+ mirrors seed → decay →
   substitution → fixed point already; the remaining gaps are expression-keyed
   extents, semantic base equivalence, and cast subsumption.
2. **Clang Static Analyzer** offers a ready-made path-sensitive engine
   (`CoreEngine`, `ExplodedGraph`, `ProgramState::assumeDual`, `RangeConstraintManager`).
   The incremental route is CFG + `FlowSensitive/DataflowAnalysis` first (only
   `clangAnalysis`), then migrate the name-keyed `nullVars_`/`releasedVars_`
   to symbol/region-keyed state. A big-bang rewrite is not justified: C+ has
   policy-specific diagnostics (`CPLUS-*`, JSON contracts) with no upstream
   equivalent.
3. **GCC analyzer** contributes (as design only) call summaries with
   argument/region remapping, success/failure-split allocation states, and
   shortest-feasible-path diagnostics. GPLv3 prohibits copying any source.
4. **Fil-C** demonstrates deterministic free invalidation: a global FREE
   sentinel plus a free flag set by CAS, with rechecks after calls that may
   free. C+'s bounded 4096-entry recent-free cache can lose stale-pointer
   information after eviction; adding a generation tag to the side table is a
   compatible, ABI-neutral improvement.
5. **Cyclone/CHERI** contribute region handles, outlives constraints, pointer
   kind lattices, and provenance/bounds narrowing. All require new syntax or a
   capability ABI if copied; as library-only or compiler-side facts they are
   compatible.
6. **Secondary**: CCured's SAFE/SEQ/FSEQ/WILD taxonomy and C2Rust's permission
   sets are usable as *diagnostic tiers* with unchanged pointer layout; CORAL's
   `#pragma` lifetime annotations are ABI-neutral but its licence forbids any
   reuse.

## 4. What was actually reused

1. **Clang/LLVM APIs** (Apache-2.0 WITH LLVM-exception): `tooling::buildASTFromCodeWithArgs`,
   Clang AST visitors, `clang::CFG`, `DiagnosticsEngine` patterns, and the
   host backend. These are *linked*, not copied.
2. **Algorithms adapted from upstream designs** (no source copied):
   - fixed-point bounds collection and call-site substitution (Checked C 3C);
   - array decay carrying extents and unknown-extent fail-closed behaviour
     (Apple Bounds Safety and Checked C);
   - extent propagation on pointer-value transformations (SoftBoundCETS);
   - flow-sensitive ownership state and defer-at-every-exit discipline
     (Cake docs, Cyclone docs).
3. **Upstream test cases studied and mirrored as C+ regressions**: Checked C
   `inferred-bounds/calls.c` → C+ direct/nested/return decay cases; Checked C
   `dynamic-checks/array-read-code-gen.c` → `tests/runtime_fail/bounds.c`.
   No upstream test file was copied into the repository.
4. **Predicates only (second-pass reports)**: the zero-divisor, signed-division
   overflow, shift-range, sign/range and bounds predicates in §7 were read as
   *specifications* and re-expressed in C+'s own AST visitors. No UBSan handler
   (`ubsan_handlers.cpp`), Clang SA checker or GCC analyzer source was reused.

## 5. What was implemented independently, and why

C+ emits ordinary C for an x86 Windows DLL and must preserve the native ABI
and existing `--default/--strict/--legacy-c` modes. Every upstream approach
that changes pointer representation (Checked C checked pointers, Apple wide
pointers, SoftBoundCETS metadata, Fil-C capabilities) is therefore
incompatible. C+ instead keeps bounds and ownership facts in compiler-side
maps and a small runtime side table, and fails closed in STRICT where a proof
is unavailable. This is a deliberate design constraint, not an oversight.

A recurring clarification from the second-pass reports: C+ enforcement is
**native and source-level**. `compiler/Sema/CPlusRewriter.cpp` emits calls to
`__cplus_checked_{add,sub,mul,div,rem,shl,shr}_i64` and `__cplus_bounds_check`
into the generated C, with definitions in `runtime/cplus_runtime.c`. The
sanitizer flags in `compiler/Driver/Driver.cpp` (including
`implicit-integer-truncation`) are a separate, optional backend layer; they are
not the mechanism behind C+'s compile-time diagnostics or native guards, and
must not be credited with them.

## 6. Verified C+ behaviour and remaining follow-up

Verified after the earlier bounds work:

- `c+ --strict --sanitize=none tests/34_array_decay_bounds.cp` rejects with
  `[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] array index 4 exceeds heap allocation 'p' of 16 bytes`;
- native safety harness 44/44, strict40 suite 27 rejected / 0 mismatches;
- 159-test Agent 1 suite: 156 PASS, 0 FAIL, 3 documented SKIP;
- safe corpus 90/90 in both modes, 0 false positives.

Not yet implemented (candidate follow-ups grounded in the upstream study):

- expression-keyed extents through casts, pointer arithmetic and subobject
  selection (SoftBound `propagateMetadata` analogue);
- symbolic-index element lifetime state (field/container audit);
- cross-TU ownership manifests (separate research report).

No claim is made that C+ implements Checked C, Apple Bounds Safety,
SoftBoundCETS, Fil-C, CHERI, Cake or Cyclone guarantees.

## 7. Second-pass addendum: arithmetic, conversions, pointer arithmetic, interprocedural

The four additional research reports are summarised here at the same evidence
standard as §3: pinned revision, real implementation files, a reuse decision,
and an honest C+ status. The matrix in §2 is the compact index; this section
explains the **Implemented** versus **Design-only** labels. As everywhere in
this document, "designed after" means "algorithm studied", never "source
copied".

### 7.1 Division, remainder and shift

**Upstream evidence.** Checked C at `a2843eae17d96cbfad2cf386dd60de0b849cfb6d`
diagnoses bad divisors and shifts in `clang/lib/Sema/SemaExpr.cpp`:
`DiagnoseBadDivideOrRemainderValues:10824` and `DiagnoseBadShiftValues:11407`.
LLVM/Clang at `c3b40cbb3379e837f4dde28bd40f00ed1966989a` emits the matching
IR traps in `clang/lib/CodeGen/CGExprScalar.cpp`:
`EmitUndefinedBehaviorIntegerDivAndRemCheck:4289` and `EmitShl:5108`; the
runtime failure path is `compiler-rt/lib/ubsan/ubsan_handlers.cpp`
`handleDivremOverflowImpl:325`. Clang SA contributes
`clang/lib/StaticAnalyzer/Checkers/DivZeroChecker.cpp`. GCC's
`gcc/analyzer/region-model.cc` `get_gassign_result:953` is GPL-3.0-or-later and
was read as research only. Licences: Apache-2.0 WITH LLVM-exception for the
Clang/LLVM/compiler-rt files, GPL-3.0-or-later for GCC.

**Reusable:** the *predicates* — zero divisor, `INT_MIN / -1`, and shift count
inside `[0, width)`. **Not reusable:** the UBSan handler/trap path and the GCC
analyzer source. Neither was copied.

**C+ status — Implemented, natively.** `VisitBinaryOperator` in
`compiler/Sema/AstSafetyCheck.cpp` emits `division by zero`, `remainder by
zero`, the `cannot be excluded` variants, `signed integer overflow in
division/remainder`, and the negative/wide/unproven shift-count diagnostics.
Eligible runtime-valued add/sub/mul/div/rem/shl/shr (non-macro, main-file,
within the guard ABI's width limits) are wrapped by `__cplus_checked_*_i64`
guards emitted into generated C by
`compiler/Sema/CPlusRewriter.cpp` and implemented in `runtime/cplus_runtime.c`.
These are C+ guards, **not UBSan instrumentation**, and they are independent of
`--sanitize=none`.

**Design-only / gaps.** No general value-range analysis: non-zero proofs are
name-keyed and narrow (`if (d)` / `while (d)`, positive `for` counters), and
`if (d != 0) return n / d;` is still rejected in STRICT. Nested helper chains,
multi-return helpers, casts and interprocedural arithmetic chains are not
inferred; runtime-variable signed overflow is handled by the native guard (or
an opt-in sanitizer), not statically proven. See `docs/ARITHMETIC_SAFETY.md` §7.

### 7.2 Implicit conversions

**Upstream evidence.** LLVM/Clang at `c3b40cbb3379e837f4dde28bd40f00ed1966989a`:
`clang/lib/Sema/SemaChecking.cpp` `CheckImplicitConversion ~13657` and
`IntRange ~11961`, plus the UBSan implicit-conversion predicates in
`clang/lib/CodeGen/CGExprScalar.cpp ~1142`. Microsoft GSL
`include/gsl/narrow` (MIT) supplies the `narrow`/`narrow_cast` contract
vocabulary; Checked C `clang/lib/Sema/SemaCast.cpp` `CheckBoundsCast ~3081`
relates casts to bounds. Licences: Apache-2.0 WITH LLVM-exception and MIT.

**Reusable:** the round-trip and sign predicates (does the value survive the
cast?), not GSL syntax and not the UBSan conversion handlers.

**C+ status — Partial (pointer provenance only).** `VisitCastExpr` treats
integer values derived from a real pointer (`(uintptr_t)p`, tagged `u & ~1`) as
verified round-trips and rejects integer-forged pointers with `pointer created
from integer value has unverifiable provenance` (error in STRICT, warning in
DEFAULT). Test: `int_to_ptr` in `tests/native/cplus_safety_tests.c`.

**Design-only.** There is no general implicit-narrowing, sign-change or integer
round-trip checker — no `IntRange` / `CheckImplicitConversion` analogue, no GSL
`narrow` contract, no `CheckBoundsCast` equivalent. The
`implicit-integer-truncation` flag in `compiler/Driver/Driver.cpp` is an opt-in
backend sanitizer, not a C+ guard.

### 7.3 Pointer arithmetic

**Upstream evidence.** Apple `clang/lib/Sema/SemaExpr.cpp` pointer restrictions;
Checked C `clang/lib/Sema/BoundsUtils.cpp` `ExpandToRange:109`; LLVM
`llvm/lib/Transforms/Instrumentation/BoundsChecking.cpp`
`ObjectSizeOffsetEvaluator`; Clang SA
`clang/lib/StaticAnalyzer/Checkers/ArrayBoundChecker.cpp`; SoftBoundCETS
`propagateMetadata`; CHERI subobject bounds as a reference architecture.
Licences: Apache-2.0 WITH LLVM-exception (Apple, LLVM, Checked C, Clang SA),
BSD-3/MIT (SoftBoundCETS), MIT CHERI headers.

**Reusable:** the decay-to-range rule, "check only when object size is
provable", and the discipline of carrying extents on every pointer-value
change. Declined: wide pointers, capability ABI, IR-level checks and full path
sensitivity.

**C+ status — Implemented.** Array decay carries extents (`stackArrayExtents_`,
`heapAllocs_`, `callParamExtents_`); one-past-end and dynamic-index handling,
`pointer arithmetic outside array`, and `pointer subtraction`/`relational
comparison ... of pointers to distinct objects is undefined` are enforced in
`VisitBinaryOperator`. Dynamic indices call the native `__cplus_bounds_check`
guard emitted into generated C (see §5).

**Design-only.** Expression-keyed extent propagation through casts, pointer
arithmetic and subobject selection (the SoftBound `propagateMetadata` analogue
already listed in §6), and CHERI-style subobject bounds. C+ remains name-keyed
and ABI-neutral.

### 7.4 Interprocedural reasoning

**Upstream evidence.** Checked C `clang/lib/Sema/SemaBounds.cpp`
`AbstractForFunctionType:129`, `ConcretizeFromFunctionTypeWithArgs:264`,
`CheckBoundsDeclAtCallArg:2480`; `clang/include/clang/AST/CanonBounds.h` and
`clang/lib/AST/CanonBounds.cpp` `CompareExprSemantically:268`,
`GetExprIntDiff:323`, `PreorderAST.h`; 3C `clang/lib/3C/Constraints.cpp`
`graphBasedSolve ~150-236`; Apple
`DynamicCountPointerAssignmentAnalysis.cpp` `computeMemberExprKey:44` and
`TransformDynamicCountWithFunctionArgument.cpp:3698`; GCC
`gcc/analyzer/call-summary.cc` `call_summary_replay` (GPL, research only).
Clang SA
`clang/include/clang/StaticAnalyzer/Core/PathSensitive/FunctionSummary.h` was
checked and **rejected**: it is inlining heuristics, not a callee bounds/effect
summary API. That negative result is recorded so it is not revisited.

**Reusable:** the collect-to-fixed-point plus call-argument substitution shape.
Design references only: semantic base equivalence and integer-difference
proofs, `PreorderAST` canonicalisation, graph-based constraint solving, Apple
dynamic-count pairing, and GCC summary replay.

**C+ status — Implemented.** Fixed-point collection and call-site extent
substitution (`callParamExtents_`, `returnedParam_`, `returnedFixedExtent_`,
`callReturnExtent_`); ownership effect summaries (`allocates`, `frees(arg)`,
`borrows`, `writes`, `returnsOwned`) in `OwnershipAnalysis.cpp` plus simple user
`frees(arg)`/`returnsOwned` collection in `AstSafetyCheck.cpp`; arith
helper-effect inference for a single `return a OP b;` on direct parameters.

**Design-only / declined.** Abstract/concretize bounds in function types,
`CompareExprSemantically`/`GetExprIntDiff` proofs, `graphBasedSolve`-style
constraint graphs, Apple's dynamic-count member/argument transform, field- and
container-aware interprocedural extents, and GCC-style summary replay.
`FunctionSummary.h` is not used.

In all four areas the enforcement mechanism is the same: C+ decides at compile
time in its AST visitors and, where a value is not statically known, emits its
own guards into the generated C (`__cplus_checked_*`, `__cplus_bounds_check`).
None of this is achieved by linking UBSan or by importing GPL analyzer code.

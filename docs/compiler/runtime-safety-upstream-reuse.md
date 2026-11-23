# Runtime memory-safety upstream reuse audit

Companion to `docs/compiler/upstream-reuse.md` and
`docs/compiler/static-analysis-upstream-reuse.md`. This document covers
runtime metadata and provenance mechanisms. **No upstream implementation
source was copied**; all entries are design/predicate references.

## Pins and licences

| Project | Pin | Licence | Disposition |
| --- | --- | --- | --- |
| Fil-C (`pizlonator/fil-c`) | `d3f9f822c276b446c8687ce70605b71aefc19beb` | LLVM Apache-2.0 WITH LLVM-exception; runtime BSD-style (`libpas/LICENSE.txt`) | Design reference only; fat pointers/GC/ABI declined |
| TrapC | no verifiable source repository | none | Do not adapt; WG14 N3423 is a language paper, not an implementation |
| Checked C (`checkedc/checkedc-clang`) | `a2843eae17d96cbfad2cf386dd60de0b849cfb6d` | Apache-2.0 WITH LLVM-exception (root `LICENSE.TXT`; LLVM-family repos have no separate `clang/LICENSE.TXT`) | Predicates adapted; no fork |
| Apple Bounds Safety (`swiftlang/llvm-project`) | `ca74e48841e588074fba77543e4e0fc643edc52c` (docs), `f63645674df4…` (implementation) | Apache-2.0 WITH LLVM-exception | Rules adapted; wide pointers declined |
| SoftBoundCETS (`santoshn/softboundcets-34`) | `9a9c09f04e16f2d1ef3a906fd138a7b89df44996` | BSD-3-Clause + per-file MIT-style headers | Metadata shape adapted; trie/shadow stack declined |
| CHERI LLVM (`CTSRD-CHERI/llvm-project`) | `7e122876ee01a7a82585f7674c94f12fc1cc7689` | Apache-2.0 WITH LLVM-exception / MIT headers | Provenance concepts only; capability ABI declined |
| LLVM sanitizers (`llvm/llvm-project`) | `424c2d9b7e4de40d0804dd374721e6411c27d1d1` (llvmorg-20.1.1) | Apache-2.0 WITH LLVM-exception | Reporting taxonomy adapted; runtime code declined |

## Source files actually inspected

- Fil-C: `llvm/lib/Transforms/Instrumentation/FilPizlonator.cpp` (17.7k lines),
  `libpas/src/libpas/filc_runtime.h` (`filc_ptr` at `:314-316`, free flag at
  `:177-199`), `filc_runtime.c` (`ensure_aux_ptr_slow_size_specialized`
  `:2708-2776`, `free_impl` `:3326-3336`), `fugc.c`, `fil-c.org/how`.
- Checked C: `clang/lib/CodeGen/CGDynamicCheck.cpp` (`EmitDynamicBoundsCheck`,
  null check, subsumption, `llvm.trap` failure block), `clang/lib/Sema/BoundsUtils.cpp`
  (`ExpandToRange`), `tests/dynamic_checking/bounds/subscript.c`.
- Apple: `BoundsSafetyTraps.def/.cpp` (trap taxonomy), `ptrcheck.h`
  (`__unsafe_indexable` boundary), `DynamicCountPointerAssignmentAnalysis.cpp`
  (`computeMemberExprKey:44`, paired-assignment predicates), `SemaBoundsSafety.cpp`.
- SoftBoundCETS: `softboundcets-lib/softboundcets.h` (`{base,bound,key,lock}`),
  `SoftBoundCETS.cpp` (`propagateMetadata:1469`, `handleGEP:4021`,
  `handleAlloca:3744`, shadow stack `:4079-4211`).
- CHERI: `cheriintrin.h`, `CGExpr.cpp` subobject bounds (`:616-852`),
  `SemaExpr.cpp` ambiguous provenance (`:11475`), `cap-provenance.c`.
- LLVM sanitizers: `compiler-rt/lib/asan/asan_mapping.h`, `asan_poisoning.cpp`,
  `asan_allocator.cpp`, `ubsan_handlers.cpp`, `msan.h`, `dfsan.cpp`.

## Reuse decisions

| Technique | Upstream | Decision | C+ implementation |
| --- | --- | --- | --- |
| Out-of-band metadata, ordinary pointer representation | Fil-C (InvisiCaps), SoftBound, CHERI concept | **Adapted** | `cplus_live` side table; registration helpers return the same pointer |
| Free flag / O(1) stale invalidation | Fil-C free flag, SoftBound key/lock | **Partial** | recent-free cache + tombstones; no generation token yet |
| Propagate metadata on decay/cast/GEP/call/return | SoftBound `propagateMetadata`, CHERI subobject narrowing | **Adapted** | compiler extent summaries (`callParamExtents_`, `returnedAllocExtent_`), cast-transparent lookups |
| Check merging / trap taxonomy | Fil-C `CheckKind`, Apple `BoundsSafetyTraps.def` | **Adapted** | stable `C+ runtime error:` messages with deterministic exit codes |
| Foreign boundary: unknown is unsafe, explicit adoption | Apple `__unsafe_indexable`, Checked C bounds-safe interfaces | **Adapted** | unknown extents fail closed in STRICT; no invented bounds |
| Shadow memory mapping and redzones | ASan/MSan | **Declined** | requires VM reservation, allocator interposition, compiler-rt |
| Fat pointers / capability ABI / GC | Fil-C, CHERI | **Declined** | would break native ABI and calling convention |
| Shadow stack for metadata | SoftBound | **Declined** | C+ uses per-TU compile-time summaries |
| TrapC | no source | **Declined** | nothing to pin, licence or audit |

## Verified C+ behavior

- Native guards work under `--strict --sanitize=none`: `tests/compiler/runtime_memory_safety/run_suite.py`
  28/28 cases, and `tests/compiler/agent1/run_native_matrix.sh` 24/24.
- ABI is unchanged: `tests/compiler/abi_safety/run_suite.py` 33/33 checks for
  pointer sizes, struct layout, signatures, callbacks, symbol names and
  precompiled library interop.
- Runtime registration covers `malloc`, `calloc`, `realloc` and `free`,
  including out-parameter assignments and realloc failure paths; verified by
  the full regression suite (109/0 repo, 152/0 core, 90/90 safe corpus).

No claim of Fil-C, CHERI or ASan equivalence is made. The C+ side table is a
weaker, ABI-preserving subset: it cannot provide byte-granular interior
coverage, race safety, or post-reclamation guarantees.

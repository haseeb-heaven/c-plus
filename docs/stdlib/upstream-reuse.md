# Upstream reuse for the C+ standard library

Date: 2026-11-23. Branch `agent/modern-stdlib`. This document records ACTUAL
source inspected (not just documentation), exact URLs/paths, pinned revisions
where available, licenses, ABI verdicts, which designs were adapted, and which
C+ regression tests verify the results. Per-source detail is in
`docs/stdlib/upstream_study/*.md` (9 files, one per source). The two additions
for this update — `trapc.md` and `softboundcets.md` — were not present when the
earlier sections below were authored; they landed during this task and were
read in full before the rows and notes here were written. The runtime-side
companion is `docs/stdlib/abi-preserving-runtime-safety.md` (also new).

## 1. Sources studied and license review

| # | Upstream | Revision / pin | Exact identifier | ABI verdict | Audit doc |
|---|---|---|---|---|---|
| 1 | Microsoft STL | `main` @ `0cabaf3c7e0a73fe3040aabce40cd8833e80392b` | `Apache-2.0 WITH LLVM-exception` | C++-only; different object model — design reference only | `upstream_study/microsoft_stl.md` |
| 2 | Microsoft GSL | `main` (fetched 2026-11-23) | `MIT License (MIT)`, Copyright (c) 2015 Microsoft Corp. | C++ templates / generic `span` — design reference only | `upstream_study/microsoft_gsl.md` |
| 3 | SaferCPlusPlus | `master`, API `pushed_at` 2026-10-12 | `BSL-1.0` (Boost Software License 1.0) | C++ pointer-wrapper classes — design reference only | `upstream_study/safer_cplusplus.md` |
| 4 | Microsoft Secure CRT (docs) + vendored safeclib | safeclib `90b076e94c4b0fb202dddf948d5d620e5395c1b0` | Docs only; safeclib MIT-style (Cisco/Reini Urban); Intel MIT | Thin wrappers keep the C signatures — compatible | `upstream_study/secure_crt.md` |
| 5 | Cake | `main` @ `2da2afd1759a7f3a62024ac781af65c8832bbe5f` (v0.17.07) | `GPL-3.0-or-later` + AI-training-use addendum — **REJECTED** | Separate front end / ownership model — REJECTED | `upstream_study/cake.md` |
| 6 | Fil-C | fetched docs + `fugc.h`, `stdfil.h` | LLVM: Apache-2.0 WITH LLVM-exceptions; fetched runtime files BSD-2-clause | **NOT ABI-preserving**: fat/capability pointers, custom ABI, mandatory GC — not copied | `upstream_study/fil_c.md` |
| 7 | LLVM/Clang | `main` @ pinned commit recorded in study | `Apache-2.0 WITH LLVM-exception` | Existing toolchain; no new ABI surface (one renamed function body copied) | `upstream_study/llvm_clang.md` |
| 8 | TrapC — **no repository**; docs at `https://trapc.org/` and `https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3423.pdf` (WG14 `n3423`, 2025-01-07); independent report `https://www.theregister.com/2026/01/26/trapc_claude_c_memory_safe_robin_rowe/` | **N/A** — no repo, commit or tag exists; most precise pins are the `n3423` date and trapc.org posts 2026-01-26…2026-02-02 | **UNDETERMINED** — "FOSS intended, license not published"; no LICENSE file and no SPDX identifier (**do not** write MIT/BSD/Apache) | **NEGATIVE**: one-way ABI only; managed pointers with hidden RTTI, no-op `free`/`delete`, trap-aware wrappers, zeroed trap returns → reuse would change the pointer contract or C signatures; also no source published | `upstream_study/trapc.md` |
| 9 | SoftBoundCETS — `https://github.com/santoshn/softboundcets-34` (API `https://api.github.com/repos/santoshn/softboundcets-34`) | `master` HEAD `9a9c09f04e16f2d1ef3a906fd138a7b89df44996` (commit 2014-09-19, "Merge pull request #4 from edmcman/master"; **no tags**) | GitHub SPDX `BSD-3-Clause` (key `bsd-3-clause`); top-level `LICENSE` blob `9a6efbb3e8e28d263c74e0b434830f46c09fe255` © 2013 Santosh Nagarakatte with an unfilled `{organization}` placeholder in clause 3; per-file MIT-style notices © 2011/2014 with named UPenn/Rutgers authors | **Pointer representation unchanged** (one word, no fat pointer/tagging; `sizeof`/alignment/register passing unchanged) → layout-compatible with C+ thin pointers. But hidden shadow-stack calling convention, `softboundcets_*` symbol renaming, `main` → `softboundcets_pseudo_main`, 8-byte slot granularity and 64-bit-only huge virtual reservations mean library-only code cannot claim equivalent guarantees; full fidelity is compiler/runtime (Agent 1) | `upstream_study/softboundcets.md` |

License decisions: only permissive licenses (MIT, BSL-1.0,
Apache-2.0-with-LLVM-exception, BSD-2-clause) were considered reusable. Cake
was already rejected in `docs/UPSTREAM_AUDIT.md`; **no Cake source, header or
test was copied** and its study is idea-level traceability only. Microsoft CRT
material was used as documentation, not copied code.

The two new rows follow the same rule. SoftBoundCETS is SPDX `BSD-3-Clause` for
the tree (top-level `LICENSE`) with per-file MIT-style notices; both notice
families are permissive, but **no SoftBoundCETS source was copied** and only
check/range-predicate ideas were paraphrased. TrapC's license is
**UNDETERMINED**: only "free open source software" intent is published, with no
LICENSE file, no SPDX identifier and no repository to fetch one from, so its
study is documentation-based traceability only and **no reuse is authorized**.
The literal `$ echo "Open Source MIT" > LICENSE` in `n3423` is a Cmaker
tutorial example, **not** a statement of TrapC's license.

## 2. Source files actually inspected

- **MS STL**: `stl/inc/{vector,xstring,xmemory,xutility}` and tests
  `Dev10_579381_vector_grow_to`, `GH_005472_do_not_overlap`,
  `GH_002030_asan_annotate_{vector,string}`,
  `P0966R1_string_reserve_should_not_shrink`,
  `P1206R7_string_append_range` (all under `tests/std/tests/.../test.cpp`).
- **GSL**: `include/gsl/{span,pointers,narrow,assert,util,span_ext}`, `LICENSE`,
  tests `span_tests.cpp`, `notnull_tests.cpp`, `owner_tests.cpp`,
  `assertion_tests.cpp`, `utils_tests.cpp` (13/18 URLs succeeded; 5 404s
  recorded — `not_null`/`owner` live in `pointers`).
- **SaferCPlusPlus**: `include/{msepointerbasics,msescope,mseregistered,
  mserefcounting,msemsevector,mseasyncshared}.h` and
  `apps/legacy_examples/msetl_example/{msevector_test.h,msetl_example_defs.h}`. The brief's
  `msl/*` paths are stale; real headers are flat in `include/`.
- **Secure CRT/safeclib**: Microsoft Learn pages for security features,
  parameter validation, `strcpy_s`, `strncpy_s`, truncation, `fopen_s`,
  `fprintf_s`, `gets_s`, secure templates; local
  `third_party/safeclib/src/{mem/memset_s.c,str/strcpy_s.c,str/strncpy_s.c,
  io/fopen_s.c,io/fprintf_s.c,io/gets_s.c,abort_handler_s.c,ignore_handler_s.c}`
  and Intel `third_party/intel-safestringlib/safeclib/{strcpy_s.c,memset_s.c}`.
- **Cake**: `README.md`, `LICENSE`, `quickref.md`, `flow.md`, `manual.md`,
  `src/cake_compat.h`, `src/{defer.h,flow.h,flow_branch.h}`,
  `src/include/{stddefer.h,stdlib.h,stdio.h,threads.h}`, defer/flow3 unit tests.
- **Fil-C**: `fil-c.org/{how,invisicaps,fugc,runtime,safepoints,stdfil}.html`,
  `libpas/src/libpas/fugc.h`, `filc/include/stdfil.h`.
- **LLVM**: `compiler-rt/include/sanitizer/{asan_interface.h,ubsan_interface.h,
  common_interface_defs.h}`, `libcxx/include/__vector/vector.h`,
  `libcxx/include/__assert`, `libcxx/include/__verbose_abort`,
  `libc/src/string/memset_explicit.cpp`, `LICENSE.TXT`.
- **TrapC — NO SOURCE FILES INSPECTED.** No repository, commit, tag, LICENSE or
  source file exists publicly. Only author-published documentation was read:
  WG14 `n3423` (`https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3423.pdf`,
  2025-01-07), `https://trapc.org/` (About, "TrapC, a Year Later", "The
  Register on Vibing TrapC", "C++ Style Member Functions") and The Register
  (2026-01-26). The `testof` / RTTI / `trap` descriptions are documented
  behavior, **not source findings**; a third-party gist repost of a whitepaper
  snippet was explicitly not used as source evidence.
- **SoftBoundCETS** (all at `master` HEAD
  `9a9c09f04e16f2d1ef3a906fd138a7b89df44996`): `LICENSE` (blob
  `9a6efbb3e8e28d263c74e0b434830f46c09fe255`), `README.md` (blob
  `0f3d7b2ddd93137ab25b1df12ac389838255f0b5`),
  `softboundcets-lib/softboundcets.h` (blob
  `4a94ab440d5c24d56bd23c1b7aa274980f38f59b`, metadata record + checks),
  `softboundcets-lib/softboundcets.c` (blob
  `fe6379211618c69ff2f8cfcd3149cb4d4422a830`),
  `softboundcets-lib/softboundcets-checks.c` (blob
  `0bca2f9a3d3b00ae4ba2d59f4c06284231adc672`),
  `softboundcets-lib/softboundcets-wrappers.c` (blob
  `c4ac66cb6f11b96f30fe3ef68b7515e6f6960711`) and
  `softboundcets-llvm-clang34/lib/Transforms/SoftBoundCETS/SoftBoundCETS.cpp`
  (blob `c059a38b6089b9f8be19a94219c4ffda3f91fb14`, 5164 lines; selected
  ranges only). The MPX variants, `LICENSE.TXT` and the upstream `tests/` tree
  were **[NOT INSPECTED]** by the study, as reported there.

## 3. Reuse mapping

| C+ module | Upstream project | Source files | Reused design/code | Changes made | Regression tests |
|---|---|---|---|---|---|
| `vector.hp` | MS STL; GSL; LLVM | `stl/inc/vector`; `include/gsl/span`; `__vector/vector.h` | Subtract-form overflow checks (`limit - current`), commit-last mutation with scoped rollback, pinned extent/bounds contracts, `_LIBCPP_ASSERT` tiering | C++ templates/RAII unavailable; C+ uses `size_t` overflow checks, `stdlib_alloc` + explicit rollback, `at_checked` + generation-stamped `slice`. Power-of-two doubling kept as documented C+ behavior. | `deep_error_vector.cp`, `deep_vector_task4.cp`, `deep_collections_alloc.cp`, `extra_string_vector_alias.cp` |
| `string.hp` / `string_view` | MS STL; GSL; SaferCPlusPlus; Secure CRT | `stl/inc/xstring`; `include/gsl/pointers`; `msepointerbasics.h`; `strcpy_s.c` | Overlap-safe append (upstream copies old buffer before reading aliased source), `Expects`-style validation at accessors, source/lifetime token concept | Tier-2 `error *` reporting instead of exceptions/contracts; `string_view` remains caller-responsible (no generation) — documented gap, not silently "fixed" | `deep_string_append.cp`, `deep_string_view.cp`, `deep_string_task4.cp`, `extra_string_vector_alias.cp` |
| `io.hp`, `file.hp` | Secure CRT; MS STL; LLVM runtime | `gets_s.c`, `fopen_s.c`, `fprintf_s.c`; `stl/inc/xstring`; sanitizer interfaces | Bounded loops that drain/clear on overflow and always NUL-terminate; owned-vs-borrowed handle discipline; invalid-parameter policy | Annex K names/semantics not copied; C+ returns `STDLIB_OK/EOF/FAIL` with `error *` and exact peek logic for CRLF/EOF | `deep_io_1byte.cp`, `deep_io_token.cp`, `deep_io_task2.cp`, `deep_file_lines.cp`, `deep_file_lifecycle.cp`, `deep_file_task2.cp`, `extra_io_whitespace.cp`, `extra_file_binary.cp` |
| `collections.hp` | MS STL; GSL; SaferCPlusPlus | `stl/inc/vector`; `include/gsl/span`; `msemsevector.h` | Open addressing with tombstones, power-of-two capacity, 70% load guard, allocate-all-then-commit rollback | C+ variant stores plain `int`/owned key copies; no iterators/exceptions; `slice` uses generation counter instead of debug iterators | `deep_collections_growth.cp`, `deep_collections_collisions.cp`, `deep_collections_alloc.cp`, `deep_collections_boundary.cp`, `extra_collections_clear.cp` |
| `error.hp` / alloc metadata | GSL; Fil-C; LLVM; SoftBoundCETS + TrapC (ideas only, new) | `include/gsl/narrow`; `fugc.h`, `stdfil.h`; `asan_interface.h`; `softboundcets-lib/softboundcets.h`; WG14 `n3423` (docs only) | First-class liveness/length metadata queries with explicit unknown/freed result; generation-stamped views; two-phase narrowing; SoftBoundCETS four-field `{base,bound,key,lock}` record and O(1) lock zeroing; TrapC per-pointer extent/`testof` queries | C+ uses `stdlib_alloc_size/length/generation/check_generation` and `stdlib_note`; exact-base-address lookup only (no interior-pointer owner query); **no** capability pointers, shadow stack, compiler annotations, managed-pointer RTTI or hidden per-pointer metadata | `deep_error_vector.cp`, `deep_error_task5.cp`, `extra_error_metadata.cp`, `alloc_fault_matrix` (294 checks) |
| `stdlib/memory.hp` — bounds-aware wrappers (**file added by this task**) | SoftBoundCETS; TrapC (ideas only); local `stdlib/error.hp` side table | `softboundcets-lib/softboundcets.h` (spatial check `ptr < base \|\| ptr + size > bound`, `memcopy_check`/`memset_check`, monotonic `max/min` shrink); WG14 `n3423` (extent/`testof`/offset-to-base, docs only); `stdlib/error.hp:102-125,415-524` | Thin, additive wrappers `stdlib_mem_capacity/count/token/check/copy/move/set/borrow/transfer/release`; subtract-form range validation `off > cap` / `n > cap - off` against tracked capacity; generation token refuses stale/reissued blocks; fail-closed `error *` reporting instead of `abort()`/`backtrace()` | **No new side table, no fat pointers, no ABI change**: raw `T *` stays one word, callers pass (base, explicit offset, length), capacity comes from `stdlib/error.hp`; exact-base-address requirement documented (interior access needs `(base, offset)` passed separately, not a pre-summed `base + offset` pointer); TrapC managed-pointer semantics and SoftBoundCETS mmap'd trie/shadow stack/abort policy deliberately not ported | `tests/stdlib/deep_memory_bounds.cp` (local test added by this task) |
| ABI evidence for the rejected mechanisms — **files added by this task** | Fil-C, SoftBoundCETS, TrapC (all rejected for ABI reasons); local `tests/abi/abi_layouts.c` | `tests/abi/abi_compat_{pointers,layouts,header_neutrality,c_driver,c_part,cplus_caller,cplus_lib}.c/.cp`, `tests/abi/abi_compat_shared.h`, `tests/abi/abi_compat_expected_{libc,safecrt}_symbols.txt` | Evidence that pointers stay thin, public layouts and symbol names are unchanged, and C/C++ headers are ABI-neutral; substantiates the "no pointer widening / no new exported symbol" verdicts above | No upstream code copied; these are local probes. The earlier `abi_layouts.c` probe recorded 70/70 layout assertions on both toolchains (`docs/stdlib/abi-preserving-runtime-safety.md`); this document claims no result for the new `abi_compat_*` files or for `tests/stdlib/agent2/abi_safety_probe.cp` | `tests/abi/abi_layouts.c`; `tests/stdlib/agent2/abi_safety_probe.cp` (test added by this task) |
| `thread.hp` | MS STL; SaferCPlusPlus; LLVM | `std::mutex`/`lock_guard` patterns; `mseasyncshared.h`; sanitizer thread interfaces | RAII guard ownership, guarded destruction, watchdog-bounded waits | C++ template guards reduced to `mutex`/`guard` classes; compiler RAII name-dedup defect is Agent 1 work | `deep_thread_guard.cp`, `deep_thread_nested.cp`, `deep_thread_task5.cp`, `extra_thread_condvar.cp` |
| `json.hp`, logging | LLVM; vendored yyjson; safeclib | `asan_interface.h`; `third_party/yyjson`; `abort_handler_s.c` | Failure-atomic commit, constraint-handler discipline, bounded write loop | yyjson reused as-is (not reimplemented); its allocator is not injectable — SKIP recorded, not fabricated | `deep_json_types.cp`, `deep_json_stress.cp`, `deep_json_task5.cp`, `extra_json_cleanup.cp`, `test_logging_telemetry.c` |
| Runtime/compiler-facing | LLVM; Fil-C; Cake (idea only) | `asan_interface.h`, `__assert`, `flow.h`, Fil-C runtime docs | Sanitizer poisoning/annotation, contract-in-signature, defer effects | **Not implemented here**: compiler/runtime ownership belongs to Agent 1; Cake license rejected | `SANITIZER_MATRIX.md`, `AGENT1_COMPILER_GAPS.md` |

## 4. Code actually ported

1. `memset_explicit` — previously copied into this repository from LLVM libc
   `llvmorg-20.1.1`, Apache-2.0 WITH LLVM-exception, with a glibc duplicate-provider
   guard. Provenance recorded in `docs/libc/C23_MATH_DEPENDENCY_CLOSURE.md` and
   related inventory docs. Within §§1–9 this was the only verbatim upstream
   code; the later safeclib integration (I1 in §10.3) also ports upstream bodies
   verbatim under the MIT license, with notices reproduced.
2. safeclib / Intel SafeStringLib sources are vendored and compiled by
   `scripts/build_safecrt.sh` (40 integrated APIs, 10 adapter-only, 387 not
   integrated). No new upstream source was added in this work.
3. No Microsoft STL/GSL/SaferCPlusPlus/Cake/Fil-C/**TrapC**/**SoftBoundCETS**
   source was copied. Their techniques were adapted as documented above.
   TrapC had no source to copy (§1 row 8); SoftBoundCETS source was fetched and
   read, but only its check/range-predicate ideas were paraphrased (§1 row 9).
4. **Explicitly NOT copied because they would break the C ABI:** Fil-C's
   fat/capability pointer representation and custom ABI, and SoftBoundCETS's
   shadow metadata (mmap'd two-level trie keyed by pointer storage address,
   shadow-stack calling convention, temporal key/lock words, free map) were
   deliberately **not** ported. TrapC's managed-pointer hidden RTTI and no-op
   `free`/`delete` were not adopted for the same reason. C+ keeps ordinary
   one-word `T *`, unchanged C signatures and additive `static inline` helpers
   in `stdlib/error.hp` and the new `stdlib/memory.hp`.

## 5. Why some code was written independently

C+ `stdlib/*.hp` is a header-only, C-compatible API surface with no exceptions,
no templates/concepts, a checked-allocator/error-object model, and a compiler
that owns lifetime enforcement. Full STL/GSL containers cannot be copied
without Agent 1 language features; Cake is license-incompatible; Fil-C's
capabilities are compiler/runtime features. Where upstream designs were
applicable (overflow checks, rollback guards, bounds contracts, metadata
queries), they were adapted and covered by the C+ tests listed above.

The two new studies reinforce this boundary: TrapC's managed-pointer RTTI and
SoftBoundCETS's shadow metadata both depend on compiler/runtime support and
would change the C ABI, so only the boundary-preserving parts — a generation
token, a subtract-form range predicate, fail-closed "unknown" results and
base-address-keyed side-table lookup — were adapted into `stdlib/error.hp` and
the new `stdlib/memory.hp` instead of the upstream mechanisms.

## 6. Upstream tests adapted

No upstream test file was copied verbatim. Cases adapted as ideas: MS STL
`GH_002030_asan_annotate_vector/string` (capacity annotations) → UBSan/strict
deep tests; `GH_005472_do_not_overlap` → `deep_string_append.cp` self/overlap
cases; GSL `span_tests.cpp` bounds cases → `at_checked` boundary tests;
SaferCPlusPlus `msevector_test.h` lifetime cases → generation/slice tests;
Cake `defer`/`flow3` ownership cases → guard/early-return tests (idea only,
license rejected for code). Exact adapted case lists are in the per-source
study docs.

For the two new studies the answer is the same with one clarification: TrapC has
no published source and therefore no upstream test suite to adapt; the
SoftBoundCETS `tests/` tree was **[NOT INSPECTED]** by its study, and no
SoftBoundCETS test file was copied or executed. Their ideas are covered only by
locally written tests added by this task: `tests/stdlib/deep_memory_bounds.cp`
(capacity/count queries; zero-length, exact-fit and one-over read/write cases;
NULL and untracked pointers; interior access via explicit `(base, offset)`;
stale generation tokens; borrow/transfer/release; logging-off/on parity) and
`tests/stdlib/agent2/abi_safety_probe.cp` plus `tests/abi/abi_compat_*` (pointer
sizes, allocation metadata queries and ABI neutrality). No pass/fail result for
these new files is asserted here; the recorded evidence and its limits are in
`docs/stdlib/abi-preserving-runtime-safety.md`.

## 7. C+ regression tests passed

The aggregate runner `tests/stdlib/agent2/run_all.sh` executed 51 tests
(28 `deep_*.cp`, 11 `agent2/*.cp`, 11 curated existing tests) with
`--strict --sanitize=none`, timeouts, stdin fixtures and byte-exact output
fixtures: **50 PASS, 0 FAIL, 0 BUILD_FAILURE, 0 TIMEOUT, 0 SKIPPED, 0 BLOCKED**.
The three-mode deep runner executed 28 tests × 3 modes = 84 invocations with
0 failures. `tests/run_all_evidence.sh` ran 23 aggregate suites with 0 failures.
`SANITIZER_MATRIX.md`: 72 builds attempted, 27 executed, 23 PASS, 4 genuine
`-fsanitize=integer` findings in runtime hashing (outside Agent 2 scope), and
ASan/Leak/Thread/Memory/CFI unsupported by the backend probe.

## 8. Upstream features still unsupported in C+

- STL/GSL iterator + debug-iterator machinery (no iterators in C+ containers).
- GSL `span`/`not_null`/`narrow` as generic templates (needs Agent 1).
- SaferCPlusPlus full scope/lifetime tracking and thread-safe shared pointers.
- Cake ownership contracts and two-level defer (license rejected; compiler work).
- Fil-C capabilities, GC/safepoints, custom ABI (compiler/runtime, Agent 1).
- MSVC CRT Annex K names/`invalid_parameter_handler` semantics (C+ uses
  `error *` and `stdlib_note` instead).
- Sanitizer container annotations and manual ASan poisoning: evaluated and
  rejected after audit (distinct host allocations; see `docs/libc/` audits).
- LLVM C23 math closure (23 blocked APIs) — no dependency-closed source.
- TrapC managed pointers, hidden RTTI, `testof`/`offsetof`/`lastof`,
  compiler-determined scope-exit freeing, no-op `free`/`delete`, trap-aware
  POSIX wrappers and `trap`/`trap.return`/`alias`/`new`/`nullptr` (Agent 1
  language/runtime; **no TrapC source is published**).
- SoftBoundCETS's mmap'd two-level trie (keyed by pointer *storage* address),
  shadow stack with slot 0 as return value, temporal key/lock space, free map,
  8-byte slot granularity, 64-bit-only virtual reservations, the LLVM 3.4 IR
  instrumentation pass / `-fsoftboundcets`, `main` wrapping and symbol
  renaming. Its hard `abort()`/`backtrace()` policy, approximate `+1024`
  bounds and the documented `size > dest_bound` `memcpy` typo were explicitly
  **not** copied.
- Interior-pointer owner lookup: `stdlib_meta_find` matches exact base
  addresses only, so C+ cannot answer TrapC's "which allocation owns this
  advanced pointer?" query; `stdlib/memory.hp` requires callers to pass
  `(base, offset)` instead of an interior pointer alone.

## 9. ABI-preserving safety studies added in this update

The three inputs named for this update were **unavailable when the earlier
sections of this document were authored**, but landed during the task and were
read before this edit: `docs/stdlib/abi-preserving-runtime-safety.md`,
`docs/stdlib/upstream_study/trapc.md` and
`docs/stdlib/upstream_study/softboundcets.md`. No claim is made here about
content that was not read.

The ABI-preserving design actually in the tree: `stdlib/error.hp` keeps an
open-addressed side table (`stdlib_meta_slot` = `ptr`, `byte_size`, `elem_size`,
`generation`) and exposes `stdlib_alloc_size`, `stdlib_alloc_query_length`,
`stdlib_alloc_generation` and `stdlib_alloc_check_generation`;
`stdlib/memory.hp` (new) is a thin, additive bounds-aware wrapper layer over
that table. Neither widens or tags a pointer, renames a public symbol or
changes a C signature. The ABI evidence and its limits are recorded in
`docs/stdlib/abi-preserving-runtime-safety.md` (70/70 layout assertions on both
toolchains for the pre-existing `tests/abi/abi_layouts.c` probe; symbol export,
calling convention and Windows x86 ABI explicitly **unverified**).

**Non-copies (explicit):** Fil-C's pointer representation (fat/capability
pointers, custom ABI, mandatory GC) and SoftBoundCETS's metadata (mmap'd
two-level trie, shadow stack, key/lock words, free map) were **NOT copied**
because either would break the C ABI. TrapC's managed-pointer hidden RTTI was
not copied for the same reason, and no TrapC source exists to copy. Only ideas
that fit a thin-pointer, unchanged-signature C surface were adapted, as
recorded in §§3–4.

---

## 10. Integration results from the parallel sibling agents (2026-11-23)

This section is the UPDATE requested for the required document. It replaces
"design reference only" / "ideas only" language in §§1–9 with **actual
integration results** wherever a sibling agent changed a source file and
executed a test. Nothing here is claimed from a build alone or from a study
alone.

### 10.1 Evidence standard and status of sibling documents

- All **nine** sibling notes under `docs/stdlib/upstream_reuse/` and
  `tests/stdlib/upstream_reuse/` were present and read in full before this edit:
  `SAFECRT_INTEGRATION.md`, `alloc_policy_NOTES.md`, `filc_diag_NOTES.md`,
  `gsl_adapt_NOTES.md`, `json_yyjson_NOTES.md`, `lifetime_adapt_NOTES.md`,
  `softbound_NOTES.md`, `string_stl_NOTES.md`, `vector_stl_NOTES.md`.
  **No sibling document is PENDING.**
- Every INTEGRATED row below satisfies the rule: a **changed source file**
  (new or modified header / `.c`) and an **executed regression test** with a
  recorded self-checking result. The author of this document independently
  re-compiled and re-ran all nine test programs on 2026-11-23; the results in
  §10.5 are from those independent runs, not only from the sibling notes.
- The changed files were landed by another agent in local commit
  `ce018ca3fba50e59d1ecdf4f3d926e2b7eaf2ad9` ("Integrate proven upstream
  implementations into C+ stdlib"). This document was not changed by that
  commit; its only edit is this section.
- Toolchain caveat: at verification time the C+ driver failed to build any
  instrumented runtime with `CPLUS_EXIT_ARITHMETIC` undeclared in
  `c-plus-core/runtime/cplus_runtime.c` (a cross-repo header/runtime skew
  documented in `docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md`). The
  environment-only workaround `CPLUS_INCLUDE=.../c-plus-core/runtime/include`
  (used for the independent re-runs) changes no source and relaxes no check.
  Canonical `--sanitize=undefined` results below are therefore quoted from the
  sibling notes, not re-asserted by this document where the note itself marks
  them blocked.

### 10.2 INTEGRATED — summary

| # | Component (sibling note) | Upstream + pin | License | Changed C+ source file(s) | Test (independent re-run) | ABI-visible API change |
|---|---|---|---|---|---|---|
| I1 | safeclib `bsearch_s`/`memrchr_s` | `rurban/safeclib` @ `90b076e94c4b0fb202dddf948d5d620e5395c1b0` | MIT | `libc/src/string/upstream_safecrt.c` (new), `libc/install-include/cplus_safecrt_upstream.h` (new), `tests/libc/build_libc.sh` (wiring, by parent) | `test_safecrt_upstream`: all checks passed, rc 0 | **None** — two new prefixed symbols only |
| I2 | mimalloc size-overflow / size-class primitives | `microsoft/mimalloc` @ `c683b7c60c91cb2809977e37d3f4d084d7497180` (`main`; raw `master` path byte-identical) | MIT | `stdlib/alloc_policy.hp` (new) | `ALLOC_POLICY 42 / ALLOC_POLICY_FAILURES 0`, rc 0 | **None** — new `static inline` scalar helpers only |
| I3 | Fil-C liveness/bounds queries | `pizlonator/fil-c` (branch `deluge`) @ `a80e32804fd44d295deb1ce1b0098962022da0a3` | BSD-2-Clause-style (Epic Games / Filip Pizlo; libpas BSD-2-Clause, Apple) | `stdlib/allocator.hp` (modified, +108 lines) | `ALLOC_DIAG 26 0`, rc 0 | **None** — read-only `static inline` queries added |
| I4 | Microsoft GSL `narrow` / `not_null` / `span` | `microsoft/GSL` `main`, per-file touches `417ef68` (`span`), `a75212b` (`pointers`), `08687fb` (`narrow`) | MIT | `stdlib/gsl_adapt.hp` (new) | `GSL_ADAPT 80 0`, rc 0 | **None** — new `gsl_*` names; pointer sizes asserted unchanged |
| I5 | yyjson depth/length budgets | `ibireme/yyjson` @ `be9bd74bb3c96e00006c2075c330a6b8b90123dc` (v0.13.0) | MIT | `stdlib/json.hp` (modified, +122 lines) | `JSON_YYJSON 35 0`, rc 0 | **None** — additive `json_parse_budget`, `read_size()`, `value_count()`; constructor signature and single-member layout unchanged |
| I6 | SaferCPlusPlus lifetime tokens | `duneroadrunner/SaferCPlusPlus` @ `5edaf67acf746d9ee472a06a51094b31a3db57c0` | BSL-1.0 | `stdlib/lifetime_adapt.hp` (new) | `LIFETIME_ADAPT 25 0`, rc 0 | **None** — two new additive value descriptors; no `T*` redefinition |
| I7 | SoftBoundCETS check predicates | `santoshn/softboundcets-34` @ `9a9c09f04e16f2d1ef3a906fd138a7b89df44996` | BSD-3-Clause tree / per-file MIT-style | `stdlib/memory.hp` (modified, +130 lines) | `SOFTBOUND 51 0`, rc 0 | **None** — `static inline` helpers on the existing side table; no new symbol |
| I8 | Microsoft STL string growth / overlap-safe append | `microsoft/STL` @ `0cabaf3c7e0a73fe3040aabce40cd8833e80392b` | Apache-2.0 WITH LLVM-exception | `stdlib/string.hp` (modified) | `upstream-string-checks 28 / upstream-string-failures 0`, rc 0 | **None** — same 3 fields and method signatures; new helper additive |
| I9 | Microsoft STL vector growth / commit-last guards | `microsoft/STL` @ `0cabaf3c7e0a73fe3040aabce40cd8833e80392b` | Apache-2.0 WITH LLVM-exception | `stdlib/vector.hp` (modified) | `VECTOR_STL 14 0`, rc 0 | **None** — two new helpers; existing methods and capacity contract unchanged |

### 10.3 INTEGRATED — per-record detail

**I1 — safeclib `bsearch_s` / `memrchr_s` (`docs/stdlib/upstream_reuse/SAFECRT_INTEGRATION.md`)**
- Upstream URL: <https://github.com/rurban/safeclib>; pin
  `90b076e94c4b0fb202dddf948d5d620e5395c1b0`; provenance from
  `third_party/safeclib/{UPSTREAM_COMMIT,PROVENANCE.md,COPYING}`. License **MIT**
  (© 2012–2013 Cisco Systems; © 2017 Reini Urban); notices reproduced verbatim.
- Source paths: `third_party/safeclib/src/misc/bsearch_s.c` (body
  `_bsearch_s_chk`) and `third_party/safeclib/src/extmem/memrchr_s.c`
  (`_memrchr_s_chk` → `_memrchr_s_uchk`, portable `HAVE_MEMRCHR`-undefined loop
  kept byte-for-byte). Neither was in the existing `scripts/build_safecrt.sh`
  compile list.
- Copied/adapted C+ files: `libc/src/string/upstream_safecrt.c` (ported bodies,
  self-contained: no `safeclib_private.h`, no archive symbols),
  `libc/install-include/cplus_safecrt_upstream.h` (public declarations,
  `CPLUS_SC_UPSTREAM_` error constants), `tests/libc/build_libc.sh` (parent
  wiring: added `-I.../install-include` and the new `.c`).
- API changes: **none to any existing ABI**; only the two new global symbols
  `cplus_sc_upstream_bsearch_s` and `cplus_sc_upstream_memrchr_s` (confirmed by
  `nm -g`). Public entry points drop upstream's trailing `basebos`/`destbos`
  object-size parameters; BOS branches and constraint handlers become a local
  no-op, `errno` is set at the same sites as upstream.
- Regression test: `tests/libc/test_safecrt_upstream.c` (success, not-found,
  context threading, NUL-byte search, every constraint path). Independent
  re-run: clean C99 `-Wall -Wextra -pedantic` builds, `test_safecrt_upstream:
  all checks passed`, rc 0, linked beside `build/libc/libcplus_hardened.a`.
- Performance: none recorded.

**I2 — mimalloc primitives (`tests/stdlib/upstream_reuse/alloc_policy_NOTES.md`)**
- Upstream URL: <https://github.com/microsoft/mimalloc>; pin
  `c683b7c60c91cb2809977e37d3f4d084d7497180` on `main` (
  `git ls-remote ... refs/heads/main`; no `master` ref exists — raw `master`
  and `main` return byte-identical files, ETag
  `ee4783ccfb7974c80e563c52d3fb3e374c7f16fc5dff5b5548cc0da8bafaf5d9`).
  License **MIT** (© 2018–2025 Microsoft Corporation, Daan Leijen); full notice
  reproduced in the header.
- Source paths: `include/mimalloc/internal.h` (`_mi_align_up`:405–410,
  `_mi_wsize_from_size`:463–466, `mi_mul_overflow`:474–489,
  `mi_count_size_overflow`:493–506; ETag
  `b8154bfad2f738d586ce2aeb7d076d152473604b17f8552144efd1158930ecaf`),
  `src/alloc.c` (call sites :132, :221–224, :268–271). The brief's
  `src/prim/prim.h` path 404s; `internal.h` was used instead.
- Original implementation → C+: quick-path `count==1`, `__builtin_umul*`
  overflow test, `*total = SIZE_MAX` on overflow, `mask & (size+mask)` rounding,
  `(size+w−1)/w` word ceiling, zero-size normalization.
- Copied/adapted C+ files: `stdlib/alloc_policy.hp` (new, 177 lines,
  `stdlib_count_size_overflow`, `stdlib_size_class_round_up`,
  `stdlib_size_class_words`, `stdlib_size_normalize_zero`).
- API changes: **none** — only `static inline` scalar helpers with caller-owned
  out-params; no struct, enum, global, exported symbol, allocator state or
  existing file touched. NULL-out and wrap cases return a failure code instead
  of upstream's assert-only preconditions.
- Regression test: `alloc_policy.cp` (SIZE_MAX boundaries, exact fits, zero
  normalization, size-class/word rounding, composed short-circuit pipeline).
  Independent re-run: `ALLOC_POLICY 42 / ALLOC_POLICY_FAILURES 0`, rc 0; sibling
  also recorded a clean UBSan run with the `CPLUS_INCLUDE` workaround.
- Performance: none recorded.

**I3 — Fil-C liveness queries (`tests/stdlib/upstream_reuse/filc_diag_NOTES.md`)**
- Upstream URL: <https://github.com/pizlonator/fil-c>, branch `deluge`, pin
  `a80e32804fd44d295deb1ce1b0098962022da0a3` ("Versioning", 2026-10-23;
  obtained from the public Atom feed because the REST API 403'd). License
  **BSD-2-Clause-style** (© 2023–2026 Epic Games, Inc.; © 2026 Filip Pizlo),
  libpas `LICENSE.txt` also © 2018–2022 Apple Inc.
- Source paths: `filc/include/stdfil.h` (`FILC_VERSION 687u`; free-bit/free-
  singleton docs at :146–159; `zhasvalidcap` :214; `zinbounds` :219–222;
  `zvalinbounds` :224–231) and `filc/src/runtime.c`.
- Original implementation → C+: `zhasvalidcap`'s "valid, non-free capability"
  question and `zvalinbounds`' overflow-safe `size <= upper - ptr` predicate,
  mapped onto the existing `stdlib/error.hp` side table (live slot ↔
  `generation != 0`; free flag ↔ `STDLIB_META_TOMB` + zeroed fields).
- Copied/adapted C+ files: `stdlib/allocator.hp` (modified, +108 lines):
  `STDLIB_ALLOC_CAP_NONE/LIVE`, `stdlib_alloc_cap_state`,
  `stdlib_alloc_hasvalidcap`, `stdlib_alloc_cap_covers`,
  `stdlib_alloc_cap_check`.
- API changes: **none** — all `static inline`, fail-closed, read-only; no field,
  allocation, global state, pointer widening or ABI change. Deliberate
  deviation: a zero-length probe still requires a live base (state query, not
  a pure bounds query), and a freed address is indistinguishable from a never-
  allocated one by address alone (generation identity closes that gap).
- Regression test: `filc_diag.cp` (live/freed/recycled state, bounds edges,
  invalid refusals). Independent re-run: `ALLOC_DIAG 26 0`, rc 0; sibling also
  recorded byte-identical strict/UBSan stdout (SHA-256
  `1c9ca50093f57769aa111490cfb586861a2553fe79d0ab4b9f53204ee72bf305`) and
  `allocator_test` `ALLOCATOR_TEST 21 0` as a regression guard.
- Performance: none recorded.

**I4 — Microsoft GSL `narrow` / `not_null` / `span`
(`tests/stdlib/upstream_reuse/gsl_adapt_NOTES.md`)**
- Upstream URL: <https://github.com/microsoft/GSL>, `main`; per-file commit
  touches `417ef68` (`include/gsl/span`), `a75212b` (`include/gsl/pointers`),
  `08687fb` (`include/gsl/narrow`) from the GitHub history pages (REST API
  403'd). License **MIT** (© 2015 Microsoft Corporation), notice reproduced.
- Source paths / original implementation: `narrow`'s three-stage
  representability check (static-cast-defined preflight, cast, round-trip
  equality, sign check); `pointers`' deleted null constructor/operators and
  `Expects(ptr_ != nullptr)`; `span`'s `Expects(idx < size())`,
  `Expects(count <= size())`, `Expects((size() >= Offset) && (Count == ...))`,
  `Expects(data || size() == 0)`.
- Copied/adapted C+ files: `stdlib/gsl_adapt.hp` (new, 434 lines) with
  `gsl_narrow_*`, `gsl_not_null_i32/void`, `gsl_span_i32`. Failures are
  `STDLIB_FAIL` + `STDLIB_CATEGORY_{RANGE,INVALID,BOUNDS}` instead of throws;
  the span descriptor adds two provenance fields solely so strict-mode range
  checks can be satisfied.
- API changes: **none** — new names only; header asserts
  `sizeof(int*) == sizeof(void*)` and `sizeof(gsl_not_null_i32) == sizeof(int*)`;
  no existing header, runtime or compiler file edited.
- Regression test: `gsl_adapt.cp` (narrow boundaries/refusals, not_null
  make/get/swap/zero-overhead, span make/at/first/last/subspan/nested/ABI).
  Independent re-run: `GSL_ADAPT 80 0`, rc 0; sibling recorded a clean strict
  check, byte-identical no-sanitizer/UBSan stdout (81 lines) and confirmed
  UBSan symbols in the instrumented binary.
- Performance: only a qualitative "zero overhead" assertion for `not_null`
  (single-pointer struct); no timing measurement.

**I5 — yyjson parse budgets (`tests/stdlib/upstream_reuse/json_yyjson_NOTES.md`)**
- Upstream URL: <https://github.com/ibireme/yyjson>; pin
  `be9bd74bb3c96e00006c2075c330a6b8b90123dc`, v0.13.0. License **MIT**
  (YaoYuan 2020). Vendored files: `third_party/yyjson/{yyjson.c,yyjson.h,LICENSE}`.
- Source paths / original implementation: `YYJSON_READER_DEPTH_LIMIT` (default
  0, i.e. disabled) at `yyjson.h:113–117` and `yyjson.c:350–356`; `ctn_depth`
  accounting at `yyjson.c:5528–5562`; `yyjson_read_opts()` NULL/zero-length and
  `len >= USIZE_MAX - YYJSON_PADDING_SIZE` guards at `yyjson.c:6396–6417`;
  upstream tests `test/test_err_code.c` and `test/test_json_reader.c` at the
  pin (read over raw GitHub, not vendored).
- Copied/adapted C+ files: `stdlib/json.hp` (modified, +122 lines):
  `CPLUS_JSON_MAX_DEPTH` (16384) / `CPLUS_JSON_MAX_INPUT_LENGTH` (64 MiB),
  `json_input_within_budget()` lexically counting containers outside strings,
  the constructor's pre-parse budget check, plus additive `json_parse_budget`,
  `json::read_size()`, `json::value_count()`.
- API changes: **none** — `json` still holds exactly one `yyjson_doc *`; all
  existing method signatures, error codes and call sites unchanged; defaults
  exceed every checked-in fixture.
- Regression test: `json_yyjson.cp` (direct upstream error codes, malformed/
  truncated prefixes, depth budget incl. escapes, explicit length/`SIZE_MAX`,
  large in-budget input). Independent re-run (with
  `-Ithird_party/yyjson -Lbuild/ext -lyyjson`): `JSON_YYJSON 35 0`, rc 0;
  sibling re-ran `json_test`, `deep_json_task5` (`JSON_TASK5 23 0`),
  `hostile_json` (`HOSTILE_JSON 29 0`) and `re_json_nesting`
  (`RESOURCE_EXHAUSTION 7 0`) without regression.
- Performance: no C+ before/after timing; sibling compiled the vendored parser
  directly under `clang -fsanitize=undefined,alignment` and reported
  `YYJSON_UBSAN_SMOKE ok` on a corpus including 16385 nesting levels (driver
  and objects kept outside the repo).

**I6 — SaferCPlusPlus lifetime tokens (`tests/stdlib/upstream_reuse/lifetime_adapt_NOTES.md`)**
- Upstream URL: <https://github.com/duneroadrunner/SaferCPlusPlus>, `master`
  pin `5edaf67acf746d9ee472a06a51094b31a3db57c0`. License **BSL-1.0**
  (© 2015 Noah Lopez), full text quoted in the note.
- Source paths / original implementation: `include/msescope.h` (`TScopeID` :98,
  `lifetime_info1_t`/`no_lifetime_info1` :140–141, `TXScopeOwnerPointer`/
  `make_xscope_owner` :983–993, :2985–2987) and
  `include/msepointerbasics.h` (`TPointer` token tag :1508–1516, guarded
  `operator*` :1524–1528, scope marker bases :876–913).
- Copied/adapted C+ files: `stdlib/lifetime_adapt.hp` (new, 268 lines):
  `lifetime_token_t`/`LIFETIME_NO_TOKEN`, `lifetime_owner`,
  `lifetime_borrow` (`{void *ptr; lifetime_token_t token;}`), `lifetime_check`,
  `lifetime_read/write`, `lifetime_transfer`, `lifetime_owner_release`. The
  static type-level token becomes a runtime generation token checked against
  `stdlib/error.hp` metadata **before** any byte is touched.
- API changes: **none** — two new additive value descriptors; ordinary `T *`
  untouched, no allocator/linker/compiler edits.
- Regression test: `lifetime_adapt.cp` (valid borrow, stale refusal with no
  stale dereference, transfer invalidation, double-release refusal).
  Independent re-run: `LIFETIME_ADAPT 25 0`, rc 0; sibling recorded identical
  strict and UBSan stdout with a temporary `CPLUS_CLANG` wrapper adding
  `-DCPLUS_EXIT_ARITHMETIC=110`.
- Performance: none recorded.

**I7 — SoftBoundCETS check predicates (`tests/stdlib/upstream_reuse/softbound_NOTES.md`)**
- Upstream URL: <https://github.com/santoshn/softboundcets-34>, `master` pin
  `9a9c09f04e16f2d1ef3a906fd138a7b89df44996` (REST metadata 403'd; pin verified
  by `git ls-remote`). License: top-level `LICENSE` **BSD-3-Clause**
  (© 2013 Santosh Nagarakatte, unfilled `{organization}` placeholder in clause
  3) plus per-file **MIT-style** notice (© 2014 Nagarakatte / Martin, UPenn).
- Source paths / original implementation: `softboundcets-lib/softboundcets.h`
  (metadata record `{base,bound,key,lock}` :61–105; spatial check :539/:555;
  subtract-form bulk checks :576/:620/:703 incl. the `src` `dest_bound` typo
  that was **not** copied; temporal `*lock != key` :1004; `*lock = 0` on free
  :1078; key counter :1182) and `softboundcets-checks.c` (only includes the
  header).
- Copied/adapted C+ files: `stdlib/memory.hp` (modified, +130 lines):
  `stdlib_mem_bounds`, `stdlib_mem_span_ok`, `stdlib_mem_span_contains`,
  `stdlib_mem_key_current`, `stdlib_mem_query` (plus `stdlib_mem_token`).
- API changes: **none** — `static inline` helpers on the existing side table;
  capacities come from `stdlib/error.hp`, not fat pointers; no trie, shadow
  stack, lock allocator, free map, `abort()` policy, symbol renaming or pointer
  widening. Interior pointers still require callers to pass `(base, offset)`.
- Regression test: `softbound.cp` (bounds query, exact fit, one-over with
  sentinel-byte checks, interior pointer, stale generation after transfer and
  realloc). Independent re-run: `SOFTBOUND 51 0`, rc 0 under
  `--strict --sanitize=none` after the sibling replaced a raw `p[i]` loop with
  `memcmp`; sibling also reported `deep_memory_bounds` `61 0`,
  `mo_ownership_lifetime` `48 0` and `f100_mem_guarded` 1200 cases ok.
- Performance: none recorded. Canonical UBSan blocked by the external runtime
  skew (no UBSan pass/fail claimed).

**I8 — Microsoft STL string growth / append
(`tests/stdlib/upstream_reuse/string_stl_NOTES.md`)**
- Upstream URL: `microsoft/STL` pin
  `0cabaf3c7e0a73fe3040aabce40cd8833e80392b`; files
  `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/{xstring,xmemory,filesystem}`
  and `LICENSE.txt`. License **Apache-2.0 WITH LLVM-exception**.
- Source paths / original implementation: `_Calculate_growth` (~2942),
  `_Reallocate_for`/`_Reallocate_grow_by` (~2960–3022, allocate-before-retire),
  `_Append` (~3046–3072, `_Traits_move_batch` in-place move), `_Insert`/`_Replace`
  overlap splitting. `_Alloc_temporary2` was searched for and **does not occur**
  in the fetched headers, so no claim is made about it (this corrects the
  "overlap-safe temporary" wording in §3).
- Copied/adapted C+ files: `stdlib/string.hp` (modified): new
  `stdlib_string_calculate_growth(requested, old_space)` (`STDLIB_ALLOC_LIMIT`
  replaces `max_size()`, mask 0, 2x factor retained, saturation settles at the
  exact request) and a rewritten `string::append(const char *)` that uses
  `__builtin_memmove` on the fit path and copies from the still-live old buffer
  on the growth path — removing the earlier manual `uintptr_t` alias window.
- API changes: **none** — same three fields, method names/signatures/return
  codes and telemetry; additive helper only.
- Regression test: `string_stl.cp` (self-append across reallocation, repeated
  growth to 64 bytes, interior suffixes with/without growth, alias-after-
  `clear()`, hard-limit refusal with zero allocations, near-limit exact growth,
  injected allocation-failure atomicity). Independent re-run:
  `upstream-string-checks 28 / upstream-string-failures 0`, rc 0; sibling also
  re-ran `deep_string_append`, `deep_string_task4` (`86 0`), `overflow_string`,
  `af_string` (`26 0`) and `deep_string_view` without regression.
- Performance: none recorded (a capacity-growth correctness contract, not a
  throughput claim).

**I9 — Microsoft STL vector growth / guards
(`tests/stdlib/upstream_reuse/vector_stl_NOTES.md`)**
- Upstream URL: `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/{vector,xmemory}`
  at inherited pin `0cabaf3c7e0a73fe3040aabce40cd8833e80392b`; license
  **Apache-2.0 WITH LLVM-exception**. (The raw fetch itself shows no commit
  hash; the pin is inherited from `upstream_study/microsoft_stl.md`.)
- Source paths / original implementation: `_Calculate_growth` :2009–2025
  (subtraction-form geometric overflow guard, demand floor), `_Get_size_of_n`
  (`xmemory`:111–122, `_Count > (size_t)-1/_Ty_size`), `max_size()` :1920–1923,
  `_Allocation_guard` :443–456 / `_Reallocation_guard2` :628–643, `_Reallocate`
  :1639–1671 (allocate/construct before publish).
- Copied/adapted C+ files: `stdlib/vector.hp` (modified): `vector_allocation_bytes`
  and `vector_calculate_growth`, explicit `used >= max_count` refusals, and
  commit-last mutation in `reserve`/`copy_from`/`shrink_to_fit`. Three
  `size_t bytes = 0;` initialisers were added because the strict analyser does
  not infer writes through out-pointers. The **1.5x multiplier was deliberately
  not copied** — C+ keeps 2x and min-4, pinned by `deep_error_vector.cp` and
  `deep_vector_task4.cp`.
- API changes: **none** — two additive helpers; existing methods, signatures
  and documented capacity sequence unchanged.
- Regression test: `vector_stl.cp` (14 checks: `_Get_size_of_n` exact/overflow/
  zero, growth seed/geometric/floor/half-boundary/saturation/ceiling, capacity
  sequence 4→8→16, reserve overflow atomicity, commit-last failure/retry,
  deterministic LCG model). Independent re-run: `VECTOR_STL 14 0`, rc 0;
  sibling also re-ran `deep_error_vector` (byte-identical to `.expected`) and
  `deep_vector_task4` (`VECTOR_TASK4 16 0`).
- Performance: none recorded; canonical UBSan/integer sanitizers blocked by the
  external runtime skew (no sanitizer pass/fail claimed).

### 10.4 RESEARCH-ONLY / REJECTED — no integration claimed

These rows have a study or a documented decision but **no changed source file
and no executed integration test**; they stay non-integrated exactly as §§1–9
recorded:

| Source | Status | Why it is not an integration |
|---|---|---|
| Cake | **REJECTED** | `GPL-3.0-or-later` + AI-training-use addendum; no Cake source/header/test copied; idea-level traceability only. |
| TrapC | **RESEARCH-ONLY / no source** | No repository, commit, tag, LICENSE or SPDX; license UNDETERMINED; managed-pointer RTTI/no-op `free` would change the ABI. Only documentation read. |
| Fil-C pointer representation (InvisiCaps, GC, safepoints, `pizlonated_` mangling) | **REJECTED for port** | ABI/pointer-contract change; only the liveness/bounds *query vocabulary* was adapted (I3), never the representation. |
| SoftBoundCETS trie, shadow stack, key/lock space, free map, `-fsoftboundcets`, `main` wrapping, symbol renaming, `abort()` policy | **REJECTED for port** | Requires an instrumenting compiler and changes the calling convention; only check predicates adapted (I7), with the upstream `src` typo and `+1024` approximate bounds explicitly excluded. |
| LLVM/Clang sanitizer interfaces and compiler/runtime work | **RESEARCH-ONLY** | Existing toolchain only; `memset_explicit` was copied before this task (§4.1), no new upstream source added. |
| MS STL 1.5x growth, `_Alloc_mask`, iterator-proxy/orphan machinery, exceptions/RAII guards | **REJECTED for port** | Would break pinned capacity fixtures or require Agent 1 language features; not adapted (I8/I9). |
| GSL generic `span<T,E>`/`not_null<T>`/`narrow<T,U>` templates, `Expects` terminate | **RESEARCH-ONLY** | Re-expressed as concrete C+ value descriptors/fail-closed errors (I4); no generics in C+. |
| SaferCPlusPlus static scope-pointer enforcement, registered/norad pointer machinery | **RESEARCH-ONLY** | C17 has no type-level tokens; runtime generation token adapted (I6), static scpptool checks not reproduced. |
| yyjson implementation source / upstream test files | **RESEARCH-ONLY** | Vendored parser reused as-is; only budget technique adapted (I5); no upstream test file copied. |
| mimalloc allocator itself | **RESEARCH-ONLY** | Only four overflow/size-class predicates adapted (I2); no allocator code or state copied. |
| Performance/benchmark artifacts (`benchmarks/safety_compare/`) | **NOT AN INTEGRATION** | Cross-language speed/RSS comparison committed alongside the integrations; no upstream source was integrated by those files and they are not evidence for any row in §10.2. |

### 10.5 ABI/API change ledger and verification

- **No ABI-visible API was changed by any integration.** Every change is either
  a new header of `static inline` helpers / additive value descriptors, or an
  additive change to an existing header that leaves every existing public
  signature, struct layout, error code and telemetry point intact. I1 adds two
  new prefixed C symbols; I2, I3, I4, I6, I7 add no exported symbol; I5, I8, I9
  add additive names only. This is consistent with the thin-pointer verdict in
  §9: raw `T *` stays one word, no pointer is widened or tagged, no public C
  signature changes.
- Independent re-verification by this document's author on 2026-11-23
  (`build/c+`, `CPLUS_INCLUDE=.../c-plus-core/runtime/include`, timeout-
  protected, outputs captured outside the repo):

| Test | Build | Run rc | Self-check summary |
|---|---|---|---|
| `test_safecrt_upstream.c` (C99 clang, linked with `libcplus_hardened.a`) | ok | 0 | `all checks passed` |
| `alloc_policy.cp` (`--strict --sanitize=none`) | ok | 0 | `ALLOC_POLICY 42 / FAILURES 0` |
| `filc_diag.cp` (`--strict --sanitize=none`) | ok | 0 | `ALLOC_DIAG 26 0` |
| `gsl_adapt.cp` (`--strict --sanitize=none`) | ok | 0 | `GSL_ADAPT 80 0` |
| `json_yyjson.cp` (`--strict --sanitize=none`, `-Ithird_party/yyjson -Lbuild/ext -lyyjson`) | ok | 0 | `JSON_YYJSON 35 0` |
| `lifetime_adapt.cp` (`--strict --sanitize=none`) | ok | 0 | `LIFETIME_ADAPT 25 0` |
| `softbound.cp` (`--strict --sanitize=none`) | ok | 0 | `SOFTBOUND 51 0` |
| `string_stl.cp` (`--strict --sanitize=none`) | ok | 0 | `28 / 0` |
| `vector_stl.cp` (`--strict --sanitize=none`) | ok | 0 | `VECTOR_STL 14 0` |

  Total: **9/9 integrations independently built and executed, 0 failures.**
  These are contract-conformance results for bounded, deterministic inputs,
  not proofs of memory safety. Canonical `--sanitize=undefined` is blocked by
  the external `c-plus-core` runtime skew; where a sibling obtained UBSan
  coverage via an env-only/flag-only bridge, that result is attributed above
  to the sibling note and not re-asserted here.

### 10.6 Performance measurements

None of the nine integration notes records a component-specific before/after
performance measurement. The only quantitative performance work landed in the
same commit is the separate cross-language suite in `benchmarks/safety_compare/`
(median/p95 wall time and peak RSS for `alloc_bench`, `bench_modern`, `modern`,
`loop`, `nested`, `safety_matrix`; e.g. `alloc_bench` C+ `0.039720 s` median vs
Rust `0.010404 s`, both with matching checksum
`4750706934088118880`). Those artifacts compare C+ with Rust and are explicitly
not upstream-reuse integrations; they are listed here only so this document
does not silently omit available performance data. No speed claim is made for
any of I1–I9.

### 10.7 PENDING status

No sibling note is missing: all nine expected integration notes were present
and read before this edit, and all nine have a changed source file and an
executed test (§10.1, §10.5). Sources with no integration are classified in
§10.4 rather than marked PENDING. This document itself is the only file
modified by this task; no source, build file, test file or git index entry was
changed by the document author.

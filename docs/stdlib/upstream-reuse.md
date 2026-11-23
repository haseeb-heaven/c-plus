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
  `examples/msetl_example/{msevector_test.h,msetl_example_defs.h}`. The brief's
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
   related inventory docs. This is the only verbatim upstream code in the scope.
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

# Upstream reuse for the C+ standard library

Date: 2026-11-23. Branch `agent/modern-stdlib`. This document records ACTUAL
source inspected (not just documentation), exact URLs/paths, pinned revisions
where available, licenses, which designs were adapted, and which C+ regression
tests verify the results. Per-source detail is in
`docs/stdlib/upstream_study/*.md` (7 files, one per source).

## 1. Sources studied and license review

| # | Upstream | Revision / pin | Exact identifier | Audit doc |
|---|---|---|---|---|
| 1 | Microsoft STL | `main` @ `0cabaf3c7e0a73fe3040aabce40cd8833e80392b` | `Apache-2.0 WITH LLVM-exception` | `upstream_study/microsoft_stl.md` |
| 2 | Microsoft GSL | `main` (fetched 2026-11-23) | `MIT License (MIT)`, Copyright (c) 2015 Microsoft Corp. | `upstream_study/microsoft_gsl.md` |
| 3 | SaferCPlusPlus | `master`, API `pushed_at` 2026-10-12 | `BSL-1.0` (Boost Software License 1.0) | `upstream_study/safer_cplusplus.md` |
| 4 | Microsoft Secure CRT (docs) + vendored safeclib | safeclib `90b076e94c4b0fb202dddf948d5d620e5395c1b0` | Docs only; safeclib MIT-style (Cisco/Reini Urban); Intel MIT | `upstream_study/secure_crt.md` |
| 5 | Cake | `main` @ `2da2afd1759a7f3a62024ac781af65c8832bbe5f` (v0.17.07) | `GPL-3.0-or-later` + AI-training-use addendum — **REJECTED** | `upstream_study/cake.md` |
| 6 | Fil-C | fetched docs + `fugc.h`, `stdfil.h` | LLVM: Apache-2.0 WITH LLVM-exceptions; fetched runtime files BSD-2-clause | `upstream_study/fil_c.md` |
| 7 | LLVM/Clang | `main` @ pinned commit recorded in study | `Apache-2.0 WITH LLVM-exception` | `upstream_study/llvm_clang.md` |

License decisions: only permissive licenses (MIT, BSL-1.0,
Apache-2.0-with-LLVM-exception, BSD-2-clause) were considered reusable. Cake
was already rejected in `docs/UPSTREAM_AUDIT.md`; **no Cake source, header or
test was copied** and its study is idea-level traceability only. Microsoft CRT
material was used as documentation, not copied code.

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

## 3. Reuse mapping

| C+ module | Upstream project | Source files | Reused design/code | Changes made | Regression tests |
|---|---|---|---|---|---|
| `vector.hp` | MS STL; GSL; LLVM | `stl/inc/vector`; `include/gsl/span`; `__vector/vector.h` | Subtract-form overflow checks (`limit - current`), commit-last mutation with scoped rollback, pinned extent/bounds contracts, `_LIBCPP_ASSERT` tiering | C++ templates/RAII unavailable; C+ uses `size_t` overflow checks, `stdlib_alloc` + explicit rollback, `at_checked` + generation-stamped `slice`. Power-of-two doubling kept as documented C+ behavior. | `deep_error_vector.cp`, `deep_vector_task4.cp`, `deep_collections_alloc.cp`, `extra_string_vector_alias.cp` |
| `string.hp` / `string_view` | MS STL; GSL; SaferCPlusPlus; Secure CRT | `stl/inc/xstring`; `include/gsl/pointers`; `msepointerbasics.h`; `strcpy_s.c` | Overlap-safe append (upstream copies old buffer before reading aliased source), `Expects`-style validation at accessors, source/lifetime token concept | Tier-2 `error *` reporting instead of exceptions/contracts; `string_view` remains caller-responsible (no generation) — documented gap, not silently "fixed" | `deep_string_append.cp`, `deep_string_view.cp`, `deep_string_task4.cp`, `extra_string_vector_alias.cp` |
| `io.hp`, `file.hp` | Secure CRT; MS STL; LLVM runtime | `gets_s.c`, `fopen_s.c`, `fprintf_s.c`; `stl/inc/xstring`; sanitizer interfaces | Bounded loops that drain/clear on overflow and always NUL-terminate; owned-vs-borrowed handle discipline; invalid-parameter policy | Annex K names/semantics not copied; C+ returns `STDLIB_OK/EOF/FAIL` with `error *` and exact peek logic for CRLF/EOF | `deep_io_1byte.cp`, `deep_io_token.cp`, `deep_io_task2.cp`, `deep_file_lines.cp`, `deep_file_lifecycle.cp`, `deep_file_task2.cp`, `extra_io_whitespace.cp`, `extra_file_binary.cp` |
| `collections.hp` | MS STL; GSL; SaferCPlusPlus | `stl/inc/vector`; `include/gsl/span`; `msemsevector.h` | Open addressing with tombstones, power-of-two capacity, 70% load guard, allocate-all-then-commit rollback | C+ variant stores plain `int`/owned key copies; no iterators/exceptions; `slice` uses generation counter instead of debug iterators | `deep_collections_growth.cp`, `deep_collections_collisions.cp`, `deep_collections_alloc.cp`, `deep_collections_boundary.cp`, `extra_collections_clear.cp` |
| `error.hp` / alloc metadata | GSL; Fil-C; LLVM | `include/gsl/narrow`; `fugc.h`, `stdfil.h`; `asan_interface.h` | First-class liveness/length metadata queries with explicit unknown/freed result; generation-stamped views; two-phase narrowing | C+ uses `stdlib_alloc_size/length/generation/check_generation` and `stdlib_note`; no capability pointers or compiler annotations (Agent 1) | `deep_error_vector.cp`, `deep_error_task5.cp`, `extra_error_metadata.cp`, `alloc_fault_matrix` (294 checks) |
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
3. No Microsoft STL/GSL/SaferCPlusPlus/Cake/Fil-C source was copied. Their
   techniques were adapted as documented above.

## 5. Why some code was written independently

C+ `stdlib/*.hp` is a header-only, C-compatible API surface with no exceptions,
no templates/concepts, a checked-allocator/error-object model, and a compiler
that owns lifetime enforcement. Full STL/GSL containers cannot be copied
without Agent 1 language features; Cake is license-incompatible; Fil-C's
capabilities are compiler/runtime features. Where upstream designs were
applicable (overflow checks, rollback guards, bounds contracts, metadata
queries), they were adapted and covered by the C+ tests listed above.

## 6. Upstream tests adapted

No upstream test file was copied verbatim. Cases adapted as ideas: MS STL
`GH_002030_asan_annotate_vector/string` (capacity annotations) → UBSan/strict
deep tests; `GH_005472_do_not_overlap` → `deep_string_append.cp` self/overlap
cases; GSL `span_tests.cpp` bounds cases → `at_checked` boundary tests;
SaferCPlusPlus `msevector_test.h` lifetime cases → generation/slice tests;
Cake `defer`/`flow3` ownership cases → guard/early-return tests (idea only,
license rejected for code). Exact adapted case lists are in the per-source
study docs.

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

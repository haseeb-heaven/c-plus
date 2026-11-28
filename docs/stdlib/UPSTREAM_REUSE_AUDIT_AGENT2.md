# Phase 4 audit — ACTUAL upstream code reuse (Agent C / Agent2)

**Branch:** `agent/modern-stdlib`  **Date:** 2026-11-23  **Repo root:** `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`

This audit verifies *actual* upstream code reuse by (a) reading the vendored upstream source and the C+ destination files on disk, (b) grepping the ported markers, (c) `nm`-checking exported symbols where relevant, and (d) rebuilding and running the named test under `timeout 30` with the exact flags recorded in the owning `*_NOTES.md`. **Documentation-only and study-only items get no INTEGRATED credit.**

> Ownership: this document is the only file created/modified by this agent. No source, test, header, build script, or git index was touched.

---

## Headline counts

| Classification | Projects |
|---|---|
| **INTEGRATED** (code/algorithm actually ported + test passing) | 8: Microsoft STL (vector + string), Microsoft GSL, SaferCPlusPlus, Fil-C (contract level), SoftBoundCETS, Safeclib/Secure-CRT, mimalloc, LLVM libc (`memset_explicit`) |
| **RESEARCH-ONLY** (no code used) | 1: jemalloc |
| **REJECTED** (no code, legal block) | 1: Cake |
| Named tests built & run | 19 binaries (7 C+ upstream_reuse + 1 safecrt standalone + 10 vendor + 1 LLVM C23) |
| Tests passing | **18 / 19** |
| Tests failing | **1 / 19** — `test_vendor_mem` (`vendor_mem: 23 1`, 1 check), pre-existing deliberate C+ security-patch divergence (§7) |

All 7 C+ upstream-reuse tests, the safecrt standalone test, 9/10 vendor tests, and the LLVM C23 test pass. No UBSan claim is made for the C+ tests: their canonical `--sanitize=undefined` build is blocked by the documented external-runtime `CPLUS_EXIT_ARITHMETIC` header drift (see each NOTES); the LLVM C23 test ran with UBSan and passed.

---

## 1. Microsoft STL — Apache-2.0 WITH LLVM-exception — **INTEGRATED**

1. **Source paths inspected (on disk / fetched):** `stdlib/vector.hp`, `stdlib/string.hp`; upstream `stl/inc/vector` (growth/guards), `stl/inc/xstring` (capacity/append), `stl/inc/xmemory` (`_Get_size_of_n`), `stl/inc/filesystem`, `LICENSE.txt`.
2. **Commit/version:** `0cabaf3c7e0a73fe3040aabce40cd8833e80392b` (`microsoft/STL` `main`, 2026-11-21), recorded in `docs/stdlib/upstream_study/microsoft_stl.md`; raw fetches carry SPDX in-file but no embedded hash.
3. **License:** Apache License 2.0 **WITH LLVM-exception** (`// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception`; LLVM exception appendix in `LICENSE.txt`).
4. **Code actually ported (verbatim quotes from destination):**
   - `stdlib/vector.hp:71-82` — `vector_calculate_growth()` with the subtraction-form overflow guard:
     ```c
     if (oldcapacity >= max_count) return max_count;
     if (oldcapacity > max_count - oldcapacity)
         return max_count; /* geometric growth would overflow -> saturate */
     geometric = oldcapacity + oldcapacity;
     if (geometric < newsize) return newsize; /* demand floor */
     ```
   - `stdlib/vector.hp:47-55` — `vector_allocation_bytes()` (`count > STDLIB_ALLOC_LIMIT / sizeof(int)` refuse) = `_Get_size_of_n` pre-multiply guard. `stdlib/vector.hp:185` marks `/* ---- commit point: nothing above can leave the vector modified ---- */` (allocation-guard/commit-last discipline).
   - `stdlib/string.hp:101-118` — `stdlib_string_calculate_growth()` (mask | geometric step | overflow guard | hard limit) = `_Calculate_growth`.
   - `stdlib/string.hp:294-340` — `string::append()` rewritten: fit path uses `__builtin_memmove` (`_Traits_move_batch`), growth path allocates and copies from the still-live old buffer before retiring it (`_Reallocate_grow_by` ordering).
   - **Deliberate divergence:** the upstream **1.5×** step was *not* copied; C+ keeps 2× (documented, fixture-pinned).
5. **C+ destinations:** `stdlib/vector.hp`, `stdlib/string.hp`; tests `tests/stdlib/upstream_reuse/{vector_stl,string_stl}.cp`.
6. **Status:** **INTEGRATED** (algorithm/structure port, not a verbatim paste; with explicit documented multiplier divergence).
7. **Tests + actual results:**
   ```
   timeout 30 ./build/c+ build tests/stdlib/upstream_reuse/vector_stl.cp --strict --sanitize=none -o build/upstream_reuse/vector_stl_strict
   timeout 30 ./build/upstream_reuse/vector_stl_strict
   -> BUILD_RC=0 RUN_RC=0 stderr=0B  FINAL: VECTOR_STL 14 0

   timeout 30 ./build/c+ build --strict --sanitize=none tests/stdlib/upstream_reuse/string_stl.cp -o $TMP/string_stl_strict
   timeout 30 $TMP/string_stl_strict
   -> BUILD_RC=0 RUN_RC=0 stderr=0B  FINAL: upstream-string-checks 28 / upstream-string-failures 0
   ```
8. **Remaining reusable components:** C++ templates/`if constexpr`, `bad_array_new_length` exceptions, `allocator_traits::max_size`, RAII allocation guards, ASan container annotations, `_Vector_val` tombstones/orphan iterators — none portable to C17/C+; deliberately not ported. The 1.5× growth constant is available but fixture-locked to 2×.

---

## 2. Microsoft GSL — MIT — **INTEGRATED**

1. **Source paths inspected:** `stdlib/gsl_adapt.hp`; upstream `include/gsl/narrow` (three-stage representability check), `include/gsl/pointers` (`not_null`, deleted null ctor/arithmetic), `include/gsl/span` (`Expects` guards), `LICENSE`.
2. **Commit/version:** per-file `main` tips at fetch: `narrow`@`08687fb`, `pointers`@`a75212b`, `span`@`417ef68` (2026-08/07-25); raw URLs carry no hash.
3. **License:** MIT, Copyright (c) 2015 Microsoft Corporation (verbatim in header comment of `gsl_adapt.hp`).
4. **Code actually ported (destination quotes):**
   - `stdlib/gsl_adapt.hp:108` — narrowing refusal note `"gsl_narrow: value is not representable in target type"`; `gsl_narrow_i64_i8/u8/i16/u16/i32/u32/i64/u64`, `gsl_narrow_u64_i64/u8/u32` (`:122-260`) implement preflight → cast → round-trip equality → sign check (all four upstream stages retained), reporting `STDLIB_FAIL + STDLIB_CATEGORY_RANGE` instead of a throw.
   - `stdlib/gsl_adapt.hp:270-311` — `gsl_not_null_i32` / `gsl_not_null_void` single-pointer structs, `*_make()` guards the null refusal, no `++/--/+= /[]`.
   - `stdlib/gsl_adapt.hp:114` — `"gsl_span: element index or subrange out of bounds"`; `gsl_span_i32 {int *base; size_t count; void *owner; size_t off_elems;}` with `at()/first()/last()/subspan()` refusals mirroring GSL's `Expects(idx < size())`.
   - **Asserted ABI:** `sizeof(int *) == sizeof(void *)` and `sizeof(gsl_not_null_i32) == sizeof(int *)`.
5. **C+ destinations:** `stdlib/gsl_adapt.hp`; test `tests/stdlib/upstream_reuse/gsl_adapt.cp`.
6. **Status:** **INTEGRATED**.
7. **Test + actual result:**
   ```
   timeout 30 ./build/c+ build --strict --sanitize=none tests/stdlib/upstream_reuse/gsl_adapt.cp -o $TMP/gsl_adapt_strict_none
   timeout 30 $TMP/gsl_adapt_strict_none
   -> BUILD_RC=0 RUN_RC=0 stderr=0B  FINAL: GSL_ADAPT 80 0
   ```
   UBSan (documented workaround `-- -DCPLUS_EXIT_ARITHMETIC=110`) also reported `GSL_ADAPT 80 0` in the NOTES; not re-asserted here.
8. **Remaining reusable components:** `gsl::dyn_array`, `strict_not_null`, `byte`, floating-point narrowing preflight (quoted but not re-implemented with host FP limits), GSL contract-violation handlers.

---

## 3. SaferCPlusPlus — BSL-1.0 — **INTEGRATED**

1. **Source paths inspected:** `stdlib/lifetime_adapt.hp`; upstream `include/msepointerbasics.h`, `include/msescope.h`, `LICENSE_1_0.txt`.
2. **Commit/version:** `master` HEAD `5edaf67acf746d9ee472a06a51094b31a3db57c0` (fetched 2026-11-23).
3. **License:** Boost Software License 1.0; `Copyright (c) 2015 Noah Lopez` (verbatim notice in `lifetime_adapt_NOTES.md`).
4. **Code actually ported (destination quotes):**
   - `stdlib/lifetime_adapt.hp:72-73` — `typedef unsigned long long lifetime_token_t; #define LIFETIME_NO_TOKEN 0ULL` (maps `lifetime_info1_t`/`no_lifetime_info1 = 0`).
   - `:77-88` — `lifetime_owner { void *ptr; lifetime_token_t token; }` and `lifetime_borrow { void *ptr; lifetime_token_t token; }` (new additive descriptor; **not** a redefinition of `T*`).
   - `:95-113` — `lifetime_check()` validates ptr/token/size/generation via `stdlib_alloc_check_generation` **before** any dereference (guarded `operator*` analogue).
   - `:177-195` `lifetime_transfer()` bumps generation via `stdlib_transfer_ownership` (old borrows go stale); `:226-266` `lifetime_read()/lifetime_write()` validate token+range then `__builtin_memcpy`.
   - **Mechanism limitation honestly kept:** C17 cannot reproduce the *static* type-level token; this is runtime-check mode only.
5. **C+ destinations:** `stdlib/lifetime_adapt.hp`; test `tests/stdlib/upstream_reuse/lifetime_adapt.cp`.
6. **Status:** **INTEGRATED** (runtime-check adaptation of a static scheme).
7. **Test + actual result:**
   ```
   timeout 30 ./build/c+ build --strict --sanitize=none tests/stdlib/upstream_reuse/lifetime_adapt.cp -o $TMP/lifetime_adapt_strict_none
   timeout 30 $TMP/lifetime_adapt_strict_none
   -> BUILD_RC=0 RUN_RC=0 stderr=0B  FINAL: LIFETIME_ADAPT 25 0
   ```
8. **Remaining reusable components:** `TScopeID`/`TPointer` type tags, scope marker base classes, `scpptool` static enforcement, optimized scope-pointer release semantics — all require C++ templates/inheritance; not portable.

---

## 4. Fil-C — BSD-2-clause-style (Epic Games / Filip Pizlo) — **INTEGRATED (contract level only)**

1. **Source paths inspected:** `stdlib/allocator.hp`; upstream `filc/include/stdfil.h`, `filc/src/runtime.c`, `libpas/LICENSE.txt`.
2. **Commit/version:** `deluge` @ `a80e32804fd44d295deb1ce1b0098962022da0a3` ("Versioning", 2026-10-23). (Root `docs/UPSTREAM_AUDIT.md` records an earlier `d9041afd6b11` design-block verdict for the *full* runtime.)
3. **License:** BSD 2-Clause-style (Copyright (c) 2023-2026 Epic Games, Inc.; 2026 Filip Pizlo). Only the contract/technique was re-expressed; no Fil-C code copied.
4. **Code actually ported (destination quotes):**
   - `stdlib/allocator.hp:239` cites upstream `filc_bool zhasvalidcap(void* ptr);`; `:272-273` `#define STDLIB_ALLOC_CAP_NONE 0` / `STDLIB_ALLOC_CAP_LIVE 1`.
   - `:279` `stdlib_alloc_cap_state()` (one-lock LIVE/NONE triage); `:297-298` `stdlib_alloc_hasvalidcap()` — "Faithful C+ port of Fil-C `zhasvalidcap()`: 1 only if `ptr` currently has a live, non-free exact-base slot".
   - `:310` `stdlib_alloc_cap_covers()` = `zvalinbounds()` as `(base, offset, size)` using `offset <= cap && size <= cap - offset` (no pointer arithmetic); `:330` `stdlib_alloc_cap_check()` adds generation match (stale/free-singleton analogue).
   - **Honest divergence:** C+ tombstones the slot on free, so a freed address cannot be distinguished from never-allocated by address alone; generation identity is what detects free→recycle (documented in header).
5. **C+ destinations:** `stdlib/allocator.hp`; test `tests/stdlib/upstream_reuse/filc_diag.cp`.
6. **Status:** **INTEGRATED (contract-level diagnostic helpers only)** — the full Fil-C runtime remains design-blocked (LLVM fork/GC/capability ABI).
7. **Test + actual result:**
   ```
   timeout -k 5 30 build/c+ build tests/stdlib/upstream_reuse/filc_diag.cp --strict --sanitize=none -o $TMP/filc_diag_strict_none
   timeout -k 5 30 $TMP/filc_diag_strict_none </dev/null
   -> BUILD_RC=0 RUN_RC=0 stderr=0B  FINAL: ALLOC_DIAG 26 0
   ```
8. **Remaining reusable components:** InvisiCaps, free-singleton repointing, FUGC, `zgc_completed_cycle` epoch, `zinbounds` hardware traps — require modified LLVM/ABI; out of scope.

---

## 5. SoftBoundCETS — BSD-3-Clause / MIT-style per-file — **INTEGRATED (predicates only)**

1. **Source paths inspected:** `stdlib/memory.hp`; upstream `softboundcets-lib/softboundcets.h` (1313 lines), `softboundcets-checks.c`, `LICENSE`.
2. **Commit/version:** `master` `9a9c09f04e16f2d1ef3a906fd138a7b89df44996` (2014-09-19 PR #4 merge; no tags).
3. **License:** top-level 3-clause BSD (Copyright (c) 2013 Santosh Nagarakatte); per-file MIT-style notice (Copyright (c) 2014 Nagarakatte, Martin). Only short predicate expressions adapted, attributed in `memory.hp` comments; **no upstream file vendored**.
4. **Code actually ported (destination quotes):**
   - `stdlib/memory.hp:123-124` cites upstream `softboundcets.h:539/555`: `if ((ptr < base) || ((void*)((char*) ptr + size_of_type) > bound)) ...`.
   - `:155` `stdlib_mem_span_ok()` implements the spatial predicate as `off > size`/`n > size - off` on `uintptr_t` (no `base+size` wrap, strict-safe); `:171` `stdlib_mem_span_contains()`.
   - `:193` `stdlib_mem_bounds()` = zero-on-miss metadata lookup (returns `-1` + zeroed outputs on untracked).
   - `:216` `stdlib_mem_key_current()` uses the captured generation as key/lock (`*lock = 0` on free analogue); `:239` `stdlib_mem_query()` combines spatial + temporal checks, reports via `error*`/`stdlib_note` instead of `abort()`.
   - **Upstream typo not carried over:** `size > dest_bound` at upstream `:579/:623` was not copied.
5. **C+ destinations:** `stdlib/memory.hp`; test `tests/stdlib/upstream_reuse/softbound.cp`.
6. **Status:** **INTEGRATED (check predicates only)**.
7. **Test + actual result:**
   ```
   timeout 30 ./build/c+ build tests/stdlib/upstream_reuse/softbound.cp --strict --sanitize=none -o $TMP/softbound_strict
   timeout 30 $TMP/softbound_strict
   -> BUILD_RC=0 RUN_RC=0 stderr=0B  FINAL: SOFTBOUND 51 0
   ```
8. **Remaining reusable components:** instrumenting compiler, mmap'd two-level trie keyed by pointer storage address, shadow-stack calling convention, temporal lock allocator, free map, `abort()` policy — all require compiler/runtime work and would change the pointer contract. `shrink_bounds` (max/min) noted but not ported.

---

## 6. Microsoft Secure CRT / Safeclib (`rurban/safeclib`) — MIT — **INTEGRATED**

1. **Source paths inspected:** `libc/src/string/upstream_safecrt.c`, `libc/install-include/cplus_safecrt_upstream.h`, `libc/src/safecrt_vendor/*.c` (10 groups), `third_party/safeclib/src/misc/bsearch_s.c` (`_bsearch_s_chk` at `:115`), `third_party/safeclib/src/extmem/memrchr_s.c` (`_memrchr_s_chk` at `:83`, `_memrchr_s_uchk` at `:115`, `while (dmax--)` at `:129`), `third_party/safeclib/COPYING`, `UPSTREAM_COMMIT`, `scripts/build_safecrt.sh`, `tests/libc/build_libc.sh`.
2. **Commit/version:** `90b076e94c4b0fb202dddf948d5d620e5395c1b0` (verified on disk in `third_party/safeclib/UPSTREAM_COMMIT`); upstream release undetermined locally (`3.9.1-cplus` is a local config value).
3. **License:** MIT (Copyright (C) 2012, 2013 Cisco Systems; 2017 Reini Urban). Verbatim notices reproduced in `upstream_safecrt.c` and each `safecrt_vendor/*_group.c`.
4. **Code actually ported (destination quotes):**
   - `libc/src/string/upstream_safecrt.c:127` `cplus_sc_upstream_bsearch_s()` ← upstream `_bsearch_s_chk`; `:179` `cplus_sc_upstream_memrchr_s()` ← `_memrchr_s_chk`/`_memrchr_s_uchk`; `:215` keeps the verbatim fallback `while (dmax--) { ... }` loop. `nm -g` confirms exactly two new globals plus `___error` (errno) import — no safeclib-internal deps.
   - The 10 `libc/src/safecrt_vendor/*_group.c` files include upstream TUs **verbatim** (`mem_group.c:61-62`: `#include "extmem/memchr_s.c"` / `#include "extmem/memccpy_s.c"`), with only symbol-prefix macros and constraint-handler remapping (`mem_group.c:38-59`). Public headers `libc/install-include/cplus_vendor_*.h`.
   - Existing archive `build/safe/libcplus_safecrt.a` compiles 18 upstream `.c` files + `timingsafe_*` (`scripts/build_safecrt.sh`); `upstream_safecrt.c` is now wired into `tests/libc/build_libc.sh` (confirmed in the `for f in ...` list).
5. **C+ destinations:** `libc/src/string/upstream_safecrt.c`, `libc/src/safecrt_vendor/*.c`, `libc/install-include/cplus_{safecrt_upstream,vendor_*}.h`; tests `tests/libc/test_safecrt_upstream.c` + `tests/libc/test_vendor_*.c`.
6. **Status:** **INTEGRATED**.
7. **Tests + actual results:**
   ```
   # standalone upstream port
   timeout 30 /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
     -c libc/src/string/upstream_safecrt.c -o $TMP/upstream_safecrt.o
   timeout 30 /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
     tests/libc/test_safecrt_upstream.c $TMP/upstream_safecrt.o build/libc/libcplus_hardened.a \
     -o $TMP/test_safecrt_upstream
   timeout 30 $TMP/test_safecrt_upstream
   -> OBJ=0 TEST_BUILD=0 RUN_RC=0 stderr=0B  FINAL: test_safecrt_upstream: all checks passed
   nm -g $TMP/upstream_safecrt.o -> _cplus_sc_upstream_bsearch_s, _cplus_sc_upstream_memrchr_s, U ___error

   # 10 vendor tests — exact run_libc_tests.sh flags plus explicit -isysroot (host SDK fix, §Notes)
   for t in tests/libc/test_vendor_*.c:
     clang -isysroot $(xcrun --show-sdk-path) -O2 -Wall -Wextra -pedantic -std=c99 \
       -Ilibc -Ilibc/include -Ilibc/install-include -I. -Ithird_party/safeclib/include \
       "$t" build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a -lm -pthread -o "$b"
     timeout 30 "$b"
   -> 9 PASS / 1 FAIL (build rc 0 for all 10)
      vendor_case: 28 0 | vendor_field: 59 0 | vendor_firstlast: 1422 0
      vendor_mem: 23 1  <-- FAIL: memccpy_s NULL src clears dest (rc=400)
      vendor_set: 48 0 | vendor_spn: 39 0 | vendor_stp: 30 0
      vendor_strchr: 54 0 | vendor_strl: 2070 0 | vendor_strstr: 129 0
   ```
   **Env note:** bare `clang` on this host cannot find `errno.h`/`stdio.h` ("using sysroot for macOS 27 but targeting arm64-apple-macosx27.0.0"); adding `-isysroot $(xcrun --show-sdk-path)` fixes the build. No source was changed.
8. **Remaining reusable components:** the rest of safeclib's 100+ checked APIs; BOS-aware `__builtin_object_size` checks (deliberately dropped from the two new entry points); constraint-handler dispatch. See §7 for the one failing check.

---

## 7. Explanation of the one vendor-test failure (not a code defect introduced here)

`test_vendor_mem` check `"memccpy_s NULL src clears dest"` expects `dst[0] == '\0'` after `cplus_v_memccpy_s(dst, sizeof dst, NULL, 'a', 3)`. Upstream `memccpy_s.c:CHK_SRC_MEM_NULL_CLEAR` calls `handle_mem_error(dest, ...)`, which **upstream would zero** the destination. The vendored `third_party/safeclib/src/mem/safe_mem_constraint.h:54-72` carries an explicit **"C+ SECURITY PATCH (2026-11-23)"** that makes `handle_mem_error()` a no-op for the destination (it only invokes the constraint handler) to avoid a caller-supplied-`dmax` buffer overrun. Therefore the return code is still the correct `ESNULLP` (400) but the buffer is intentionally left intact. The failing assertion enforces the old upstream wipe semantics, so the test and the deliberate security patch disagree. This is pre-existing and unrelated to any Phase 4 change; no file was edited to "fix" it.

---

## 8. mimalloc — MIT — **INTEGRATED**

1. **Source paths inspected:** `stdlib/alloc_policy.hp`; upstream `include/mimalloc/internal.h` (`_mi_align_up:404-413`, `_mi_wsize_from_size:463-466`, `mi_mul_overflow:474-489`, `mi_count_size_overflow:493-506`), `src/alloc.c` (`:133` zero-size, `:221-224`/`:268-270` calloc), `include/mimalloc/types.h`, `LICENSE`.
2. **Commit/version:** `main` `c683b7c60c91cb2809977e37d3f4d084d7497180` (2026-08-05, release v3.4.5); raw `master` path is byte-identical to `main`.
3. **License:** MIT (Copyright (c) 2018-2025 Microsoft Corporation, Daan Leijen). Full text reproduced in `alloc_policy.hp`.
4. **Code actually ported (destination quotes):**
   - `stdlib/alloc_policy.hp:72` `stdlib_count_size_overflow()` — mirrors `mi_count_size_overflow` return convention (1 = overflow), `count==1` quick path, `*total = SIZE_MAX` on overflow, `__builtin_mul_overflow` fast path.
   - `:109` `stdlib_size_class_round_up()` = `_mi_align_up` fast branch (`mask = alignment-1; (size+mask) & ~mask`), adding rejection of bad alignment/overflow (upstream relies on asserts).
   - `:129` `stdlib_size_class_words()` = `_mi_wsize_from_size` ceiling division with a real `-1` overflow rejection.
   - `:137` `stdlib_size_normalize_zero()` = `src/alloc.c:133` zero-size rule (policy passed as argument).
5. **C+ destinations:** `stdlib/alloc_policy.hp` (new, additive, `static inline` only); test `tests/stdlib/upstream_reuse/alloc_policy.cp`.
6. **Status:** **INTEGRATED**.
7. **Test + actual result:**
   ```
   timeout 30 build/c+ build tests/stdlib/upstream_reuse/alloc_policy.cp --strict --sanitize=none -o build/alloc_policy_strict
   timeout 30 build/alloc_policy_strict
   -> BUILD_RC=0 RUN_RC=0 stderr=0B
      FINAL: ALLOC_POLICY 42 / ALLOC_POLICY_FAILURES 0
   ```
8. **Remaining reusable components:** sharded/encoded free lists, first-class heaps, guarded pages, huge-page arenas, tcache, HPA, `EAGAIN/EFAULT/EOVERFLOW` taxonomy — all replace the host allocator; explicitly rejected by `docs/stdlib/allocator-architecture.md:448`.

---

## 9. jemalloc — BSD-style (no SPDX asserted) — **RESEARCH-ONLY (no code used)**

1. **Source paths inspected:** `docs/stdlib/allocator/UPSTREAM_RESEARCH.md:34,305-357,427-460`; `docs/stdlib/allocator-architecture.md:449`. Upstream files were fetched for study only: `COPYING`, `src/jemalloc.c`, `src/arena.c`, `include/jemalloc/internal/emap.h`.
2. **Commit/version:** `dev` tip `b37b28d77eb92e6332e5c9d282db85d76740908c` (2026-11-20).
3. **License:** two-clause BSD-style ("2002-present Jason Evans", "2007-2012 Mozilla Foundation", "2009-present Facebook, Inc."); no SPDX string in `COPYING` (403 from GitHub license API; no SPDX asserted).
4. **Code actually ported:** **NONE.** Grep of `libc/`, `stdlib/`, `tests/` for `jemalloc` in source returns zero files. The study transferred only the *fail-closed metadata lookup shape* and the *subtract-form range-test idea*, both of which were already present locally (`stdlib_meta_find`, `stdlib_mem_check_range`), not copied. `allocator-architecture.md:449` marks jemalloc **"Not copied"**; `UPSTREAM_RESEARCH.md:448-460` mandates "No allocator copying".
5. **C+ destinations:** none.
6. **Status:** **RESEARCH-ONLY** — no INTEGRATED credit. (The general fail-closed/generation patterns credited elsewhere are attributed to SoftBoundCETS/Fil-C/MS STL, not jemalloc.)
7. **Test proving it:** no test exists and none is claimed; this is a documentation/study entry. Nothing to run.
8. **Remaining reusable components:** `emap_t`/rtree fail-closed lookup, `compute_size_with_overflow` half-word guard, arena/slab bins, `dealloc_no_tsd` fork/teardown care — explicitly deferred as allocator/compiler runtime territory; adopting them would add a new runtime dependency and change the ABI/ownership contract.

---

## 10. Cake (`thradams/cake`) — GPL-3.0 + AI-training addendum — **REJECTED (no code)**

1. **Source paths inspected:** `docs/stdlib/upstream_study/cake.md` (notes only), `docs/UPSTREAM_AUDIT.md:12`. No Cake source/header/test exists in `libc/`, `stdlib/`, `tests/` (grep confirms zero source matches).
2. **Commit/version:** `main` `2da2afd1759a7f3a62024ac781af65c8832bbe5f`, `CAKE_VERSION "0.17.07"` (2026-11-12).
3. **License:** GPLv3 **plus AI Model Training Use addendum** forbidding use of the source as "input, or reference" for AI models.
4. **Code actually ported:** **NONE.** The study doc itself records that inspection was in tension with the addendum and copies no Cake file into the repo.
5. **C+ destinations:** none.
6. **Status:** **REJECTED** — legally incompatible with this MIT project; no code inspected beyond README/license for the integration decision. No INTEGRATED credit.
7. **Test proving it:** none — by design; no code, no test.
8. **Remaining reusable components:** not actionable — ownership/borrowing/`defer`/flow ideas already implemented natively in C+; any reuse requires a maintainer license decision. Do not integrate.

---

## 11. LLVM (`llvm/llvm-project`) — Apache-2.0 WITH LLVM-exception — **INTEGRATED (one verbatim body) + mostly RESEARCH-ONLY**

1. **Source paths inspected:** `libc/src/c23_upstream_small.c`, `tests/libc/test_c23_upstream_small.c`, `docs/libc/C23_SMALL_UPSTREAM.md`, `libc/src/math/math_c23_upstream.c`, `tests/libc/test_math_c23_upstream.c`, `libc/upstream/` (directory listing). Upstream: `libc/src/string/memset_explicit.cpp`; LLVM math `libc/src/math/generic/<name>.cpp` + `src/__support/FPUtil/*` (study only).
2. **Commit/version:** tag `llvmorg-20.1.1` (commit `424c2d9b7e4de40d0804dd374721e6411c27d1d1` per `docs/libc/API_GAP_CLOSURE_PLAN.md:202`); `memset_explicit.cpp` blob `a3e4dad874397463cb5315679f99a312ac3a53fe`. Main `main` tip `1e0049e03939cef968db8085e2c36298c82270ff` used for the separate sanitizer-interface study.
3. **License:** Apache License 2.0 **WITH LLVM-exception**; SPDX header reproduced verbatim in `c23_upstream_small.c:76-80`.
4. **Code actually ported (destination quote):**
   - `libc/src/c23_upstream_small.c:72-109` is explicitly labelled "Vendored: LLVM Project, libc/src/string/memset_explicit.cpp, revision llvmorg-20.1.1" and contains the body with the upstream license header, the `// avoid dead store elimination` comment, the compiler barrier `__asm__("" : : "r"(dst) : "memory")` and `return dst;` preserved. Only 5 documented adaptations: `memset_explicit → cplus_memset_explicit`, `[[gnu::noinline]] → __attribute__((noinline))`, `inline_memset → cplus_memset` (the inline dependency closure was **not** self-contained), `asm → __asm__`, plus one `cplus_libc_log` marker.
   - **Note on `libc/upstream/`:** this directory contains only the **musl** snapshot (`libc/upstream/musl/`), not LLVM. The LLVM code is not vendored there; it is the directly-copied `memset_explicit` body in `libc/src/c23_upstream_small.c`. `timespec_getres` and `FE_TIESAWAY` were **BLOCKED** (no source / compiler-provided macro) — no algorithm invented.
   - LLVM math: `libc/src/math/math_c23_upstream.c` copies **0 of 23** APIs (every one needs `src/__support/FPUtil` C++ internals) — **RESEARCH-ONLY**.
5. **C+ destinations:** `libc/src/c23_upstream_small.c`; test `tests/libc/test_c23_upstream_small.c`. (Not wired into `libc/CMakeLists.txt` — parent-agent wiring noted.)
6. **Status:** **INTEGRATED** for `memset_explicit`; **RESEARCH-ONLY/BLOCKED** for everything else LLVM.
7. **Test + actual result:**
   ```
   SYS=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
   timeout 30 /usr/bin/clang -isysroot "$SYS" -std=c99 -Wall -Wextra -Wpedantic -Werror \
     -fsanitize=undefined -fno-omit-frame-pointer -g -Ilibc -Ilibc/log \
     libc/src/c23_upstream_small.c libc/log/log.c libc/src/string/memory.c \
     tests/libc/test_c23_upstream_small.c -o $TMP/test_c23_upstream
   timeout 30 env UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 $TMP/test_c23_upstream
   -> BUILD_RC=0 (zero warnings, -Werror) RUN_RC=0
      FINAL: c23_upstream_small: ok
      stderr=63B, expected [CPLUS-LIBC] memset_explicit / memset log markers only
      (2 SKIP lines for timespec_getres / FE_TIESAWAY)
   ```
8. **Remaining reusable components:** LLVM libc's other self-contained string/memory routines (subject to the same pin/dependency-closure rule), sanitizer annotation APIs (`__sanitizer_*`, docs-only here), and the 23 math APIs if a self-contained closure is found — currently all blocked.

---

## Consolidated test execution record

| # | Test | Build flags (NOTES.md, under `timeout 30`) | Build RC | Run RC | Final line |
|---|---|---|---|---|---|
| 1 | vector_stl | `./build/c+ build … --strict --sanitize=none -o build/upstream_reuse/vector_stl_strict` | 0 | 0 | `VECTOR_STL 14 0` |
| 2 | string_stl | `./build/c+ build --strict --sanitize=none … -o $TMP/string_stl_strict` | 0 | 0 | `upstream-string-checks 28` / `upstream-string-failures 0` |
| 3 | gsl_adapt | `./build/c+ build --strict --sanitize=none … -o $TMP/gsl_adapt_strict_none` | 0 | 0 | `GSL_ADAPT 80 0` |
| 4 | lifetime_adapt | `./build/c+ build --strict --sanitize=none … -o $TMP/lifetime_adapt_strict_none` | 0 | 0 | `LIFETIME_ADAPT 25 0` |
| 5 | filc_diag | `build/c+ build … --strict --sanitize=none …` (`timeout -k 5 30`) | 0 | 0 | `ALLOC_DIAG 26 0` |
| 6 | softbound | `./build/c+ build … --strict --sanitize=none -o $TMP/softbound_strict` | 0 | 0 | `SOFTBOUND 51 0` |
| 7 | alloc_policy | `build/c+ build … --strict --sanitize=none -o build/alloc_policy_strict` | 0 | 0 | `ALLOC_POLICY 42` / `ALLOC_POLICY_FAILURES 0` |
| 8 | test_safecrt_upstream | `clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include …` + hardened archive | 0 | 0 | `test_safecrt_upstream: all checks passed` |
| 9-18 | 10 × test_vendor_* | `clang -isysroot $(xcrun --show-sdk-path) -O2 … -Ithird_party/safeclib/include … hardened.a safecrt.a -lm -pthread` (`timeout 30`) | 0 (all) | 9×0, 1×1 | `vendor_mem: 23 1` (§7); others `…: N 0` |
| 19 | test_c23_upstream_small | `clang -isysroot … -std=c99 -Werror -fsanitize=undefined … -Ilibc -Ilibc/log …` | 0 | 0 | `c23_upstream_small: ok` |

All runs used `timeout 30` (vendor builds additionally used an explicit `-isysroot` to work around a host SDK misconfiguration; no repository file was changed). Where a NOTES file documents a UBSan mode for the C+ tests, that mode is blocked by the pre-existing external `cplus_runtime.c` / `CPLUS_EXIT_ARITHMETIC` header drift; only the canonical strict/no-sanitizer results above are asserted for those seven.

## Bottom line

- **8 projects genuinely contributed code** to this branch (MS STL, GSL, SaferCPlusPlus, Fil-C predicates, SoftBoundCETS predicates, safeclib/Secure-CRT, mimalloc, LLVM `memset_explicit`).
- **jemalloc = research-only, Cake = rejected**; neither gets reuse credit.
- **18/19 named tests pass**; the single failure is a documented, deliberate security-patch/test-expectation mismatch in `test_vendor_mem`, not a Phase 4 regression.

# ABI-preserving runtime safety for the C+ standard library

- **Branch / date:** `agent/modern-stdlib`, 2026-11-23.
- **File ownership for this task:** this document only. No header, test, build
  file, or git index entry was created or modified by this author.
- **Purpose:** record the runtime metadata architecture that provides
  *diagnostic* safety without changing the C ABI, the measured ABI evidence,
  the limits at foreign-C boundaries, the upstream sources actually inspected,
  the adaptations actually made, the executed test counts, and the measured (or
  explicitly unmeasured) performance overhead.

Labels used throughout:

- **[VERIFIED]** — directly supported by a checked-in source file with a
  `path:line` citation, a recorded command output artifact, or a recorded
  result file named in this document.
- **[UNTESTED]** — designed or documented but not exercised by a recorded
  command in this checkout.
- **[PENDING INPUT]** — named as an input for this task but not present in the
  checkout when this document was authored; no claim is made about it.
- **[INFERENCE]** — design reading, not a measured or quoted upstream fact.

No percentage is derived or invented here. Every ratio or count is quoted from
the named evidence file. Where a property is asserted by design but not proved
by an executed test, the two are stated separately.

> **Input-path corrections.** The task brief cites
> `docs/stdlib/agent2_audit/module_audits/{error,string,vector,collections,file}.hp.md`
> and `docs/stdlib/agent2/SANITIZER_MATRIX.md`. Those exact directories do not
> exist on this branch. The files that do exist, and are cited below, are
> `tests/stdlib/agent2_audit/module_audits/*.hp.md` and
> `tests/stdlib/agent2/SANITIZER_MATRIX.md`. Likewise, the brief's
> `docs/stdlib/agent2_audit/` does not exist. No citation below points at a
> path that was not read or at an artifact that was not observed.

---

## 1. Runtime metadata architecture

The safety layer is **out-of-band**: public object representations and pointer
values are untouched. All extra state lives in private tables in the C runtime
and in one weak header-defined state object.

### 1.1 The three metadata stores

| Store | File / location | What it records | Visibility |
|---|---|---|---|
| Hardened-libc allocation table | `libc/src/stdlib/alloc.c:24-49` (`meta_slot`, `g_tab`, counters) | base address, byte size, element size, generation, liveness | private `static` in the C file |
| SafeCRT allocation table | `stdlib/safe/src/safe_alloc.c:21-30,46-59` (`slot`, `g_tab`, `g_recent`) | same shape; separate table with its own lock and generation | private `static` in the C file |
| Modern-stdlib side table | `stdlib/error.hp:102-125` (`stdlib_meta_slot`, `stdlib_state_t`, weak `stdlib_state`) | `ptr`, `byte_size`, `elem_size`, `generation` for every `stdlib_alloc` block | header-internal; one weak instance per program |

The three stores are independent. There is no cross-table reconciliation and no
single global registry of every live pointer in the process.

### 1.2 Side-table layout (no inline headers)

- **libc `meta_slot`** — `void *ptr; size_t byte_size; size_t elem_size;
  unsigned long long generation;` (`libc/src/stdlib/alloc.c:24-29`). The
  public mirror `cplus_alloc_meta` adds `int is_live` and is asserted at
  `sizeof = 40`, alignment `8`, field offsets `0/8/16/24/32`
  (`tests/abi/abi_layouts.c:77-83`). The `meta_slot` itself is private and is
  **not** part of the measured public ABI.
- **SafeCRT `slot` / `cplus_safe_alloc_meta`** — the public struct is asserted
  with the identical `40/8` and `0/8/16/24/32` shape
  (`tests/abi/abi_layouts.c:117-123`); the private slot is in
  `stdlib/safe/src/safe_alloc.c`.
- **Modern `stdlib_meta_slot`** — `const void *ptr; size_t byte_size;
  size_t elem_size; unsigned long long generation;`
  (`stdlib/error.hp:102-107`). It is allocated as an open-addressed array
  (`stdlib_state.meta_tab`, `:117`) and resized with `calloc` + rehash
  (`:289-323`). Its size is not covered by the ABI probe because it is not a
  public type.

Because metadata is stored by **base address** in a table, a `T *` returned by
any of these allocators is still a one-word pointer. No length, capability, or
generation word is prepended to the allocation and none is carried in the
pointer value. The ABI probe confirms the platform baseline
`sizeof(void*) = sizeof(size_t) = sizeof(unsigned long) = 8` in both toolchains
(`build/abi/host.out:1`, `build/abi/cplus.out:1`).

### 1.3 Generation counters

- Two independent global counters exist: `g_gen` (`alloc.c:45`,
  `safe_alloc.c:29`) and `stdlib_state.meta_gen` (`stdlib/error.hp:121`).
- Both increment on every metadata record/transfer: `alloc.c:159,327,537`,
  `safe_alloc.c:98,107,304,524`, `stdlib/error.hp:329,341,510`.
- `stdlib_meta_forget()` tombstones the slot and resets its generation to `0`
  (`stdlib/error.hp:348-362`); the libc table similarly removes and records a
  tombstone (`alloc.c:185-197`).
- Query APIs expose the token: `cplus_alloc_check_generation` (`alloc.c:511-522`),
  `cplus_safe_ptr_check_generation` (`safe_alloc.c:484-493`), and
  `stdlib_alloc_generation` / `stdlib_alloc_check_generation`
  (`stdlib/error.hp:450-472`). Passing `0` means "no check / invalid"
  (`error.hp:463-464`).
- **Checked generation is opt-in.** The audited finding is explicit: a stale
  alias at a reissued address can free or transfer the *new* live block because
  the checked free/transfer paths do not compare a caller-supplied generation
  (finding F-01, `docs/libc/AUDIT_alloc.md:196`, `:249-265`). The modern
  `stdlib_transfer_ownership` also relies only on table membership
  (`stdlib/error.hp:494-515`).
- `size_t`/`unsigned long long` wrap is not guarded; a full wrap would make a
  stale token compare equal again (`docs/libc/AUDIT_alloc.md:213`;
  `tests/stdlib/agent2_audit/module_audits/vector.hp.md:219`). **[UNTESTED]**
  at wrap.

### 1.4 Allocation registry

- Open-addressed, power-of-two capacity tables with a splitmix64-style
  `uintptr_t` hash (`alloc.c:51-59`, `error.hp:263-271`). Empty slots are
  `NULL`; deleted slots are tombstones (`META_TOMB`, `STDLIB_META_TOMB` at
  `alloc.c:36` and `error.hp:253-254`).
- The libc table grows from an initial capacity and rehashes under one mutex;
  growth is bounded by `ncap <= SIZE_MAX / 4u / sizeof(meta_slot)`
  (`alloc.c:127-152`, `stdlib/error.hp:289-323`).
- Live counts/bytes are maintained symmetrically under the same lock
  (`alloc.c:39-48,189-197,544-574`; `stdlib/error.hp:116-121`). Permanent
  inflation after a raw host `free()` of a tracked pointer is a documented
  finding (F-02, `docs/libc/AUDIT_alloc.md:197`, `:267-279`).
- ABA guards re-find the old address after `realloc` and require
  `generation == old_gen && old_gen != 0` before adjusting bookkeeping
  (`alloc.c:316-335`, `safe_alloc.c:295-312`). The host `realloc` itself runs
  with the metadata lock released; the guard protects bookkeeping only, not
  concurrent stale-pointer dereferences (F-04,
  `docs/libc/AUDIT_alloc.md:199`, `:141-151`). **[UNTESTED]** under a hammering
  concurrency stress test.

### 1.5 Bounded recently-free cache

- **libc:** an open-addressed array of 1024 `recent_slot { void *ptr;
  unsigned long long generation; }` (`alloc.c:31-37,49`). Linear probing gives
  each distinct freed address its own slot until the table fills; on full it
  overwrites the bucket's first slot (`alloc.c:65-101`). The comment at
  `:65-68` records that this replaced a direct-mapped cache which
  misclassified real double frees on bucket collision.
- **SafeCRT:** still a direct-mapped `void *g_recent[RECENT_SLOTS]`,
  `RECENT_SLOTS = 1024` (`safe_alloc.c:21,30`); `cplus_safe_ptr_was_freed` is an
  address-only check (`safe_alloc.c:426-433`). The port of the open-addressed
  `{ptr, generation}` pattern is a recorded recommendation, not a change made
  here (`docs/libc/AUDIT_alloc.md:311-325`).
- Classification is explicitly **best-effort/heuristic**: a fresh host
  `malloc` at a just-freed C+ address can be counted as a double free, and a
  real double free can be counted as an invalid free when the table is full
  (F-05/F-06, `docs/libc/AUDIT_alloc.md:200-201`, `:311-325`).

### 1.6 Transfer and release

`stdlib_release()` tombstones the side-table entry, bumps telemetry, then calls
`cplus_release()` (`stdlib/error.hp:518-524`). Ownership transfer validates
handles, requires a live metadata slot, bumps the generation and nulls the
source (`stdlib/error.hp:494-515`; `alloc.c:524-542`;
`safe_alloc.c:511-530`). No wrapper changes the C declaration of the
underlying allocation/free functions.

---

## 2. ABI guarantees and the measured evidence

### 2.1 Thin pointers stay thin **[VERIFIED]**

The baseline line printed by both binaries is identical:
`abi_layouts: baseline sizeof(void*)=8 sizeof(size_t)=8 sizeof(unsigned long)=8`
(`build/abi/host.out:1`, `build/abi/cplus.out:1`). Metadata is out-of-band
(§1.2); no pointer is widened, tagged in its high bits, or replaced by a
fat-pointer struct. The project's own tooling change from native compiler to
`c+` C backend does not perturb this.

### 2.2 No public layout changes **[VERIFIED]**

Probe: `tests/abi/abi_layouts.c`. It asserts `sizeof`, `__alignof__`,
`offsetof`, and enumerator values for every public struct/enum in
`libc/install-include` and `stdlib/safe` (plus the `mbstate_t` adapter and the
anonymous C11 thread-result enum). Recorded result:

| Check | Result |
|---|---|
| `/usr/bin/clang -std=c99` build + run | `abi_layouts: 70/70 layouts match`, exit 0, 0 compiler warnings |
| `./build/c+ build --sanitize=none` (C backend) build + run | `abi_layouts: 70/70 layouts match`, exit 0, 0 program warnings |
| `diff -u build/abi/host.out build/abi/cplus.out` | byte-identical, 72 lines each, exit 0 |
| Mismatch lines in either output | 0 |

`N = 70` = 36 size/alignment + 21 field-offset + 13 enumerator-value
assertions (`docs/libc/ABI_EVIDENCE_PLAN.md:37-40`). Measured examples:

| Type | size | align | asserted offsets / values | Source |
|---|---:|---:|---|---|
| `cplus_alloc_meta` | 40 | 8 | `byte_size`@0, `elem_size`@8, `elem_count`@16, `generation`@24, `is_live`@32 | `tests/abi/abi_layouts.c:77-83` |
| `cplus_safe_alloc_meta` | 40 | 8 | identical offsets | `tests/abi/abi_layouts.c:117-123` |
| `cplus_safe_stat` | 48 | 8 | `api`@0, `calls`@24, `ok`@32, `fail`@40 | `tests/abi/abi_layouts.c:125-131` |
| `cplus_safe_platform_info` | 28 | 4 | seven `int`s at 0,4,…,24 | `tests/abi/abi_layouts.c:133-142` |
| `cplus_thrd_t` / `cplus_mtx_t` / `cplus_cnd_t` | 32 / 128 / 128 | 8 | opaque handles | `tests/abi/abi_layouts.c:86-91` |
| `errno_t` / `rsize_t` | 4 / 8 | 4 / 8 | typedef identity | `tests/abi/abi_layouts.c:163-166` |

The two archives `build/libc/libcplus_hardened.a` and
`build/safe/libcplus_safecrt.a` are genuinely on both link lines; the probe
itself uses only `sizeof`/`offsetof`/`printf`, so it does not depend on them
(`docs/libc/ABI_EVIDENCE_PLAN.md:42-48`).

Exact reproducing commands (from `tests/abi/run_abi_layouts.sh:6-24` and
`docs/libc/ABI_EVIDENCE_PLAN.md:10-26`):

```sh
sh tests/abi/run_abi_layouts.sh
# equivalent underlying steps:
/usr/bin/clang -O1 -std=c99 -Wall -Wextra -pedantic \
  -Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/abi/abi_layouts.c \
  build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
  -o build/abi/abi_layouts
./build/c+ build --sanitize=none tests/abi/abi_layouts.c \
  -Ilibc -Ilibc/install-include -Istdlib/safe \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  -o build/abi/abi_layouts.cplus -- \
  build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a
timeout 30 build/abi/abi_layouts > build/abi/host.out
timeout 30 build/abi/abi_layouts.cplus > build/abi/cplus.out
diff -u build/abi/host.out build/abi/cplus.out
tail -1 build/abi/host.out
```

### 2.3 No symbol renaming, no calling-convention change

- **[VERIFIED by source inspection]** The new safety surface is additive and
  keeps existing names. Public allocation entry points remain `cplus_malloc`,
  `cplus_calloc`, `cplus_realloc`, `cplus_free`, `malloc_s`, `calloc_s`,
  `realloc_s`, `free_s`, `aligned_alloc_s`, `cplus_aligned_alloc`
  (`docs/libc/AUDIT_alloc.md:38-49`, `:58-105`, `:88-105`). The modern side
  table is implemented as `static inline` functions in `stdlib/error.hp`, so it
  adds no new exported ABI symbol (`stdlib/error.hp:254-525`). The only
  verbatim upstream code in the whole scope, `memset_explicit`, was renamed to
  `cplus_memset_explicit` when vendored
  (`docs/stdlib/upstream-reuse.md:73-76`).
- **[VERIFIED by the new ABI-compat suite]** `tests/abi/run_abi_compat.sh`
  adds the evidence the original probe lacked. Its recorded run
  (`build/abi/compat/results.log`) ends with
  `RESULT: abi_compat: 69 passed, 0 failed`. It checks exported symbol names
  with `nm -gU` against three expected-symbol files and asserts no ` T __Z`
  C++ mangling (`run_abi_compat.sh:79-95,162-188`), and it exercises real
  C↔C+ calls with `abi_compat_record` **by value and by pointer** in both
  directions (`run_abi_compat.sh:124-160`). Per-probe summary lines:
  `abi_compat_pointers: 20/20`, `abi_compat_layouts: 80/80`,
  `abi_compat_c_driver: 13/13`, `abi_compat_cplus_caller: 10/10`. Details in
  §6.1.
- **[UNTESTED]** The original `abi_layouts` probe alone still does **not**
  verify symbol names, mangling, exports, visibility, ordinals, or symbol
  versioning, and it does **not** verify argument passing, struct-by-value
  rules, aggregate returns, varargs, stack alignment, or
  `__cdecl`/`__stdcall` rules (`docs/libc/ABI_EVIDENCE_PLAN.md:92-96`). The new
  suite closes the *name* and *C↔C+ call* cases on arm64 only; it does not
  disassemble call sites, does not test varargs, and says nothing about
  Windows x86 calling conventions or ordinals.
- **[UNTESTED]** All measured ABI numbers are Apple clang 16 / arm64-apple-darwin
  (LP64). Windows x86 (4-byte `long`/pointer/`size_t`, MSVC packing, different
  `mbstate_t`, different enum rules) was not measured. The concrete plan to
  close that gap — clang-cl/MSVC golden table, `#pragma pack` assertions, a
  calling-convention probe, `dumpbin /exports` audit, and an EXE-over-DLL
  round-trip — is recorded but explicitly **planned, not executed**
  (`docs/libc/ABI_EVIDENCE_PLAN.md:120-148`).

---

## 3. Limitations at foreign C boundaries

These are properties of the design, not gaps that a header-only library can
close. All are either documented findings or explicit call-site rules.

### 3.1 Uninstrumented precompiled C libraries

- Metadata covers only allocations made through the named C+ wrappers. A
  pointer produced by a precompiled, uninstrumented C library is invisible to
  every `is_live`/`size`/`generation` query; those queries return `0`/`-1`
  (`docs/libc/AUDIT_alloc.md:33-34`, `:67-71`).
- Conversely, a raw host `free()` of a tracked pointer leaves a false-live slot
  and permanently inflated counters (F-02, `docs/libc/AUDIT_alloc.md:197`).
  `cplus_aligned_alloc` is deliberately **untracked** and must not be passed to
  `cplus_free`; `aligned_alloc_s` is tracked and must be freed with `free_s`
  (F-03, `:198`, `:281-293`).
- No interposition is used. The proposed `cplus_alloc_forget` /
  `cplus_safe_alloc_forget` and `cplus_aligned_free` APIs are recommendations in
  `docs/libc/AUDIT_alloc.md:267-293`, not functions added by this work.

### 3.2 Arbitrary `malloc` pointers and interior pointers

- `cplus_alloc_remaining_capacity` accepts interior addresses by range check,
  so an arbitrary address that happens to fall inside a live range is reported
  as valid capacity. This is a **diagnostic hint, not a provenance proof**
  (`docs/libc/AUDIT_alloc.md:66`, F-16/F-19 at `:211,214`).
- All other liveness/size/meta APIs require the exact base address; interior
  pointers report non-live (F-16, `docs/libc/AUDIT_alloc.md:211`).
- Bounded helpers rely on `__builtin_object_size` plus the tracked table; when
  capacity is unknown the default mode proceeds **unchecked**, and the source
  capacity of bounded `memcpy`/`memmove` is never independently verified
  (F-12, `docs/libc/AUDIT_alloc.md:207`, `:311-325`;
  `docs/libc/AUDIT_memory_string.md:147-148`, `:426-439`).
- The metadata gives no C abstract-machine provenance, no interior-object
  identity, and no protection against host-allocator mixing
  (`docs/libc/AUDIT_alloc.md:32-34`).

### 3.3 Address reuse

- Checked free/transfer without an expected generation can act on a *new* live
  block at a recycled address (F-01, `docs/libc/AUDIT_alloc.md:196`). The
  generation check exists but is opt-in (§1.3).
- The recently-free cache is address-based and bounded at 1024 entries; after
  enough other frees a genuinely freed pointer reports "not freed", and a
  recycled address reports "freed" (F-05/F-06,
  `docs/libc/AUDIT_alloc.md:200-201`).
- The `realloc` ABA guard covers bookkeeping only; it cannot make a raw stale
  alias safe (§1.4, F-04).
- Generation wraparound is unguarded (§1.3). **[UNTESTED]** at wrap.

### 3.4 Non-local exits

- `setjmp`/`longjmp`, `pthread_cancel`, exceptions4c unwinding, signal handlers,
  `abort`, `quick_exit`, and `_Exit` do not run the side-table cleanup path.
  Cleanup is explicit (`stdlib_release`, `drop`, `close`) or compiler-inserted
  for owning classes; no metadata registration is undone on a non-local exit,
  and no unwind hook was added here. **[UNTESTED]** — no fixture exercises
  longjmp/cancellation against the side table.
- The error callback has a per-thread re-entrancy guard
  (`stdlib/error.hp:96-100`), but cross-thread callback behaviour and
  contention on the metadata lock are not exercised by any committed fixture
  (`tests/stdlib/agent2_audit/module_audits/error.hp.md:219`, §7 item 5).
- `cplus_check_live` is fatal and only understands C+ allocator blocks, so it
  cannot serve as a recoverable `slice::valid()` query for stack or embedded
  owners (`docs/stdlib/upstream_study/safer_cplusplus.md:453-457`).
- The nested-UBSan `InReport` limitation after `longjmp` recovery is documented
  and **must not be claimed as fixed** by this work
  (`docs/stdlib/upstream_study/llvm_clang.md:694-695`).

---

## 4. Upstream projects inspected

All nine study documents under `docs/stdlib/upstream_study/` were read for this
document (`softboundcets.md` and `trapc.md` landed during authoring). Revision,
license and file inventory are quoted from those documents; no additional
upstream fetch was performed for this task.

| # | Project | Exact revision / pin | Exact license identifier | Source files actually inspected (per study) | Study doc |
|---|---|---|---|---|---|
| 1 | Microsoft STL | `main` @ `0cabaf3c7e0a73fe3040aabce40cd8833e80392b` (2026-11-21) | `Apache-2.0 WITH LLVM-exception` | `stl/inc/{vector,xstring,xmemory,xutility}`; tests `Dev10_579381_vector_grow_to`, `GH_005472_do_not_overlap`, `GH_002030_asan_annotate_{vector,string}`, `P0966R1_string_reserve_should_not_shrink`, `P1206R7_string_append_range` | `upstream_study/microsoft_stl.md` |
| 2 | Microsoft GSL | `main`, fetched 2026-11-23; `span` blob `090a8bd917d2fb96a2401577f50ed2b30012c437` | `MIT License (MIT)`, Copyright (c) 2015 Microsoft Corporation | `include/gsl/{span,pointers,narrow,assert,util,span_ext}`, `LICENSE`; tests `span_tests.cpp`, `notnull_tests.cpp`, `owner_tests.cpp`, `assertion_tests.cpp`, `utils_tests.cpp` | `upstream_study/microsoft_gsl.md` |
| 3 | SaferCPlusPlus | `master`, shallow-clone HEAD `5edaf67acf746d9ee472a06a51094b31a3db57c0` | `BSL-1.0`, Copyright (c) 2015 Noah Lopez | `include/{msepointerbasics,msescope,mseregistered,mserefcounting,msemsearray,msemsevector,mseasyncshared}.h`; `apps/legacy_examples/msetl_example/{msevector_test.h,msetl_example_defs.h}` | `upstream_study/safer_cplusplus.md` |
| 4 | Microsoft Secure CRT (documentation only) + vendored safeclib / Intel | Microsoft Learn pages M1–M10 (no source fetched); safeclib `90b076e94c4b0fb202dddf948d5d620e5395c1b0`; Intel `f4491915cc682556ed64d4eb6a2461d8eed2eb0c` | Microsoft docs only; safeclib MIT-style (Cisco 2012–2013, Reini Urban 2017); Intel MIT (2014–2018, incl. Cisco notice) | `third_party/safeclib/src/{mem/memset_s.c,str/strcpy_s.c,str/strncpy_s.c,io/fopen_s.c,io/fprintf_s.c,io/gets_s.c,str/safe_str_constraint.c,abort_handler_s.c,ignore_handler_s.c,...}`; Intel `safeclib/{strcpy_s.c,memset_s.c}` | `upstream_study/secure_crt.md` |
| 5 | Cake | `main` @ `2da2afd1759a7f3a62024ac781af65c8832bbe5f`, `CAKE_VERSION "0.17.07"` | `GPL-3.0-or-later` + AI-training-use addendum — **REJECTED** | `README.md`, `LICENSE`, `quickref.md`, `flow.md`, `manual.md`, `src/cake_compat.h`, `src/{defer.h,flow.h,flow_branch.h}`, `src/include/{stddefer.h,stdlib.h,stdio.h,threads.h}`, defer/flow3 tests | `upstream_study/cake.md` |
| 6 | Fil-C | docs + raw `deluge` branch; raw `stdfil.h` = `FILC_VERSION 687u` | `LLVM-LICENSE.txt` = Apache-2.0 with LLVM Exceptions (API reports `NOASSERTION`/`other`); fetched runtime files carry per-file BSD-2-clause, Copyright (c) 2023–2026 Epic Games / 2024–2026 Filip Pizlo | `fil-c.org/{how,invisicaps,fugc,runtime,safepoints,stdfil}.html`; `libpas/src/libpas/fugc.h`; `filc/include/stdfil.h` | `upstream_study/fil_c.md` |
| 7 | LLVM / Clang | tag `llvmorg-20.1.1` = `424c2d9b7e4de40d0804dd374721e6411c27d1d1` | `Apache-2.0 WITH LLVM-exception` | `LICENSE.TXT`; `compiler-rt/include/sanitizer/{asan_interface.h,ubsan_interface.h,common_interface_defs.h,lsan_interface.h,hwasan_interface.h,tsan_interface.h,coverage_interface.h}`; `libcxx/include/{vector,string,__vector/vector.h,__assert,__verbose_abort,__debug_utils/sanitizers.h}`; `libc/src/string/memset_explicit.cpp`; clang `AttrDocs.td`; selected libcxx tests | `upstream_study/llvm_clang.md` |
| 8 | SoftBoundCETS (`santoshn/softboundcets-34`) | `master` HEAD `9a9c09f04e16f2d1ef3a906fd138a7b89df44996` (2014-09-19); no tags exist | GitHub SPDX `BSD-3-Clause`; top-level `LICENSE` © 2013 Santosh Nagarakatte with an unfilled `{organization}` placeholder; per-file MIT-style notices © 2011/2014 (Nagarakatte, Martin, Zhao, Zdancewic, UPenn) | `softboundcets-lib/{softboundcets.h,softboundcets.c,softboundcets-checks.c,softboundcets-wrappers.c}`; `softboundcets-llvm-clang34/lib/Transforms/SoftBoundCETS/SoftBoundCETS.cpp` (selected ranges); `README.md`, `LICENSE` | `upstream_study/softboundcets.md` |
| 9 | TrapC (Robin Rowe / TRASEC / Heroic Robots) | **N/A — no repository, commit or tag exists**; only WG14 paper `n3423` (2025-01-07) and trapc.org posts (2026-01-26 … 2026-02-02) | **UNDETERMINED — no LICENSE published**; only "free open source software" intent. Do **not** assign MIT/BSD/SPDX; the literal `"Open Source MIT"` in n3423 is a Cmaker tutorial example, not TrapC's license | **No source files inspected — none published.** Primary documents only: `n3423.pdf` (WG14), trapc.org About / "TrapC a Year Later", The Register 2026-01-26 | `upstream_study/trapc.md` |

SoftBoundCETS is directly relevant to the ABI claim: its study concludes that
the pointer **representation is unchanged** (single word, no fat pointers), so
it is layout-compatible with C+ thin pointers, but that the hidden
shadow-stack calling convention, `softboundcets_` symbol renaming, `main`
wrapping, 8-byte slot granularity and 64-bit-only huge virtual reservations
make library-only equivalence impossible
(`upstream_study/softboundcets.md:326-367,536-551`). That matches the split in
§2.1–§2.3: layout is measured, calling convention and symbols are not.

Cumulative reuse decisions are recorded in `docs/stdlib/upstream-reuse.md:9-40`.
Only permissive licenses were treated as reusable; Cake was rejected before
this study and remains idea-level traceability only
(`docs/stdlib/upstream-reuse.md:21-25`). No Microsoft CRT source was fetched or
copied (`upstream_study/secure_crt.md:3-9`).

`softboundcets.md` and `trapc.md` both landed while this document was being
written and are included above (rows 8–9). TrapC's study is documentation-only:
no repository, commit, license file or source file exists publicly, so its
license is **UNDETERMINED** and its mechanism descriptions are marked
`[DOCUMENTED]`, not source findings
(`upstream_study/trapc.md:26-37,92-121,144-150`). `docs/stdlib/upstream-reuse.md`
was written against the original seven-study set and does not yet list
SoftBoundCETS or TrapC; §4 rows 8–9 are the newer inventory entries.

---

## 5. Code and algorithm adaptations actually made

No upstream source was copied into the runtime metadata architecture of §1.
The tables, hashes, tombstones, generation checks and recent-free caches were
implemented independently in `libc/src/stdlib/alloc.c`,
`stdlib/safe/src/safe_alloc.c` and `stdlib/error.hp`. The conceptual convergence
with upstream is documented, not a code port: Fil-C's "liveness token exposed
as a query" maps to `stdlib_alloc_generation`/`check_generation`
(`upstream_study/fil_c.md:312-318`); SaferCPlusPlus's registered-pointer and
norad registries map to the same liveness-token idea
(`upstream_study/safer_cplusplus.md:512-518`). Fil-C's own metadata is trusted
by check code on every access and belongs to its compiler/runtime
(`upstream_study/fil_c.md:93-99`), while the study explicitly records that
**Cake** has no user-level allocation tracking to copy and that this
repository is already ahead there (`upstream_study/cake.md:106-108`). Likewise,
**no SoftBoundCETS code was copied**: its four-field `{base, bound, key, lock}`
record, O(1) lock-zeroing invalidation, free-map tombstones, monotonic
max/min bounds shrink and overflow-safe range predicate are recorded as
*ideas* for `error.hp`/`slice` in `upstream_study/softboundcets.md:383-455`,
while its mmap'd tables, `abort()` failure policy, `size > dest_bound` memcpy
typo and approximate `+1024` bounds are on that study's do-not-copy list
(`:456-463`). **No TrapC code exists to copy**, and none was used; its study
records only idea-level transfers (queryable per-pointer metadata,
offset-to-base recovery, caller-local failure handling)
(`upstream_study/trapc.md:318-358`) and explicitly rejects reuse because
hidden RTTI, no-op `free()`, trap-aware signatures and automatic lifetime
management would change the pointer contract and C signatures
(`:260-314,362-382`).

| C+ surface / file | Upstream source cited | Adaptation actually made | Evidence |
|---|---|---|---|
| `stdlib/vector.hp` | MS STL `stl/inc/vector`; GSL `include/gsl/span`; LLVM `libcxx/include/__vector/vector.h` | Subtract-form overflow checks, commit-last mutation with explicit rollback, `at_checked` + generation-stamped `slice`; C+ keeps power-of-two doubling as documented behaviour | `docs/stdlib/upstream-reuse.md:62` |
| `stdlib/string.hp` | MS STL `stl/inc/xstring`; GSL `pointers`; SaferCPlusPlus `msepointerbasics.h`; `strcpy_s.c` | Overlap-safe append, validation at accessors, source/lifetime token idea; tier-2 `error *` reporting instead of exceptions; `string_view` remains caller-responsible | `docs/stdlib/upstream-reuse.md:63` |
| `stdlib/io.hp`, `stdlib/file.hp` | `gets_s.c`, `fopen_s.c`, `fprintf_s.c`; MS STL `xstring` | Bounded drain-to-delimiter loops, always NUL-terminate, owned-vs-borrowed handle discipline, `STDLIB_OK/EOF/FAIL` instead of Annex K names | `docs/stdlib/upstream-reuse.md:64` |
| `stdlib/collections.hp` | MS STL `vector`; GSL `span`; SaferCPlusPlus `msemsevector.h` | Open addressing with tombstones, power-of-two capacity, 70% load guard, allocate-all-then-commit rollback | `docs/stdlib/upstream-reuse.md:65` |
| `stdlib/error.hp` (metadata) | GSL `narrow`; Fil-C `fugc.h`, `stdfil.h`; LLVM `asan_interface.h` | First-class liveness/length queries with explicit unknown/freed results, generation-stamped views, two-phase narrowing; **no** capability pointers and no compiler annotations | `docs/stdlib/upstream-reuse.md:66` |
| `stdlib/thread.hp` | MS STL lock patterns; SaferCPlusPlus `mseasyncshared.h` | RAII guard ownership, guarded destruction, watchdog-bounded waits reduced to `mutex`/`guard` classes | `docs/stdlib/upstream-reuse.md:67` |
| `json.hp`, logging | LLVM; vendored yyjson; `abort_handler_s.c` | Failure-atomic commit, constraint-handler discipline, bounded write loop; yyjson reused as-is, its non-injectable allocator recorded as SKIP | `docs/stdlib/upstream-reuse.md:68` |
| `libc/src/c23_upstream_small.c` | LLVM libc `libc/src/string/memset_explicit.cpp` at `llvmorg-20.1.1`, blob `a3e4dad874397463cb5315679f99a312ac3a53fe` | The **only verbatim upstream code** in scope; five documented adaptations (rename, `noinline` spelling, `inline_memset` → `cplus_memset`, `__asm__`, one log marker) | `docs/stdlib/upstream-reuse.md:73-76`; `upstream_study/llvm_clang.md:779-821` |
| Scripts / vendored trees | safeclib, Intel safestringlib | Vendored and compiled by `scripts/build_safecrt.sh`: 40 INTEGRATED, 10 ADAPTER_ONLY, 387 NOT_INTEGRATED; no new upstream source added here | `docs/stdlib/upstream-reuse.md:77-80`; `upstream_study/secure_crt.md:219-282` |

Upstream tests were **not copied verbatim**. Cases were adapted as ideas:
`GH_002030_asan_annotate_{vector,string}` → UBSan/strict deep tests;
`GH_005472_do_not_overlap` → `deep_string_append.cp` self/overlap cases;
GSL `span_tests.cpp` → `at_checked` boundary tests; SaferCPlusPlus
`msevector_test.h` → generation/slice tests; Cake `defer`/`flow3` →
guard/early-return tests (idea only, license rejected)
(`docs/stdlib/upstream-reuse.md:93-102`).

No upstream test was **executed** as part of the studies
(`upstream_study/microsoft_stl.md:519`, `upstream_study/safer_cplusplus.md:541-543`,
`upstream_study/llvm_clang.md:868-870`).

---

## 6. Test results with exact commands and counts

All counts below are quoted from recorded result files already in the
checkout. This author did not run a test runner for this document.

### 6.1 ABI layout probe — executed **[VERIFIED]**

`sh tests/abi/run_abi_layouts.sh` → `70/70 layouts match` on both toolchains,
`diff` exit 0, 0 mismatches; artifacts `build/abi/host.out` and
`build/abi/cplus.out` (72 lines each), verified byte-identical by `diff -u`.
Details and commands in §2.2.

**New `abi_compat_*` suite — executed [VERIFIED].** These files landed while
this document was being written and were read:

- `tests/abi/abi_compat_pointers.c` — real-host-C probe asserting data and
  function pointers are flat 8-byte values, `uintptr_t`/`void *` memcpy
  round-trips preserve bits, pointer equality and callability, and null
  pointers are all-zero bits (`:25-118`). Its summary line is
  `abi_compat_pointers: N/N pointer ABI checks match`.
- `tests/abi/abi_compat_layouts.c` — real-host-C probe that re-asserts the
  public layouts plus `abi_compat_record` (`sizeof 32`, align 8, offsets
  `i@0, u@4, d@8, p@16, n@24`) and checks libc/SafeCRT meta-struct equality
  (`:68-169`). Its summary line is `abi_compat_layouts: N/N layouts match`.
- `tests/abi/abi_compat_shared.h` — plain-C declarations consumed by both
  compilers; `abi_compat_record` has no C+ ownership annotations (`:24-32`).
- `tests/abi/abi_compat_cplus_lib.cp` — real C+ source lowered by `./build/c+`,
  exporting plain-C-signature functions (`abi_cplus_add`, by-value and
  by-pointer record calls, `abi_cplus_meta_write`) (`:12-83`).
- `tests/abi/abi_compat_c_driver.c` — real `/usr/bin/clang`-compiled C caller
  that checks struct-by-value/pointer round-trips and reads a
  `cplus_alloc_meta` written by C+ (`:52-80`).
- `tests/abi/abi_compat_c_part.c`, `tests/abi/abi_compat_cplus_caller.cp` —
  the reverse direction: a precompiled C archive called from real C+ code
  (`run_abi_compat.sh:138-160`).
- `tests/abi/abi_compat_header_neutrality.c` — ordinary C layouts printed with
  and without `-include cplus_libc.h -include cplus_libc_safe.h`, then `diff`ed
  (`run_abi_compat.sh:190-203`).
- Three `tests/abi/abi_compat_expected_*_symbols.txt` files — the nm symbol
  allow-lists for the export check.
- `tests/abi/run_abi_compat.sh` — restricted-PATH runner; `set -u`, no
  `|| true`, bounded `timeout`s, per-step RC logging.

Recorded run artifact `build/abi/compat/results.log` ends with
`RESULT: abi_compat: 69 passed, 0 failed`. Exact command:

```sh
sh tests/abi/run_abi_compat.sh       # summary: "abi_compat: 69 passed, 0 failed"
```

Per-probe final lines in `build/abi/compat/*.out`:

| Probe | Recorded line |
|---|---|
| `pointers.out` | `abi_compat_pointers: 20/20 pointer ABI checks match` |
| `layouts.out` | `abi_compat_layouts: 80/80 layouts match` |
| `c_driver.out` | `abi_compat_c_driver: 13/13 C->C+ boundary checks match` |
| `cplus_caller.out` | `abi_compat_cplus_caller: 10/10 C+->C boundary checks match` |

The remaining passes are the symbol-presence/absence checks, the two archive
builds, and the header-neutrality `diff` (exit 0, byte-identical `1822`-byte
outputs); `run_abi_compat.sh:206-212` exits non-zero if any step failed.

All results are on **arm64-apple-darwin / Apple clang 16** only; this suite does
not supply Windows x86 export or calling-convention evidence (§2.3).

### 6.2 Agent 2 aggregate regression suite — executed **[VERIFIED]**

Command (from `tests/stdlib/agent2/run_all.sh:1-20`):

```sh
sh tests/stdlib/agent2/run_all.sh
# per-test pattern recorded in tests/stdlib/agent2/results/*.commands:
build/c+ build tests/stdlib/<test>.cp --strict --sanitize=none -o <run>/<test>
<run>/<test>   # under timeout, with stdin fixture and byte-exact stdout/stderr fixtures
```

Recorded run: `tests/stdlib/agent2/results/summary.txt:1-56` and
`results/summary.json` (head `0261270`, `build_timeout=90`, `run_timeout=30`):

| PASS | FAIL | BUILD_FAILURE | TIMEOUT | SKIPPED | BLOCKED | TOTAL |
|---:|---:|---:|---:|---:|---:|---:|
| 50 | 0 | 0 | 0 | 0 | 0 | 50 |

Grouping in `summary.txt:5-54`: 28 `deep_*` tests, 11 `agent2/*` tests (including
`cross_module`, `extra_error_metadata`, `extra_string_vector_alias`), and 11
curated existing tests (including `stdlib_hardening_test`,
`containers_safety_test`, `foundation`, `negative`). Every row shows build
exit 0 and run exit 0.

**New `abi_safety_*` sources — present, no recorded result [UNTESTED].** Two
files landed while this document was being written:

- `tests/stdlib/agent2/abi_safety_probe.cp` — temporary toolchain/API probe
  (`:1`) checking 12 named conditions: `stdlib_alloc` size/generation,
  `stdlib_release` zeroing the generation, `malloc_s` metadata and
  remaining-capacity (including an interior pointer), and `free_checked_s`
  success/double-free/stack-pointer refusal (`:32-47`), printing
  `ABI_SAFETY <checks> <failures>` (`:49`).
- `tests/stdlib/agent2/abi_safety_alloc_lifecycle.cp` — dedicated side-table
  coverage (`:1-25`): alloc/release with `meta_live` and telemetry balance (A),
  non-fatal stale-alias detection via generation/`query_length`/transfer
  refusal (B), untracked stack-address refusal (C), use-after-free detection
  through metadata queries only with the stale pointer never dereferenced (D),
  64 repeated cycles (E), and an explicit SKIP for a nonexistent custom
  allocator plug-in point (F) (`:63-206`). A literal second `stdlib_release`
  is deliberately not called because it is fatal by design (`:109`). The final
  line is `ABI_SAFETY <checks> <failures>` (`:208`).

Neither appears in the 50-test aggregate above (that summary predates them),
and no result artifact was recorded at final check. No result is claimed here.

### 6.3 Sanitizer matrix — executed **[VERIFIED]**

Recorded in `tests/stdlib/agent2/SANITIZER_MATRIX.md` and quoted in
`docs/stdlib/upstream-reuse.md:112-114`:

- 9 deep tests × 8 configurations = **72 build attempts**; **27 executed**
  (`strict_none`, `ubsan`, `integer` only); **23 PASS**; **4 FAIL**.
- The 4 FAIL are genuine `-fsanitize=integer` reports from
  `runtime/cplus_runtime.c:174:5` (the splitmix64 multiplier
  `x *= (uintptr_t)0xbf58476d1ce4e5b9ULL`), recorded as FAIL, not UNSUPPORTED
  (`SANITIZER_MATRIX.md:133-143`).
- `address`, `leak`, `thread`, `memory`, `cfi` were rejected by the driver
  build probe: no binary, no run — UNSUPPORTED, **not PASS**
  (`SANITIZER_MATRIX.md:123-146`). Host Apple clang 16 ASan additionally aborts
  at startup even for `int main(){return 0;}`
  (`upstream_study/llvm_clang.md:664-670`).

Representative exact command (from `SANITIZER_MATRIX.md:160-170`):

```sh
timeout 30 build/c+ build tests/stdlib/deep_collections_alloc.cp \
  --strict --sanitize=none -o "$OUT/deep_collections_alloc__strict_none"
timeout 30 "$OUT/deep_collections_alloc__strict_none" < /dev/null
```

### 6.4 Other recorded aggregates **[VERIFIED as recorded]**

- Three-mode deep runner: 28 tests × 3 modes = **84 invocations**, 0 failures
  (`docs/stdlib/upstream-reuse.md:110-111`).
- `tests/run_all_evidence.sh`: 23 aggregate suites, 0 failures (same citation).
- `docs/libc/ABI_EVIDENCE_PLAN.md:33-35` records 0 compiler warnings and 0
  program warnings for the ABI probe (the driver emits two benign
  `linker input unused` warnings for the archives,
  `ABI_EVIDENCE_PLAN.md:42-46`).

No live-game, Wine, DLL-injection, or Windows x86 execution is claimed by this
document.

---

## 7. Performance overhead

### 7.1 What was actually measured **[VERIFIED as recorded]**

`benchmarks/safety_compare/RESULTS.md` contains real measurements from
2026-11-22 and 2026-11-23 (macOS arm64, Apple M3 Pro, Apple clang 16, rustc
1.100.0, Goose v2026.4). Both C+ variants were built at `-O2` (`--release`);
safe mode adds `-fsanitize=undefined,bounds,null,alignment`,
`-fno-sanitize-recover=all`, `-fno-omit-frame-pointer`; legacy mode is
`-std=gnu89 -DCPLUS_LEGACY_C=1` with no sanitizers
(`RESULTS.md:104-115`).

| Workload | C+ safe (median) | C+ legacy (median) | Recorded ratio |
|---|---:|---:|---:|
| loop sum (2e8), `RESULTS.md:11,14` | 0.1075s | 0.0038s | 29.85x vs plain C; ~30x safe-vs-legacy on this workload |
| nested (4000²), `RESULTS.md:12,14` | 0.0090s | 0.0023s | 3.90x vs plain C |
| modern stdlib workload (7 interleaved runs), `RESULTS.md:119-126` | 0.0140s (min 0.0134, max 0.0188) | 0.0061s (min 0.0052, max 0.0077) | 2.31x median vs C+ legacy |

**Scope of that evidence:** the "modern" workload exercises
`stdlib/string.hp` and `stdlib/vector.hp` with 2,000,000 `push_back` + `at`
reads and 200,000 `append` calls; all variants printed checksum
`2200000200000` (`RESULTS.md:47-61,119-124`). The observed difference is
attributed in the source to safe-mode UBSan instrumentation applied to the
lowered stdlib code, not to a separately measured metadata cost
(`RESULTS.md:128-137`). The document explicitly warns that non-instrumented
compilers folded the integer loops into closed forms, that Rust/Goose numbers
are cross-checks rather than apples-to-apples comparisons, and that variance
on this host is material (ratio ranged ~1.8x–3.1x across three
non-interleaved passes) (`RESULTS.md:17-33,128-145`). Fil-C was **not measured**
(no toolchain installed) (`RESULTS.md:28-29,150-151`).

### 7.2 What is NOT measured

- **No isolated overhead number exists for the side table, generation counters,
  allocation registry, or recently-free cache of §1.** No benchmark in this
  checkout toggles those mechanisms independently of safe-mode sanitizers, and
  no per-query cost was recorded. No number is invented here. **[UNTESTED]**
- No Windows x86 performance figure, no foreign-C-boundary overhead figure, and
  no multi-threaded contention figure for `meta_lock` was measured.

### 7.3 Exact commands to measure it

Reproduce the **recorded** whole-mode comparison from `RESULTS.md:63-102`:

```sh
mkdir -p build/bench_safety
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build --release \
  benchmarks/safety_compare/modern.cp -o build/bench_safety/cplus_safe_modern
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build --release --legacy-c \
  benchmarks/safety_compare/modern.cp -o build/bench_safety/cplus_legacy_modern
CPLUS_GOOSE=/Users/haseeb-mir/.local/bin/goose \
  "$CPLUS_GOOSE" build benchmarks/safety_compare/modern.goose \
  -o build/bench_safety/scoped_apps
rustc -O benchmarks/safety_compare/modern.rs -o build/bench_safety/rust_modern

python3 - <<'PY'
import subprocess,time,statistics
out='build/bench_safety'
variants=['cplus_safe','cplus_legacy','rust','goose']
bins={v:f'{out}/{v}_modern' for v in variants}
for v in variants: subprocess.run([bins[v]],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
samples={v:[] for v in variants}
for rnd in range(7):
    for v in variants:
        t=time.perf_counter(); subprocess.run([bins[v]],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
        samples[v].append(time.perf_counter()-t)
for v in variants:
    s=samples[v]; print(f'{v:14s} {statistics.median(s):8.4f}s  min={min(s):.4f} max={max(s):.4f}')
PY
```

Reproduce the original loop/nested comparison:

```sh
CPLUS_GOOSE=<path> sh benchmarks/safety_compare/run_bench.sh
```

To obtain the **missing isolated** metadata overhead, a new experiment is
required (not run here): time a fixed loop of `stdlib_alloc` +
`stdlib_alloc_size` / `stdlib_alloc_generation` / `stdlib_alloc_check_generation`
calls with `--sanitize=none`, against the same loop with the metadata calls
removed, using the same 1-warmup / 7-interleaved-run / median discipline as
above. No existing build flag disables the metadata in §1, so no such number
can be reported today.

---

## 8. Explicit non-goals — compiler/runtime work owned by Agent 1

Nothing in this document proposes the following. Each is either a compiler or
runtime responsibility, or is blocked by language/toolchain limits, and each is
named as such in the cited studies:

1. **Fat pointers, capabilities, pointer tagging, per-load bounds checks, GC,
   store barriers, safepoints, custom panics, custom ABI and loader work.** All
   are Fil-C compiler/loader properties; "there is no interoperability with
   Yolo-C" and the whole program plus all dependencies must be compiled with
   Fil-C (`upstream_study/fil_c.md:252-291`, `:287-291`). Fil-C also does not
   support 32-bit systems (`:373-377`). The same holds for SoftBoundCETS's
   instrumenting LLVM 3.4 pass, mmap'd trie/lock/free-map spaces, shadow stack,
   symbol prefixing and `main` wrapping
   (`upstream_study/softboundcets.md:467-494`).
2. **`[CPLUS-BOUNDS]` / `[CPLUS-LIFETIME]` / `[CPLUS-OWNER]` / `[CPLUS-RAII]` /
   `[CPLUS-FFI]` static analysis and tagged diagnostics** that work with
   `--sanitize=none`; `compiler/Analysis/BoundsAnalysis.cpp` is a placeholder
   and UBSan-only aborts do not count
   (`upstream_study/llvm_clang.md:676-691`, `:697-703`).
3. **Sanitizer instrumentation and runtime callbacks** (`__asan_*`,
   `__sanitizer_*`, shadow memory, quarantine, sanitizer option hooks,
   hardening-tier selection). The public UBSan header declares exactly one
   function, `__ubsan_default_options`, so a stdlib cannot opt into checks by
   linking a header (`upstream_study/llvm_clang.md:192-209`, `:704-713`). ASan
   container annotations are deliberately not added
   (`docs/stdlib/upstream-reuse.md:125-126`).
4. **`diagnose_if` / `enable_if` / `lifetimebound`, `_Owner` / `_View` /
   `_Dtor` / `_Clear` annotations, `compile_assert`, `static_debug`, `//lint`,
   `_Defer`, and defer lowering.** These are compiler-owned or Cake-only
   surface (`upstream_study/llvm_clang.md:599-602`;
   `upstream_study/cake.md:146-161`).
5. **Generic `span<T,E>`, `not_null<T>`, `narrow<T,U>`, templates, concepts,
   SFINAE, exceptions, `std::terminate` contracts, iterator proxy registries,
   SSO unions, and RAII scope guards whose destructor performs cleanup.** C+
   has no templates, overloading, exceptions, or proxy state
   (`upstream_study/microsoft_gsl.md:251-309`;
   `upstream_study/microsoft_stl.md:453-489`).
6. **Replacing the generation-based borrow scheme with a borrow checker or a
   full runtime pointer registry.** The intrusive registered-pointer list and
   norad counter of SaferCPlusPlus require pointer wrapper classes with hooks in
   copy/move/destruct that `.hp` does not have
   (`upstream_study/safer_cplusplus.md:475-481`).
7. **Vendoring Cake.** GPL-3.0 plus the AI-training-use addendum; already
   rejected by this project. No Cake source, header, or test was copied
   (`docs/stdlib/upstream-reuse.md:17,21-25`).
8. **Any claim of memory safety, live-game success, or Windows x86 binary
   compatibility from the evidence in this document.** Passing fixtures are
   contract-conformance evidence, not memory-safety proof
   (`tests/stdlib/agent2_audit/module_audits/vector.hp.md:21-28`).

---

## 9. VERIFIED / UNTESTED summary

| Claim | Status | Evidence |
|---|---|---|
| Thin pointers unchanged; no inline metadata | **VERIFIED** (both toolchains) | `build/abi/host.out:1`, `build/abi/cplus.out:1`; `libc/src/stdlib/alloc.c:24-49`; `stdlib/error.hp:102-125` |
| Public `sizeof`/alignment/field-offset/enumerator layout unchanged | **VERIFIED** (70/70 × 2, byte-identical) | `tests/abi/abi_layouts.c:77-166`; `docs/libc/ABI_EVIDENCE_PLAN.md:33-40`; §2.2 |
| Existing symbol names and call signatures retained; new helpers are `static inline` | **VERIFIED by source inspection + nm audit** | `docs/libc/AUDIT_alloc.md:38-49,58-105`; `stdlib/error.hp:254-525`; `build/abi/compat/results.log` (`69 passed, 0 failed`) |
| Exported names unmangled; C↔C+ struct-by-value/pointer calls; header neutrality (arm64 only) | **VERIFIED** | `build/abi/compat/results.log`; `run_abi_compat.sh:79-188` |
| Windows x86 ABI, varargs, disassembly-level calling convention | **UNTESTED** | `docs/libc/ABI_EVIDENCE_PLAN.md:90-110,120-148`; §2.3 |
| Allocation registry, generation counters, side table, recent-free cache behave as specified in audited paths | **VERIFIED by source + recorded fixtures** | `docs/libc/AUDIT_alloc.md:58-105,194-215`; `tests/stdlib/agent2/results/summary.txt` (50/0/0/0/0/0) |
| Stale alias at a reissued address, generation wrap, concurrent `realloc`, host-`free` bypass | **UNTESTED / documented finding** | F-01/F-02/F-04/F-18 in `docs/libc/AUDIT_alloc.md:196-215` |
| Non-local exits (`longjmp`, cancellation, signals, unwind) interacting with the side table | **UNTESTED** | §3.4; `upstream_study/llvm_clang.md:694-695` |
| `softboundcets.md` input | **VERIFIED as a source study** (landed during authoring, incorporated) | §4 row 8, §5, §8 |
| `trapc.md` input | **VERIFIED as a documentation-only study** (no source, no license, no commit) | §4 row 9, §5, §8 |
| `abi_compat_*` suite | **VERIFIED executed** — `69 passed, 0 failed`; per-probe 20/20, 80/80, 13/13, 10/10 | §6.1, `build/abi/compat/results.log` |
| `abi_safety_probe.cp` / `abi_safety_alloc_lifecycle.cp` | **UNTESTED** — sources present and read, no recorded result artifact | §6.2 |
| Safe-vs-legacy whole-mode overhead on two recorded workloads | **VERIFIED as recorded** (real medians, no invented numbers) | `benchmarks/safety_compare/RESULTS.md:9-15,117-145` |
| Isolated metadata overhead | **UNTESTED** — no number exists | §7.2, §7.3 |

---

## 10. Evidence inventory

Primary files read for this document:

- `tests/abi/abi_layouts.c`, `tests/abi/run_abi_layouts.sh`,
  `build/abi/host.out`, `build/abi/cplus.out`,
  `docs/libc/ABI_EVIDENCE_PLAN.md`
- New ABI boundary sources and their recorded run:
  `tests/abi/run_abi_compat.sh`, `tests/abi/abi_compat_layouts.c`,
  `tests/abi/abi_compat_pointers.c`, `tests/abi/abi_compat_shared.h`,
  `tests/abi/abi_compat_c_driver.c`, `tests/abi/abi_compat_c_part.c`,
  `tests/abi/abi_compat_cplus_lib.cp`, `tests/abi/abi_compat_cplus_caller.cp`,
  `tests/abi/abi_compat_header_neutrality.c`,
  `tests/abi/abi_compat_expected_*_symbols.txt`,
  `build/abi/compat/results.log` and the `build/abi/compat/*.out` summaries
- New ABI-safety sources (read, no recorded result):
  `tests/stdlib/agent2/abi_safety_probe.cp`,
  `tests/stdlib/agent2/abi_safety_alloc_lifecycle.cp`
- `docs/libc/AUDIT_alloc.md`, `docs/libc/AUDIT_memory_string.md`
- `stdlib/error.hp` (metadata side table, allocator, transfer, release);
  `libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`
- Module audits (the task brief's `docs/stdlib/agent2_audit/...` path does not
  exist): `tests/stdlib/agent2_audit/module_audits/{error,string,vector,collections,file}.hp.md`
- `tests/stdlib/agent2/results/{summary.txt,summary.json,*.commands}`,
  `tests/stdlib/agent2/run_all.sh`, `tests/stdlib/agent2/SANITIZER_MATRIX.md`
- `benchmarks/safety_compare/RESULTS.md`, `benchmarks/safety_compare/run_bench.sh`
- `docs/stdlib/upstream-reuse.md` and all nine
  `docs/stdlib/upstream_study/*.md` files present at final check (the original
  seven plus `softboundcets.md` and `trapc.md`)

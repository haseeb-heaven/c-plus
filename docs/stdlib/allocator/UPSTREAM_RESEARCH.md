# Task 5 Phase 7 — upstream allocator research (allocator/UPSTREAM_RESEARCH.md)

Branch: `agent/modern-stdlib`. Date of fetches: 2026-11-23.

Scope: **research only**. This file is the only artifact created or modified.
No header, source, test, build file, or git index entry was touched. No upstream
code was built or executed. All upstream facts below come from URLs fetched with
`webfetch` and are labelled:

- **[OBSERVED]** — directly quoted or closely paraphrased from a fetched URL in
  the evidence log (§7), with repository, branch/SHA and file named.
- **[PRIOR STUDY]** — retrieved by an earlier sibling agent and recorded in a
  committed file in this repository; cited as corroboration, never as a sole
  basis for a claim that I could not re-verify myself.
- **[INFERENCE]** — my engineering interpretation; not an upstream statement.
- **[FETCH FAILURE]** — retrieval failed; recorded honestly in §7, with the
  workaround used. No source claim rests on a failed URL.

Local files inspected for the adaptation mapping: `stdlib/error.hp`
(lines 14–23, 60–65, 96–125, 365–414, 476–541, 581–592), `stdlib/memory.hp`
(lines 1–60, 68–153), and the sibling studies
`docs/stdlib/upstream_study/{fil_c,softboundcets,microsoft_stl}.md`.

---

## 1. Provenance, exact pins, and license identifiers

| Upstream | Repository URL | Branch / pin used | License identifier quoted from the LICENSE file |
|---|---|---|---|
| Fil-C | `https://github.com/pizlonator/fil-c` | `deluge`, tip `d3f9f822c276b446c8687ce70605b71aefc19beb` (commit feed `updated` 2026-11-22T20:20:27Z, "Remove pointless comments") | No root `LICENSE` (**404**, §7). `LLVM-LICENSE.txt` begins: "The LLVM Project is under the Apache License v2.0 with LLVM Exceptions:". `filc/include/stdfil.h` carries a per-file BSD-style 2-clause header: "Copyright (c) 2023-2026 Epic Games, Inc." / "Copyright (c) 2026 Filip Pizlo. All Rights Reserved." |
| SoftBoundCETS | `https://github.com/santoshn/softboundcets-34` | `master`; pin `9a9c09f04e16f2d1ef3a906fd138a7b89df44996` **[PRIOR STUDY]** (`docs/stdlib/upstream_study/softboundcets.md` §"Revision policy"; no tags exist; my `master` raw fetch returned the same header). | Top-level `LICENSE`: "Copyright (c) 2013, Santosh Nagarakatte / All rights reserved." with the three BSD conditions, but clause 3 contains the literal placeholder "Neither the name of the {organization} nor the names of its contributors…". `softboundcets.h` carries a permissive MIT-style per-file notice naming Santosh Nagarakatte, Milo M. K. Martin, Jianzhou Zhao, Steve Zdancewic and the University of Pennsylvania. |
| Microsoft STL | `https://github.com/microsoft/STL` | `main`, tip `0cabaf3c7e0a73fe3040aabce40cd8833e80392b` (2026-11-21T17:52:40Z) | `LICENSE.txt` begins: "The Microsoft C++ Standard Library is under the Apache License v2.0 with LLVM Exception:". Every inspected header carries `// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception`. |
| mimalloc | `https://github.com/microsoft/mimalloc` | `main`, tip `c683b7c60c91cb2809977e37d3f4d084d7497180` (2026-08-05T18:12:57Z, "Merge pull request #1357 … release v3.4.5") | `LICENSE`: "MIT License / Copyright (c) 2018-2025 Microsoft Corporation, Daan Leijen". Source headers say "This is free software; you can redistribute it and/or modify it under the terms of the MIT license." |
| jemalloc | `https://github.com/jemalloc/jemalloc` | `dev`, tip `b37b28d77eb92e6332e5c9d282db85d76740908c` (2026-11-20T18:45:56Z, "Fix type conversions for 64-bit portability") | `COPYING`: "Unless otherwise specified, files in the jemalloc source distribution are subject to the following license:" — a two-clause BSD-style text with copyrights "2002-present Jason Evans", "2007-2012 Mozilla Foundation", "2009-present Facebook, Inc.". The file contains **no SPDX identifier string**; GitHub license metadata was unreadable (403, §7), so I do not assert one. |
| LLVM sanitizer runtime | `https://github.com/llvm/llvm-project` | `main`, tip `1e0049e03939cef968db8085e2c36298c82270ff` (2026-11-23T03:56:50Z) | `compiler-rt/LICENSE.TXT`: "The LLVM Project is under the Apache License v2.0 with LLVM Exceptions:" and "The compiler_rt library is dual licensed under both the University of Illinois \"BSD-Like\" license and the MIT license." Interface headers carry `// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception`. |

**[INFERENCE] Licensing position for this repository:** every license above is
permissive, but this study transfers *ideas and API shapes only*; no upstream
source text was vendored. If any excerpt were ever carried, the per-file notice
would have to be preserved (especially Fil-C's Epic Games / Filip Pizlo BSD
header and SoftBoundCETS's per-file MIT-style header). Per `AGENTS.md`,
`third_party/` is untouched.

---

## 2. Fil-C — allocation tracking and free/liveness metadata

**Files inspected:** `filc/include/stdfil.h` (full, version `FILC_VERSION 687u`),
`https://fil-c.org/invisicaps.html`, `https://fil-c.org/fugc.html`, and the
`deluge` commit Atom feed.

**[OBSERVED] `malloc`/`free` are forwarders to a GC allocator.** From
`stdfil.h`: "libc's malloc just forwards to this. There is no difference between
calling `malloc` and `zgc_alloc`." `void* zgc_alloc(__SIZE_TYPE__ count)` returns
zero-initialized memory and "May allocate slightly more than `count`, based on
the runtime's minalign (which is currently 16)". `zgc_aligned_alloc`,
`zgc_realloc`, `zgc_aligned_realloc`, `zgc_realloc_preserving_alignment` exist;
realloc/free require `ptr == zgetlower(ptr)` or the runtime panics.

**[OBSERVED] Object header + aux allocation (the tracking data structure).**
From `invisicaps.html`: a flight pointer has a trusted *lower* (pointing just
above the object header) and an untrusted *intval*. The header holds the upper
bound, an *aux word*, and flags. "If an object had any pointers stored into it,
an aux allocation is allocated, which has the same size as the object's
payload." Pointers at rest keep their *lower* in the aux allocation and their
*intval* in the payload; the aux word's lower 48 bits point at the aux
allocation and "the upper 16 bits are used for flags and additional data".
If the aux word is NULL, GC treats the object as a leaf.

**[OBSERVED] Query API for bounds and liveness (exact signatures from
`stdfil.h`):**

```c
void* zgetlower(void* ptr);
void* zgetupper(void* ptr);
filc_bool zis_readonly(void* ptr);
#define zlength(ptr) /* (upper - ptr) in units of *ptr */
filc_bool zhasvalidcap(void* ptr);  /* "has a capability and that capability is not free" */
static inline filc_bool zinbounds(void* ptr);
static inline filc_bool zvalinbounds(void* ptr, __SIZE_TYPE__ size);
static inline void zcheck(void* ptr, __SIZE_TYPE__ size);
static inline void zchecked_add(__SIZE_TYPE__ a, __SIZE_TYPE__ b);  /* __builtin_add_overflow */
static inline void zchecked_mul(__SIZE_TYPE__ a, __SIZE_TYPE__ b);  /* __builtin_mul_overflow */
```

`zvalinbounds` in the raw header uses an explicit, overflow-safe form:
`size <= (__SIZE_TYPE__)(upper - (char*)ptr)` after `ptr >= lower && ptr < upper`.

**[OBSERVED] Free is logical invalidation.** `stdfil.h`: "Freeing an object in
Fil-C does not cause memory to be reclaimed immediately. Instead, it changes the
upper bounds of the object to be the lower bounds and sets the free flag. This
causes all subsequent accesses to trap with a Fil-C panic." `invisicaps.html`
adds the aux-word *free bit* for diagnostics. `fugc.html`: "FUGC will redirect
all capability pointers (lowers…) to free objects to point at the free singleton
object instead. This allows freed object memory to really be reclaimed." Guarantees
quoted from `fugc.html`: access after free traps; double free traps; un-freed
objects are reclaimed automatically.

**[OBSERVED] FUGC collector shape.** `fugc.html` describes the collector as
"parallel concurrent on-the-fly grey-stack Dijkstra accurate non-moving",
with store barrier (no load barrier), soft handshakes, black allocation,
SIMD bitvector sweep ("<5% of its time sweeping"), finalizer queues (`zgc_finq`),
weak references (`zweak`), weak maps (`zweak_map`) and a GC-cycle observation API
(`zgc_completed_cycle`, `zgc_requested_cycle`, `zgc_request_and_wait`,
`zscavenge_synchronously`).

**[OBSERVED] Allocation-site tracking in the runtime itself.** The commit feed
entry `240f816f10daeb17778dae82e936414e69401f73` (2026-11-21) describes
replacing "the fixed 64K open-addressing table + custom hash with a
PAS_CREATE_HASHTABLE (`alloc_stat_table`) keyed by the origin pointer, hashed via
`pas_hash_ptr` … The table now grows dynamically instead of preallocating 65536
entries." This is a *diagnostics* table in `libpas`/`filc_runtime.c`, not a
program-visible allocation API. **[NOT INSPECTED]** `filc_runtime.c` itself
(not fetched; too large).

---

## 3. SoftBoundCETS — shadow metadata representation and checks

**Files inspected:** `softboundcets-lib/softboundcets.h` (full) and top-level
`LICENSE`, both from `master` (pin recorded in §1).

**[OBSERVED] One metadata record, four fields (default build):**

```c
typedef struct {
  void*  base;   /* lowest valid address (inclusive)      */
  void*  bound;  /* one-past-the-end address (exclusive)  */
  size_t key;    /* temporal generation identifier       */
  void*  lock;   /* address of the word holding the live key */
} __softboundcets_trie_entry_t;
```

Compile-time variants shrink this to `{base,bound}` or `{key,lock}` (2 fields,
16 bytes); the default is **4 fields / 32 bytes**. Indices are
`__BASE_INDEX=0`, `__BOUND_INDEX=1`, `__KEY_INDEX=2`, `__LOCK_INDEX=3`; unused
indices take the sentinel `10000000`.

**[OBSERVED] Metadata is keyed by the *storage location of the pointer
variable*, not by the pointee address.** `__softboundcets_metadata_store(void*
addr_of_ptr, base, bound, key, lock)` and `_load(...)` use
`primary_index = ptr >> 25; secondary_index = (ptr >> 3) & 0x3fffff;`
i.e. an 8-byte-granularity two-level trie. `__softboundcets_copy_metadata`
early-returns when `from_ptr % 8 != 0`. On a trie miss with preallocation off,
`_load` writes all four outputs to `0` — a fail-closed default.

**[OBSERVED] Spatial and temporal check predicates:**

```c
if ((ptr < base) || ((void*)((char*)ptr + size_of_type) > bound)) abort();  /* spatial */
size_t temp = *((size_t*)pointer_lock); if (temp != key) abort();          /* temporal */
```

**[OBSERVED] O(1) bulk invalidation.** `__softboundcets_memory_deallocation`
does `*((size_t*)ptr_lock) = 0;` then pushes the lock onto
`__softboundcets_lock_next_location`; every stale pointer sharing that lock now
fails the key check. Stack objects use a bump pointer in
`__softboundcets_stack_temporal_space_begin`.

**[OBSERVED] Double-free map.** `__softboundcets_add_to_free_map` uses
`index = (ptr_key + counter) % __SOFTBOUNDCETS_N_FREE_MAP_ENTRIES`, accepts
`tag == 0 || tag == 2`, and `__softboundcets_check_remove_from_free_map` aborts
on `tag == 0` (double/unmatched free) and writes tombstone `2` on success.

**[OBSERVED] Auxiliary structures:** a documented shadow-stack frame layout
(`1) size of previous frame; 2) size of current frame; 3) base/bound/key/lock of
each argument`, slot 0 for returns), `mmap`'d `MAP_NORESERVE` tables (temporal
space `64*1024*1024` words, free map `32*1024*1024` entries, primary trie
`8*1024*1024`, secondary `4*1024*1024`), and
`__softboundcets_shrink_bounds()` computing `base = max(new,old)`,
`bound = min(new,old)`.

**[OBSERVED / DO-NOT-COPY]** The `src` bound condition in
`__softboundcets_memcopy_check` compares `size > (size_t)dest_bound` instead of
`src_bound` — an upstream typo. `_abort()` calls `abort()` after a `backtrace()`
on non-FreeBSD; there is no recoverable error object. The 32-bit constants and
64-bit `>>25`/`>>3` indices are hard-wired address-layout assumptions.

**[OBSERVED] ABI verdict (from the source itself):** every C pointer remains a
single machine word; metadata lives in the trie, shadow stack, temporal lock
space and free map. The *hidden calling convention* (pointer arguments copied
through the shadow stack) and symbol renaming are out-of-band compiler/runtime
properties **[PRIOR STUDY]** (`docs/stdlib/upstream_study/softboundcets.md` §5).

---

## 4. Microsoft STL — allocator design

**Files inspected:** `LICENSE.txt` and `stl/inc/xmemory` (full header; preview
covered lines 1–1336 of 2848). Growth algorithms in `stl/inc/vector` are
**[PRIOR STUDY]** (`docs/stdlib/upstream_study/microsoft_stl.md` §3).

**[OBSERVED] Allocation size checks happen before the multiply.**
`_Get_size_of_n<_Ty_size>(_Count)` computes
`constexpr size_t _Max_possible = static_cast<size_t>(-1) / _Ty_size;` and
throws `bad_array_new_length` if `_Count > _Max_possible`.
`_Allocate_manually_vector_aligned` checks `if (_Block_size <= _Bytes)`
(add overflow) and `_STL_VERIFY(_Ptr_container != 0, "invalid argument")`
**even in release**, with the comment "validate even in release since we're
doing p[-1]". `allocator::deallocate` asserts `_Ptr != nullptr || _Count == 0`.

**[OBSERVED] Big-allocation alignment uses a fixed threshold and an in-band
back-pointer.** On x86/x64: `_Big_allocation_threshold = 4096`,
`_Big_allocation_alignment = 32`, `_Non_user_size = sizeof(void*) + 31`
(an extra pointer in debug), sentinel `0xFAFAFAFAFAFAFAFAULL` / `0xFAFAFAFAUL`.
`_Allocate_manually_vector_aligned` stores the original container pointer at
`((uintptr_t*)_Ptr)[-1]` and, in debug, the sentinel at `[-2]`;
`_Adjust_manually_vector_aligned` verifies a back-shift range
`[_Min_back_shift, _Non_user_size]`.

**[OBSERVED] `allocator_traits` dispatch.** `allocator_traits` selects
`_Default_allocator_traits` or `_Normal_allocator_traits` via
`_Is_default_allocator`; it synthesises `pointer`, `const_pointer`,
`void_pointer`, propagation traits, `rebind_alloc`, `allocate`,
`allocate_at_least` (C++23), `construct`, `destroy`, `max_size` and
`select_on_container_copy_construction`. `std::allocator<T>::max_size()` is
`static_cast<size_t>(-1) / sizeof(_Ty)`.

**[OBSERVED] Guard-object pattern.** `_Tidy_guard` / `_Tidy_deallocate_guard`
call `_Target->_Tidy()` / `_Tidy_deallocate()` in their destructor only when
`_Target != nullptr`; `_Alloc_construct_ptr::_Allocate()` sets `_Ptr = nullptr`
before allocating ("if allocate throws, prevents double-free") and its
destructor deallocates only if engaged. This is the allocator-side form of the
commit-last / scope-guard discipline.

**[OBSERVED] Sanitizer-facing alignment model.** `_Asan_granularity = 8`;
`_Get_asan_aligned_first_end()` rounds pointers to the 8-byte shadow granule and
`_Clamp_to_end()` clamps `mid` to `end`. The header documents this as the
fix-up needed before calling `__sanitizer_annotate_contiguous_container`; it is
conditioned on the sanitizer runtime being present.

**[PRIOR STUDY] Growth policy** (not re-fetched by me):
`_Calculate_growth` uses `if (_Oldcapacity > _Max - _Oldcapacity/2) return _Max;`
then geometric 1.5x floored at demand; `basic_string` rounds with an
`_Alloc_mask` (15/7/3/1/0 by element size) and reserves a terminator byte.
Cited only as corroboration; see the sibling study for exact line numbers.

---

## 5. mimalloc — sharded free lists and pointer encoding

**Files inspected:** `LICENSE`, `readme.md`, `include/mimalloc.h`,
`include/mimalloc/internal.h`, `src/alloc.c`.

**[OBSERVED] Design summary from `readme.md`:** "free list sharding: instead of
one big free list (per size class) we have many smaller lists per 'mimalloc
page' … (A mimalloc page contains blocks of one size class and is usually 64KiB
on a 64-bit system)." "free list multi-sharding: … there is one list for
thread-local `free` operations, and another one for concurrent `free`
operations. Free-ing from another thread can now be a single CAS". Also: eager
page purging, secure mode with "guard pages, randomized allocation, encrypted
free lists", first-class heaps, bounded metadata overhead (~0.2%).
`MI_SMALL_WSIZE_MAX = 128` in `mimalloc.h`.

**[OBSERVED] Fast path (from `src/alloc.c`).** `_mi_page_malloc_zero` is
described as "Fast allocation in a page: just pop from the free list. Fall back
to generic allocation only if the list is empty." Its body is:

```c
mi_block_t* const block = page->free;
if mi_unlikely(block == NULL) return _mi_malloc_generic(heap, size, zero, 0, ppage);
page->free = mi_block_next(page, block);
page->used++;
```

Zero-size requests become one byte via `mi_heap_malloc_small_zero` when padding
or guards are enabled; `mi_heap_calloc` uses `mi_count_size_overflow(count,size,&total)`.
`mi_realloc` keeps the old block if `newsize <= size && newsize >= size/2`
("not more than 50% waste"), otherwise allocates fresh, copies, and frees the
old pointer **only on success**. `mi_strdup` guards `len > MI_MAX_ALLOC_SIZE - 1`.

**[OBSERVED] Pointer encoding in `include/mimalloc/internal.h`.** Free-list next
pointers are encoded per page with two keys: `mi_ptr_encode` computes
`mi_rotl(x ^ keys[1], keys[0]) + keys[0]`, decode is
`mi_rotr(x - keys[0], keys[0]) ^ keys[1]`. Rationale quoted from the header:
"Many hardened allocators xor the next pointer `p` with a secret key `k1` …
mimalloc uses an extra key `k2` and encodes as `((p^k2)<<<k1)+k1` … both keys
are unique per page". `mi_block_next` additionally rejects a decoded `next`
outside the same page with `EFAULT`. `mi_ptr_encode_canary` zeroes the low byte
"to prevent spurious read overflows … (issue #951)".

**[OBSERVED] Failure taxonomy in `internal.h`:** `EAGAIN` double free, `ENOMEM`
out of memory, `EFAULT` corrupted free-list or metadata, `EINVAL` invalid
pointer, `EOVERFLOW` `count*size` overflow. `mi_mul_overflow` uses
`__builtin_umul_overflow` with a portable fallback based on
`MI_MUL_COULD_OVERFLOW = ((size_t)1 << (4*sizeof(size_t)))` (half-word sqrt
trick).

**[OBSERVED] Address-derived metadata lookup.** `_mi_ptr_segment(p)` aligns
`(uintptr_t)p - 1` down by `MI_SEGMENT_MASK`; `_mi_segment_page_of` indexes
`segment->slices` by `diff >> MI_SEGMENT_SLICE_SHIFT`; `_mi_heap_get_free_small_page`
indexes `heap->pages_free_direct[_mi_wsize_from_size(size)]`. The thread-free
list packs a delayed-free flag into the low two bits of `xthread_free`.

**[OBSERVED] C++ integration** (`include/mimalloc.h`): `mi_stl_allocator<T>`
with `max_size() = PTRDIFF_MAX/sizeof(T)`, `is_always_equal = true_type`,
POCCA/POCMA/POCS all true, `rebind`, and `allocate(count)` via `mi_new_n`;
`mi_heap_stl_allocator` uses a `shared_ptr<mi_heap_t>` and `is_always_equal =
false_type`. **[INFERENCE]** Directly relevant as a *design inventory*
(allocator as a policy object), but none of this machinery exists in this
repository's C+ headers.

---

## 6. jemalloc — emap/radix-tree metadata and size classes

**Files inspected:** `COPYING`, `src/jemalloc.c` (lines 1–1823 of 2355 available
in preview), `src/arena.c` (lines 1–1562 of 1829 in preview), and
`include/jemalloc/internal/emap.h` (full).

**[OBSERVED] Allocation entry path** (`src/jemalloc.c`). `static_opts_t` /
`dynamic_opts_t` separate per-API policy (overflow allowed, min alignment,
errno behaviour, `oom_string`) from request data (`result`, `usize`,
`num_items`, `item_size`, `alignment`, `zero`, `tcache_ind`, `arena_ind`).
`compute_size_with_overflow` uses a half-word guard:
`static const size_t high_bits = SIZE_T_MAX << (sizeof(size_t)*8/2);`
then `if (likely((high_bits & (num_items | item_size)) == 0)) return false;`
else verifies `*size / item_size == num_items`. `imalloc` saves and restores
`errno` on success. `do_rallocx` has an in-place same-size fast path for slab
allocations before looking up arenas/tcaches.

**[OBSERVED] Metadata lookup data structure** (`include/jemalloc/internal/emap.h`).

```c
typedef struct emap_s emap_t;
struct emap_s { rtree_t rtree; };

typedef struct emap_alloc_ctx_s emap_alloc_ctx_t;
struct emap_alloc_ctx_s { size_t usize; szind_t szind; bool slab; };
```

`emap_alloc_ctx_lookup(tsdn, emap, ptr, &ctx)` inlines the L1 rtree cache hit
(`rtree_leaf_elm_lookup_fast`) and falls back to `rtree_read_slow`;
`emap_full_alloc_ctx_try_lookup` tolerates an unmapped pointer;
`emap_edata_lookup_batch` performs batched lookups with a metadata visitor.
`emap_register_boundary` / `_interior`, `emap_split_prepare|commit` and
`emap_merge_prepare|commit` maintain the address→extent mapping across
split/merge. `emap_alloc_ctx_usize_get` returns `sz_index2size(szind)` for slab
allocations, otherwise the stored `usize`.

**[OBSERVED] Arena/slab structures** (`src/arena.c`): `bin_t` bins per size
class with `slabcur`, `slabs_nonfull`, `slabs_full` and per-bin mutex;
`arena_slab_alloc` calls `pa_alloc(..., /*slab*/ true, ...)` then
`bitmap_init(slab_data->bitmap, &bin_info->bitmap_info, false)`;
`arena_bin_reset` walks the three slab lists; `arena_ptr_array_flush_impl_small`
partitions a flush batch by `edata_bin_owner_get` to group locks, and its
metadata visitor prefetches each `edata` once ("Objects flushed together are
usually on the same slab"); `arena_extent_alloc_large` chooses
`zero_override = zero && (usize >= opt_calloc_madvise_threshold)`, otherwise
`memset`. `arena_prof_promote` uses `safety_check_set_redzone` for sampled small
allocations. `arena_destroy` uses atomic publication of the arena pointer.

**[OBSERVED] Fork/thread teardown care:** `dealloc_no_tsd()` in `jemalloc.c`
hands an object straight back with `idalloctm(TSDN_NULL, …)` when
`tsd_teardown_done()`, deliberately skipping profiling, junk fill and thread
events, with the trade-offs documented inline.

**[INFERENCE]** The reusable shape is "look up allocation metadata by pointer
through a sparse structure, return `{size, size-class, slab?}` with a
fail-closed/unmapped result", not the radix tree itself.

---

## 7. LLVM sanitizer runtime interfaces

**Files inspected:** `compiler-rt/LICENSE.TXT`,
`compiler-rt/include/sanitizer/asan_interface.h`,
`compiler-rt/include/sanitizer/common_interface_defs.h`.

**[OBSERVED] Public ASan interface (exact signatures):**

```c
void __asan_poison_memory_region(void const volatile *addr, size_t size);
void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
int  __asan_address_is_poisoned(void const volatile *addr);
void *__asan_region_is_poisoned(void *beg, size_t size);
```

Both poison functions carry the explicit note: "This function is not
thread-safe because no two threads can poison or unpoison memory in the same
memory region simultaneously", and poisoning "could poison only a subregion …
due to ASan alignment restrictions".

**[OBSERVED] Container-overflow interface** (`common_interface_defs.h`):

```c
void __sanitizer_annotate_contiguous_container(
    const void *beg, const void *end, const void *old_mid, const void *new_mid);
void __sanitizer_annotate_double_ended_contiguous_container(...);
int  __sanitizer_verify_contiguous_container(const void *beg, const void *mid, const void *end);
const void *__sanitizer_contiguous_container_find_bad_address(
    const void *beg, const void *mid, const void *end);
```

The documented model is exactly the vector/string model: `[beg, mid)` holds
elements, `[mid, end)` is reserved capacity, and annotations are inserted into
methods like `push_back`/`pop_back`. When `__SANITIZER_DISABLE_CONTAINER_OVERFLOW__`
is defined, all of these become inline no-ops (verifiers return 1 / NULL). The
header explicitly says "Use this function with caution and do not use for
anything other than vector-like classes."

**[OBSERVED] Other interfaces worth noting:** interceptor weak hooks
(`__sanitizer_weak_hook_memcmp`, `_strcmp`, `_strstr`, `_memmem`, …), fiber
switch hooks (`__sanitizer_start_switch_fiber` / `_finish_switch_fiber`),
`__sanitizer_set_death_callback`, `__sanitizer_print_stack_trace`, and ASan
report introspection (`__asan_get_report_description`, `__asan_locate_address`,
`__asan_get_alloc_stack`, `__asan_get_free_stack`). These are *runtime hooks*,
not library algorithms.

**[INFERENCE]** The transferable idea is the **annotation/verification
protocol**: expose a named operation that records a size transition and a named
query that can locate the first bad address. It can only be *mirrored* as a
pure-C++ checked API in this repository; calling these symbols would introduce
a sanitizer-runtime dependency, which is rejected (§9).

---

## 8. Adaptability matrix — can it enter a header-only C+ stdlib without ABI change?

The local baseline is a thin-pointer, header-only library: `stdlib/error.hp`
defines `stdlib_meta_slot {ptr, byte_size, elem_size, generation}` in a weak
global `stdlib_state_t` side table; `stdlib_alloc` wraps `cplus_alloc` after a
limit/fail-injection check and records metadata; `stdlib_mem_*` in
`stdlib/memory.hp` performs fail-closed, subtraction-form range checks plus
optional generation-token checks. All of the below are **[INFERENCE]**.

| Technique (upstream) | ABI / pointer representation effect | Adapt? | Local analogue / constraint |
|---|---|---|---|
| Fail-closed metadata lookup on a miss (SoftBoundCETS `_load` zero outputs; jemalloc `emap_*_try_lookup`; local `stdlib_meta_find`) | None; table is internal | **YES — already present** | `stdlib_alloc_size()` returns 0 for unknown pointers; `stdlib_mem_check` refuses them. Formalise as a documented contract rather than adding machinery. |
| Subtraction-form range test `n > cap - off` (SoftBoundCETS `ptr > bound - size`; mimalloc overflow guards; local `stdlib_mem_check_range`) | None | **YES — already present** | `stdlib/memory.hp:142–151`. Must not inherit SoftBoundCETS's `size > dest_bound` typo. |
| Monotonic generation / lock token captured before use, O(1) invalidation on release or realloc (SoftBoundCETS CETS key+lock; Fil-C free bit; local `stdlib_alloc_generation`) | None; token is an integer | **YES — already present, worth documenting** | `stdlib_mem_token` / `stdlib_mem_check_token`, `vector::slice::stale()`. Honest limit: copied raw aliases cannot be invalidated (see `stdlib/memory.hp:32–54`). |
| Tombstone on release + double-release detection (SoftBoundCETS free map tag `2`; mimalloc `EAGAIN`; local `STDLIB_META_TOMB`) | None | **YES — already present** | `stdlib_meta_forget` sets `ptr = STDLIB_META_TOMB`, `generation = 0`. |
| Checked multiply/limit before allocation (`__builtin_mul_overflow`, half-word `high_bits` guard, `MI_MAX_ALLOC_SIZE - 1`) | None | **YES — already present** | `stdlib_alloc` limit check and `stdlib_calloc` overflow guard; keep the check *before* calling the allocator. |
| Commit-last mutation: allocate into a local, copy, publish only on success (MS STL guards; mimalloc `mi_realloc`; local `stdlib_realloc`) | None | **YES — already present as an idiom** | Express the *pattern* with explicit branches; do not import C++ RAII guard objects or exceptions. |
| Monotonic bounds narrowing for derived views (`max/min` shrink in SoftBoundCETS) | None; additive API on views | **YES — small additive change** | Could tighten `vector::slice::sub` contracts; arithmetic only, no new dependency. |
| Epoch/cycle observation for "safe time to sweep weak refs" (Fil-C `zgc_completed_cycle`) | None | **YES — already analogous** | `stdlib_alloc_seq()` / `stdlib_alloc_fail_at()` already provide a test-observable epoch; document it as an epoch, not a timestamp. |
| Debug junk / sentinel fills (jemalloc `0xa5`/`0x5a`; MSVC destructor tombstones; mimalloc `MI_DEBUG_UNINIT`) | None, but changes observable buffer contents | **CONDITIONAL** | Only behind an opt-in config flag; never enable by default (would break fixtures that expect zeroing). |
| `allocation_result` / `allocate_at_least` / `allocator_traits` / fancy pointers / POCCA-POCMA-POCS (MS STL, mimalloc STL adapters) | Adds template/type-system machinery; changes allocator contract | **NO** | C+ headers have plain `T*`, no concepts/`if constexpr`/allocator_traits. |
| Size-class bins, sharded/CAS free lists, encoded free lists, guarded pages, huge-page arenas, tcache, HPA (mimalloc, jemalloc) | Replaces the host allocator; would add a new runtime dependency and new metadata layout | **NO** | `stdlib_alloc` deliberately wraps the managed `cplus_alloc`; importing an allocator is out of scope and would not preserve the existing ABI/ownership contract. |
| Side-table-on-pointer-slot trie, shadow stack, temporal lock space (SoftBoundCETS) | Hidden calling convention, symbol renaming, `main` wrapping, `mmap`'d 128 MB-class tables, 8-byte slot granularity, 64-bit-only | **NO** | The *shape* (four-field record, fail-closed lookup) is already adapted; the mechanism is compiler/runtime territory. |
| InvisiCaps capabilities, aux allocations, free-singleton repointing, FUGC (Fil-C) | Requires modified LLVM, custom ABI/loader, GC safepoints, whole-program compilation | **NO** | `stdfil.h` itself `#error`s without `__PIZLONATOR_WAS_HERE__`. Cannot be imported into `.hp` headers or DLL-injected retail code. |
| ASan/UBSan annotations and interceptor hooks (`__sanitizer_*`) | Requires sanitizer runtime and compiler flags | **NO** | Can mirror the *annotation/verification taxonomy* as plain checked APIs; must not call the symbols. |

---

## 9. Explicit rejections

1. **No allocator copying.** Neither mimalloc's sharded free lists /
   encoded free lists / first-class heaps, nor jemalloc's arenas / slabs / rtree
   emap / tcache / HPA, nor Fil-C's libpas + FUGC will be vendored, linked, or
   implemented in `src/`. They replace the managed `cplus_alloc` and introduce
   dependencies this repository does not have.
2. **No pointer-representation changes.** No fat pointers, no InvisiCap
   lower/intval pairs, no CHERI-style capabilities, no SoftBoundCETS trie/shadow
   stack, no pointer-bit tagging. `T*` stays one address word; `sizeof(T*)`,
   layout, and calling convention remain unchanged. Adding bounds to a pointer
   value would be an ABI break.
3. **No new dependencies.** mimalloc, jemalloc, compiler-rt/ASan, libpas and a
   modified LLVM are not added as build or runtime dependencies. The
   sanitizer interfaces are read as documentation only.
4. **No hard-abort failure policy.** SoftBoundCETS `__softboundcets_abort()`,
   Fil-C `zerror`/`zsafety_error`/panic, and mimalloc's `abort` on `xmalloc`
   errors do not transfer: this library reports via `0`/`-1` plus
   `stdlib_note` / `error` and must remain recoverable (`stdlib/error.hp:6`).
5. **No huge virtual reservations or hard-coded address masks** (`>>25`,
   `(>>3)&0x3fffff`, `MAP_NORESERVE`, 1 GiB arenas, fixed TLS slots).
6. **No copied bug for bug**: the SoftBoundCETS `size > dest_bound` typo and
   approximate `+1024` bounds are explicitly rejected.
7. **No claims of upstream-equivalent safety.** A header-side table detects
   only tracked blocks and only when callers pass the base and a captured token;
   it is not provenance, not whole-program bounds checking, and not
   use-after-free detection for arbitrary raw pointers.

---

## 10. Evidence log — exact URLs and outcomes

All retrievals were performed on 2026-11-23 with `webfetch`.

| # | URL | Result | Notes |
|---|---|---|---|
| 1 | `https://raw.githubusercontent.com/microsoft/mimalloc/main/LICENSE` | SUCCESS | MIT text quoted in §1. |
| 2 | `https://raw.githubusercontent.com/microsoft/STL/main/LICENSE.TXT` | **404 — FETCH FAILURE** | Correct filename is `LICENSE.txt` (row 6). |
| 3 | `https://raw.githubusercontent.com/jemalloc/jemalloc/dev/LICENSE` | **404 — FETCH FAILURE** | Correct file is `COPYING` (row 11). |
| 4 | `https://raw.githubusercontent.com/microsoft/STL/main/LICENSE` | **404 — FETCH FAILURE** | Same as #2. |
| 5 | `https://raw.githubusercontent.com/jemalloc/jemalloc/dev/COPYING` | SUCCESS | BSD-style text quoted in §1. |
| 6 | `https://raw.githubusercontent.com/microsoft/STL/main/LICENSE.txt` | SUCCESS | Apache-2.0 + LLVM exception. |
| 7 | `https://api.github.com/repos/microsoft/mimalloc/commits/main` | **403 — FETCH FAILURE** | GitHub REST API rate-limited all session requests; replaced by Atom feeds. |
| 8 | `https://api.github.com/repos/microsoft/mimalloc/tags?per_page=5` | **403 — FETCH FAILURE** | No tag list obtained; version taken from `readme.md` instead. |
| 9 | `https://api.github.com/repos/jemalloc/jemalloc`, `.../commits/dev` | **403 — FETCH FAILURE** | Replaced by Atom feed. |
| 10 | `https://api.github.com/repos/microsoft/STL/commits/main` | **403 — FETCH FAILURE** | Replaced by Atom feed. |
| 11 | `https://api.github.com/repos/pizlonator/fil-c` | **403 — FETCH FAILURE** | Branch `deluge` taken from raw URLs + Atom feed. |
| 12 | `https://api.github.com/repos/santoshn/softboundcets-34/license` | **403 — FETCH FAILURE** | License text taken from raw `LICENSE`; SPDX identifier from **[PRIOR STUDY]**. |
| 13 | `https://github.com/microsoft/mimalloc/commits/main.atom` | SUCCESS | Tip `c683b7c6…`, 2026-08-05. |
| 14 | `https://github.com/jemalloc/jemalloc/commits/dev.atom` | SUCCESS | Tip `b37b28d7…`, 2026-11-20. |
| 15 | `https://github.com/microsoft/STL/commits/main.atom` | SUCCESS | Tip `0cabaf3c…`, 2026-11-21. |
| 16 | `https://github.com/pizlonator/fil-c/commits/deluge.atom` | SUCCESS | Tip `d3f9f822…`, 2026-11-22; allocation-profiler commit body quoted in §2. |
| 17 | `https://github.com/llvm/llvm-project/commits/main.atom` | SUCCESS | Tip `1e0049e0…`, 2026-11-23. |
| 18 | `https://raw.githubusercontent.com/microsoft/mimalloc/main/include/mimalloc.h` | SUCCESS | Public API + `mi_stl_allocator`. |
| 19 | `https://raw.githubusercontent.com/microsoft/mimalloc/main/README.md` | **404 — FETCH FAILURE** | Correct filename is `readme.md` (row 20). |
| 20 | `https://raw.githubusercontent.com/microsoft/mimalloc/main/readme.md` | SUCCESS (truncated preview) | Design bullets and release dates quoted in §5; quoted text was in the visible portion. |
| 21 | `https://raw.githubusercontent.com/microsoft/mimalloc/main/src/alloc.c` | SUCCESS | Fast path, realloc rule, guarded allocation quoted in §5. |
| 22 | `https://raw.githubusercontent.com/microsoft/mimalloc/main/include/mimalloc/internal.h` | SUCCESS | Free-list encoding, error codes, overflow helpers quoted in §5. |
| 23 | `https://raw.githubusercontent.com/jemalloc/jemalloc/dev/src/jehash.c` | **404 — FETCH FAILURE** | Path not present at this revision; no claim made about it; not re-searched. |
| 24 | `https://raw.githubusercontent.com/jemalloc/jemalloc/dev/src/jemalloc.c` | SUCCESS (truncated preview) | `static_opts_t`, `compute_size_with_overflow`, `imalloc`, `do_rallocx`, `dealloc_no_tsd` quoted in §6. |
| 25 | `https://raw.githubusercontent.com/jemalloc/jemalloc/dev/src/arena.c` | SUCCESS (truncated preview) | Bins/slabs/bitmap, flush partitioning, large-extent zero policy quoted in §6. |
| 26 | `https://raw.githubusercontent.com/jemalloc/jemalloc/dev/include/jemalloc/internal/emap.h` | SUCCESS | `emap_t`, `emap_alloc_ctx_t`, lookup/batch APIs quoted in §6. |
| 27 | `https://raw.githubusercontent.com/llvm/llvm-project/main/compiler-rt/LICENSE.TXT` | SUCCESS | License identifiers quoted in §1. |
| 28 | `https://raw.githubusercontent.com/llvm/llvm-project/main/compiler-rt/lib/asan/asan_interface.h` | **404 — FETCH FAILURE** | Correct path is `compiler-rt/include/sanitizer/asan_interface.h` (row 30). |
| 29 | `https://raw.githubusercontent.com/llvm/llvm-project/main/compiler-rt/lib/sanitizer_common/sanitizer_common_interface.h` | **404 — FETCH FAILURE** | Correct path is `compiler-rt/include/sanitizer/common_interface_defs.h` (row 31). |
| 30 | `https://raw.githubusercontent.com/llvm/llvm-project/main/compiler-rt/include/sanitizer/asan_interface.h` | SUCCESS | Poison/unpoison API quoted in §7. |
| 31 | `https://raw.githubusercontent.com/llvm/llvm-project/main/compiler-rt/include/sanitizer/common_interface_defs.h` | SUCCESS | Container annotation/verification API quoted in §7. |
| 32 | `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/softboundcets-lib/softboundcets.h` | SUCCESS | Primary evidence for §3. |
| 33 | `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/LICENSE` | SUCCESS | BSD-style text with `{organization}` placeholder quoted in §1. |
| 34 | `https://fil-c.org/invisicaps.html` | SUCCESS | Object header / aux allocation / free bit quoted in §2. |
| 35 | `https://fil-c.org/fugc.html` | SUCCESS | Collector properties and free-singleton repointing quoted in §2. |
| 36 | `https://raw.githubusercontent.com/pizlonator/fil-c/deluge/filc/include/stdfil.h` | SUCCESS | `FILC_VERSION 687u`, allocation/query API quoted in §2. |
| 37 | `https://raw.githubusercontent.com/pizlonator/fil-c/deluge/LLVM-LICENSE.txt` | SUCCESS | "Apache License v2.0 with LLVM Exceptions". |
| 38 | `https://raw.githubusercontent.com/pizlonator/fil-c/deluge/LICENSE` | **404 — FETCH FAILURE** | No root `LICENSE`; used `LLVM-LICENSE.txt` + per-file headers. |
| 39 | `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/xmemory` | SUCCESS (truncated preview) | Allocator checks / traits / guards quoted in §4 from visible lines 1–1336. |

**Failures summary:** 14 failed URLs (7 GitHub REST API 403s, 7 raw 404s), all
listed above. Two large files (`readme.md`, `src/jemalloc.c`, `src/arena.c`,
`stl/inc/xmemory` — four previews total) were truncated; only quoted text from
the visible portion is used. No source claim in this document depends on a
failed fetch.

---

## 11. Top adaptable techniques (ranked)

All items are **[INFERENCE]** concept transfers requiring no pointer change, no
ABI change, and no new dependency. Most are already implemented locally; the
actionable outcome is contract discipline and documentation, not new machinery.

1. **Fail-closed, exact-base metadata queries with subtraction-form bounds.**
   Upstream: SoftBoundCETS trie-miss zero outputs, jemalloc `emap_*_try_lookup`,
   mimalloc size queries. Local: `stdlib_alloc_size`, `stdlib_mem_check_range`
   (`n > cap - off`). Highest value because it is the foundation every other
   checked helper relies on, and the safe arithmetic already exists.
2. **Generation / lock token captured before use, with O(1) invalidation.**
   Upstream: CETS `{key,lock}` zeroing on free, Fil-C free bit. Local:
   `stdlib_mem_token`, `stdlib_alloc_check_generation`, `vector::slice::stale`.
   Document precisely who bumps the token, when to check it, and that a stale
   *source* pointer is not detected (`stdlib/memory.hp:40–45, 157–163`).
3. **Overflow-safe request arithmetic and limits before calling the allocator.**
   Upstream: jemalloc `compute_size_with_overflow`, mimalloc
   `mi_count_size_overflow`, MS STL `_Get_size_of_n`. Local: `stdlib_calloc`
   `__builtin_mul_overflow`, `STDLIB_ALLOC_LIMIT`, `stdlib_alloc` injected
   failure. Keep the check *before* the allocation, never after.
4. **Commit-last mutation with explicit guarantee tiers.** Upstream: MS STL
   `_Alloc_construct_ptr` / `_Tidy_guard`, mimalloc `mi_realloc` (free old only
   on success). Local: `stdlib_realloc` and container reserve/copy paths. Write
   down strong vs basic guarantee per operation rather than adding RAII guards.
5. **Tombstones plus a test-observable epoch.** Upstream: SoftBoundCETS free-map
   tag `2`, mimalloc `EAGAIN`, Fil-C `zgc_completed_cycle`. Local:
   `STDLIB_META_TOMB` and `stdlib_alloc_seq()` / `stdlib_alloc_fail_at()`.
   Cheap, additive, and useful for adversarial fixtures.
6. *(smaller)* **Monotonic bounds narrowing for derived views** (SoftBoundCETS
   `shrink_bounds` `max/min`) can refine `vector::slice::sub` contracts with
   pure arithmetic.

**Bottom line:** the highest-value output of this research is confirmation that
this repository's existing side-table + generation + fail-closed-range design
already converges with the *adaptable* parts of Fil-C, SoftBoundCETS, MS STL,
mimalloc, jemalloc and LLVM sanitizer interfaces. Everything else in those
projects is allocator, compiler, or runtime machinery that must stay out of the
header-only C+ stdlib.

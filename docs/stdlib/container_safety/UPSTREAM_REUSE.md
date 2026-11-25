# Upstream source reuse for memory / containers (`container_safety`)

Date: 2026-11-23. Branch `agent/modern-stdlib`. Task 2 Phase 5.

**Scope of this document.** It maps reusable techniques from the five already-committed
upstream studies (`docs/stdlib/upstream_study/{microsoft_stl,microsoft_gsl,safer_cplusplus,
fil_c,softboundcets}.md`) and from `docs/stdlib/upstream-reuse.md` onto the **actual current
code** in `stdlib/memory.hp`, `stdlib/string.hp`, `stdlib/vector.hp` and
`stdlib/collections.hp` (plus the side table they all read, `stdlib/error.hp`). Every
`file:line` below was read from the working tree at the time of writing. Every upstream
claim is either (a) carried from a committed study that already records the exact URL and
revision, or (b) freshly re-verified in §1. No code was copied verbatim, no header was
edited, and no build/test result is asserted here.

Strict file ownership for this task: this document is the only file created or modified.

---

## 1. Evidence actually consulted

### 1.1 Local files read (current code)

| File | Lines read | Role in this document |
|---|---|---|
| `stdlib/memory.hp` | 1–227 (full) | additive bounds-aware wrappers; primary new adaptation site |
| `stdlib/error.hp` | 90–159, 340–379, 390–623 | `stdlib_meta_slot` side table, length/generation queries, transfer/release |
| `stdlib/string.hp` | 1–400 (full) | subtract-form checks, overlap-safe append, clamped substring, borrowed view |
| `stdlib/vector.hp` | 1–308 (full) | generation-stamped `slice`, subtract-form growth guard, commit-last mutation |
| `stdlib/collections.hp` | 1–120, 160–259 and symbol scan to 640 | open addressing, tombstones, load guard, allocate-all-then-commit rebuild |

### 1.2 Committed studies read in full

`microsoft_stl.md` (pin `0cabaf3c7e0a73fe3040aabce40cd8833e80392b`, Apache-2.0 WITH
LLVM-exception), `microsoft_gsl.md` (main, MIT), `safer_cplusplus.md` (clone HEAD
`5edaf67acf746d9ee472a06a51094b31a3db57c0`, BSL-1.0), `fil_c.md` (branch `deluge`,
fetched `fugc.h` + `stdfil.h`, BSD-2-clause / LLVM-exception), `softboundcets.md` (master
HEAD `9a9c09f04e16f2d1ef3a906fd138a7b89df44996`, BSD-3-Clause tree + per-file MIT-style
notices), and `upstream-reuse.md` (the aggregate audit, including its TrapC row — no
repository, license UNDETERMINED).

### 1.3 Fresh re-verification with `webfetch` (exact URLs)

The four load-bearing upstream snippets were fetched again for this task. Confirmed text
is noted verbatim in the mapping tables; nothing here rests on a failed fetch.

| URL fetched 2026-11-23 | Returned | Confirmed here |
|---|---|---|
| `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/vector` | full text (4194 lines; tool saved rest to a local tool-output file) | `_Reallocation_guard2`, `_Allocation_guard`, `_Vaporization_guard`; `if (_Count > max_size() - _Oldsize) _Xlength();`; `_Calculate_growth(_Newsize)`; `_Alloc_temporary2<_Alty> _Obj(...); // handle aliasing`; "provide strong guarantee" / "provide basic guarantee" branch comments; `_STL_VERIFY(this->_Getcont() == _Right._Getcont(), "vector iterators incompatible")`; `_Verify_offset`, `_Verify_range` ("vector iterator range transposed"); `_Vector_val` destructor tombstones; `_Asan_*_guard` and the `GH-2464` zero-capacity comment. |
| `https://raw.githubusercontent.com/microsoft/GSL/main/include/gsl/span` | full text | `Expects(data \|\| ExtentType::size() == 0)` in `storage_type`; `Expects(idx < size())`, `Expects(size() > 0)`; `Expects((size() >= Offset) && (Count == dynamic_extent \|\| (Count <= size() - Offset)))`; three-pointer `span_iterator` with `begin_/end_/current_` checks and MSVC `_Verify_range`/`_Unwrapped`; empty-base `storage_type : public ExtentType`; `size_bytes() = size() * sizeof(element_type)`. |
| `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/softboundcets-lib/softboundcets.h` | full text | 4-field `{base,bound,key,lock}` trie record; `if (ptr < base \|\| (char*)ptr + size_of_type > bound) abort();`; `memcopy_check`/`memset_check` `dest > dest_bound - size` form **and the `size > (size_t) dest_bound` typo in the `src` branch**; `if (temp != key) abort();` temporal check; `shrink_bounds` = `max(base)`, `min(bound)`; free map tags `0`/`2`; `{ptr >> 25}`, `((ptr >> 3) & 0x3fffff)`, `from_ptr % 8 != 0`, `__WORDSIZE == 32` branch, `MAP_NORESERVE`. |
| `https://raw.githubusercontent.com/pizlonator/fil-c/deluge/filc/include/stdfil.h` | full text | `zlength`, `zinbounds`, `zvalinbounds` with `size <= upper - ptr`, `zhasvalidcap` ("has a capability and that capability is not free"), `zcheck`/`zcheck_readonly`, `zchecked_add`/`zchecked_mul`, free-bit/`free`-singleton semantics, `zptrtable` auto-purge, `zgc_completed_cycle`, `#define FILC_VERSION 687u`, and `#error "Cannot use <stdfil.h> from Yolo-C"` unless `__PIZLONATOR_WAS_HERE__`. |

No SaferCPlusPlus or TrapC URL was re-fetched: SaferCPlusPlus facts are taken from the
committed study's pinned clone (the study records every URL and the stale-path corrections),
and TrapC has no source repository at all (see §4).

---

## 2. Reuse verdicts by source

| Source | Reused here? | Exact reason |
|---|---|---|
| Microsoft STL | **ideas only** | Apache-2.0 WITH LLVM-exception permits reuse with notices, but the source is C++ templates/RAII/exceptions. Adapted: subtract-form checks, commit-last mutation, overlap-safe copy primitive, checked-access taxonomy. Not copied: any STL source text. |
| Microsoft GSL | **ideas only** | MIT, but `span`/`not_null`/`narrow` are templates/contracts. Adapted: invariant-at-construction, check-at-each-access, `(data \|\| size == 0)` rule, two-phase narrowing idea (narrowing helper is **not** present in the four headers, so not claimed as adapted). |
| SaferCPlusPlus | **ideas only** | BSL-1.0, but the mechanisms are template pointer wrappers, intrusive pointer lists and clang annotations for an external analyzer. Adapted: checked subrange + clamp shape, generation-style liveness idea. Not copied: registered/norad pointer machinery. |
| Fil-C | **ideas only, contract level** | Pointer representation and ABI are changed (InvisiCaps, GC, safepoints, `pizlonated_` mangling); no Yolo-C interop. Adapted only the *query vocabulary* (capacity/length/validity as named, fail-closed queries) and fail-closed invalidation on release. |
| SoftBoundCETS | **check predicates only** | Pointer representation is unchanged (one word) but the calling convention, symbol names and `main` are rewritten and metadata needs an instrumenting compiler. Adapted: subtract-form range predicate, key/lock-as-generation idea, fail-closed miss. Not copied: the trie, shadow stack, lock allocator, free map, abort policy. |
| TrapC (via `upstream-reuse.md`) | **no reuse** | No repository, no source, license UNDETERMINED. Documentation only; no code, no test, no API shape copied. |

---

## 3. Technique → current-code adaptation map

### 3.1 `stdlib/memory.hp` — the additive bounds-aware wrapper layer

| Upstream technique (anchor) | Current adaptation (exact `file:line`) | What changed / why it is not a copy |
|---|---|---|
| Subtract-form range check to avoid overflow in the test itself: MS STL `if (_Count > max_size() - _Oldsize)` (fresh URL §1.3); SoftBoundCETS `dest > dest_bound - size` (fresh URL §1.3). | `stdlib_mem_check_range` `stdlib/memory.hp:101-128`: NULL/untracked refusal (104–116), `off > cap` (117), `n > cap - off` (122), `STDLIB_CATEGORY_BOUNDS` + `CPLUS_EXIT_BOUNDS` notes (118–125). | Capacity is a `size_t` fetched from the existing side table, not a pointer bound. No pointer comparison, no upstream `abort()`. |
| Fail-closed metadata miss (SoftBoundCETS `metadata_load` zeroes all fields on trie miss; Fil-C invalid capability → trap). | `stdlib_mem_check` `stdlib/memory.hp:86-94` returns `STDLIB_FAIL` when `ptr == NULL || stdlib_alloc_size(ptr) == 0`; `stdlib_mem_check_range` repeats the check (111–116). Header comment `stdlib/memory.hp:20-29` documents untracked/interior-pointer cases. | "Fail closed" is an `error *` + telemetry note, not a trap. No side table of our own: it reads `stdlib_meta_find` via `stdlib_alloc_size` (`stdlib/error.hp:477-486`). |
| Liveness token / O(1) invalidation: SoftBoundCETS `{key,lock}`, `*lock = 0` on free; Fil-C `zhasvalidcap` + free-bit; generation idea from `safer_cplusplus.md` §2.4 (norad counter). | `stdlib_mem_token` `stdlib/memory.hp:66-68` → `stdlib_alloc_generation` `stdlib/error.hp:513-522`; `stdlib_mem_check_token` `stdlib/memory.hp:73-82` → `stdlib_alloc_check_generation` `stdlib/error.hp:524-536`; mismatch notes `STDLIB_CATEGORY_LIFECYCLE` / `CPLUS_EXIT_USE_AFTER_FREE` (77–79). `token == 0` opts out (70–72, 85). | Generation is a single `unsigned long long` in the local side table (`stdlib/error.hp:106`, bumped at `:341, :574, :453`), not a shared lock word. No mmap'd temporal space, no free map. |
| Named size/length queries: Fil-C `zlength`/`zgetupper`/`zvalinbounds`, GSL `span::size()`/`size_bytes()`. | `stdlib_mem_capacity` `stdlib/memory.hp:48-50` → `stdlib_alloc_size` `stdlib/error.hp:477-486`; `stdlib_mem_count` `stdlib/memory.hp:56-59` → `stdlib_alloc_query_length` `stdlib/error.hp:538-556` (sets `*out_count = 0` on failure, 542–545). `stdlib_alloc_length` `stdlib/error.hp:499-511` and `stdlib_safe_div` `:462-474` implement the element-count query without variable division. | Queries return 0/-1 for unknown/freed pointers instead of trapping (Fil-C) or a static extent (GSL). |
| Accept `(data, 0)` and reject null+nonzero: GSL `Expects(data || size == 0)` (fresh URL §1.3); Fil-C `zvalinbounds` `if (!size) return 1`. | `n == 0` is a validated no-op in `stdlib_mem_copy` `stdlib/memory.hp:146-147`, `stdlib_mem_move` `:162-163`, `stdlib_mem_set` `:175-176` — validation runs **before** the zero-length early return. | Same rule, expressed as a plain `if` in a header; no `Expects`/terminate. |
| Overlap-safe copy vs copy primitive: MS STL `_Traits_move_batch` (`char_traits::move` = memmove semantics) on in-place paths; GSL `span` has no copy primitive. | `stdlib_mem_copy` `stdlib/memory.hp:137-150` uses `memcpy` (148) and documents that overlap is the caller's problem (135–136); `stdlib_mem_move` `:153-166` uses `memmove` (164). | Two explicit functions; no type traits, no `if constexpr`. |
| Ownership transfer invalidates outstanding borrows; borrowed handle does not own: SaferCPlusPlus §4 (scope ownership non-transferable), GSL `owner<T*>` (annotation only), Fil-C one-action invalidation. | `stdlib_mem_borrow` `stdlib/memory.hp:188-195` validates without transferring; `stdlib_mem_transfer` `:203-215` → `stdlib_transfer_ownership` `stdlib/error.hp:558-579` (generation bump at `:574`); `stdlib_mem_release` `:220-225` → `stdlib_release` `stdlib/error.hp:581-588` and NULLs the handle. | C+ resource classes + `drop`/`stdlib_release` are already stronger than GSL's alias; no `not_null`/`owner` typedef, no intrusive pointer list. |
| Error-return instead of hard abort (SoftBoundCETS `__softboundcets_abort()`; Fil-C `zsafety_errorf`). | Every guarded wrapper routes failure through `error_fail_cat` (`stdlib/memory.hp:77-79, 89-91, 105-108, 112-115, 118-125, 205-213`), with a NULL `error*` still reported via `stdlib_note` (`stdlib/memory.hp:31-35`). | Recoverable API; fatal probes remain the runtime's job (`stdlib/error.hp` header boundary). |

`stdlib/memory.hp` is **new** and is the only header in this task whose public surface was
extended for upstream ideas. Its own header comment (`stdlib/memory.hp:1-9`) explicitly
records "no fat pointer, no side table of our own, and no runtime change".

### 3.2 `stdlib/string.hp`

| Upstream technique (anchor) | Current adaptation (exact `file:line`) | What changed / why it is not a copy |
|---|---|---|
| Subtract-before-add overflow checks: MS STL `max_size() - _Old_size < _Size_increase` and `_Clamp_suffix_size`; `stdlib/realloc`-style overflow guards. | `string::append` `stdlib/string.hp:254-255`: `extra > STDLIB_ALLOC_LIMIT - 1 \|\| this->count > STDLIB_ALLOC_LIMIT - 1 - extra`. `reserve` `stdlib/string.hp:216` uses `want > STDLIB_ALLOC_LIMIT`. | Integer-only arithmetic against the local `STDLIB_ALLOC_LIMIT`, not `max_size()` from an allocator trait. The `- 1` reserves the terminator. |
| Demand floor on growth: MS STL `_Calculate_growth` = geometric, floored at demand (fresh URL §1.3). | `stdlib/string.hp:269-279`: `need = count + extra + 1`; if `need > space`, `want = need`, then `want = space * 2` guarded by `space <= STDLIB_ALLOC_LIMIT / 2` and re-floored by `if (want < need) want = need` (272–275). | The *floor* shape is adapted; the multiplier is 2x, not 1.5x, and the `_Alloc_mask` round-up (and its pinned capacities 1007/1510) was **not** copied — see §4.4. |
| Overlap-safe append / alias-safe temporary: MS STL `_Append` copies the old buffer before reading an aliased source (`microsoft_stl.md` §5) and `_Alloc_temporary2 ... // handle aliasing` (fresh URL §1.3). | `stdlib/string.hp:260-283`: integer-address alias window (264–265), `offset` captured **before** `string_reserve` (267), re-based `memmove` after reallocation (280–281), `memcpy` otherwise (283). Comment at 262–263 explains why integer addresses are compared. | No `char_traits::move`, no template lambda callback, no SSO. **Known gap deliberately left unchanged:** the window is `[chars, chars + count]`, so an interior pointer past `count` after `clear()` still takes the `memcpy` branch; this is the highest residual risk recorded in `microsoft_stl.md` §9.2/S-1 and is *not* silently "fixed" here. |
| Commit-last mutation with scoped rollback: MS STL `_Reallocation_guard2` / `_Allocation_guard` (fresh URL §1.3). | `reserve` `stdlib/string.hp:212-234` (fresh allocate 221, copy 224–225, release old 228–229, publish 230–232); `copy_from` `:296-321`; `append` reallocation via `string_reserve` `:277-278` before any write. | Explicit cleanup branches instead of RAII guard objects (Agent 1 scope, §4.3). |
| Checked offset / clamp / always-terminate: MS STL `_Check_offset`, `_Check_offset_exclusive`, `_Clamp_suffix_size`; SaferCPlusPlus `subsection` clamps with `min` (`safer_cplusplus.md` §3). | `substr` `stdlib/string.hp:328-345`: `start > count` refusal (334), `n > available` clamp (337–338), `n + 1 > out_size` clamp (339–340), `memmove` (342), forced terminator (343). `field` `:358-381` repeats the clamp+terminate shape (369–373). | Plain arithmetic; exceptions replaced by `-1`. These refusals remain **silent** (no `stdlib_note`) — the "emit notes on currently silent refusals" idea from the studies was not adopted here. |
| Checked view descriptor / refuse-on-stale: GSL `span` `Expects` on every access; SaferCPlusPlus `TRandomAccessSection`; MS STL debug iterators. | `string_view` `stdlib/string.hp:79-114` stores only `(const char *chars, size_t count)` (81–82); `equals` checks `other == NULL` (99–100) and stale `chars` (101–102); `to_view` `stdlib/string.hp:383-388` copies pointer+count only. | **Not adapted:** no generation stamp, no `at_checked`, no library-enforced refusal. This is the documented gap in `upstream-reuse.md` §3 and `microsoft_gsl.md` §8; adding it would be a header API change beyond this mapping doc. |

### 3.3 `stdlib/vector.hp`

| Upstream technique (anchor) | Current adaptation (exact `file:line`) | What changed / why it is not a copy |
|---|---|---|
| Generation-stamped borrowed view with refusal: MS STL debug iterators (`_Verify_offset`, `_Compat`, `_Verify_range`, "vector iterators incompatible"/"range transposed", fresh URL §1.3); GSL `span` checked `operator[]`/`subspan`; SaferCPlusPlus `TRandomAccessSection`; SoftBoundCETS four-field slice + monotonic shrink (`softboundcets.md` §6.2). | `slice` `stdlib/vector.hp:29-86`: fields `items/count/generation_ref/generation_at` (30–33); `stale()` `(47-50)` compares live counter; `valid()` (52–54); `at_checked` `:57-67` returns -1 + `STDLIB_CATEGORY_BOUNDS` note **without reading stale memory** (60–64); `sub` `:71-85` clamps `length = min(length, count - start)` (76–78) and **propagates the same generation ref/stamp** (81–82). | A coarse, header-only generation check instead of a per-iterator proxy registry. `vector.hp:16-19` explicitly documents that a slice outliving its owner is undetectable — unchanged. |
| Subtract-form overflow guard / saturating growth: MS STL `space > max - space/2` + geometric floored at demand (fresh URL §1.3). | `reserve` `stdlib/vector.hp:116` `want > STDLIB_ALLOC_LIMIT / sizeof(int)`; `push_back` `:139` `space > (STDLIB_ALLOC_LIMIT / sizeof(int)) / 2`; `insert` `:197` repeats the guard. Doubling at `:144, :199`. | The guard is the adapted part. 1.5x saturation, `allocator::max_size()` and `_Xlength()` were **not** copied (§4.4). Power-of-two doubling is kept as documented C+ behavior. |
| Commit-last mutation, failure-atomic with explicit rollback: MS STL `_Reallocation_guard2`, `_Vaporization_guard`, `_Allocation_guard` (fresh URL §1.3). | `reserve` `stdlib/vector.hp:111-133` (allocate 121, copy 124–125, release 126–127, publish 128–131); `copy_from` `:233-259` (self-copy no-op 241–242, fresh 243–251, commit 252–257); `shrink_to_fit` `:263-286` (fresh 276, release 281, publish 282–284). | Same discipline, no C++ destructor guards; header comment `:5-7` promises the failure-atomic contract in words. |
| Invalidation on mutation / propagate metadata on derived views: SoftBoundCETS "bump generation on realloc, do not patch each slice" (`softboundcets.md` §6.2.6) and `propagate_metadata_shadow_stack_from(1,0)` (§4.2). | `generation++` at `stdlib/vector.hp:130, 185, 208, 222, 228, 257, 273, 284, 303`; non-reallocating `push_back` writes without bumping (`:149`); `view()` records `&this->generation` and its value (`:290-295`); `slice::sub` propagates both (`:81-82`). | A single counter in the owner, not per-pointer metadata copied by a compiler pass. |
| Checked accessor + sentinel compatibility accessor: MS STL `at()` throws `out_of_range`; GSL `Expects`. | `at_checked` `stdlib/vector.hp:165-175` returns -1 + note; `at` `:155-162` keeps the 0 sentinel for compatibility (documented at 153–154). | No exceptions; the sentinel API is deliberately not silently changed (see `microsoft_stl.md` §9.1/V-4). |
| Invariant checked at each step / empty-range handling: GSL `span` `Expects(size() > 0)` at `front/back`. | `pop_back` `stdlib/vector.hp:178-187` refuses `used == 0` (181–182); `erase` `:215-216`; `slice::valid()` `:52-54` requires non-null, non-empty and fresh. | Plain `if` + return; no `Expects`/terminate. |

### 3.4 `stdlib/collections.hp`

| Upstream technique (anchor) | Current adaptation (exact `file:line`) | What changed / why it is not a copy |
|---|---|---|
| Allocate-all-then-commit with rollback on partial failure: MS STL `_Reallocation_guard2` (fresh URL §1.3). | `intmap::stdlib_rebuild` `stdlib/collections.hp:163-210`: three buffers allocated first (172, 175, 180), each failure path releases the earlier ones (177, 182–183), commit only at 197–206. `strmap::stdlib_rebuild` `:368-410` repeats it (allocs 374/377/382, rollback 378/383–385, commit 407–410). | Explicit `stdlib_release` branches; no RAII guard. This is the strongest direct match to the upstream pattern. |
| Overflow-checked growth before multiplication/doubling: MS STL `_Get_size_of_n`, SoftBoundCETS `zchecked_mul` analogue. | `stdlib_map_next_pow2` `stdlib/collections.hp:57-65` (guard `cap > (SIZE_MAX >> 2)` at 60); `stdlib_map_rebuild_capacity` `:80-83`; `stdlib_ring_copy` `:94-98`; header rule `:16-17`. | Integer guards against `STDLIB_ALLOC_LIMIT`, not `_Xlength()`/abort. |
| Tombstones for deletion/release: SoftBoundCETS free-map tag `2` (fresh URL §1.3); open-addressed tables generally. | `MAP_EMPTY/MAP_FULL/MAP_DELETED` `stdlib/collections.hp:33-35`; first tombstone remembered and reused at `:226, 240-241, 249-251`; set rebuid purges tombstones at `:187-196`. | The C+ open-addressed table is a **local** structure; SoftBoundCETS's free map is a different mmap'd table keyed by allocation key. Only the tombstone idea is shared. |
| Power-of-two capacity + bitmask indexing: general technique; SoftBoundCETS trie uses `>>25`/`(>>3)&0x3fffff` with `2^22` slots (fresh URL §1.3). | `stdlib_map_next_pow2` `stdlib/collections.hp:57-65` (min 8), index `hash & (capacity - 1)` at `:152, 189, 237, 253` and the `strmap`/`intset` equivalents (`:353, :450, :558, :626`). | Same masking idea; the hard-wired 64-bit shifts, `>>3` 8-byte granularity and `% 8` early return were **not** copied (§4.1). |
| Load-factor guard so probing terminates: general hash-table discipline. | `stdlib_map_needs_room` `stdlib/collections.hp:68-72`: `((used+1) * 10) >= (capacity * 7)` with 64-bit widening; `stdlib_map_rebuild_capacity` `:77-86` doubles only when live entries exceed half, otherwise rebuilds at the same size to purge tombstones. | Local constants (70%); no upstream code or upstream constant was copied. |
| No iterators / by-value access instead of debug iterator validation: contrast with MS STL iterator proxy and GSL `span_iterator`. | Header rule `stdlib/collections.hp:20-22`; `get/at_checked/top_checked/front_checked` by value, e.g. `:257-...`; probe loops bounded by capacity (`:153`, `:354`, `:559`). | The iterator proxy registry and three-pointer checked iterator were **not** copied (Agent 1 scope, §4.3); by-value access removes the dangling-iterator class instead of detecting it. |

---

## 4. What was NOT copied, and why

### 4.1 ABI / pointer representation

- **Fil-C** `InvisiCaps`: capability pointers with a trusted *lower* stored in an aux
  allocation, per-load bounds checks, free-bit + free-singleton repointing, GC/safepoints,
  store barriers, `pizlonated_` symbol mangling and a whole-program "no Yolo-C interop"
  requirement (`fil_c.md` §§1–5). None of it was copied: it changes the pointer contract
  and the ABI and cannot coexist with an injected DLL into an unmodified executable. The
  `stdfil.h` fetched here `#error`s outside the Fil-C compiler (`__PIZLONATOR_WAS_HERE__`).
  Only the *query vocabulary* (`zlength`/`zvalinbounds`/`zhasvalidcap` →
  `stdlib_mem_capacity`/`stdlib_mem_count`/`stdlib_mem_check`) transferred.
- **SoftBoundCETS** metadata: the mmap'd two-level trie (`>>25`, `(>>3)&0x3fffff`,
  `MAP_NORESERVE`), shadow stack (slot 0 = return value), temporal key/lock space, free
  map, 8-byte slot granularity, 64-bit-only huge reservations, `-fsoftboundcets`,
  `softboundcets_` symbol renaming and `main` → `softboundcets_pseudo_main`
  (`softboundcets.md` §§2, 5, 7). None copied. Its check *predicates* were re-expressed as
  `size_t` comparisons in `stdlib/memory.hp:117-126`. The upstream `size > dest_bound`
  `src` typo and `+1024` approximate libc bounds were explicitly **not** carried over,
  nor was `__softboundcets_abort()`/`backtrace()`.
- **TrapC** managed pointers, hidden RTTI, `testof`/`offsetof`/`lastof`, no-op `free`/`delete`
  and trap-aware wrappers: no repository exists and no source was available (`upstream-reuse.md`
  §1 row 8). Zero reuse; neither code nor API shape.
- **In every C+ header here** a raw `T *` remains one word; no pointer is widened, tagged,
  or wrapped, no public C signature changes, and no new exported symbol is introduced.

### 4.2 License

- Permissive licenses only were eligible: MS STL `Apache-2.0 WITH LLVM-exception`, GSL
  `MIT`, SaferCPlusPlus `BSL-1.0`, Fil-C runtime files BSD-2-clause (LLVM parts
  Apache-2.0-with-LLVM-exception), SoftBoundCETS tree `BSD-3-Clause` with per-file
  MIT-style notices. All studies transferred **ideas/short quotes**, not vendored source;
  therefore no attribution obligation was triggered by this task.
- **Cake** (`GPL-3.0-or-later` + AI-training-use addendum) was already rejected in
  `docs/UPSTREAM_AUDIT.md`; no Cake source/header/test was copied and none is mapped above.
- **TrapC** license is UNDETERMINED (intent only, no LICENSE/SPDX/repository); no reuse is
  authorized. The literal `echo "Open Source MIT" > LICENSE` in WG14 `n3423` is a Cmaker
  tutorial example, not TrapC's license (`upstream-reuse.md` §1).
- `third_party/` was not touched.

### 4.3 Agent 1 compiler / language scope

Not copied because the C+ header surface has no templates, concepts, SFINAE, exceptions,
operator overloading or scope-guard lowering (see the studies' "requires Agent 1" sections
and `upstream-reuse.md` §8):

- C++ exceptions and contracts: `bad_alloc`, `length_error`, `out_of_range`, GSL
  `Expects`/`Ensures` → `std::terminate()`, SaferCPlusPlus `MSE_THROW`. C+ uses `0`/`-1`
  + `error *` + `stdlib_note`.
- RAII guard objects whose destructor cleans up (`_Reallocation_guard2`, `_Tidy_guard`,
  `_Asan_*_guard`): only the *pattern* was adapted with explicit `stdlib_release` branches
  in `vector.hp:126-127, 252-253, 281-282` and `collections.hp:177, 182-183, 378, 383-385`.
- Per-iterator owner proxy registry (`_Iterator_base12`, `_Container_proxy`, `_Orphan_all`,
  `_Verify_offset`, `_Compat`, `_Verify_range`) and GSL's three-pointer checked iterator:
  replaced by the generation-stamped `slice`/by-value access; owner-outliving-view remains
  undetectable (`vector.hp:16-19`).
- Generic `span<T,E>`, `not_null<T>`, `narrow<T,U>`, SaferCPlusPlus scope/registered/norad
  pointer wrappers and lifetime annotations for an external analyzer: no generics in C+.
- SSO union storage, fancy pointers, `allocator_traits`, `if constexpr`, ASan container
  annotations and destructor tombstones: type-system/compiler/runtime features.
- Fil-C GC/safepoints and SoftBoundCETS's LLVM IR pass: compiler/runtime work owned by
  Agent 1, not library code.

### 4.4 Deliberate non-adaptations inside otherwise-reused sources

- **1.5x saturating growth and the string `_Alloc_mask` round-up** (MS STL top reuse
  technique in `microsoft_stl.md` §12). Only the subtract-form guard and demand floor were
  adapted; changing the multiplier/round-up would break the pinned capacity expectations
  recorded in `microsoft_stl.md` §9.1/V-1 and §9.2/S-2. Current code keeps 2x doubling
  (`vector.hp:144, 199`; `string.hp:273`) and the min-8 power-of-two rule
  (`collections.hp:57-65`).
- **Generation tracking for `string_view`**: deliberately *not* added. It would be a
  public struct-field/API change; the gap is documented (`string.hp:79-114`,
  `upstream-reuse.md` §3) rather than silently fixed.
- **Closing the `string::append` alias-window gap**: the window at `string.hp:264-265` is
  still `[chars, chars + count]`, not the whole allocation; the residual overlapping-
  `memcpy` risk is recorded in `microsoft_stl.md` §9.2/S-1 and left unchanged here.
- **Emitting `stdlib_note` on silent refusals** (`substr`/`field` out-buffer,
  `string_view::equals(NULL)`): idea recorded, not implemented in the four headers.
- **No narrowing helper, no `at_checked` on `string_view`, no `Expects`-style contract
  policy, no exceptions**: none of these symbols exist in the current headers, so no
  reuse is claimed.

---

## 5. Top adapted techniques (ranked by impact in the current tree)

1. **Subtract-form range/capacity arithmetic** — MS STL `limit - current` checked
   demand and SoftBoundCETS `dest_bound - size` predicate → `stdlib/memory.hp:117-126`,
   `stdlib/string.hp:254-255`, `stdlib/vector.hp:116, 139, 197`,
   `stdlib/collections.hp:60, 94-98`. Eliminates overflow in the *test* itself; pure
   `size_t`, no pointer arithmetic, no `abort()`.
2. **Commit-last mutation with explicit rollback** — MS STL `_Reallocation_guard2` →
   `stdlib/vector.hp:111-133, 233-259, 263-286`; `stdlib/string.hp:212-234, 296-321`;
   `stdlib/collections.hp:163-210, 368-410`. This is why every mutator in these headers
   can promise failure atomicity (`vector.hp:5-7`, `string.hp:7-12`,
   `collections.hp:12-15`).
3. **Generation token as a liveness/lifecycle check** — SoftBoundCETS key/lock, Fil-C
   free-bit, SaferCPlusPlus runtime-lifetime idea → `stdlib/memory.hp:66-94, 188-215`
   over `stdlib/error.hp:513-579`; `vector.hp:47-85, 130, 290-303`. Fail-closed
   `STDLIB_CATEGORY_LIFECYCLE` refusal instead of a trap; no shared lock word.
4. **Checked view with refuse-on-stale and clamped subrange** — GSL `span`/SaferCPlusPlus
   section/MS STL debug checks → `vector.hp:29-86` (`at_checked`, `sub`, `stale`);
   clamped + terminated `substr`/`field` in `string.hp:328-381`. No iterator registry;
   owner-outliving-view caveat kept explicit.
5. **Overlap-safe copy primitive plus source-offset captured before reallocation** —
   MS STL `_Append`/`_Alloc_temporary2` → `stdlib/string.hp:260-283` and the separate
   `stdlib_mem_copy`/`stdlib_mem_move` pair `stdlib/memory.hp:137-166`. In-place uses
   `memmove`; reallocating path rebases a saved offset.
6. **Named, fail-closed metadata queries** — Fil-C `zlength`/`zvalinbounds`/`zhasvalidcap`
   → `stdlib/memory.hp:48-59, 86-94` on `stdlib/error.hp:477-556`. Unknown/freed pointers
   yield 0/-1, not a trap.
7. **Invariant at construction, check at each access** — GSL `span`
   `(data || size == 0)` and `Expects(idx < size())` → `stdlib/memory.hp:146-147, 162-163,
   175-176` (validate, then zero-length no-op) and `collections.hp` checked by-value
   accessors (`:20-22`). Terminate policy not transplanted.
8. **Tombstones + allocate-all-then-commit rebuild** — local open addressing
   (`collections.hp:33-35, 163-210, 368-410`) with the tombstone idea shared with the
   SoftBoundCETS free map; no upstream table structure copied.

---

## 6. Limits of this document

- Line numbers are from the working tree on 2026-11-23 and will drift.
- Only the four URLs in §1.3 were freshly re-fetched here; all other upstream statements
  are quotations/paraphrases of the committed studies, which record their own exact URLs,
  revisions and fetch failures. No claim rests on a failed fetch.
- This is a source-reading/mapping task. Nothing was built, executed, injected or tested;
  no memory-safety or live-game result is claimed. Passing C+ fixtures cited in the studies
  are contract-conformance evidence, not proof of the upstream guarantee.
- SaferCPlusPlus and GSL "top techniques" that require generics, exceptions or a compiler
  analyzer are listed in §4.3 as **not** adapted; they are not counted in §5.
- Only `docs/stdlib/container_safety/UPSTREAM_REUSE.md` was created. No header, test,
  build file or git index entry was changed by this task.

# SoftBoundCETS (santoshn/softboundcets-34) upstream source study

Branch under study: `agent/modern-stdlib`.
Scope: **study only**. The only file created or modified for this task is this
document. No headers, build files, tests, runtime code, or git index entries were
touched. No upstream compiler or runtime was built or executed; upstream source
was fetched and read only.

Evidence labels (same convention as sibling studies such as `llvm_clang.md`):

- **[OBSERVED]** — text retrieved from the exact URLs in §8, or from a committed
  upstream file quoted with a path and line/function name.
- **[INFERENCE]** — my design reading; not a quoted upstream fact.
- **[NOT FOUND]** / **[NOT INSPECTED]** — artifact requested by the brief that
  does not exist at the pinned revision, or that I deliberately did not fetch;
  reported rather than invented.

Revision policy: GitHub's tags endpoint returned `[]` — **there are no tags**.
Every URL below therefore uses the `master` HEAD SHA resolved through the
commits API: `9a9c09f04e16f2d1ef3a906fd138a7b89df44996` (commit date
`2014-09-19T15:04:04Z`, message "Merge pull request #4 from edmcman/master",
tree `8cae63ff7ab8d1004da2e792805e68b381fdb87e`). I re-fetched the primary
runtime header through a SHA-pinned URL (`.../9a9c09f.../softboundcets-lib/softboundcets.h`)
and confirmed byte-for-byte identical content to the `master` form, so the
`master` and SHA URL forms are interchangeable at the time of this study.

---

## 1. Provenance and license

| Item | Value | Evidence |
|---|---|---|
| Repository | `santoshn/softboundcets-34` | [OBSERVED] API `repos/...`; full name `santoshn/softboundcets-34`, `fork: false`, created `2013-12-25T23:41:11Z` |
| Description | "SoftBoundCETS for LLVM+Clang version 34" | [OBSERVED] API `description` |
| Default branch | `master` | [OBSERVED] API `default_branch` |
| Pin | `9a9c09f04e16f2d1ef3a906fd138a7b89df44996` | [OBSERVED] commits API + SHA-pinned raw fetch |
| Tags | none (`[]`) | [OBSERVED] tags API |
| GitHub license metadata | key `bsd-3-clause`, name `BSD 3-Clause "New" or "Revised" License`, SPDX id `BSD-3-Clause` | [OBSERVED] API `license` object |
| Top-level `LICENSE` (blob `9a6efbb3e8e28d263c74e0b434830f46c09fe255`) | "Copyright (c) 2013, Santosh Nagarakatte / All rights reserved." + the three BSD conditions + "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\" ..." | [OBSERVED] raw `LICENSE` |

Two license facts are worth recording verbatim because they affect reuse:

1. **[OBSERVED]** The top-level `LICENSE` clause 3 contains the **unfilled
   template placeholder** `* Neither the name of the {organization} nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.` The file itself
   never spells out "BSD-3-Clause"; that identifier comes from GitHub's
   classifier. For our records the SPDX identifier is `BSD-3-Clause`, with the
   caveat that the literal license text has the `{organization}` placeholder.
2. **[OBSERVED]** Individual source files (`softboundcets.h`, `softboundcets.c`,
   `softboundcets-checks.c`, `softboundcets-wrappers.c`, `SoftBoundCETS.cpp`)
   carry a different, MIT-style header: `Permission is hereby granted, free of
   charge, to any person obtaining a copy of this software ...` followed by
   three conditions and a non-endorsement clause naming Santosh Nagarakatte,
   Milo M. K. Martin, Jianzhou Zhao, Steve Zdancewic and the University of
   Pennsylvania. `softboundcets.h` / `softboundcets.c` / `SoftBoundCETS.cpp`
   say `Copyright (c) 2014 ...`; the checks/wrappers files say `Copyright (c)
   2011 ...`. So the tree mixes a top-level BSD-3-Clause `LICENSE` with
   per-file permissive MIT/BSD-style notices.

**Reuse position [INFERENCE]:** both notice families are permissive and
compatible with this repository, but any *verbatim* code transfer requires
retaining the relevant per-file notice and copyright line. The design ideas in
§6 are ideas, not copied code, and are paraphrased accordingly.

Source files actually inspected (all blobs verified through the contents API):

| Path (under repo root) | Blob SHA | Role |
|---|---|---|
| `LICENSE` | `9a6efbb3e8e28d263c74e0b434830f46c09fe255` | License text |
| `README.md` | `0f3d7b2ddd93137ab25b1df12ac389838255f0b5` | Build/usage, limitations |
| `softboundcets-lib/softboundcets.h` | `4a94ab440d5c24d56bd23c1b7aa274980f38f59b` | **metadata representation + all check functions** |
| `softboundcets-lib/softboundcets.c` | `fe6379211618c69ff2f8cfcd3149cb4d4422a830` | runtime init, tables, `main` wrapper |
| `softboundcets-lib/softboundcets-checks.c` | `0bca2f9a3d3b00ae4ba2d59f4c06284231adc672` | just `#include "softboundcets.h"` |
| `softboundcets-lib/softboundcets-wrappers.c` | `c4ac66cb6f11b96f30fe3ef68b7515e6f6960711` | libc wrappers, metadata propagation helpers |
| `softboundcets-llvm-clang34/lib/Transforms/SoftBoundCETS/SoftBoundCETS.cpp` | `c059a38b6089b9f8be19a94219c4ffda3f91fb14` | **LLVM IR instrumentation pass** (5164 lines; selected sections read) |

**[NOT INSPECTED]** `softboundmpx.*`, `SoftBoundCETSMPX.cpp`, `SoftBoundMPX.cpp`,
`FixByValAttributesPass.cpp`, `InitializeSoftBoundCETS.cpp`,
`softboundcets-llvm-clang34/LICENSE.TXT`, and the `tests/` tree. They exist
in the tree listings but were outside the brief's minimum ("runtime header/source
that defines metadata representation and the check functions") and are not
needed for the ABI verdict.

---

## 2. Shadow metadata representation

### 2.1 The metadata record

**[OBSERVED]** `softboundcets-lib/softboundcets.h` defines one record,
`__softboundcets_trie_entry_t`, with four fields in the default build:

```c
typedef struct {
  void*  base;   /* lowest valid address (inclusive)         */
  void*  bound;  /* one-past-the-end address (exclusive)     */
  size_t key;    /* temporal generation identifier           */
  void*  lock;   /* address of the word holding the live key */
} __softboundcets_trie_entry_t;
```

Compile-time variants shrink it: `__SOFTBOUNDCETS_SPATIAL` → `{base,bound}`
(2 fields, 16 bytes), `__SOFTBOUNDCETS_TEMPORAL` → `{key,lock}` (2 fields,
16 bytes), `__SOFTBOUNDCETS_SPATIAL_TEMPORAL` (or default) → 4 fields,
**32 bytes**. Index macros are `__BASE_INDEX=0, __BOUND_INDEX=1, __KEY_INDEX=2,
__LOCK_INDEX=3`; unused indices are set to the sentinel `10000000` in
single-dimension builds. `softboundcets.c` asserts
`sizeof(__softboundcets_trie_entry_t) >= 16`.

**Critical nuance [OBSERVED]:** metadata is stored **by address of the pointer
variable** (`addr_of_ptr`), not by the pointer value. `__softboundcets_metadata_store`
and `__softboundcets_metadata_load` index the table with the *storage location*
that holds a pointer. This is a "side table on pointer slots", not a map from
pointee addresses.

### 2.2 Two-level radix trie

**[OBSERVED]** From `__softboundcets_metadata_store` / `_load` in
`softboundcets.h`:

```c
primary_index   = ptr >> 25;                 /* 2^23 primary entries      */
secondary_index = (ptr >> 3) & 0x3fffff;     /* 2^22 secondary slots      */
entry_ptr = &__softboundcets_trie_primary_table[primary_index][secondary_index];
```

The `>> 3` encodes an **8-byte granularity**: one metadata slot per 8-byte
aligned pointer-sized cell. `__softboundcets_copy_metadata` explicitly bails
out with `if (from_ptr % 8 != 0) return;`. On a miss in `_load` with
`__SOFTBOUNDCETS_PREALLOCATE_TRIE == 0`, all four outputs are set to `0`
(fail-closed default for uninitialized pointer reads).

The tables are `mmap`'d with `MAP_NORESERVE`, so the virtual reservation is
large but lazily backed. **[OBSERVED]** 64-bit constants in `softboundcets.h`:
temporal space `64 * 1024 * 1024` words, free map `32 * 1024 * 1024` words,
primary trie `8 * 1024 * 1024` entries, secondary table `4 * 1024 * 1024`
entries (× 32 B ≈ 128 MB each), stack temporal space `1024 * 64` words,
global lock space `1024 * 32` words, shadow stack `128 * 32` words.

### 2.3 Shadow stack (out-of-band call metadata)

**[OBSERVED]** `softboundcets.h` documents the frame layout verbatim:

```
1) size of the previous stack frame
2) size of the current stack frame
3) base/bound/key/lock of each argument
```

`__softboundcets_allocate_shadow_stack_space(num_pointer_args)` advances the
pointer by `prev_stack_size + 2` and writes `num_pointer_args *
__SOFTBOUNDCETS_METADATA_NUM_FIELDS` as the current size; `_deallocate_...`
subtracts `read_value + 2`. Offset of field `f` for argument `arg_no` is
`2 + arg_no * METADATA_NUM_FIELDS + __{BASE,BOUND,KEY,LOCK}_INDEX`.
**Slot 0 is the return-value slot** (used by `__softboundcets_store_return_metadata`
and by the pass's `introduceShadowStackLoads(call_inst, ..., 0)`); pointer
arguments start at slot 1.

### 2.4 Temporal lock space and free map

**[OBSERVED]** `softboundcets.c` initialises:

- `__softboundcets_key_id_counter = 2` (`0` = unused, `1` = globals — stated in
  the source comment `/* key 0 means not used, 1 is for globals*/`);
- a global lock word `__softboundcets_global_lock` containing `1`;
- a bump-allocated temporal space for heap locks and a separate one for stack
  frames (`__softboundcets_stack_temporal_space_begin`);
- `__softboundcets_lock_next_location`, a recycled-lock free list;
- `__softboundcets_free_map_table`, an open-addressing table with tombstones.

`__softboundcets_memory_allocation` obtains `temp_id = key_id_counter++`, takes a
lock word from `_allocate_lock_location()`, writes `*lock = temp_id`, and adds
`(temp_id, ptr)` to the free map. `__softboundcets_stack_memory_allocation`
uses a bump pointer in the stack temporal space (or a constant key/lock under
`__SOFTBOUNDCETS_CONSTANT_STACK_KEY_LOCK`).

---

## 3. Spatial and temporal check algorithms

### 3.1 Spatial dereference checks

**[OBSERVED]** `__softboundcets_spatial_load_dereference_check` and
`_store_dereference_check` in `softboundcets.h` are identical in substance:

```c
if ((ptr < base) || ((void*)((char*)ptr + size_of_type) > bound))
  __softboundcets_abort();
```

`size_of_type` is passed by the compiler pass from the LLVM load/store type.
The check therefore validates `[ptr, ptr + sizeof(T)) ⊆ [base, bound)`.

**[OBSERVED]** `__softboundcets_spatial_call_dereference_check(base, bound, ptr)`
uses the weaker form `if ((base != bound) && (ptr != base)) abort();` — i.e. an
indirect call target must equal the base of a non-degenerate object, preventing
calls into the middle of a function object.

### 3.2 Temporal checks

**[OBSERVED]** `__softboundcets_temporal_load_dereference_check(pointer_lock,
key[, base, bound])` and its `_store_` twin:

```c
size_t temp = *((size_t*)pointer_lock);
if (temp != key) __softboundcets_abort();
```

A pointer carries a *key* in its metadata and a pointer to a *lock word*. The
lock word holds the same key while the object is live. Invalidation is O(1):
**[OBSERVED]** `__softboundcets_memory_deallocation(ptr_lock, ptr_key)` does
`*((size_t*)ptr_lock) = 0;` then pushes the lock onto the reuse free list. Every
stale pointer sharing that lock now fails `key != 0`. Stack objects use
`__softboundcets_stack_memory_deallocation`, which decrements the stack
temporal bump pointer and zeroes the word.

### 3.3 Bulk memory operations

**[OBSERVED]** `__softboundcets_memcopy_check` and `_memset_check` have variants
per mode. The spatial+temporal variant checks, under `#ifndef __NOSIM_CHECKS`:

```c
if (size >= LONG_MAX) abort();
if (dest < dest_base || (char*)dest > ((char*)dest_bound - size) ||
    (size > (size_t)dest_bound)) abort();
if (src  < src_base  || (char*)src  > ((char*)src_bound  - size) ||
    (size > (size_t)dest_bound)) abort();          /* sic */
if (dest_key != *((size_t*)dest_lock)) abort();
if (src_key  != *((size_t*)src_lock))  abort();
```

**[OBSERVED / caution]** The `src` bound condition compares `size` against
`dest_bound`, not `src_bound`, in both the spatial-only and combined variants;
this looks like an upstream typo. The temporal-only variant drops the range
checks entirely and only compares keys. I flag this as **do not copy verbatim**
(see §6.4).

`__softboundcets_copy_metadata(dest, from, size)` copies trie entries (32 bytes
each in combined mode) in 8-byte strides, walking primary-table boundaries and
allocating missing secondary tables. This is what makes `memcpy` of pointer
storage preserve bounds/keys.

### 3.4 Double-free detection

**[OBSERVED]** `__softboundcets_check_remove_from_free_map(ptr_key, ptr)` probes
`index = (ptr_key + counter) % N`; a slot with `tag == 0` means "no live
allocation" → `abort()` (double free / unmatched free); a slot equal to `ptr`
is set to `2` (tombstone) and returns. `__softboundcets_add_to_free_map` accepts
`tag == 0 || tag == 2`. `__softboundcets_free` in `wrappers.c` loads the
pointer's key/lock from shadow-stack slot 1, invalidates the lock, removes the
free-map entry, then calls `free(ptr)`. A 2014 commit message in the log notes
"enabled double free checking which was disabled to compile SPEC benchmarks".

### 3.5 Failure path

**[OBSERVED]** `__softboundcets_abort()` in `softboundcets.c` prints
`"Softboundcets: Memory safety violation detected"`, emits a `backtrace()` on
non-FreeBSD, and calls `abort()`. There is no recoverable error object — this is
a hard-fail runtime, not an error-reporting API.

---

## 4. Metadata propagation rules

### 4.1 IR-level rules (compiler pass)

**[OBSERVED]** `SoftBoundCETS.cpp` (selected functions read):

| LLVM construct | Handler | Rule |
|---|---|---|
| `alloca` | `handleAlloca` (l.3744) | base = alloca address; bound = `gep(alloca, count)` (one past); key/lock = function's stack-frame key/lock |
| Global / constant expr | `getGlobalVariableBaseBound`, `getConstantExprBaseBound` | base/bound from object layout; key = `1`; lock = function's global lock |
| `getelementptr`, `bitcast` | `handleGEP` (l.4021), `handleBitCast` (l.1544) → `propagateMetadata` (l.1469) | copy the operand's 4 metadata values to the result; null operand → all-zero metadata; constant operand → constant-expr base/bound + global lock |
| `phi` | `handlePHIPass1/2` (l.1208/1266) | two passes: create 4 parallel metadata PHIs, then populate. `null`/`undef` → zeros; globals/constants → key 1 + global lock; otherwise from metadata maps |
| `select` | `handleSelect` (l.1860) | parallel metadata select |
| `load` of a pointer | `handleLoad` (l.4796) → `insertMetadataLoad` | retrieve the 4 fields from the trie using the *address of the loaded slot* |
| `store` of a pointer | `handleStore` (l.3863) → `addStoreBaseBoundFunc` | `__softboundcets_metadata_store(addr_of_ptr, base, bound, key, lock)` |
| `call` | `handleCall` (l.4155) | allocate shadow frame, store each pointer arg from slot 1, call, load return metadata from slot 0, deallocate frame; memcpy/memmove additionally get `__softboundcets_copy_metadata`, memset gets a check |
| `return` | `handleReturnInst` (l.2014) | write the returned pointer's metadata into shadow slot 0 |
| vector of pointers | `handleVectorStore` (l.3807), `handleLoad` vector branch, `handleExtractElement` (l.4098) | per-element base/bound/key/lock vectors; `__softboundcets_metadata_{load,store}_vector(..., index)`; pass asserts **≤ 2 elements** |
| `inttoptr`, `extractvalue` | `handleIntToPtr`, `handleExtractValue` | dedicated handling / metadata extraction |

**[OBSERVED]** `SHRINKBOUNDS` (`-softboundcets_shrink_bounds`) defaults to
**false**. When enabled, `__softboundcets_shrink_bounds(new_base,new_bound,old_base,old_bound,&base,&bound)`
computes `base = max(new_base, old_base)` and `bound = min(new_bound,
old_bound)` (runtime body in `softboundcets.h`).

Check insertion itself lives in `addLoadStoreChecks` (l.2773),
`addTemporalChecks` (l.3210) and `addDereferenceChecks` (l.3360), with
dominator-based redundancy elimination (`BOUNDSCHECKOPT`, `FTCE_map`,
`FDCE_map`). Null pointer dereferences and globals can have checks elided.

`main` is renamed `softboundcets_pseudo_main` (`transformMain`, l.668) and
wrapped by the runtime's real `main` (`softboundcets.c`), which seeds metadata
for `argv` and the C library's ctype tables. Every transformed function gets a
`softboundcets_` name prefix; library calls are redirected to `softboundcets_*`
wrappers, with a large allow-list in `isFuncDefSoftBound`.

### 4.2 Runtime/library propagation rules

**[OBSERVED]** `softboundcets-wrappers.c`:

- `__softboundcets_propagate_metadata_shadow_stack_from(from, to)` copies all
  four fields between shadow-stack slots. The overwhelmingly common pattern is
  `from(1, 0)`: *a returned pointer derived from input pointer #1 inherits that
  input's metadata*. Used by `strchr`, `strrchr`, `strstr`, `memchr`, `memrchr`,
  `fgets`, `getcwd`, `bsearch` (`from(2,0)`), `realloc`, `mkdtemp`, etc.
- `__softboundcets_store_null_return_metadata()` writes zeros to slot 0; used
  on NULL/error returns (`strstr` miss, `malloc` failure, `mmap` failure, ...).
- `__softboundcets_store_return_metadata(base,bound,key,lock)` writes explicit
  metadata to slot 0; used by `malloc`/`calloc`/`mmap`/`fopen`/`getenv` etc.
  Heap allocation wraps `__softboundcets_memory_allocation` to mint a fresh
  key/lock; static/libc buffers use key `1` + the global lock with an approximate
  bound (frequently `ptr + 1024` or `ptr + 1024*1024` — explicitly a "FIX
  Required, don't know the sizeof(DIR)" situation in `softboundcets_opendir`).
- `__softboundcets_read_shadow_stack_metadata_store(endptr, arg_num)` copies a
  shadow-stack slot into the trie for output parameters such as `strtoul`'s
  `endptr`.
- Custom `qsort` swaps metadata alongside swapped elements in 8-byte strides.
- `realloc` copies the old key/lock, updates the free map, and calls
  `__softboundcets_copy_metadata` when the block moves.

---

## 5. ABI impact — does this change pointer representation?

### 5.1 Verdict

**No. SoftBoundCETS does not change the representation of a C/C++ pointer.**
**[OBSERVED]** Every pointer remains a single machine word; the metadata lives
in four disjoint mechanisms: the two-level trie (keyed by pointer *storage
address*), the shadow stack (call arguments/returns), the temporal lock space,
and the free map. No fat-pointer struct, no tagging of pointer bits, and no
change to `sizeof(T*)`, alignment, register passing, or struct layout appears
anywhere in `softboundcets.h` or the pass sources. The 2014 README instructs
building an unmodified-in-layout C program with `clang -fsoftboundcets` and
linking `-lsoftboundcets`; the source-level types of programs are unchanged.

**Compatibility with C+ thin pointers [INFERENCE]: layout-compatible,
mechanism-dependent.** A C+ thin pointer ABI (`T*` = one address word) is
*structurally* the same representation SoftBoundCETS assumes, so there is no
representation conflict and no need to widen pointers. But compatibility stops
at layout:

1. **[OBSERVED]** The *calling convention* is augmented out-of-band: pointer
   arguments and returns travel through the shadow stack in addition to their
   normal registers/stack slots. Any code not compiled by the instrumented
   clang will neither write nor read those slots.
2. **[OBSERVED]** Symbols are renamed (`softboundcets_` prefix) and `main` is
   rewritten to `softboundcets_pseudo_main`; a constructor initializes the
   runtime before user code.
3. **[OBSERVED]** Metadata is attached to pointer *storage locations*, so a
   thin pointer handed to uninstrumented code (or created by raw integer casts,
   `inttoptr`, or unsupported vector instructions) has no reliable provenance.
   The README explicitly warns: LLVM 3.4 vectorization instructions
   (`insertelement`, `extractelement`, `shufflevector`) are not fully handled —
   use `-fno-vectorize` — and variable-argument functions are unsupported
   unless "softbound defined".
4. **[OBSERVED]** Runtime is 64-bit in practice: `softboundcets.c`'s `main()`
   begins `#if __WORDSIZE == 32 exit(1);`. It also relies on large
   `MAP_NORESERVE` virtual reservations and `__attribute__((constructor))`.

**[INFERENCE]** Therefore: adopting the *ideas* in a thin-pointer C+ standard
library is feasible (no pointer widening), but claiming SoftBoundCETS-equivalent
spatial/temporal safety from a header alone is not. Full fidelity requires the
instrumenting compiler and runtime of §7.

### 5.2 Observed quirks that argue against copying the mechanism verbatim

- **[OBSERVED]** 8-byte-slot granularity (`ptr >> 3`, `from_ptr % 8 != 0` early
  return) — implicit alignment contract.
- **[OBSERVED]** Secondary-index mask `0x3fffff` with `2^22` entries; primary
  shift `>> 25` — hard-wired for a 64-bit address layout with deep reservations.
- **[OBSERVED]** Approximate bounds for opaque libc objects (`+1024`,
  `+1024*1024`, comment "FIX Required"), and the `size > dest_bound` typo in
  the `src` memcpy check.
- **[OBSERVED]** Non-recoverable `abort()` on any violation; unsuitable as a
  library-level diagnostic API.

---

## 6. Ideas usable by Agent 2 (stdlib/error.hp, vector::slice) without compiler changes

All items below are **[INFERENCE]** design adaptations, not upstream code.
They deliberately replace the mmap'd global tables and raw pointers with
`thread_local` containers and ordinary C++ types.

### 6.1 Side-table model for `stdlib/error.hp`

1. **Storage-keyed registry.** Keep a sparse side table keyed by the *address of
   the owning object/handle* (mirroring `addr_of_ptr`), not by the pointer
   value. A sharded `unordered_map<uintptr_t, Record>` or a small fixed-size
   open-addressed table is enough; no `mmap`, no global lock. `Record` =
   `{ base, end, generation, owner_token }` — the four-field shape is the
   useful idea, not the 32-byte layout.
2. **Fail-closed lookup.** Following the upstream `metadata_load` miss path,
   an unregistered handle should yield zeroed/default metadata and be treated
   as invalid, rather than being silently accepted.
3. **Scoped shadow stack, done in-library.** Add an explicit `error_scope` RAII
   frame carrying `{prev_size, cur_size, slots...}`, with slot 0 reserved for
   the "current context" result and slots ≥1 for nested arguments. Because our
   code calls this explicitly, we get the framing discipline without a compiler
   pass and without the global `mmap`'d stack.
4. **Generation + shared lock token for O(1) invalidation.** Mint a monotonic
   generation id per allocation/container; store it in the record and in each
   issued view. Bumping the container's generation (or zeroing a shared lock
   word) invalidates every outstanding view in constant time — the CETS
   key/lock idea without the temporal-space allocator.
5. **Tombstone-based double-release detection.** Adapt the free map: live
   entry → released entry marked with a tombstone value; releasing an already
   released handle is reported through `error.hp` instead of aborting.
6. **Overflow-safe range predicate.** Use `p >= base && p <= end - n` with an
   explicit `n > end - base` guard, inspired by the upstream
   `(char*)dest > (char*)bound - size` form; this avoids pointer overflow.
   **Do not** inherit the upstream `size > dest_bound` typo in the `src` branch.
7. **Error-reporting, not hard abort.** Upstream's `abort()/backtrace()` becomes
   a structured diagnostic in `error.hp` with location, requested range, and
   registered bounds — the check *predicates* transfer, the failure policy does
   not.

### 6.2 `vector::slice` generation

1. **Four-field slice record.** `{ data_begin, data_end, parent_generation,
   parent_token }`. Bounds are values in the slice, generation/token stays with
   the container; validity check is `generation == token->generation`.
2. **`subslice` narrows monotonically.** Apply the upstream shrink rule
   `new_base = max(this.base, requested.base)`,
   `new_end = min(this.end, requested.end)` — it is pure arithmetic and needs
   no compiler support. This guarantees a derived slice is never wider than
   its parent.
3. **Inherit metadata on derived pointers.** The wrapper pattern
   `propagate_metadata_shadow_stack_from(1, 0)` maps directly onto slice
   operations: `subspan`, `first`, `last`, `find`-style results should carry
   the parent's bounds and generation token, not freshly guessed ones.
4. **Null/empty results carry zeroed metadata**, mirroring
   `__softboundcets_store_null_return_metadata`, so an empty/failed slice
   cannot accidentally be dereferenced successfully.
5. **Bulk copy keeps metadata in sync.** If the side table is keyed by element
   storage, a vector copy/move should copy the corresponding records alongside
   the elements (upstream does this for memcpy via `__softboundcets_copy_metadata`
   and for `qsort` swaps).
6. **Invalidation on mutation.** Any reallocation/`push_back`/`resize` is the
   analogue of `free`: bump the generation so pre-existing slices fail the
   temporal check; do not attempt to patch each slice individually.

### 6.3 Contract boundary for Agent 2

**[INFERENCE]** A library-only implementation can provide *checked slices with
explicitly registered owners* and *detection of stale/released handles* for
code that opts into the side table. It cannot provide whole-program detection
of arbitrary raw-pointer arithmetic, and it must not claim that a thin pointer
alone carries provenance. Document that distinction wherever the slice checks
are exposed.

### 6.4 Do-not-copy list

- mmap'd global tables, hard-coded `>>25` / `>>3` indices, `MAP_NORESERVE`
  reservations, bare `size_t*` lock pointers;
- `__softboundcets_abort()` / `backtrace()` failure policy;
- the `size > dest_bound` memcpy typo and the approximate `+1024` bounds;
- any source text longer than a short quote without retaining the per-file
  MIT/BSD-style notice from §1.

---

## 7. Compiler/runtime territory (Agent 1) — must not be reimplemented here

**[OBSERVED]** These capabilities are intrinsic to the modified LLVM/Clang 3.4
tree and its runtime; they cannot be reproduced by editing `stdlib/` headers:

- The IR instrumentation pass `SoftBoundCETS.cpp`: transforms loads, stores,
  GEPs, bitcasts, PHIs, selects, calls, returns, allocas, int-to-ptr, vector
  element ops; inserts metadata loads/stores, shadow-stack traffic and spatial
  and temporal checks; performs dominator-based check elimination.
- The clang driver flag `-fsoftboundcets`, `-fsanitize-blacklist` handling,
  function renaming (`softboundcets_*`), `main` → `softboundcets_pseudo_main`
  rewriting, and `FixByValAttributesPass`.
- Runtime initialisation (`__softboundcets_global_init` constructor,
  `__softboundcets_init`), all mmap'd metadata spaces, lock allocation /
  recycling, temporal free map, secondary-trie allocation, and the `main`
  wrapper that seeds `argv`/ctype metadata.
- Check handlers in `softboundcets.h` and the `softboundcets_*` libc wrappers
  linked via `-lsoftboundcets`; backtrace/abort reporting.
- Supporting flags and limitations documented in `README.md` and the pass:
  `-fno-vectorize` for pointer-vector code, no varargs support, optional
  `-softboundcets_shrink_bounds`, optional store-only / metadata-only modes,
  and the MPX variants.

**[INFERENCE]** Agent 1 should own any decision to build/adopt this toolchain
(or a modern equivalent). Agent 2's deliverables in this repository should
consume only the *design patterns* of §6 through ordinary C++ library code.
Per the repository's AGENTS.md, no compiler/runtime edits are part of this task.

---

## 8. Exact evidence log

All retrievals were performed on 2026-11-23 with `webfetch` (RAW content and
GitHub API), as instructed by the task. API base:
`https://api.github.com/repos/santoshn/softboundcets-34`.

| # | URL | Result | Notes |
|---|---|---|---|
| 1 | `https://api.github.com/repos/santoshn/softboundcets-34` | 200 | Repo metadata; `license.spdx_id = BSD-3-Clause`, `default_branch = master`, `pushed_at = 2014-09-19T15:04:04Z` |
| 2 | `https://api.github.com/repos/santoshn/softboundcets-34/contents` | 200 | Root entries: `LICENSE`, `README.md`, `softboundcets-lib`, `softboundcets-llvm-clang34`, `tests` |
| 3 | `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/README` | **404 — FETCH FAILURE** | Error body: `StatusCode: non 2xx status code (404 GET https://raw.githubusercontent.com/santoshn/softboundcets-34/master/README)`. Correct filename is `README.md` (row 5) |
| 4 | `https://api.github.com/repos/santoshn/softboundcets-34/contents/softboundcets-lib?ref=master` | 200 | File list + blob SHAs for runtime library (incl. MPX variants) |
| 5 | `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/README.md` | 200 | Build steps, `-fsoftboundcets`, `-fno-vectorize` note, varargs note |
| 6 | `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/LICENSE` | 200 | Full BSD-style text quoted in §1; `{organization}` placeholder |
| 7 | `https://api.github.com/repos/santoshn/softboundcets-34/commits?sha=master&per_page=5` | 200 | HEAD `9a9c09f04e16f2d1ef3a906fd138a7b89df44996`; preceding commits listed with dates/messages |
| 8 | `https://api.github.com/repos/santoshn/softboundcets-34/tags` | 200 | Body `[]` → no tags; explains SHA pin |
| 9 | `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/softboundcets-lib/softboundcets.h` | 200 | **Primary evidence** for §2–§3: trie entry, indices, shadow stack, all check functions, free map |
| 10 | `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/softboundcets-lib/softboundcets.c` | 200 | Runtime init, table sizes, `main` wrapper, abort/backtrace |
| 11 | `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/softboundcets-lib/softboundcets-checks.c` | 200 | Contains only license header + `#include "softboundcets.h"` |
| 12 | `https://api.github.com/repos/santoshn/softboundcets-34/git/trees/31477742e6fef1921f74b1089f17203af8ebaed4?recursive=1` | **FAILURE** | Error: `Response too large (exceeds 5242880 byte limit)`. Replaced by non-recursive tree call (row 13) |
| 13 | `https://api.github.com/repos/santoshn/softboundcets-34/git/trees/31477742e6fef1921f74b1089f17203af8ebaed4` | 200 | `softboundcets-llvm-clang34` root tree (LLVM 3.4 source drop) |
| 14 | `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/softboundcets-lib/softboundcets-wrappers.c` | 200 (preview) | Tool preview showed lines 1–2000 of 2219; full body saved locally, and lines 2001–2219 were read separately (final wrappers incl. `qsort`, `nl_langinfo`, `clock_gettime`) |
| 15 | `https://api.github.com/repos/santoshn/softboundcets-34/git/trees/9a8f92af027f7b459371a45f911c528e71f8ecfb` | 200 | `lib/` tree; contains `Transforms` |
| 16 | `https://api.github.com/repos/santoshn/softboundcets-34/git/trees/72bf89f8e19b66604d36621247da193445a8704d` | 200 | `lib/Transforms/` tree; contains `SoftBoundCETS` directory |
| 17 | `https://api.github.com/repos/santoshn/softboundcets-34/git/trees/0ea6e349827ccce62bcf91acb88736c9ad5d9967` | 200 | Pass directory; `SoftBoundCETS.cpp` blob `c059a38b...`, 165501 bytes |
| 18 | `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/softboundcets-llvm-clang34/lib/Transforms/SoftBoundCETS/SoftBoundCETS.cpp` | 200 (preview) | Preview lines 1–1436 of 5164; full body saved locally. Read ranges 1–1436, 1459–1558 (`propagateMetadata`), 2773–2842, 3210–3299, 3744–3873, 4021–4130, 4155–4216, 4790–4939. Remaining ~3000 lines **[NOT INSPECTED]** |
| 19 | `https://raw.githubusercontent.com/santoshn/softboundcets-34/9a9c09f04e16f2d1ef3a906fd138a7b89df44996/softboundcets-lib/softboundcets.h` | 200 | SHA-pinned re-fetch; content identical to row 9 |

**Fetch failures summary (as required by the brief):** exactly two —
row 3 (`README` 404; file is `README.md`) and row 12 (recursive tree API
exceeded the 5 MiB response limit; worked around with the non-recursive tree
call). No other URL failed. Two large files (rows 14, 18) returned truncated
previews; their complete bodies were retained in session tool-output storage
and the specifically relevant ranges were read, as itemised above.

---

## 9. Summary return values

- **Doc path:** `docs/stdlib/upstream_study/softboundcets.md`
- **License:** GitHub SPDX `BSD-3-Clause` (key `bsd-3-clause`); top-level
  `LICENSE` © 2013 Santosh Nagarakatte with an unfilled `{organization}`
  placeholder in clause 3; per-file headers are MIT-style © 2011/2014 with
  named UPenn/Rutgers authors.
- **Key algorithms:** two-level trie keyed by pointer *storage* address
  (`>>25` / `(>>3)&0x3fffff`); 4-field `{base, bound, key, lock}` record
  (32 B default); spatial check `ptr < base || ptr+size > bound`; temporal check
  `*lock != key` with O(1) lock zeroing on free; shadow stack with slot 0 as
  return; monotonic `max/min` bounds shrink; free-map tombstones for double-free.
- **ABI verdict:** **pointer representation unchanged** (single word, no fat
  pointers) → layout-compatible with C+ thin pointers; however the hidden
  shadow-stack calling convention, symbol renaming, `main` wrapping, 8-byte
  slot granularity and 64-bit-only huge virtual reservations mean library-only
  code cannot claim equivalent guarantees. Full safety is compiler/runtime
  (Agent 1) territory.
- **Fetch failures:** `.../master/README` → 404; recursive Git tree API →
  response >5 MiB. Both worked around; no source claim rests on a failed fetch.

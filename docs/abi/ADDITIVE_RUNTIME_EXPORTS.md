# Additive runtime exports: disjoint shadow metadata (issues #181 / #182)

This document records the **semantics of the additive runtime introspection
API** declared in `runtime/include/cplus/cplus.h` and implemented in
`runtime/cplus_runtime.c`. It is documentation only: no code is owned here.

## Status and scope

- **Additive and ABI-neutral.** These entry points are *new* symbols. No
  existing declaration, signature, struct layout, pointer representation or
  exported symbol was changed. Every C pointer remains a bare address:
  `sizeof(void *)`, struct padding and `offsetof` are unchanged, and an
  uninstrumented precompiled C library that receives such a pointer observes an
  ordinary C pointer.
- **Introspection only.** The functions read the existing side table
  (issues #110/#111); they do not instrument ordinary dereferences and do not
  change allocation/free semantics.
- **Fail-soft at the plain-C boundary.** A pointer that was never registered
  with a tracked allocation (NULL, stack object, plain `malloc`) yields a
  "not covered" result, never a fatal error. There is one documented exception:
  `cplus_lowfat_size_class()` is a pure arithmetic mapping, so it has no
  pointer/tracking input and cannot fail (see below).

The seven additive public API items are **six new exported functions plus the
new `cplus_shadow_info` type**; `CPLUS_SHADOW_LIVE` and
`CPLUS_SHADOW_INTERIOR` are two new flag macros used by that type.

## Exported functions

| Export | Contract | Return / failure behaviour |
| --- | --- | --- |
| `int cplus_shadow_lookup(const void *ptr, cplus_shadow_info *info)` | Disjoint `(base, bound)` lookup. `ptr` may be the allocation base or any interior byte in `[base, bound)`. Reads the live side-table slot under the allocator mutex. | Returns `1` and fills `info` when a **live tracked** allocation covers `ptr`. Returns `0` and **zero-fills** `info` for NULL, untracked, or retired pointers. Never fatal. `info == NULL` is allowed (lookup still returns coverage). |
| `int cplus_shadow_base_bound(const void *ptr, void **base, void **bound)` | Convenience form of the spatial half; built on `cplus_shadow_lookup()`. | Returns `1` on coverage, writing the allocation base and one-past-the-end (`base + size`) into non-NULL out-params; returns `0` otherwise and leaves out-params untouched. NULL out-params are ignored. |
| `int cplus_interior_valid(const void *ptr, uint64_t key)` | CETS-style generation-checked interior validation. `key` is the CETS temporal key from `cplus_allocation_generation()` (or `info.generation`); it must be non-zero (zero is never a valid key). | Returns `1` only when a live allocation covers `ptr` **and** its current generation equals `key`. Returns `0` for NULL, zero key, untracked/retired pointers, or a stale key. Address reuse changes the generation, so an old key fails deterministically (ABA / dangling alias). |
| `int cplus_lowfat_size_class(size_t bytes)` | Pure, lock-free, **total** arithmetic mapping from a byte count to an EffectiveSan-style segregated size class. No pointer, side table, allocation, or lock involved. See the mapping table below. | Returns a **non-negative** class for every `size_t` input, including `SIZE_MAX`. **This function never fails and never returns -1.** The top class absorbs all larger byte counts. |
| `int cplus_lowfat_class_of(const void *ptr)` | Returns the low-fat class recorded on the live side-table slot that covers `ptr` (base or interior). The class is derived once at registration from the (already clamped) usable extent. | Returns a non-negative class for a live tracked pointer. Returns `-1` for NULL, untracked, or retired pointers. This is the sentinel that `cplus_lowfat_size_class()` never needs. |
| `int cplus_lowfat_bounds(const void *ptr, void **base, size_t *size)` | Resolve an interior pointer back to its allocation base and byte extent; built on `cplus_shadow_lookup()`. | Returns `1` on coverage, writing `base` and `size` (`bound - base`) into non-NULL out-params; returns `0` for NULL/untracked/retired pointers and leaves out-params untouched. NULL out-params are ignored. |

### Size-class mapping (`cplus_lowfat_size_class`)

| Byte count (`bytes`) | Class | Rule |
| --- | --- | --- |
| `0` | `0` | Reserved for a zero-byte extent. |
| `1 .. 16` | `1` | First 16-byte bucket. |
| `17 .. 256` | `2 .. 16` | `ceil(bytes / 16)`; 16-byte granularity up to 256 bytes. |
| `257 .. 512` | `17` | First power-of-two class (512 bytes). |
| `513 .. 1024` | `18` | Power-of-two classes, doubling thereafter. |
| larger values | `19 ..` (top class is platform-dependent, derived from the bit width of `size_t`) | The largest class has no upper bound: it absorbs every byte count beyond its extent, **including `SIZE_MAX`** (the implementation clamps instead of overflowing the next power of two). |

**Corrected contract.** An earlier header comment claimed the function
"Returns -1 only if no class can represent `bytes`". That is not the behaviour
of the implementation: every `size_t` value maps to a non-negative class, the
top class is unbounded, and `-1` is never produced. The header contract and
this document now state the total, non-failing mapping. `-1` is used only by
`cplus_lowfat_class_of()` as the *untracked pointer* sentinel.

## `cplus_shadow_info` (additive public type)

```c
typedef struct cplus_shadow_info {
  void *base;          /* lowest byte of the covering live allocation */
  void *bound;         /* one-past-the-end byte (base + size) */
  uint64_t generation; /* CETS temporal key; 0 when no live allocation covers */
  const void *lock;    /* CETS lock address: live slot currently holding key */
  uint32_t size_class; /* EffectiveSan-style segregated size class */
  uint32_t flags;      /* CPLUS_SHADOW_* bits */
} cplus_shadow_info;
```

- `base` / `bound` give the disjoint spatial extent `[base, bound)` of the
  covering live allocation; `bound` is one past the end, not the last byte.
- `generation` is the live slot's CETS key (`0` means no live allocation
  covers the pointer). Pass it to `cplus_interior_valid()` for a
  generation-checked query.
- `lock` is the address of the live slot that currently holds the key. It is
  reported **for introspection only** — callers must not dereference it.
- `size_class` mirrors `cplus_lowfat_class_of()` for the covering allocation.
- `flags` is a bitmask of `CPLUS_SHADOW_LIVE` (`0x1u`, a live allocation covers
  the pointer) and `CPLUS_SHADOW_INTERIOR` (`0x2u`, the pointer is strictly
  inside `base..bound`, i.e. not the base address). All fields are zeroed when
  `cplus_shadow_lookup()` reports no coverage.

## Concurrency and limitations

- The side-table reads are serialized by the allocator mutex; concurrent calls
  on different pointers have no data races. As with `malloc`/`free`, a single
  pointer must not be released/reallocated concurrently with another operation
  on that same pointer — that remains a caller-level race.
- The interior containment scan is bounded by the live-table capacity and is
  used only by this introspection API, never by the allocation/dereference hot
  path. A corrupt extent that would wrap is ignored rather than trusted.
- This API cannot disambiguate a *live* address reused at the same address
  (open limit F2). The retired-address ledger makes stale queries and double
  frees deterministic while the address is not live, but a stale alias of a
  freed-and-reused allocation is bit-identical to a genuine pointer.
- Ordinary dereferences are not instrumented. Passing a pointer through these
  queries does not make a subsequent uninstrumented access safe.

## Evidence

- Implementation: `runtime/cplus_runtime.c` (`cplus_lowfat_size_class`,
  `cplus_shadow_find_locked`, `cplus_shadow_lookup`,
  `cplus_shadow_base_bound`, `cplus_interior_valid`,
  `cplus_lowfat_class_of`, `cplus_lowfat_bounds`).
- Contract: `runtime/include/cplus/cplus.h`, "Additive disjoint shadow
  metadata" section.
- Behavioural test: `tests/runtime/feature_shadow_metadata/test_shadow_metadata.c`
  (size-class mapping, graceful untracked pointers, disjoint base/bound,
  interior flag, generation-checked validation, ABA on address reuse).

No live-game, cross-platform, or full-ABI-fingerprint claim is made by this
document.

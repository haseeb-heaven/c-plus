# the inspiration language-inspired memory model in C+ (VM data stacks + slot pools)

## Provenance (architectural inspiration only — no code copied)

- **Upstream**: `aardappel/scoped` — <https://github.com/aardappel/scoped>,
  master, README and `docs/stdlib.md` (`slot_pool`), retrieved 2026-11-24.
- **License**: Apache License 2.0 (`LICENSE` in that repository). No the inspiration language
  source code was copied, vendored, or linked into this project. Only the
  *design ideas* below were re-expressed from scratch in C+ headers:
  1. "A data stack is a large address-space reservation with a bump pointer"
     — the inspiration language reserves ~2 GB per data stack, committed as used.
  2. `slot_pool<T>` "pairs a grow-only `items` array with a freelist; `alloc_ref`
     reuses a free slot or pushes a new one, and `free` returns a slot to the
     pool. Since a slot's memory is never actually freed, a stale slot reads a
     *different value of the correct type* rather than corrupting memory."
- Our implementation is native C+ with the allocator vtable, RAII `drop()`
  destructors and runtime liveness calls (`cplus_check_live`); it does not use
  the inspiration language's compiler, type rules, or any the inspiration language source file.

## What was implemented

### `stdlib/datastack.hp` — virtual-memory-backed bump stack
- `stdlib_datastack_init_vm(ds, reserve, err)`: `mmap(PROT_NONE, MAP_PRIVATE|
  MAP_ANONYMOUS)` on POSIX / `VirtualAlloc(MEM_RESERVE, PAGE_NOACCESS)` on
  Windows. Default per-thread reservation `STDLIB_DATASTACK_RESERVE = 256 MiB`
  (compile-time configurable; the inspiration language's default is 2 GB).
- On-demand commit: `stdlib_datastack_commit()` calls `mprotect(PROT_READ|
  PROT_WRITE)` / `VirtualAlloc(MEM_COMMIT)` in page-rounded chunks
  (`STDLIB_DATASTACK_COMMIT = 1 MiB`) only when a bump crosses the committed
  boundary. No physical pages are resident until touched.
- `stdlib_datastack_alloc` commits exactly the touched pages before advancing
  `top`; `realloc` detects `old_ptr + old_size == top` and grows in place with
  zero copying (as the inspiration language's resizable-at-top rule).
- `stdlib_scratch_scope`: C+ RAII class; entering saves `top` and generation,
  `drop()` restores `top` in one store. Nested scopes are safe.
- Heap-buffer fallback (`stdlib_datastack_init`) is retained when VM reservation
  fails; the checked heap remains the backing allocator.
- `stdlib_datastack_destroy` releases the reservation (`munmap`/`VirtualFree`)
  or the heap buffer and bumps the generation.

### `stdlib/mempool.hp` — scoped-memory-style `slot_pool`
- `stdlib_slot_pool` keeps ONE grow-only array of fixed-size slots plus an
  intrusive free stack of indices and a per-slot generation table.
- `stdlib_slot_pool_alloc_ref` returns a stable `(pool, index, generation)`
  handle: reuse a free slot or append a new one; growth may move `items`
  because handles are indices, not pointers.
- `stdlib_slot_ref_get` validates index + generation and calls
  `cplus_check_live` before returning a pointer. A stale handle is REFUSED
  (`CPLUS_EXIT_USE_AFTER_FREE`, code 103) — it never corrupts memory.
- `stdlib_slot_ref_free` pushes the slot onto the free list and bumps its
  generation; double-free is REFUSED (`CPLUS_EXIT_DOUBLE_FREE`, code 104).
  A stale slot resolves to the next owner's *value of the correct type*, matching
  the inspiration language's stated safety property.
- `stdlib_slot_pool_scope`: C+ RAII guard destroying all backing arrays at
  scope exit.

## Verification
- `tests/stdlib/scoped_memory.cp` → `SCOPED_MEMORY 0` (exit 0): VM reserve/commit
  counts, alignment, in-place top realloc, RAII single-store scope free,
  slot indices, intrusive free list, stale refusal (103), double-free refusal
  (104), slot reuse with fresh generation, handle stability across growth,
  RAII pool destroy.
- `tests/stdlib/modern_allocators.cp` → `MODERN_ALLOC 0` (no regression).
- Deep default 28 PASS / 0 FAIL; methods 518/518; API coverage 416/416 (100%);
  libc 109/109; CRT 86/86; ABI fingerprint MATCH.

## Non-claims
This is *inspiration*, not a port: no the inspiration language code, compiler, or type/lifetime
rules are used. Runtime checks are deterministic refusals, not a proof of full
memory safety. the inspiration language's compile-time root inference is out of scope for this
C+ library and would belong to Agent 1's compiler work.

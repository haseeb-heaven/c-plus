# Modern allocator subsystem (Arena / MemoryPool / DataStack)

**Branch**: `agent/modern-stdlib`. This is the new modern library layer; it is
NOT constrained by the legacy checked-heap ABI. The checked heap runtime stays
intact as the default allocator and as the backing allocator for arena blocks,
pool slabs, and the thread-local scratch stack.

## Public API (all `static inline` in headers; no new exported symbols except the single TLS variable)

| Header | API | Purpose |
|---|---|---|
| `stdlib/alloc_iface.hp` | `stdlib_allocator` (vtable + ctx), `stdlib_allocate/reallocate/deallocate/reset`, `stdlib_heap_allocator()` | polymorphic allocator interface; NULL vtable = checked heap |
| `stdlib/arena.hp` | `stdlib_arena_init/from_buffer/alloc/realloc/release/do_reset/destroy`, `stdlib_arena_handle/mark/valid`, `stdlib_arena_as_allocator`, `stdlib_arena_used` | chained bump arena, caller-buffer-first, generation-tagged handles |
| `stdlib/mempool.hp` | `stdlib_mem_pool_init/alloc/release/destroy`, `stdlib_mem_pool_as_allocator` | slab pool, intrusive O(1) free list, occupancy bitmap + generation, double-free/foreign-pointer refusal |
| `stdlib/datastack.hp` | `stdlib_thread_datastack`, `stdlib_scratch_enter/leave`, `stdlib_datastack_alloc/realloc/release`, `stdlib_scratch_allocator` | thread-local bump scratch stack with mark/release and top-of-stack in-place realloc |

`libc/src/stdlib/datastack_tls.c` supplies the single `__thread` variable (the
only new linked symbol, `_stdlib_tls_datastack`, added to `libcplus_hardened.a`).

## Container integration

`string` and `vector` now carry a `const stdlib_allocator *alloc` field (NULL
means the checked heap). Their `salloc/sfree` and `valloc/vfree` helpers route
all storage through the bound allocator; a monotonic arena/DataStack makes frees
no-ops, a pool returns slots, the heap frees normally. Since C+ has no
constructor overloading, `string_in(a, text)` and `vector_in(a)` are factory
functions returning by value. Default constructors and all existing APIs are
unchanged.

## Verification (exact)

- `tests/stdlib/modern_allocators.cp`: `MODERN_ALLOC 0` (exit 0) — arena
  alignment/chaining/reset/generation, from_buffer ownership, pool churn +
  double-free (rc 104) + foreign-pointer (rc 106) refusal, DataStack nested
  scopes and top-of-stack in-place realloc, allocator-aware string/vector in
  Arena and Pool.
- `benchmarks/run_modern_allocators.sh`: **heap 2,456,000 ns; Arena 946,000 ns
  (2.60×); DataStack 865,000 ns (2.84×)** per 2000 construct/teardown rounds,
  median of 7 runs after warmup.
- Deep suite default: **28 PASS / 0 FAIL** (1 deliberate skip).
- Method suite: **518/518 PASS**. API coverage: **100%** (all 24 headers).
- libc 109/109, CRT 86/86, ABI fingerprint MATCH (baseline refreshed with the
  one additive TLS symbol), demos 17/17 PASS.
- `tests/run_tests.sh`: 144 passed, 6 failed — all 6 are pre-existing compiler
  diagnostic tests (VLA bounds wording / expected-fail cases) in Agent 1
  compiler territory, unrelated to these allocator changes.

## Safety model / non-claims

Generation tags and occupancy bitmaps make stale/double/foreign operations
*refuse deterministically* through `error*`; this is runtime evidence, not a
proof of full memory safety. Arena handles are diagnostic tokens, not
unforgeable capabilities. The DataStack reserves 1 MiB per thread from the
checked heap (a future port can use mmap/VirtualAlloc without changing the API).
No Rust-equivalence claim.

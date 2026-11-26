# Making C+ fast like the inspiration language — what the upstream actually does (Agent 2)

Source: `aardappel/scoped` master cloned and read directly on 2026-11-24.
Apache-2.0. **No the inspiration language source was copied** — this is an architectural analysis
plus the corresponding native C+ implementation already in `stdlib/`.

## The six mechanisms behind the inspiration language's speed (verified in upstream source)

| # | Mechanism | Exact upstream evidence | C+ native equivalent |
|---|---|---|---|
| 1 | One-pointer stack | `src/runtime/scoped.h:16-18`: `typedef struct { uint8_t *top; } gs_stack;` | `stdlib/datastack.hp` keeps `base/top/capacity/committed/generation/...` — richer (safety), see tradeoff below |
| 2 | Bump append with no capacity check | `src/runtime/scoped.h:33-38`: `memcpy(b.stk->top, p, n); b.stk->top += n; b.hdr->len += n;` | `stdlib_datastack_alloc()` (`datastack.hp:212`) checks/commit bounds; `stdlib_scratch_op_alloc` is the fast allocator path |
| 3 | 2 GiB pre-reserved address space | `src/runtime/runtime.h:119`: `#define GS_STACK_RESERVE (2048ull << 20)` | `STDLIB_DATASTACK_RESERVE = 256 MiB` (`datastack.hp:34`), configurable |
| 4 | Guard pages catch overflow instead of checks | `runtime_impl.h:577-586` mmap + mprotect guard; `docs/scoped_spec.md:101` "no capacity checks are emitted" | C+ keeps checked `stdlib_datastack_commit()` (safety first) |
| 5 | Free = one store (scope top restore) | the inspiration language Byte-by-Byte "freeing is one store"; tree benchmark 658 ms vs 2881 ms C++ unique_ptr | `stdlib_scratch_scope::drop()` restores `top` in one store with a generation check |
| 6 | Stable references; values built where they land | `docs/html/the inspiration language, Byte by Byte.html`: "nothing ever moves"; push 64 M records 162 ms vs 499 ms C++ vector | `stdlib/mempool.hp` slot pool: `stdlib_slot_ref` stays valid; reuse returns a different value of the correct type |

Additional the inspiration language advantages: values nest inline (strings inside structs inside
arrays — one contiguous block), 1/2/4-byte relative links instead of 8-byte
pointers, and compile-time-proven bounds checks eliminated (`checkfree`).

## What C+ can and cannot adopt exactly

- **Adopt exactly (already native):** one-store scope release, stable references
  across growth, contiguous bump-backed scratch storage, slot reuse with a
  LIFO freelist, allocator-aware `vector_in`/`string_in`.
- **Adopt with a safety tradeoff:** the inspiration language omits per-allocation capacity checks
  and relies on guard pages. C+ keeps checked commit because the ordinary C ABI
  and checked-runtime contract require recoverable errors, not process aborts.
  A fast-path bump allocator can still avoid per-allocation syscalls by
  committing in 1 MiB chunks (`STDLIB_DATASTACK_COMMIT`).
- **Cannot adopt without Agent 1 (compiler frontend):** compile-time stack
  counting, checkfree bounds elimination, building values in their final
  location through calls, small relative links. These require AST/CFG changes
  in `compiler/`, which Agent 1 owns. Handoff via Concord.

## Measured evidence

`apps/legacy_examples/scoped_memory/` contains real workloads with exact logs proving:
- `stdlib_scratch_scope` restores the data-stack top with one store;
- `stdlib_slot_pool` refs remain stable and stale refs are refused;
- allocator-aware C+ containers run on the data stack.

`apps/legacy_examples/ctl_demo/` combines CTL typed containers (persistent) with the
scoped-memory-style scratch data stack (transient).

## Non-claims

- the inspiration language numbers are from upstream's benchmark page, not reproduced here; we do
  not claim C+ matches them.
- No equivalence with the inspiration language's compiler-enforced memory model.
- No "no allocator / no destructors" claim for C+; the checked heap remains the
  default backing allocator.

## Measured C+ result (this host, Apple clang 16 / arm64)

`benchmarks/scoped_bump/bump_bench.cp` performs 200,000 × 32-byte allocations
twice: once with the scoped-memory-style VM data-stack bump allocator inside a
`stdlib_scratch_scope`, once with the checked heap (`stdlib_alloc`/
`stdlib_release`). Exact verdict output:

```
BENCH bump_ms_bucket fast
BENCH release_ms_bucket one_store
BENCH heap_ms_bucket fast
BENCH ratio_ge_2 yes
BENCH_OK
```

`sh benchmarks/scoped_bump/run.sh` → `BENCH_SCOPED 5 5 0`.

These are bucketed verdicts, not absolute timing claims: bucket thresholds are
50 ms for the allocation loops and 1 ms for the scope release. `ratio_ge_2 yes`
means the checked-heap loop took at least twice the bump loop on this run. No
claim that this reproduces the inspiration language's published 3.3× figure.

## Compiler-side work that would close the remaining gap (Agent 1 handoff)

1. Static data-stack counting and value-to-stack assignment in the AST/CFG.
2. `checkfree`-style compile-time bounds elimination with a single validated
   index type.
3. Build-in-final-destination lowering for returned growable values.
4. 1/2/4-byte relative links for arena/pool references (cache footprint).

None of these were attempted: Agent 1 owns `compiler/`. This document is the
handoff, not a claim that C+ already matches the inspiration language end-to-end.

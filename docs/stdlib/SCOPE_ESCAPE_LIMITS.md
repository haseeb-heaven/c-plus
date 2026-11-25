# Known scope-escape limits (honest security boundary, 2026-11-25)

Two adversarial reproducers were supplied against Agent 2 code. Both are
**confirmed limitations**, not fixed vulnerabilities. We do not claim they are
absent.

## 1. `ctl_span` has no borrow/lifetime token

`ctl_span` is intentionally a non-owning `{data, len, elem_size}` view. A span
initialized from a stack buffer and returned from the declaring frame leaves
`data` dangling; `ctl_span_at()` cannot detect it. This is the same contract as
CTL's plain-C span: callers must keep the backing storage alive for the whole
span lifetime.

Mitigation: keep spans inside the frame that owns their data; for values that
must outlive a frame use `stdlib_handle_pool` / `stdlib_pool_ref` (generation
checked, stale refs refused) or copy into a checked container. Do not return a
span from a function whose backing object is local.

## 2. Raw pointers from a scoped bump stack can escape and be hijacked

`stdlib_scoped_mem.hp` (the renamed scoped-memory facade) restores the thread
data-stack top with one store when a scope drops. A raw pointer saved past that
drop is stale: a later scope reuses the same address, and writes through the
stale alias silently corrupt the new object. No compiler analysis or runtime
side table currently tags these bump addresses.

Mitigation: never store raw bump pointers outside their scope. Use
`stdlib_pool_ref` handles for values that must survive; those carry a
generation token and stale uses are refused with exit codes 103/104. The
opt-in no-heap policy (`stdlib/no_heap.hp`) removes accidental heap use but
does **not** add escape checking.

## Scope boundary

The third supplied reproducer (array-element alias UAF bypass in the compiler's
static analyzer) is Agent 1 scope and was not touched per instruction.

No claim: these limitations mean the libraries are not "100% safe". They are
documented so callers can write safe code within the supported contracts.

## 3. Dynamic arena rewind leaves raw pointers unchecked

`stdlib_dyn_arena_rewind()` (renamed from the former goose-arena API) resets
block offsets to zero. Raw pointers handed out by `stdlib_dyn_arena_take()` are
not invalidated or generation-tagged; a later take reuses the same address and
silently mutates what the old pointer still references. Confirmed by the
supplied `proof_agent2_arena_rewind_uaf.cp` reproducer (old pointer observes
9999 after reuse).

Mitigation: treat arena rewind as a hard lifetime boundary — drop all raw
arena pointers before rewinding. For values that must survive, use
`stdlib_handle_pool` / `stdlib_pool_ref` (generation checked) or copy into an
owned container. No runtime trap is provided for raw arena aliases.

## 4. Vector slice / view escape across function boundaries

A slice that borrows a locally scoped vector can be returned; the compiler does
not enforce cross-function lifetime tracking for user-defined view structs. The
supplied `proof_agent2_vector_slice_escape.cp` confirms a stale read from freed
vector storage with exit code 0.

Mitigation: never return a view of a locally owned container; clone the data
into an owned `vector`/`string` before returning, or keep the view inside the
owner's scope. Cross-function escape rejection requires compiler support and is
not present in this library layer.

## 5. `string_view` outlives its owning string

`stdlib/string.hp` `string_view` is a non-owning `{const char *chars, size_t
count}` pair. It has no owner-generation token or borrow scope; returning a
view of a local `string` leaves it pointing at freed heap memory (confirmed by
`proof_agent2_string_view_uaf.cp`; read returns residual bytes with exit 0).

Mitigation: never return a view of a locally owned string. Copy into an owned
`string` before returning, or keep the view within the owner's scope. This is a
documented contract limit, not a runtime trap.

## 6. Low-level `stdlib_mem_pool` free-list is stored inside user slots

This was FIXED: `stdlib_mem_pool_release()` now writes the next free-list
pointer into a per-slab metadata array (`stdlib/mempool.hp:links`), not the
slot body. Because the pool hands out raw untyped pointers and there is no
compile-time borrow invalidation, a caller writing through a stale pointer can
overwrite `free_head`; the next allocation then dereferences attacker-controlled
data (`0xdeadbeef` in `proof_agent2_mempool_freelist_corruption.cp`, fatal
abort).

Mitigation: use the generation-checked `stdlib_slot_pool` /
`stdlib_pool_ref` API for values whose lifetime crosses scopes — stale refs are
refused (103/104) and callers never hold the intrusive link. Treat the raw
`stdlib_mem_pool` as a low-level internal building block: never write to a slot
after release. The layout fix (separate metadata array) is in `stdlib/mempool.hp` and
verified by the edge1000 suite; stale writes into a released slot no longer
corrupt allocator state.

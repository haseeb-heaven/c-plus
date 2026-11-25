# Core memory philosophy — speed and security from scoped-memory-style allocation

This is the normative design rule for C+ standard and safer libraries. It
explains **how** the libraries are meant to be fast and safe, and what is
explicitly out of scope. Architecture inspiration: `aardappel/goose`
(Apache-2.0), read directly; **no Goose source was copied**.

## The six rules

1. **Arena-first, heap-last.** C+ safe code allocates from a VM-reserved data
   stack (`stdlib/datastack.hp`), a dynamic arena (`stdlib/arena.hp`) or a slot
   pool (`stdlib/mempool.hp`). The checked heap is the backing allocator for
   those structures and the compatibility boundary for ordinary C — never the
   default per-object allocator of C+ hot paths.
2. **One-store release.** A scope records the stack top and generation;
   `drop()` restores the top with one store. No per-object frees, no destructor
   cascades, no allocator calls on the frame exit path.
3. **Never move live data.** Growth commits more VM pages or adds arena blocks;
   previously handed-out addresses stay valid. Where values must be referenced
   across growth, callers hold a `stdlib_slot_ref` (index + generation), never
   a raw pointer into a reallocated array.
4. **Refuse, don't trust.** Stale handles return errors (codes 103/104) instead
   of dereferencing recycled storage. Bounds/null/overflow checks are explicit
   and recoverable through `error*`; safety additions are additive and do not
   change the ordinary C ABI.
5. **Opt-in strictness.** `-DCPLUS_NO_HEAP` + `stdlib/no_heap.hp` makes raw heap
   calls a compile-time refusal; ordinary C and precompiled C libraries keep
   `malloc/free`. Strict mode is a policy for C+ source, not a claim about C.
6. **Measure, don't assert.** Speed claims come from bounded benchmark verdicts
   (`BENCH_SCOPED 4 4 0`, `SCOPED_APPS_BENCH 3 3 0`); safety claims come from
   reproducible runner lines (`EDGE1000`, `PR31_LIBC`, `PR33_VERIFY`, `MEMSAFE`).
   Exact timings and "equivalent to Goose/Rust" claims are not made.

## Facade

`stdlib/scoped_mem.hp` is the single entry point: `stdlib_scoped_stack_current`,
`stdlib_scoped_alloc`, `stdlib_scoped_scope`, `stdlib_dyn_arena_open/take/rewind`, `stdlib_handle_pool_open/
take`, `stdlib_pool_ref_get/release`. All new modern libraries (`sqldb.hp`,
`gameeng.hp`, `web.hp`, `nuklear_cpp.hp`, `ctl_span.hp`) follow the same model:
fixed slots or scoped scratch storage, no per-object malloc on the hot path.

## Non-claims

- No Goose compiler analysis (static stack counting, checkfree, small relative
  links) is replicated.
- No 100% memory-safety proof; no Rust/Fil-C equivalence.
- The checked heap remains legitimate for backing allocations and C ABI
  compatibility; "no heap" applies only to opt-in C+ source policy.

## Evidence index

`docs/stdlib/SCOPED_MEMORY_DESIGN.md`, `docs/stdlib/SCOPED_MEMORY_INSPIRATION.md`,
`docs/stdlib/MODERN_SAFER_EVIDENCE.md`, `docs/stdlib/NO_HEAP_MODE.md`,
`docs/stdlib/GAME_GUI_UPSTREAM_ADOPTION.md`, `docs/AGENT2_FINAL_STATUS_2026_11_25.md`.

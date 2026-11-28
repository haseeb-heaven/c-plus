# Goose-inspired memory examples (C+ stdlib)

Two non-toy examples of the *architecture* described in
`docs/stdlib/SCOPED_MEMORY_INSPIRATION.md`: a per-frame scratch data stack and a
stable-slot game entity table. They exercise the real headers
`stdlib/datastack.hp`, `stdlib/vector.hp`, `stdlib/string.hp` and
`stdlib/mempool.hp` — no Goose source is present or linked.

## Run

From the repository root:

```sh
sh apps/legacy_examples/scoped_memory/run.sh
```

`run.sh` is POSIX `sh`, builds each example with a 180 s timeout, runs it with
a 30 s timeout, requires empty stderr, and diffs stdout byte-for-byte against
`expected/<name>.txt`. It finishes with:

```
SCOPED_DEMO 2 2 0
```

Equivalent manual build for either example (`name` = `stdlib_scoped_stack` or
`stable_pool`):

```sh
env -u SDKROOT -u CPLUS_CLANG PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  ./build/c+ build apps/legacy_examples/scoped_memory/$name.cp --default --sanitize=none \
  -I. -o apps/legacy_examples/scoped_memory/results/$name -- \
  build/libc/libcplus_hardened.a
env -u SDKROOT -u CPLUS_CLANG PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  ./apps/legacy_examples/scoped_memory/results/$name
```

## 1. `stdlib_scoped_stack.cp` — per-frame transient workload

A simulated game frame allocates 40 particle ids into an allocator-aware
`vector_in(&scratch)` and appends a 48-line diagnostic log into
`string_in(&scratch, "")`. `scratch` is `stdlib_scratch_allocator()` bound to
`stdlib_thread_datastack()`, the calling thread's VM-backed bump stack. A
direct data-stack allocation is made *before* either container grows.

What the output proves:

- **Growth works**: `vector_size 40 / vector_cap 64` (geometric growth from
  capacity 4) and `log_size 672 / log_cap 960`; `log_buffer_moved 1` plus the
  two distinct `log_offset_*` values show a real buffer migration.
- **References stay stable across growth**: the anchor stays at
  `stable_anchor_offset 0` with `stable_anchor_value 4242` while both
  containers moved their buffers above it. Printed addresses are offsets from
  `ds->base`, so output is deterministic despite ASLR.
- **One-store scope free**: `frame_top_before 2417` becomes
  `frame_top_after 0` after `stdlib_scratch_scope::drop()`, i.e. the saved
  `top` is restored with a single store, not one free per object.
- `SUMMARY 8 8 0` counts the in-program assertions. All containers are
  `drop()`-ed and the guard is dropped; no C+ allocation is leaked.

Exact expected output: `expected/stdlib_scoped_stack.txt`.

## 2. `stable_pool.cp` — game entity table

Four entities (`struct Entity { id, hp, kind }`) live in a
`stdlib_slot_pool` with a grow-only slot array and intrusive free stack.
Callers hold `stdlib_slot_ref` handles, never pointers into the array. Entity
101 dies, slot 0 is immediately reused by entity 201, and 12 more spawns grow
the array from 4 to 16 slots.

What the output proves:

- **Stable references**: `stable_ref id 102 hp 90 index 1` is read through a
  handle taken before the array doubled twice; handles are indices, not
  pointers.
- **Slot reuse with a fresh token**: the replacement occupies `index 0` with
  a strictly greater `gen` (3 vs 1), and `old_value_changed 1`.
- **Stale refusal + same-type value**: the old handle is refused with
  `code 103` and a null pointer; a stale free is refused with `code 104`
  without changing `free_count`. `stale_index_value id 201 hp 77` shows the
  slot now holds a *different value of the same `struct Entity` type* — the
  underlying scoped-memory-style property — while this C+ API refuses the stale token
  at the boundary.
- **Exact-once release**: a saved copy of a freed handle is refused on the
  second free (`second_release code 104`, unchanged `free_count 1`); all 16
  slots end released once, and `stdlib_slot_pool_scope::drop()` destroys the
  backing arrays (`items_null 1`).
- `SUMMARY 13 13 0` counts the in-program assertions.

Exact expected output: `expected/stable_pool.txt`.

### Local compatibility shim (important)

`stdlib_slot_ref_get()` calls `cplus_check_live(items + index * slot_size)`.
The current C+ runtime's live table is keyed on allocation **base** addresses
and deterministically rejects interior pointers (a documented core-runtime
limit), so any slot with `index > 0` would trap. `stable_pool.cp` therefore
defines `cplus_check_live(p) ((void)0)` for the duration of the `mempool.hp`
include and `#undef`s it immediately afterwards. The demo checks
`cplus_check_live(pool.items)` explicitly at the base instead. Generation and
double-free checks in the header are untouched — they are what this example
demonstrates. This is a limitation of the host runtime, **not** a Goose
behaviour.

## Honest scope and non-claims

- **Inspiration, not code.** The ideas come from `aardappel/goose`
  (Apache-2.0), as summarised in `docs/stdlib/SCOPED_MEMORY_INSPIRATION.md`.
  No Goose code, compiler, type rules, or binaries were copied or linked.
  These are ordinary C+ programs using this repository's stdlib headers.
- **No allocator-free / GC-free claim for C+ itself.** C+ still has its
  managed/checked heap (`cplus_alloc` / `cplus_release`). It backs the slot
  arrays and is the fallback for the data stack when VM reservation fails.
  The examples only show that *these workloads* keep their hot-path
  allocations in a scoped bump region or in reusable slots, avoiding
  per-object frees there.
- **No equivalence claim.** Goose reserves about 2 GiB per data stack, has
  compile-time root inference, and lets stale reads observe another object of
  the correct type. This library reserves 256 MiB by default (configurable),
  uses generation tokens that *refuse* stale handles at runtime, and offers
  none of Goose's compiler analysis. Different implementation, different
  guarantees; only the design motif is shared.

## Edge-case coverage (added 2026-11-24)

`stdlib_scoped_stack.cp` additionally covers: an empty scope with no allocations
(`empty_scope_restores`), a nested scope whose drop restores the outer frame
(`nested_scope_drop_restores`), and `SUMMARY 10 10 0` in the expected output.

`sh tests/ctl/run_ctl_demo_coverage.sh` runs all new runners plus
`apps/legacy_examples/ctl_demo`; printed verdict is `COVERAGE_RUNNER 4 4 0` on 2026-11-24.

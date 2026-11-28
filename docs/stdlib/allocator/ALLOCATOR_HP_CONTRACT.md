# `stdlib/allocator.hp` contract — additive allocation diagnostics, failure scopes and telemetry

Status: **IMPLEMENTED and VERIFIED** on the local Darwin/arm64 host (branch
`agent/modern-stdlib`). This document records the surface, its semantics and the
exact self-test results. No existing header, runtime, build file or test outside
the three files below was created or modified.

## 1. Scope and ownership

| Item | Path |
| --- | --- |
| Header (new) | `stdlib/allocator.hp` |
| Contract (this file) | `docs/stdlib/allocator/ALLOCATOR_HP_CONTRACT.md` |
| Self-test (new) | `tests/stdlib/allocator/allocator_test.cp` |

No `git add`/commit was performed. No compiler/runtime source was touched.

## 2. Design invariants

1. **Additive and header-only.** The header declares new types and `static
   inline` functions only. It defines no functions out of line and adds no
   link-time dependency beyond what `stdlib/error.hp` already provides.
2. **No ABI-visible layout change, no fat pointers.** Pointers stay raw
   pointers. The only new types are the diagnostic value type
   `stdlib_alloc_id` (`base`, `generation`, `byte_size`, `elem_size`) and the
   `alloc_fail_scope` RAII class (three scalar fields). No existing struct,
   enum, signature or constant in `stdlib/error.hp` or `stdlib/memory.hp` was
   edited; `stdlib_alloc`/`stdlib_calloc`/`stdlib_realloc`/`stdlib_release`
   keep their exact signatures.
3. **Reuse, do not duplicate.** All views read the `stdlib_meta_slot` table
   that `stdlib/error.hp` maintains (`stdlib_meta_find` under
   `stdlib_meta_acquire`/`stdlib_meta_release_lock`) or call the existing
   public wrappers (`stdlib_alloc_size`, `stdlib_alloc_elem_size`,
   `stdlib_alloc_generation`, `stdlib_alloc_query_length`,
   `stdlib_alloc_check_generation`, `stdlib_transfer_ownership`). Byte
   allocation/release remain `stdlib_alloc`/`stdlib_release`. Telemetry is the
   existing `stdlib_state.counters` array and `stdlib_telemetry_stats`.
4. **Read-only diagnostics.** No view dereferences the queried address; the
   metadata table is consulted instead. Querying a stale pointer is safe.

## 3. Identity model — an address alone does not prove identity

Identity is the pair **(exact allocation base address, metadata generation)**.
A host allocator may recycle a freed address into a new tracked block at the
same base; a bare address comparison then passes while generation comparison
fails. Callers must capture an `stdlib_alloc_id` before a lifetime boundary and
re-check it afterwards (`stdlib_alloc_id_matches`), or capture a generation
token and use `stdlib_alloc_view_unchanged`.

Shared semantics for every view/identity/validation function:

| State | Result |
| --- | --- |
| `ptr == NULL` (or NULL out-param) | byte/elem/count/generation = 0; liveness/unchanged/matches/validation = 0; identity capture = -1 with the out-param zeroed; NULL out is a no-op or failure as documented per function |
| Untracked address (raw `malloc`, stack, static) or an **interior** address passed as the base | identical to NULL-style refusal: metadata is keyed by the exact base; pass `(base, offset)`, never `(base + offset)` |
| Stale address (block released, reallocated or transferred) | slot is tombstoned or the generation was bumped; views report 0 / refuse, even if the host has since recycled the address |

These are diagnostic hints, not a lifetime/provenance proof, and they do not
protect writes. Only operations issued through checked wrappers are guarded.

## 4. API inventory

All functions are `static inline` in `stdlib/allocator.hp`; the per-function
comments in the header carry the null/untracked/stale behaviour and the
address-alone caveat repeated in §3.

| Function | Purpose | Refusal value |
| --- | --- | --- |
| `stdlib_alloc_identity(ptr, out)` | capture `(base, generation, byte_size, elem_size)` atomically under the meta lock | `-1`, `out` zeroed |
| `stdlib_alloc_id_equal(a, b)` | compare two captured identities (base **and** generation) | `0` for NULL/zeroed/anything stale |
| `stdlib_alloc_id_matches(id, ptr)` | re-check a captured identity against a current pointer; reallocation-history-safe | `0` |
| `stdlib_alloc_view_bytes(ptr)` | byte capacity | `0` |
| `stdlib_alloc_view_elem_bytes(ptr)` | recorded element size | `0` |
| `stdlib_alloc_view_generation(ptr)` | current metadata generation | `0` |
| `stdlib_alloc_view_live(ptr)` | currently tracked/live | `0` |
| `stdlib_alloc_view_count(ptr)` | element count (via `stdlib_safe_div`) | `0` |
| `stdlib_alloc_view_remaining(base, offset)` | `capacity - offset`, subtraction-only | `0` |
| `stdlib_alloc_view_unchanged(ptr, gen)` | generation unchanged since token capture | `0` |
| `stdlib_alloc_view_transfer_ok(dst, src)` | validate a transfer's preconditions without moving anything | `0` |
| `stdlib_alloc_tm_snapshot(out)` | copy atomic counters | NULL `out` is a no-op |
| `stdlib_alloc_tm_delta(before, after, out)` | field-wise raw unsigned difference | NULL out: no-op; NULL inputs: `out` zeroed |
| `stdlib_alloc_tm_counter(index)` | one counter by `STDLIB_TM_*` index | `0` for out-of-range index |
| `stdlib_alloc_fail_begin(mode, count)` / `stdlib_alloc_fail_end()` | explicit injection window (see caveats) | — |
| `alloc_fail_scope(mode, count)`, `armed_now()`, `reset_now()`, `drop()` | RAII injection scope | — |

Modes: `ALLOC_FAIL_SCOPE_NONE` (reset), `ALLOC_FAIL_SCOPE_AT` (`stdlib_alloc_fail_at`),
`ALLOC_FAIL_SCOPE_AFTER` (`stdlib_alloc_fail_after`).

## 5. Allocation-failure scope

`alloc_fail_scope` is a C+ class with a `void drop(void)` method, so the C+
lowering emits a deterministic destructor: leaving the scope always calls
`stdlib_alloc_fail_reset()`. A zeroed handle is tolerated (reset is harmless).
This was preferred over an explicit-only API because RAII makes reset
automatic; `stdlib_alloc_fail_begin`/`stdlib_alloc_fail_end` remain available
for plain-C contexts.

**Caveats (documented in the header):** the injection state is one global
window in `stdlib_state`. Scopes are **not nestable, not re-entrant and not
thread-safe**; an inner scope overwrites an outer one. With the explicit pair,
`end` must be reached on every exit path. Do not copy or move an active scope.

## 6. Telemetry helpers

`stdlib_alloc_tm_snapshot`, `stdlib_alloc_tm_delta` and
`stdlib_alloc_tm_counter` wrap the existing atomic counters. The counters are
**off by default**: a test must call `stdlib_telemetry_set_enabled(1)` (as the
self-test does). Deltas are raw unsigned differences; a reset or wrap between
snapshots invalidates them. These counters are diagnostics, never proof of
identity, liveness or safety.

## 7. Strict-build compatibility

The checked-in strict driver (`./build/c+ build --strict --sanitize=none`)
rejects unverified operations. This header and test comply:

- **No pointer-parameter subscripts.** The only byte-level work is
  `__builtin_memcpy` of zero templates into out-parameters (the same pattern
  `stdlib/error.hp` uses for message copies).
- **No variable division/modulo.** Element counts go through
  `stdlib_alloc_query_length`, which uses the division-free `stdlib_safe_div`
  helper; `stdlib_alloc_view_remaining` uses a guarded subtraction.
- **No chained temporaries.** Functions return `int`/scalars or fill
  out-parameters; the test uses named locals rather than calling a method on a
  returned temporary.
- **Double-pointer validation.** `stdlib_alloc_view_transfer_ok` takes
  `void **dst, void **src` (matching `stdlib_transfer_ownership`), not `void *`.
- **Test locals are function-unique** (`ait_`, `att_`, `atm_`, `asc_` prefixes):
  C+ name resolution and owner analysis are translation-unit wide, and each
  tracked allocation is released through exactly one named handle.

## 8. Self-test and exact results

`tests/stdlib/allocator/allocator_test.cp` is self-checking (no `.stdin` or
`.expected`): it prints `<tag> <0|1>` per check and `ALLOCATOR_TEST <checks>
<failures>` as the final line, and exits `0` only when failures = 0. It covers
live/NULL/untracked identity, all scalar views, remaining capacity, reallocation
history, stale views, transfer validation, telemetry snapshot/delta/counter,
the RAII scope (AT and AFTER, including early `reset_now`) and the explicit
begin/end pair. It never dereferences a stale pointer.

### 8.1 Strict, no sanitizer (required first)

```sh
./build/c+ build --strict --sanitize=none tests/stdlib/allocator/allocator_test.cp \
  -o build/stdlib-tests/allocator_test.strict_none
timeout 30 ./build/stdlib-tests/allocator_test.strict_none < /dev/null
```

Build rc `0`, run rc `0`, stderr empty. stdout (byte-identical across both
verified modes):

```
id_live 1
id_null 1
id_untracked 1
view_fields 1
view_remaining 1
unchanged_stable 1
realloc_stale 1
realloc_new_id 1
view_stale 1
transfer_valid 1
transfer_stale 1
tm_delta 1
tm_null 1
scope_at_armed 1
scope_at_fail 1
scope_at_reset 1
scope_after_fail 1
scope_reset_now 1
scope_after_recover 1
explicit_fail 1
explicit_end 1
ALLOCATOR_TEST 21 0
```

### 8.2 UBSan

```sh
CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include \
  ./build/c+ build --sanitize=undefined tests/stdlib/allocator/allocator_test.cp \
  -o build/stdlib-tests/allocator_test.ubsan
timeout 30 ./build/stdlib-tests/allocator_test.ubsan < /dev/null
```

Build rc `0`, run rc `0`, stdout exactly as in §8.1, stderr empty: no UBSan
report and no C+ runtime fatal.

**Toolchain note (pre-existing, not caused by this header):** the checked-in
`./build/c+` driver compiles the instrumented runtime from
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c`
(which uses `CPLUS_EXIT_ARITHMETIC`) but defaults its include path to this
repository's `runtime/include`, whose `cplus/cplus.h` lacks that constant. An
unsuffixed `--sanitize=undefined` build therefore fails with
`use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'` in that runtime file.
Setting `CPLUS_INCLUDE` to the matching `c-plus-core/runtime/include` directory
(as above) selects the matching header and the build succeeds. No repository
file was changed to work around this.

## 9. Non-claims / limits inherited from `stdlib/error.hp`

- Only `stdlib_alloc`/`stdlib_calloc`/`stdlib_realloc` blocks and containers
  built on them carry metadata; raw `malloc`/stack/static addresses are
  invisible by design.
- Generation checks are opt-in; passing a generation of `0` disables them.
- Generation wrap is unguarded; a full wrap would make a stale token compare
  equal again.
- A freed address can be recycled; only a previously captured
  `(base, generation)` pair distinguishes the new block. An address alone does
  not prove identity.
- Telemetry is diagnostic, in-process and off by default; absence of a report
  is not a memory-safety proof.

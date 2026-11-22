# Deep collections boundary contract (`stdlib/collections.hp`)

Independent deep contract coverage for **boundary values, invalid arguments,
capacity overflow, lifecycle and deterministic fixed-seed stress** across all
seven containers (`intmap`, `intset`, `strmap`, `deque`, `list`, `stack`,
`queue`).  This document records the behaviour pinned down by the new test,
the success / recoverable classification, the exact expected stdout, the
verification commands and the findings.

## Scope

| File | Role |
| --- | --- |
| `tests/stdlib/deep_collections_boundary.cp` | new self-checking test covering items 1–10 below |
| `docs/stdlib/DEEP_COLLECTIONS_BOUNDARY_CONTRACT.md` | this document (exact stdout, commands, results, findings) |
| `stdlib/collections.hp` | **unchanged** — no library defect was reproduced |

No header was edited, and no other test file was created or modified.  The two
pre-existing deep collection suites (`deep_collections_growth.cp`,
`deep_collections_collisions.cp`) were read first and their cases are **not**
duplicated here: they already cover non-power-of-two `grow(3/7/9/100/1000)`,
200-key reinsertion, the 48-key collision chain, the 2000-iteration
intmap differential model, strmap NULL/empty keys and injected-allocation
rollback.  This suite adds only the boundary / lifecycle / invalid-argument /
fixed-seed-stress axes.

The test is self-checking: each of its 58 checks prints `<tag> 1` on success
(or `<tag> 0` on failure), followed by `BOUNDARY <checks> <failed>` and a
`DIGEST <hi32> <lo32>` line (both decimal).  `main` returns `0` only when
`<failed> == 0`.  No `.expected` file was added (per file-ownership
constraints); the stdout below is the byte-for-byte oracle.

## Contracts exercised

1. **Empty containers (`bd_empty_*`).**  A zero-initialised container has
   `size() == 0`; hash maps additionally have `valid() == 0` because `state`
   is still `NULL`.  Every accessor refuses on empty: `get`/`remove` return
   `-1`, `contains` returns `0`, and `pop_front`/`pop_back`/`pop`/`top_checked`/
   `front_checked`/`at_checked` return `-1` **without writing the out-param**
   (the sentinel value is preserved).  `clear()` is a no-op even when called
   three times on an untouched container (`strmap::clear` loops `i < capacity`
   with `capacity == 0`; the others just zero their counters).
2. **Minimum capacity (`bd_mincap_*`).**  For the three hash containers,
   `stdlib_map_next_pow2` clamps every want below 8 to 8, so `grow(0)`,
   `grow(1)` and `grow(7)` all leave a valid 8-slot table and a later insert
   succeeds.  For `deque`, `stack` and `queue` the first push allocates exactly
   one 8-element block (`stdlib_alloc_seq()` delta == 1); `deque::capacity()`
   reads 8.  `list` allocates one node on its first push.
3. **Maximum capacity / overflow (`bd_maxcap_*`).**  `grow(STDLIB_ALLOC_LIMIT)`
   rounds to `2^40`, which is rejected by `stdlib_rebuild` because
   `cap > STDLIB_ALLOC_LIMIT / sizeof(slot)`; `grow(SIZE_MAX)` makes
   `next_pow2` return `0`.  Both return `-1` **before any `stdlib_alloc` call**,
   so the allocation-sequence delta is exactly 0 and the existing entries and
   `size()` are intact.  `deque::reserve(SIZE_MAX)` is refused by
   `stdlib_ring_copy` with `stdlib_note(STDLIB_CATEGORY_RANGE, ...)` and the
   same zero-allocation guarantee.
4. **Boundary growth (`bd_bound_*`).**  Five keys fit in the cap-8 table
   (`needs_room(5,8)` is false *before* the sixth insert); the sixth triggers a
   rebuild to 16, proven by the exact rebuild buffer count in
   `stdlib_alloc_seq()` (3 for `intmap`/`strmap` rebuild buffers, 2 for
   `intset`; `strmap` adds one owned key copy = 4 total).  All six values are
   then verified.  Filling `deque`/`list`/`stack`/`queue` to 8 and pushing a
   ninth preserves element order.  Two extra cases force a real ring wrap:
   `bd_bound_deque_wrap` uses `push_front` to move `head` off zero before the
   9th push, and `bd_bound_queue_wrap` pops three then refills; both verify the
   linearised order after `stdlib_ring_copy`.
5. **Repeated insertion of the same key (`bd_repeat_*`).**  Fifty `put`/`insert`
   calls with a changing value keep `size() == 1`; the probe finds the existing
   slot and only overwrites the value.  After the first call the allocation
   sequence delta is exactly 0 for all three hash containers, and the last
   value wins.
6. **Missing keys / NULL out-params (`bd_missing_*`, `bd_nullout_*`).**
   `remove` on an empty and on a populated container for an absent key returns
   `-1` with unchanged size for all three hash types.  NULL out-params are
   refused (`-1`) by `get`, `at_checked`, `pop_front`, `pop_back`, `pop`,
   `top_checked` and `front_checked`; an out-of-range `at_checked` also returns
   `-1` with a bounds note and leaves the out-param untouched.
7. **Tombstone reuse (`bd_tomb_*`).**  With a cap-16 table, inserting 5 keys,
   removing 3 and inserting 3 new keys keeps `container_growths` unchanged:
   `needs_room(5,16)` is false, the inserted keys occupy the `MAP_DELETED`
   slots and `used_slots` is reused.  All surviving and new values are
   verified.  `intmap`/`intset` additionally show an allocation delta of 0;
   `strmap` allocates only three small key copies, never a table rebuild.
   (`container_growths` is the uniform "no growth" oracle for all three.)
8. **Fixed-seed stress (`bd_stress_*`).**  Six independent 3072-operation
   runs (18,432 operations total) driven by the LCG `0xC0FFEE` with multiplier
   `6364136223846793005` and increment `1442695040888963407`:
   - `deque` and `list`: `push_front`/`push_back`/`pop_front`/`pop_back` against
     a front-to-back shadow array, verifying `size()` and every `at_checked`
     position after **every** operation;
   - `stack`: `push`/`pop` against a LIFO shadow, verifying `top_checked` after
     every operation;
   - `queue`: `push`/`pop` against a FIFO shadow, verifying `front_checked`
     after every operation;
   - `intmap` and `intset`: a 128-key present/value shadow compared by full
     scan after every operation (presence, value and `size()`).
   Each container is re-seeded to the same value, so the runs are independent
   and reproducible.  Every observed `(size, front, back, top, key-presence)`
   is folded into a 64-bit FNV-1a digest printed as two 32-bit decimal words.
9. **Use after drop (`bd_drop_*`).**  After `drop()` (called twice to prove
   idempotence), `size() == 0`, `valid() == 0` for hash maps, all accessors
   refuse and out-params are untouched; a subsequent `put`/`insert`/`push`
   succeeds from the zero-init state (and reallocates an 8-slot block).
10. **Invalid arguments (`bd_invalid_*`).**  `strmap.put(NULL, ...)`,
    `strmap.get(NULL, &out)`, NULL out-params and `at_checked(SIZE_MAX, &out)`
    are all recoverable `-1`/`0` refusals with no crash, no state change and no
    out-param write.

Reaching the final `BOUNDARY` line under `timeout 30` also proves that every
probe/ring loop terminates.

## Classification

| Class | Meaning | Observed here |
| --- | --- | --- |
| **success** | `0` from mutators, found lookups, preserved wrapped order and shadow-model agreement | all `bd_empty_*`, `bd_mincap_*`, `bd_bound_*`, `bd_repeat_*`, `bd_tomb_*`, `bd_stress_*`, `bd_drop_*` re-init checks |
| **recoverable** | `-1` (or `0` for `contains`) with unchanged state, zero allocation for size-only refusals, no process termination | `bd_maxcap_*`, `bd_missing_*`, `bd_nullout_*`, dropped-container accessors, `bd_invalid_*` |
| **fatal** | runtime fatal / sanitizer abort | **none** — stderr is empty in all three modes; `bd_no_fatal` and `bd_stress_terminated` are always 1 |

## Exact expected stdout

`deep_collections_boundary` — 58 checks, exit 0, identical in **all three**
verification modes (60 lines, 58 `1` records + the two summary records):

```
bd_empty_intmap 1
bd_empty_intset 1
bd_empty_strmap 1
bd_empty_deque 1
bd_empty_list 1
bd_empty_stack 1
bd_empty_queue 1
bd_mincap_intmap 1
bd_mincap_intset 1
bd_mincap_strmap 1
bd_mincap_deque 1
bd_mincap_list 1
bd_mincap_stack 1
bd_mincap_queue 1
bd_maxcap_intmap 1
bd_maxcap_intset 1
bd_maxcap_strmap 1
bd_maxcap_deque_reserve 1
bd_bound_intmap 1
bd_bound_intset 1
bd_bound_strmap 1
bd_bound_deque 1
bd_bound_list 1
bd_bound_stack 1
bd_bound_queue 1
bd_bound_deque_wrap 1
bd_bound_queue_wrap 1
bd_repeat_intmap 1
bd_repeat_intset 1
bd_repeat_strmap 1
bd_missing_maps 1
bd_nullout_deque 1
bd_nullout_list 1
bd_nullout_stack 1
bd_nullout_queue 1
bd_tomb_intmap 1
bd_tomb_intset 1
bd_tomb_strmap 1
bd_stress_deque 1
bd_stress_list 1
bd_stress_stack 1
bd_stress_queue 1
bd_stress_intmap 1
bd_stress_intset 1
bd_stress_terminated 1
bd_drop_intmap 1
bd_drop_intset 1
bd_drop_strmap 1
bd_drop_deque 1
bd_drop_list 1
bd_drop_stack 1
bd_drop_queue 1
bd_invalid_strmap 1
bd_invalid_deque 1
bd_invalid_list 1
bd_invalid_stack 1
bd_invalid_queue 1
bd_no_fatal 1
BOUNDARY 58 0
DIGEST 141365270 2760897655
```

The digest is the concatenation (big-endian word order) of the two decimal
words: `0x086D1016 0xA48FF477` (i.e. `0x086D1016A48FF477`).  It is a determinism oracle only — it is not a
correctness proof by itself; the 58 per-check records above are.

## Commands

From the repository root, with the C+ compiler at `build/c+`.  All three
required modes were built and then executed under an external `timeout 30`
with stdin from `/dev/null`:

```sh
mkdir -p build/deep_collections_boundary

# 1) STRICT, sanitizers disabled (verified FIRST)
./build/c+ build tests/stdlib/deep_collections_boundary.cp \
  --strict --sanitize=none \
  -o build/deep_collections_boundary/boundary_strict
timeout 30 ./build/deep_collections_boundary/boundary_strict < /dev/null \
  > build/deep_collections_boundary/strict.stdout \
  2> build/deep_collections_boundary/strict.stderr
echo $?   # 0

# 2) Default (Safe mode: UBSan + bounds + null + alignment already enabled)
./build/c+ build tests/stdlib/deep_collections_boundary.cp \
  -o build/deep_collections_boundary/boundary_default
timeout 30 ./build/deep_collections_boundary/boundary_default < /dev/null \
  > build/deep_collections_boundary/default.stdout \
  2> build/deep_collections_boundary/default.stderr
echo $?   # 0

# 3) Explicit UBSan spelling
./build/c+ build tests/stdlib/deep_collections_boundary.cp \
  --sanitize=undefined \
  -o build/deep_collections_boundary/boundary_ubsan
timeout 30 ./build/deep_collections_boundary/boundary_ubsan < /dev/null \
  > build/deep_collections_boundary/ubsan.stdout \
  2> build/deep_collections_boundary/ubsan.stderr
echo $?   # 0
```

Optional integration check through the project runner (default mode):

```sh
tests/stdlib/run_stdlib_tests.sh deep_collections_boundary
# -> "  ok   deep_collections_boundary"
# -> "C+ stdlib tests: 1 passed, 0 failed, 0 skipped"
```

## Observed results

| Mode | Build diagnostics | Exit | stderr | Final lines |
| --- | --- | --- | --- | --- |
| `--strict --sanitize=none` | none | 0 | empty | `BOUNDARY 58 0` / `DIGEST 141365270 2760897655` |
| default (Safe) | none | 0 | empty | `BOUNDARY 58 0` / `DIGEST 141365270 2760897655` |
| `--sanitize=undefined` | none | 0 | empty | `BOUNDARY 58 0` / `DIGEST 141365270 2760897655` |

`diff` of the three captured stdout files is empty (byte-for-byte identical),
and two additional strict runs reproduced the same digest, confirming
determinism.  No sanitizer diagnostics and no runtime fatal were produced.
ASan was not part of this task's required matrix.

## Findings

**No defect in `stdlib/collections.hp` was reproduced.**  `stdlib/collections.hp`
was not modified.  The behaviour checked against the implementation is:

- all size-only refusals (`grow(SIZE_MAX)` via `next_pow2 == 0`,
  `grow(STDLIB_ALLOC_LIMIT)` via the `cap > LIMIT/sizeof(slot)` guard,
  `reserve(SIZE_MAX)` via `stdlib_ring_copy`) return `-1` before allocating;
- the 70% load threshold keeps empty slots so both `stdlib_probe` and the
  unbounded insertion loops terminate, including after tombstone reuse;
- rebuild buffer counts observed via `stdlib_alloc_seq()` match the header
  (3 for intmap/strmap, 2 for intset, 1 for a ring growth);
- out-params are written only on success, so a sentinel survives every refusal;
- `drop()` resets every container to the zero-init state, so a later
  `put`/`insert`/`push` allocates and works again, and a second `drop()` is inert.

All intermediate failures were **test-side** and were corrected in the `.cp`
file only; the library was never changed to make a check pass:

1. The C+ safe-bounds/lifetime checker required out-parameters to be
   initialised before their addresses were passed, and could not prove
   `shadow_n >= 1` for `shadow[shadow_n - 1]` after a decrement performed
   through a pointer parameter.  Fixed by initialising every scalar out-param
   and routing back reads through a guarded `bd_sh_back()` helper.
2. The wrapped-deque case initially reused the deque already at 9 elements
   from the preceding boundary check, so its expected size was wrong.  Fixed
   by using a fresh `bdw` deque.
3. The drop section shared one sentinel variable across all seven types; the
   sentinel was overwritten by a successful `get` in an earlier sub-check.
   Fixed by resetting it before each type's refusal assertions.

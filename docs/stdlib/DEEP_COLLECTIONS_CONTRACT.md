# Deep collections contract (`stdlib/collections.hp`)

Deep, independent contract tests for the checked hash containers in
`stdlib/collections.hp`.  This document records the behaviour the tests pin
down, the success / recoverable / fatal classification, the exact expected
output, and the commands used to verify it.

## Scope

| File | Role |
| --- | --- |
| `tests/stdlib/deep_collections_growth.cp` | growth, capacity rounding, tombstones, null/empty keys, injected allocation failure, leak/rollback checks |
| `tests/stdlib/deep_collections_collisions.cp` | collision-heavy insert/remove/reinsert, growth after tombstones, intmap/intset/strmap, deterministic differential model |
| `stdlib/collections.hp` | **unchanged** — no defect was reproduced (see Findings) |

No other files were created or modified.  Both tests are self-checking: every
`check()` prints `<label> 1` on success (or `<label> 0` on failure), the final
line is `RESULT <checks> <failed>`, and `main` returns `0` only when
`<failed> == 0`.  The project runner (`run_stdlib_tests.sh`) therefore treats a
pass as exit code 0; the exact stdout below is the additional byte-for-byte
oracle (no `.expected` file was added, per file-ownership constraints).

## Contracts exercised

1. **Capacity rounding.** `grow(want)` uses `stdlib_map_next_pow2`, which
   returns the smallest power of two `>= max(8, want)`.  Non-power-of-two wants
   (`3 -> 8`, `7 -> 8` no-op, `9 -> 16`, `100 -> 128`, `1000 -> 1024`) succeed,
   keep `size()`/`valid()`, and preserve all entries.  The power-of-two result
   is what makes the `& (capacity - 1)` slot mask valid.
2. **Overflow is recoverable.** `grow(SIZE_MAX)` makes `next_pow2` return `0`,
   so `grow` returns `-1` and leaves the map intact.
3. **Tombstones.** `remove()` marks a slot `MAP_DELETED` and decrements
   `count`, but not `used_slots`.  `needs_room` is therefore evaluated against
   the number of slots ever used, so the table always keeps empty slots and
   linear probing terminates.  `grow(n)` with `n <= current capacity` is a
   documented no-op; `grow(n) > capacity` rebuilds and purges tombstones, with
   `used_slots` reset to `count`.  Removed keys can be reinserted (they occupy
   the first tombstone in their probe chain) and surviving keys keep their
   original values.
4. **Empty/NULL keys (strmap).** `put(NULL, ...)` is a recoverable `-1` with a
   note, and does not allocate or make the map valid.  `get`/`contains`/
   `remove` with `NULL` are recoverable (`-1` / `0` / `-1`).  The empty string
   `""` is a legitimate key: insert, overwrite, get and remove all work and
   `size()` tracks it correctly.
5. **Allocation-failure atomicity.** `stdlib_alloc_fail_at(k)` and
   `stdlib_alloc_fail_after(0)` were used to fail each buffer of a rebuild:
   - `intmap`: three buffers (keys, values, state) — failures at 1, 2, 3;
   - `strmap`: three rebuild buffers plus the owned key copy — failures at 1,
     2, 3 (rebuild rollback) and 4 (rebuild succeeds, key copy fails).
   In every case the operation returns `-1`, `size()` and `valid()` are
   unchanged, every pre-existing key retains its value, the new key is absent,
   and a retry without injection succeeds.  When the key copy (last fallible
   step) fails, only the *physical* layout changed (capacity/tombstones);
   the public API observable state is unchanged, matching the header's
   "logical contents" wording.
6. **No leaks.** With telemetry enabled and reset immediately before the
   scenario, a failed `grow()` followed by `drop()` yields
   `alloc_count == free_count` and `alloc_failures >= 1`.
7. **No infinite probing loop.** The 48-key chain shares the low 10 hash bits
   and keeps colliding at every capacity up to 1024; inserting a further 16
   keys forces several rehashes of a 64-way chain.  Every run completes under
   `timeout 30s`; reaching the final `RESULT` line is the termination proof.
8. **Differential oracle.** A fixed-seed LCG drives 2000 deterministic
   put/remove operations over the colliding key pool; a parallel reference
   model is compared by full scan at the end (`c_diff_model`).
9. **Idempotence.** `drop()` twice and removal of absent keys are safe.

## Classification

| Class | Meaning | Observed in these tests |
| --- | --- | --- |
| **success** | operation returns `0` (mutators) / value found (lookups), `valid()` true where applicable | all `g_*`, `s_*`, `c_*` positive checks |
| **recoverable** | operation returns `-1` (or `0` for `contains`) with the container still valid, nothing leaked, no process termination | `g_oom1/2/3`, `g_fail_after`, `s_oom1/2/3`, `s_oom_keycopy`, `s_null_put`, `s_null_queries`, `g_grow_overflow`, absent-key `remove`/`get` |
| **fatal** | `cplus_fatal` / runtime safety abort | **none** — every run reaches `RESULT` and exits 0 (`g_no_fatal`, `c_terminated`) |

## Exact expected stdout

`deep_collections_growth` (32 checks, exit 0):

```
g_empty 1
g_grow3 1
g_grow7 1
g_grow9 1
g_grow100 1
g_insert 1
g_tomb_remove 1
g_grow_noop 1
g_grow_big 1
g_reinsert 1
g_reinsert_vals 1
g_survivors 1
g_all_present 1
g_grow_overflow 1
g_dropped 1
g_oom1 1
g_oom2 1
g_oom3 1
g_oom_retry 1
g_fail_after 1
s_null_put 1
s_null_queries 1
s_empty_put 1
s_empty_overwrite 1
s_empty_remove 1
s_oom_keycopy 1
s_oom_keycopy_retry 1
s_oom1 1
s_oom2 1
s_oom3 1
g_leak 1
g_no_fatal 1
RESULT 32 0
```

`deep_collections_collisions` (23 checks, exit 0):

```
c_collect_int 1
c_map_insert 1
c_map_probe_all 1
c_map_removed 1
c_map_kept 1
c_map_reinsert 1
c_map_values 1
c_collect_more 1
c_map_grow 1
c_map_grow_probe 1
c_map_dropped 1
c_set_insert 1
c_set_removed 1
c_set_reinsert 1
c_collect_str 1
c_str_insert 1
c_str_probe_all 1
c_str_removed 1
c_str_kept 1
c_str_reinsert 1
c_str_values 1
c_diff_model 1
c_terminated 1
RESULT 23 0
```

## Commands

From the repository root (the C+ compiler is `build/c+`):

```sh
# build (all three configurations)
./build/c+ build tests/stdlib/deep_collections_growth.cp      -o build/deep_collections/growth_default
./build/c+ build tests/stdlib/deep_collections_growth.cp      --strict --sanitize=none -o build/deep_collections/growth_strict
./build/c+ build tests/stdlib/deep_collections_growth.cp      --sanitize=undefined   -o build/deep_collections/growth_ubsan
./build/c+ build tests/stdlib/deep_collections_collisions.cp  -o build/deep_collections/collisions_default
./build/c+ build tests/stdlib/deep_collections_collisions.cp  --strict --sanitize=none -o build/deep_collections/collisions_strict
./build/c+ build tests/stdlib/deep_collections_collisions.cp  --sanitize=undefined   -o build/deep_collections/collisions_ubsan

# run under an external timeout (no infinite probing loops)
for t in growth collisions; do
  for cfg in default strict ubsan; do
    timeout 30s ./build/deep_collections/${t}_${cfg}
  done
done

# project runner integration (builds + runs, checks exit 0)
tests/stdlib/run_stdlib_tests.sh deep_collections
```

Observed results:

| Test | default (Safe, `-fsanitize=undefined,bounds,null,alignment`) | `--strict --sanitize=none` | UBSan | exit |
| --- | --- | --- | --- | --- |
| `deep_collections_growth` | `RESULT 32 0` | `RESULT 32 0` | `RESULT 32 0` | 0 |
| `deep_collections_collisions` | `RESULT 23 0` | `RESULT 23 0` | `RESULT 23 0` | 0 |

No sanitizer diagnostics were produced.  `--sanitize=address` is **not
available** with the selected backend (the driver rejects it with
"sanitizer 'address' is not supported by the selected backend"), so ASan was
not part of the required matrix.  Note also that in the current driver
`--strict` is accepted but the emitted backend command still carries
`-DCPLUS_SAFE=1` and no extra strictness flag; the meaningful difference in
that configuration is `--sanitize=none` removing instrumentation.

## Findings

No defect in `stdlib/collections.hp` was reproduced for any requested
scenario, so **`stdlib/collections.hp` was not modified**.  The reasoning that
was checked against the implementation:

- Every insertion path calls `stdlib_map_needs_room` *before* the unbounded
  `for(;;)` probe loop; the 70% load threshold leaves empty slots, so the loop
  terminates.  `stdlib_probe` and rebuild insertion are additionally bounded
  by capacity / by `cap > count`.
- `grow()` only rebuilds when the rounded-up power of two exceeds current
  capacity, so `& (capacity - 1)` is always a valid mask and all rebuild call
  sites pass `cap > count`.
- All rebuild functions allocate every buffer before mutating and roll back
  partial allocations on failure; `strmap::put` keeps the key copy as the last
  fallible step.  Telemetry confirmed balanced `alloc_count`/`free_count`
  after a failed growth plus `drop()`.
- Tombstones only increase `used_slots`, so they can never make the
  `needs_room` guard less conservative; same-size rebuilds purge them and
  reset `used_slots = count`.

Three early failures were in the *tests themselves* (a wrong key expectation,
a wrong stored-value expectation, and a leak scenario whose injected failure
never fired because the table had already grown to capacity 256).  They were
corrected in the test files only; library behaviour was never changed to make
a test pass.

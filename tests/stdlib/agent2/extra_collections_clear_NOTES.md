# extra_collections_clear — untested-path regression test (agent 2)

## Owned files

- `tests/stdlib/agent2/extra_collections_clear.cp`
- `tests/stdlib/agent2/extra_collections_clear_NOTES.md` (this file)

No header, existing test, or other agent's file was modified. No git add/commit
was performed. All build/run artifacts were written outside the repository to
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/ecc_artifacts/`.

## Coverage map

| Requirement | Tags |
|---|---|
| `clear()` on populated **intset** + size 0 + reuse + no leak | `ecc_set_populated`, `ecc_set_clear_size`, `ecc_set_reuse`, `ecc_set_drop_balance` |
| `clear()` on populated **deque** (non-zero head) + reuse + no leak | `ecc_deq_populated`, `ecc_deq_clear_size`, `ecc_deq_reuse`, `ecc_deq_reserve_max_rejected`, `ecc_deq_drop_balance` |
| `clear()` on populated **list** + reuse + no leak | `ecc_list_populated`, `ecc_list_clear_size`, `ecc_list_reuse`, `ecc_list_drop_balance` |
| `clear()` on populated **stack** + reuse + no leak | `ecc_stack_populated`, `ecc_stack_clear_size`, `ecc_stack_reuse`, `ecc_stack_drop_balance` |
| `clear()` on populated **queue** + reuse + no leak | `ecc_queue_populated`, `ecc_queue_clear_size`, `ecc_queue_reuse`, `ecc_queue_drop_balance` |
| Repeated `clear()` x3 then insert again (all five containers, plus intmap/strmap) | `ecc_*_triple_clear`, `ecc_*_reuse_after_triple`, `ecc_map_clear_size`, `ecc_smap_clear_size` |
| count/used_slots consistency after tombstones + clear, inferred from behavior | `ecc_set_rebuild_after_clear`, `ecc_map_tombstones`, `ecc_map_rebuild_after_clear`, `ecc_smap_tombstones`, `ecc_smap_rebuild_after_clear`, `ecc_global_balance` |
| Oversized **strmap** key (`len >= STDLIB_ALLOC_LIMIT`) | `ecc_strmap_limit_minus_one`, `ecc_strmap_oversized_key_rejected`, `ecc_strmap_oversized_key_no_alloc`, `ecc_strmap_overlimit_key_rejected`, `ecc_strmap_oversized_atomic`, `ecc_strmap_clear_frees_key` |
| Doubling-overflow guards via `reserve(SIZE_MAX)` / repeated pushes | `ecc_deq_reserve_max_rejected`, `ecc_deq_fill_to_limit`, `ecc_deq_doubling_guard`, `ecc_deq_guard_recovers`, `ecc_stack_fill_to_limit`, `ecc_stack_doubling_guard`, `ecc_stack_guard_recovers`, `ecc_queue_fill_to_limit`, `ecc_queue_doubling_guard`, `ecc_queue_guard_recovers` |
| `stdlib_map_rebuild_capacity` overflow return; `grow(SIZE_MAX)` | `ecc_rebuild_capacity_overflow`, `ecc_rebuild_capacity_double`, `ecc_rebuild_capacity_same`, `ecc_intmap_grow_max_rejected`, `ecc_strmap_grow_max_rejected`, `ecc_intset_grow_max_rejected`, `ecc_grow_max_no_alloc` |
| No leak / no double free | all `ecc_*_drop_balance`, `ecc_global_balance` |

`stdlib_map_rebuild_capacity` overflow is reached by a direct call with
`capacity = (size_t)-2`, `count = capacity/2 - 1`: the load test is true without
unsigned wrap, `capacity > SIZE_MAX >> 2`, so the helper returns 0. `grow(SIZE_MAX)`
is checked separately (it fails earlier in `stdlib_map_next_pow2`).

## Allocation-limit override (no memory exhaustion)

Every build command passes `-DSTDLIB_ALLOC_LIMIT=256`. The test also has a
matching `#define STDLIB_ALLOC_LIMIT 256` **before the includes in its own
source**: the C+ front-end's strict bounds checker does not consume `-D` flags
(they reach only the C backend), so without the source-level define a strict
build rejects the key-buffer indexing. No `stdlib/*.hp` file was changed. With
a 256-byte limit:

- an "oversized" strmap key is 256 bytes (boundary 255 bytes is accepted),
- a deque/stack/queue doubling guard is reached after 64 `int`s (the 65th push
  must return -1 without allocating),

so the largest single allocation in the whole run is 256 bytes. A runtime guard
(`ecc_limit + 1 < sizeof(ecc_bigkey)`) skips the oversized-key section if both
overrides are absent. No `stdlib_alloc_fail_at`/`fail_from` injection and no
memory exhaustion was used.

## Exact commands (run from the repository root)

Environment for every command:

```sh
ROOT=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
TMPD=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/ecc_artifacts
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export CPLUS_CLANG=/usr/bin/clang
```

1. STRICT `--sanitize=none` — **run FIRST**:

```sh
timeout 30 "$ROOT/build/c+" build "$ROOT/tests/stdlib/agent2/extra_collections_clear.cp" \
  --strict --sanitize=none -DSTDLIB_ALLOC_LIMIT=256 -o "$TMPD/ecc_strict_none"
timeout 30 "$TMPD/ecc_strict_none" > "$TMPD/strict_none.out" 2> "$TMPD/strict_none.err"
```

2. Default (no mode flags):

```sh
timeout 30 "$ROOT/build/c+" build "$ROOT/tests/stdlib/agent2/extra_collections_clear.cp" \
  -DSTDLIB_ALLOC_LIMIT=256 -o "$TMPD/ecc_default"
timeout 30 "$TMPD/ecc_default" > "$TMPD/default.out" 2> "$TMPD/default.err"
```

3. UBSan:

```sh
timeout 30 "$ROOT/build/c+" build "$ROOT/tests/stdlib/agent2/extra_collections_clear.cp" \
  --strict --sanitize=undefined -DSTDLIB_ALLOC_LIMIT=256 -o "$TMPD/ecc_ubsan"
timeout 30 "$TMPD/ecc_ubsan" > "$TMPD/ubsan.out" 2> "$TMPD/ubsan.err"
```

Harness integration (results redirected so other agents' artifacts were not
touched by `--clean`; the harness itself does not pass `-D`, which is why the
source-level define is present):

```sh
AGENT2_RESULTS="$TMPD/results" timeout 120 \
  sh "$ROOT/tests/stdlib/agent2/run_all.sh" --test extra_collections_clear --clean
```

Result: `PASS=1 FAIL=0 BUILD_FAILURE=0 TIMEOUT=0 SKIPPED=0 BLOCKED=0 TOTAL=1`,
harness exit 0.

## Per-mode results

| Mode | Build rc | Run rc | Final stdout line | stderr | Verdict |
|---|---:|---:|---|---|---|
| strict / sanitize=none (first) | 0 | 0 | `EXTRA_COLLECTIONS_CLEAR 67 0` | empty | PASS |
| default | 0 | 0 | `EXTRA_COLLECTIONS_CLEAR 67 0` | empty | PASS |
| strict / sanitize=undefined (UBSan) | 0 | 0 | `EXTRA_COLLECTIONS_CLEAR 67 0` | empty | PASS |

All three stdout files are byte-identical (SHA-256
`e55ab1b7…f09` for each); `diff` is clean in both comparisons. 68 stdout lines:
67 `<tag> <0|1>` check lines (all `1`, none `0`) plus the final summary. Exit 0
in every mode. UBSan passing is not a memory-safety proof.

## Exact stdout (all three modes)

```
ecc_set_populated 1
ecc_set_clear_size 1
ecc_set_reuse 1
ecc_set_triple_clear 1
ecc_set_rebuild_after_clear 1
ecc_set_drop_balance 1
ecc_deq_populated 1
ecc_deq_clear_size 1
ecc_deq_reuse 1
ecc_deq_triple_clear 1
ecc_deq_reuse_after_triple 1
ecc_deq_reserve_max_rejected 1
ecc_deq_drop_balance 1
ecc_list_populated 1
ecc_list_clear_size 1
ecc_list_reuse 1
ecc_list_triple_clear 1
ecc_list_reuse_after_triple 1
ecc_list_drop_balance 1
ecc_stack_populated 1
ecc_stack_clear_size 1
ecc_stack_reuse 1
ecc_stack_triple_clear 1
ecc_stack_reuse_after_triple 1
ecc_stack_drop_balance 1
ecc_queue_populated 1
ecc_queue_clear_size 1
ecc_queue_reuse 1
ecc_queue_triple_clear 1
ecc_queue_reuse_after_triple 1
ecc_queue_drop_balance 1
ecc_map_tombstones 1
ecc_map_clear_size 1
ecc_map_rebuild_after_clear 1
ecc_map_drop_balance 1
ecc_smap_tombstones 1
ecc_smap_clear_size 1
ecc_smap_rebuild_after_clear 1
ecc_smap_drop_balance 1
ecc_strmap_limit_minus_one 1
ecc_strmap_oversized_key_rejected 1
ecc_strmap_oversized_key_no_alloc 1
ecc_strmap_overlimit_key_rejected 1
ecc_strmap_oversized_atomic 1
ecc_strmap_clear_frees_key 1
ecc_strmap_osmap_drop_balance 1
ecc_deq_fill_to_limit 1
ecc_deq_doubling_guard 1
ecc_deq_guard_recovers 1
ecc_gdeq_drop_balance 1
ecc_stack_fill_to_limit 1
ecc_stack_doubling_guard 1
ecc_stack_guard_recovers 1
ecc_gstk_drop_balance 1
ecc_queue_fill_to_limit 1
ecc_queue_doubling_guard 1
ecc_queue_guard_recovers 1
ecc_gque_drop_balance 1
ecc_rebuild_capacity_overflow 1
ecc_rebuild_capacity_double 1
ecc_rebuild_capacity_same 1
ecc_intmap_grow_max_rejected 1
ecc_strmap_grow_max_rejected 1
ecc_intset_grow_max_rejected 1
ecc_grow_max_no_alloc 1
ecc_grow_containers_drop_balance 1
ecc_global_balance 1
EXTRA_COLLECTIONS_CLEAR 67 0
```

## Observed defects / findings

**No defect in `stdlib/collections.hp` or the C+ runtime was reproduced.**
Every required path behaved as documented: `clear()` left each container empty
and reusable, repeated `clear()` was harmless, oversized strmap keys were
rejected before allocation with the map unchanged, the doubling guards returned
-1 with unchanged size/capacity and no allocation, the rebuild-capacity overflow
returned 0, and allocator telemetry stayed balanced.

Two test-side issues were found and corrected **in this test only** (minimal
repros included so they are not reintroduced):

1. **The C+ front-end does not see `-D` flags.** Minimal repro: build with only
   `-DSTDLIB_ALLOC_LIMIT=256` and
   ```c
   size_t ecc_limit = (size_t)STDLIB_ALLOC_LIMIT;
   memset(ecc_bigkey, 'k', ecc_limit);
   ```
   STRICT rejected it:
   `[CPLUS-BOUNDS] fill of 1099511627776 bytes exceeds destination 'ecc_bigkey'`
   (the front-end folded the default `1<<40`). Fix: a matching
   `#define STDLIB_ALLOC_LIMIT 256` in the test source before the includes,
   keeping the build-command `-D` as well. This is a compiler-driver interface
   detail, not a library defect.

2. **A `drop()` window frees but does not allocate.** Minimal repro of the wrong
   assertion:
   ```c
   ecc_check("ecc_set_drop_balance",
             ecc_post.alloc_count > ecc_pre.alloc_count &&
             ecc_post.alloc_count - ecc_pre.alloc_count ==
                 ecc_post.free_count - ecc_pre.free_count);
   ```
   produced `ecc_set_drop_balance 0` for all 12 drop checks while
   `ecc_global_balance 1` passed. Fix: assert `free_count` increased,
   `alloc_count` unchanged, and `ownership_violations == 0`. This was an
   incorrect expectation in the new test, not a leak.

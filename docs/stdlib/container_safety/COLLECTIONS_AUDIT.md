# Collections implementation audit — Task 2 Phase 4

Branch: `agent/modern-stdlib` · Date: 2026-11-23 · Agent: Task 2 Phase 4 (1 of 10)

## 1. Scope and ownership

Audited `stdlib/collections.hp` only. Files owned for this task:

- `stdlib/collections.hp` — read-only in practice (no reproduced defect requiring a fix)
- `docs/stdlib/container_safety/COLLECTIONS_AUDIT.md` — this report

No compiler, runtime, test, or other library file was modified. No `git add`/commit.

Containers in scope: `intmap`, `intset`, `strmap`, `deque`, `list`, `stack`, `queue`.

### Concurrent-agent coordination

At first read, `stdlib/collections.hp` already carried an uncommitted diff from
another agent adapting the file to the rebuilt strict compiler (modulo `%` →
single conditional subtraction in `stdlib_ring_copy`, `deque::{push_back,
push_front,pop_front,pop_back,at_checked}` and `queue::{push,pop}`).

- Diff at audit start and at audit end: `40 insertions(+), 8 deletions(-)`,
  `git diff stdlib/collections.hp | shasum` =
  `b6df49b9edc1c579ebe7e8fefe6a58e4c6586be7` (unchanged during the audit;
  mtime 08:52, re-checked 08:57).
- Per instruction I did **not** edit the modulo/ring regions. I reviewed them
  statically (§3.4, §3.6) and found them functionally equivalent to HEAD under
  the documented invariants (`head < space`, `used <= space`, so
  `head + i < 2*space` and one subtraction suffices). No conflict to report.
- No functional defect was reproduced in any container, so **no edit to
  `collections.hp` was made at all**.

## 2. Method and environment

Compiler: repo-built `./build/c+` (mtime 2026-11-23 08:07).
Build artifacts: `/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/coll-audit`.
Every build used `timeout -k 5 60`; every run used `timeout -k 5 30`.

Modes (flags as passed to `c+ build`):

| key | flags |
| --- | --- |
| `sn` | `--strict --sanitize=none` |
| `ub` | `--strict --sanitize=undefined` |
| `dn` | `--sanitize=none` (default diagnostics) |
| `du` | `--sanitize=undefined` (default diagnostics) |

Harnesses: `tests/stdlib/deep_collections_{alloc,boundary,collisions,growth}.cp`
and the eleven `tests/stdlib/fuzz/f100_cont_*.cp` configs (F-061…F-069,
F-074, F-075).

To separate library defects from compiler regressions, the allocation test was
also rebuilt against the **unchanged HEAD** copy of `collections.hp` via
`-I<TMP>/inc` containing only `git show HEAD:stdlib/collections.hp`.

## 3. Static review by invariant

Common rules checked: zero-init validity, idempotent `drop`, failure atomicity,
overflow guards before multiply/double, bounded probes, owned-key release.

### 3.1 intmap (`int -> int`, open addressing, power-of-two cap)

- Empty/single/zero-init: constructor sets all six fields to 0/NULL;
  `stdlib_probe` returns -1 when `state == NULL`. `put`/`remove`/`get` on an
  untouched map are safe. ✔
- Probe termination: loop bounded by `capacity`; any `MAP_EMPTY` terminates
  the search. `stdlib_map_needs_room` fires at ≥70% `used_slots`, so an EMPTY
  slot always exists. ✔
- Growth/rehash: `grow` rounds via `stdlib_map_next_pow2` (returns 0 on
  overflow, checked before `cap <<= 1` against `SIZE_MAX >> 2`); `stdlib_rebuild`
  allocates keys/values/state up front and releases any partial set on failure
  (`cap < 8` / `cap > STDLIB_ALLOC_LIMIT/sizeof(int)` refused). Rehash loop
  `while (fresh_state[slot] == MAP_FULL)` terminates because `count < cap`. ✔
- Tombstones: `remove` is FULL→DELETED with `count--` and `used_slots`
  unchanged; `put` remembers `first_deleted` and reuses it without incrementing
  `used_slots`; rehash sets `used_slots = count`. ✔
- Duplicates/missing keys/overwrite: `put` probes first and overwrites the
  value in place; `remove` of absent key is -1 with no state change. ✔
- Large data / capacity overflow: verified by `deep_collections_boundary`
  (`grow(SIZE_MAX)` and `grow(STDLIB_ALLOC_LIMIT)` refuse recoverably) and
  `f100_cont_f062_tomb_intmap` (7200 ops). ✔
- Failure atomicity: `deep_collections_growth` `g_oom1/2/3`, `g_oom_retry`,
  `g_leak` all pass. ✔

### 3.2 intset

Same open-addressing skeleton as intmap minus `values`. `insert` returns 0 for
an already-present key (no allocation), rebuild allocates 2 buffers with
rollback, `remove` uses tombstones, `clear` resets all state to EMPTY. Verified
by `deep_collections_alloc` (intset paths), `deep_collections_collisions`, and
`f100_cont_f063_tomb_intset`. ✔

### 3.3 strmap (owned string keys)

- Owned-key lifecycle: `stdlib_copy_key` refuses `NULL` and `len >=
  STDLIB_ALLOC_LIMIT`; `remove` releases the key and NULLs the slot before
  marking DELETED; `clear` releases every FULL key; `drop` calls `clear` then
  releases the three arrays; rehash **moves** key pointers, so it cannot fail
  after its buffers are allocated. ✔
- Partial-init destruction / double destruction: constructor zero-inits;
  `drop` guards every pointer and is idempotent; `clear` iterates capacity
  slots only when `state != NULL`. ✔
- Collision / duplicate / empty-string / NULL keys: `strcmp` distinguishes
  full keys, `""` supported, `NULL` key/out refused. Verified by
  `deep_collections_growth` (`s_empty_*`, `s_oom_keycopy*`, `s_oom1/2/3`) and
  `f100_cont_f064/f065`. ✔
- Failure atomicity: logical contents and leak balance verified. One **latent,
  non-reproduced** observation: in `put`, if the 3-buffer rebuild succeeds but
  the key copy then fails, the function returns -1 with a *different capacity
  and purged tombstones* (logical contents unchanged). The header's top comment
  says "exactly as it was before the call"; the existing tests intentionally
  assert only logical contents. This is **not** a reproduced functional defect
  and was **not** changed.

### 3.4 deque (ring buffer) — concurrently modified region

Reviewed the converted arithmetic, not edited. With `head < space`,
`used <= space`: `push_back` `head+used < 2*space`; `push_front` `head+space-1
< 2*space`; `pop_back` `head+used-1 < 2*space`; `pop_front`/`at_checked`
`head+1` / `head+index <= 2*space`. One subtraction is correct in all cases.
`stdlib_ring_copy` gained a `space == 0` early return (safe: `used == 0`
in that state). `reserve(want)` only copies when `want > space >= used`, so
`want > used` and the copy cannot overrun. Growth overflow guard:
`space > (STDLIB_ALLOC_LIMIT/sizeof(int))/2` → -1. Verified by
`f100_cont_f066_deque_ring` (2600 ops) and `deep_collections_boundary`
(fill-to-8 + wrapped 9th push preserves order). ✔

### 3.5 list

Node allocated before any link is touched; `push_back`/`push_front` failure
atomic. Single-element and empty pop cases correctly maintain `first`/`last`.
`clear` walks `next` before release; `drop` == `clear` (idempotent). Verified by
`f100_cont_f067_list_churn` (2400 ops) and the `lpbn_*`/`lpfn_*` paths of
`deep_collections_alloc` (see §4 blocker — those paths were not reached in this
build). ✔ by inspection.

### 3.6 stack / queue

`stack::push` allocates before copying and releases the old buffer only after
success; overflow guard as deque. `queue::push` uses the same ring_copy path as
deque; converted `head+1` subtraction is correct. `clear` retains the buffer
and resets `head/used`; `drop` releases once. Verified by
`f100_cont_f068_stack_growth` (2300 ops), `f100_cont_f069_queue_ring` (2600
ops) and `deep_collections_boundary`. ✔

## 4. Harness results (exact evidence)

### 4.1 deep_collections_* — strict/no-sanitizer and strict-UBSan

| harness | sn build/run | sn final | ub build/run | ub final |
| --- | --- | --- | --- | --- |
| `deep_collections_boundary` | 0 / 0 | `DIGEST 141365270 2760897655`, 0 B stderr | 0 / 0 | identical |
| `deep_collections_collisions` | 0 / 0 | `RESULT 23 0`, 0 B stderr | 0 / 0 | identical |
| `deep_collections_growth` | 0 / 0 | `RESULT 32 0`, 0 B stderr | 0 / 0 | identical |
| `deep_collections_alloc` | 0 / **134** | aborted after `isf_lk2 1` | 0 / **134** | identical abort |

All four build cleanly under `--strict` in both sanitizer modes.

### 4.2 f100_cont_* — strict mode blocked, default mode green

Under `--strict --sanitize=none` (and UBSan) **all eleven** f100 configs fail to
**build**, with e.g.:

```
tests/stdlib/fuzz/f100_cont_engine.hp:347:18: error: [CPLUS-ARITH] remainder by zero cannot be excluded
      return (int)(r % (unsigned int)f100_kd);
```

plus lines 445, 842, 880, 937 of the same *test* header. `-w` does not help
(these are strict errors, not warnings). The test engine is not a file owned by
this task, so it was not modified. This is a strict-compiler/test-engine
interaction, not a collections failure.

Under default diagnostics (`--sanitize=none` and `--sanitize=undefined`) all
eleven build and run with rc=0, 0 bytes of stderr, no `INVARIANT` line and no
timeout. Digests match the stored `tests/stdlib/reports/logs/default/*`
fixtures exactly in both modes (e.g. `f061_digest 66c579bd06a75951`,
`f062_digest 18ff2416c2cc2d69`, `f065_digest eca40d87e3ab302d`,
`F-075 telemetry 364 364 126`).

| harness | dn | du | final line |
| --- | --- | --- | --- |
| f061 collide_int | 0/0 | 0/0 | `F-061 ok 2200 ops` |
| f062 tomb_intmap | 0/0 | 0/0 | `F-062 ok 7200 ops` |
| f063 tomb_intset | 0/0 | 0/0 | `F-063 ok 6908 ops` |
| f064 collide_strmap | 0/0 | 0/0 | `F-064 ok 2200 ops` |
| f065 tomb_strmap | 0/0 | 0/0 | `F-065 ok 7200 ops` |
| f066 deque_ring | 0/0 | 0/0 | `F-066 ok 2600 ops` |
| f067 list_churn | 0/0 | 0/0 | `F-067 ok 2400 ops` |
| f068 stack_growth | 0/0 | 0/0 | `F-068 ok 2300 ops` |
| f069 queue_ring | 0/0 | 0/0 | `F-069 ok 2600 ops` |
| f074 faults | 0/0 | 0/0 | `F-074 ok 2465 ops` |
| f075 balance | 0/0 | 0/0 | `F-075 ok 2600 ops` |

### 4.3 Investigated blocker: `deep_collections_alloc` abort

Symptom (identical in `sn`, `ub`, `dn` and with the **unchanged HEAD**
`collections.hp`):

```
C+ error: index 3 is outside the bounds of 'pre' (length 3)
```

It fires immediately after check `isf_lk2 1` (7 lines of stdout), i.e. at the
start of `test_intset_grow`, whose local is `int pre[5]`. Source inspection of
`tests/stdlib/deep_collections_alloc.cp` shows three same-named locals in
different functions — `pre[5]` at line 110 and `pre[3]` at lines 388 and 495 —
and every access is within its own declared bound (`i < 5` / `i < 3`). The
stored good logs (`tests/stdlib/reports/logs/{default,strict-none}/
deep_collections_alloc.stdout`) end with `ALLOC 59 0` (60 lines), so this is a
regression in the rebuilt compiler's runtime bounds metadata, not in
`collections.hp` and not in the test source. **Reported as a conflict/blocker
to the compiler agent; no collections change made.** Consequently the
intset-growth, deque/list/stack/queue allocation-failure paths inside that
harness were not executed in this build; equivalent fault coverage is provided
by `deep_collections_growth` and `f100_cont_f074_faults` (both green).

## 5. Defects and changes

- **Reproduced collections defects:** none.
- **Changes to `stdlib/collections.hp`:** none (deliberate — avoids colliding
  with the concurrent strict-compiler adaptation; no reproduced defect to fix).
- **Latent observations (not fixed, not reproduced failures):**
  1. `strmap::put`: a successful rebuild followed by a failed key copy returns
     -1 with a changed physical layout (logical contents intact).
  2. `stdlib_rebuild` (all three hash containers) is public and assumes a
     power-of-two `cap > count`; callers `grow`/`put`/`insert` uphold this
     through `stdlib_map_next_pow2` / `stdlib_map_rebuild_capacity`.

## 6. Conflicts to relay

1. Concurrent edits to `collections.hp` (modulo → conditional subtraction) are
   present and stable; they are functionally correct under the stated ring
   invariants and passing harnesses. I did not touch them.
2. Rebuilt strict compiler: (a) rejects `%` in the shared test engine
   `f100_cont_engine.hp`, blocking all eleven `f100_cont_*` strict builds;
   (b) emits runtime bounds metadata that aborts `deep_collections_alloc` on a
   same-named local `pre` from a different function, reproducing with HEAD
   collections. Both are compiler-side and outside this task's ownership.

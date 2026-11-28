# Safety audit — `stdlib/collections.hp`

Audit scope: `stdlib/collections.hp` (1051 lines) plus the committed deep fixtures
`tests/stdlib/deep_collections_{alloc,boundary,growth,collisions}.cp`, the
cross-family fixture `tests/stdlib/deep_cross_family.cp` (CF1/CF3/CF6), their
contract documents under `docs/stdlib/`, and the compile-time manifest
`tests/stdlib/compile_time/collections.json`. `stdlib/error.hp` is cited only as
the checked-allocator dependency. No file outside this document was created or
modified; **no test runner, compiler, or binary was executed for this audit**.

## Evidence vocabulary

- **[VERIFIED]** — recorded as passing by a committed contract document with
  exact stdout/exit status, and the corresponding check exists in the committed
  `.cp` source read during this audit. It is a *reported* observation, not a
  re-execution by this agent.
- **[DOCUMENTED-ONLY]** — promised by the header comment or a contract but no
  committed fixture pins the specific branch.
- **[UNTESTED]** — code path exists in the header but no committed fixture
  exercises it. Reported as a risk, never as a safety guarantee.

> **Safety-claim boundary.** Reaching `ALLOC 59 0`, `BOUNDARY 58 0`,
> `RESULT 32 0`, `RESULT 23 0` or `CROSS_FAMILY 89 0` is evidence of *contract
> conformance under the probed inputs only*. It is **not** proof of memory
> safety. ASan is unavailable on this host (`docs/stdlib/DEEP_COLLECTIONS_CONTRACT.md:176-182`;
> `docs/stdlib/TASK6_FINAL_REPORT.md:110-113`), and the probe loops are not
> instrumented — termination is inferred from `timeout 30`
> (`docs/stdlib/DEEP_COLLECTIONS_BOUNDARY_CONTRACT.md:109-111`). The libraries
> themselves state telemetry is "diagnostic information, not proof of safety"
> (`stdlib/error.hp:25-28`) and the alloc test repeats the same caveat
> (`tests/stdlib/deep_collections_alloc.cp:31-33`). No statement below should be
> read as a memory-safety proof.

**Defect count for this module:** **0 confirmed library defects fixed** in
`collections.hp` (all four collection contract documents record "no defect was
reproduced"; `docs/stdlib/DEEP_COLLECTIONS_ALLOC_CONTRACT.md:191-198`,
`DEEP_COLLECTIONS_BOUNDARY_CONTRACT.md:249-253`,
`DEEP_COLLECTIONS_CONTRACT.md:184-209`). **10 test-side defects** were found and
fixed in the test files only (4 in `deep_collections_alloc.cp`, 3 in
`deep_collections_boundary.cp`, 3 in the growth/collisions suites) — see §6.
**1 out-of-scope compiler defect** (RAII name de-duplication) is reproduced and
worked around; see §8.

---

## 1. Public API list

`stdlib/collections.hp` defines 7 owner classes. The static inventory counts
**63 public callable entries** for this header, **63 exercised, 0 unexercised**
(`docs/stdlib/REGRESSION_INVENTORY.md:52,81,110,156,181`) — a *call-site* count,
not a branch-coverage or safety measure (`REGRESSION_INVENTORY.md:6-15,232-234`).

| Class | Public methods (header lines) | Owner state |
| --- | --- | --- |
| `intmap` | `intmap`, `size`, `valid`, `grow`, `put`, `get`, `contains`, `remove`, `clear`, `drop` (`:109-292`) | `keys`, `values`, `state`, `capacity`, `count`, `used_slots` |
| `strmap` | `strmap`, `size`, `valid`, `grow`, `put`, `get`, `contains`, `remove`, `clear`, `drop` (`:296-515`) | `char **keys` (owned key copies), `values`, `state`, capacity/counters |
| `intset` | `intset`, `size`, `valid`, `grow`, `insert`, `contains`, `remove`, `clear`, `drop` (`:519-665`) | `keys`, `state`, capacity/counters |
| `deque` | `deque`, `size`, `capacity`, `reserve`, `push_back`, `push_front`, `pop_front`, `pop_back`, `at_checked`, `clear`, `drop` (`:669-781`) | ring buffer `items`, `head`, `used`, `space` |
| `list` | `list`, `size`, `push_back`, `push_front`, `pop_front`, `pop_back`, `at_checked`, `clear`, `drop` (`:791-905`) | `first`, `last`, `used`; `list_node` per element |
| `stack` | `stack`, `size`, `push`, `pop`, `top_checked`, `clear`, `drop` (`:909-974`) | `items`, `used`, `space` |
| `queue` | `queue`, `size`, `push`, `pop`, `front_checked`, `clear`, `drop` (`:978-1049`) | ring `items`, `head`, `used`, `space` |

Free helper algorithms (file-static, exercised through the classes):
`stdlib_map_hash_int` (`:37-45`), `stdlib_map_hash_text` (`:47-54`),
`stdlib_map_next_pow2` (`:57-65`), `stdlib_map_needs_room` (`:68-72`),
`stdlib_map_rebuild_capacity` (`:77-86`), `stdlib_ring_copy` (`:90-105`).
Constants `MAP_EMPTY=0`, `MAP_FULL=1`, `MAP_DELETED=2` (`:33-35`). No iterators
are exposed; all reads are by value (`:20-22`).

---

## 2. Allocation paths and exact counts asserted by tests

Every container allocates **only** through `stdlib_alloc` / `stdlib_release`
(`stdlib/error.hp:12-23,365-392,517-524`). The fixture
`tests/stdlib/deep_collections_alloc.cp` enumerates each previously uncovered
allocation point by resetting `stdlib_alloc_seq()` and reading it after a
successful probe (`:64-69`, method at `docs/stdlib/DEEP_COLLECTIONS_ALLOC_CONTRACT.md:52-77`).

| # | Allocation point (header) | Buffers | Probe tag / asserted count | Evidence |
| --- | --- | --- | --- | --- |
| 1 | `intmap::stdlib_rebuild`: `keys` `:161`, `values` `:164`, `state` `:169`; rollback `:165-174` | **3** | `g_oom1/2/3` (fail at 1/2/3); `bd_bound_intmap` rebuild delta 3 | [VERIFIED] `deep_collections_growth.cp:94-131`; `DEEP_COLLECTIONS_CONTRACT.md:45-50`; `deep_collections_boundary.cp:57-66` contract |
| 2 | `strmap::stdlib_rebuild`: `fresh_keys` `:363`, `fresh_values` `:366`, `fresh_state` `:371`; rollback `:367-375` | **3** | `s_oom1/2/3`; `bd_bound_strmap` delta 3 (+1 key copy) | [VERIFIED] `deep_collections_growth.cp:182-204`; `DEEP_COLLECTIONS_CONTRACT.md:49` |
| 3 | `strmap::stdlib_copy_key` (`stdlib_alloc(len+1)` `:330`) | **1** (4th in a grow+insert) | `s_oom_keycopy` (fail at 4), retry `s_oom_keycopy_retry` | [VERIFIED] `deep_collections_growth.cp:160-180` |
| 4 | `intset::stdlib_rebuild`: `keys` `:564`, `state` `:567`; rollback `:568-570` | **2** | `isf_probe` (cap 0→8), `isg_probe` (5 live, cap 8→16) | [VERIFIED] `deep_collections_alloc.cp:57-163`; `DEEP_COLLECTIONS_ALLOC_CONTRACT.md:33-34` |
| 5 | `deque::reserve` → `stdlib_make_room` → `stdlib_ring_copy` (`:695-696,99`) | **1** | `dpbf_probe` (push_back), `dpff_probe` (push_front), `dgw_probe` (wrapped ring, head=2) | [VERIFIED] `deep_collections_alloc.cp:165-300`; `DEEP_COLLECTIONS_ALLOC_CONTRACT.md:35-37` |
| 6 | `list::push_back` `:809`, `list::push_front` `:825` (one `list_node`) | **1** each | `lpbe_probe`, `lpbn_probe`, `lpfe_probe`, `lpfn_probe` | [VERIFIED] `deep_collections_alloc.cp:489-549` and contract `:38-41` |
| 7 | `stack::push` growth allocate/copy/swap (`:934-943`) | **1** | `stf_probe` (first push), `stg_probe` (8→16) | [VERIFIED] `deep_collections_alloc.cp:551-666`; contract `:42-43` |
| 8 | `queue::push` → `stdlib_ring_copy` (`:1004-1013`) | **1** | `qf_probe` (first push), `qgw_probe` (wrapped ring) | [VERIFIED] `deep_collections_alloc.cp:668-794`; contract `:44-45` |
| 9 | Allocator internals: `stdlib_alloc` → `cplus_alloc` (`error.hp:385`), metadata table `calloc` (`error.hp:307`), `stdlib_release` → `cplus_release` (`error.hp:523`) | n/a | `*_lk*` checks assert `alloc_count == free_count` and `alloc_failures >= 1` after `drop()` | [VERIFIED] `deep_collections_alloc.cp:97-100` et al.; `g_leak` at `deep_collections_growth.cp:206-223` |

**Exact test totals (from committed fixtures/contracts, not re-run here):**

- `tests/stdlib/deep_collections_alloc.cp` — **59 checks + final line**
  (`ALLOC 59 0`). 13 scenarios × {1 probe + count×(failure + recovery + leak
  check)} + 1 `alloc_no_fatal`; the two `intset` cases have count 2
  (7 checks each), the other 11 have count 1 (4 checks each):
  2×7 + 11×4 + 1 = **59**. Exact 60-line oracle at
  `docs/stdlib/DEEP_COLLECTIONS_ALLOC_CONTRACT.md:94-159` and
  `tests/stdlib/deep_collections_alloc.expected` (60 lines).
- `tests/stdlib/deep_collections_boundary.cp` — **58 checks** +
  `BOUNDARY 58 0` + `DIGEST 141365270 2760897655`
  (`0x086D1016A48FF477`) = 60 lines
  (`DEEP_COLLECTIONS_BOUNDARY_CONTRACT.md:120-186`,
  `deep_collections_boundary.expected`). Digest is a *determinism* oracle only,
  not correctness proof (`...CONTRACT.md:188-190`).
- `tests/stdlib/deep_collections_growth.cp` — **32 checks**, `RESULT 32 0`
  (`DEEP_COLLECTIONS_CONTRACT.md:78-114`).
- `tests/stdlib/deep_collections_collisions.cp` — **23 checks**, `RESULT 23 0`
  (`DEEP_COLLECTIONS_CONTRACT.md:116-143`).
- `tests/stdlib/deep_cross_family.cp` — CF1 (13 checks) exercises `strmap`
  owned-key copy across source `string` mutation/drop; CF3 (13 checks) exercises
  `intmap` move + generation-observed stale alias; CF6 (19 checks) includes
  `strmap::put(NULL, …)` note. Total `CROSS_FAMILY 89 0`
  (`DEEP_CROSS_FAMILY_CONTRACT.md:40-52,74-87,227-240`).

---

## 3. Input-validation matrix

| Input / state | Condition | Observable contract | Class | Evidence |
| --- | --- | --- | --- | --- |
| **NULL out-param** | `intmap::get`, `strmap::get` `out==NULL` (`:246-249,459-462`) | `-1`, no write | [VERIFIED] | `deep_collections_boundary.cp:858-861` `bd_invalid_strmap`; `deep_collections_boundary.contract:72-77,104-107` |
| NULL out-param | deque/list `at_checked`, pop_* `out==NULL` (`:756-758,840-842,873-877,949-950,1029-1030`) | `-1`, no write | [VERIFIED] | `bd_nullout_*`, `bd_invalid_*` (`deep_collections_boundary.cp:864-880`) |
| **NULL key** | `strmap::put(NULL,…)` (`:419-423`) note `INVALID/UNSUPPORTED` | `-1`, size 0, no allocation | [VERIFIED] | `g` `s_null_put`; `bd_invalid_strmap`; CF6 `cf6` note |
| NULL key | `strmap::get/contains/remove(NULL,…)` (`:459-484`) | `-1 / 0 / -1` | [VERIFIED] | `s_null_queries`, `bd_invalid_strmap`, `DEEP_COLLECTIONS_CONTRACT.md:40-43` |
| Empty key `""` | legitimate key | insert/overwrite/get/remove work, `size()` tracks | [VERIFIED] | `s_empty_put/overwrite/remove`, `DEEP_COLLECTIONS_CONTRACT.md:43-44` |
| **zero / empty container** | all 7 types, `size()==0`, hash `valid()==0`; all accessors refuse, out-param sentinel untouched; `clear()` ×3 no-op | success | [VERIFIED] | `bd_empty_*` (7 checks, `deep_collections_boundary.cp:91-144`) |
| **invalid index** | `at_checked(SIZE_MAX, &out)` on deque and list | `-1`, `BOUNDS` note, sentinel untouched | [VERIFIED] | `bd_invalid_deque/list` (`deep_collections_boundary.cp:864-872`); `collections.hp:756-766,873-887` |
| **overflow size** | `grow(SIZE_MAX)` → `next_pow2()==0` (`:57-65,203-210`) | `-1` before allocating; entries intact | [VERIFIED] | `g_grow_overflow` (`growth.cp:86-88`); `bd_maxcap_*` (`boundary.cp` contract `:49-56`) |
| overflow size | `grow(STDLIB_ALLOC_LIMIT)` rounds to 2^40, rejected by `cap > LIMIT/sizeof(slot)` (`:159`) | `-1`, zero alloc delta | [VERIFIED] | `DEEP_COLLECTIONS_BOUNDARY_CONTRACT.md:49-56` |
| overflow size | `deque::reserve(SIZE_MAX)` via `stdlib_ring_copy` `newspace > LIMIT/4` (`:94-98`) | `NULL`/`-1` with `RANGE` note, zero alloc | [VERIFIED] | `bd_maxcap_deque_reserve` (`boundary.contract:54-56`) |
| invalid state | `valid()==0` on empty/dropped map/set | documented state query | [VERIFIED] | `g_empty`, `g_dropped`, `bd_empty_*`, `bd_drop_*` |
| **repeated drop** | `drop()` twice on all 7 containers | idempotent; zero-init state; no double release | [VERIFIED] | `g_dropped` (`growth.cp:89-91`), `c_map_dropped` (`collisions.cp:139-141`), all `bd_drop_*` (`boundary.cp:795-847`) |
| **use after drop** | accessors after `drop()`, out-param sentinel preserved; later `put/insert/push` succeeds from zero-init | recoverable refusal + re-init | [VERIFIED] | `bd_drop_*` (7 checks, `boundary.contract:99-105`) |
| repeated insertion | 50 `put`/`insert` of same key, changing value | `size()==1`, last value wins, alloc delta 0 after first | [VERIFIED] | `bd_repeat_*` (`boundary.contract:67-71`) |
| missing key | `remove` absent key on empty and populated map/set | `-1`, size unchanged | [VERIFIED] | `bd_missing_maps` (`boundary.contract:72-73`) |
| tombstone reuse | cap-16 table, remove 3, insert 3 | `container_growths` unchanged; no table rebuild | [VERIFIED] | `bd_tomb_*` (`boundary.contract:78-84`) |
| collision chains | 48 keys sharing low 10 hash bits, chain surviving rehash to 1024 | terminates under `timeout 30`, all values found | [VERIFIED] | `c_*` 23 checks (`DEEP_COLLECTIONS_CONTRACT.md:57-62,116-143`) |
| **stale/dangling key** | caller frees key buffer after `strmap::put`; later `get` hashes freed memory | C+ gives **no** tagged diagnostic; runtime returns happen to be `-1`, stale read silent | [UNTESTED / compile-time GAP] | `tests/stdlib/compile_time/collections.json` `collections_dangling_key.cp` → ACCEPT |
| **use-after-move / mutable alias / double owner** | moved-from owner read, two `intmap*` aliases, `intmap b = a` | rejection exists for move/copy but **untagged** (PARTIAL); alias unchecked (ACCEPT) | [UNTESTED / compile-time GAP] | `compile_time/collections.json` cases `use_after_move` (PARTIAL), `mutable_alias` (ACCEPT) |
| concurrent mutation | simultaneous mutation from multiple threads | header says **not thread-safe**, requires external sync (`collections.hp:23-24`) | [UNTESTED / documented contract] | no fixture |

---

## 4. Failure-atomicity evidence

The header's promise is "when an operation returns -1 the container is exactly
as it was before the call, and nothing was leaked" (`collections.hp:12-15`).

- **Rebuild rollback.** All buffer allocations precede any mutation; a failed
  2nd/3rd buffer releases the partial set (`intmap :161-174`, `strmap :363-375`,
  `intset :564-570`). [VERIFIED] `g_oom1/2/3`, `s_oom1/2/3` and the
  `isf_oN/isg_oN` sequences (`DEEP_COLLECTIONS_ALLOC_CONTRACT.md:60-75`).
- **strmap key copy is last fallible step.** The rebuild may physically change
  capacity/tombstones, but "the public API observable state is unchanged"
  (`DEEP_COLLECTIONS_CONTRACT.md:50-55`); `s_oom_keycopy` proves value/size/valid
  unchanged and absent new key (`growth.cp:160-179`).
- **Ring/node growth.** `stdlib_ring_copy` returns `NULL` before touching the
  source (`:88-105`); `deque::reserve` (`:691-706`), `stack::push` (`:934-943`)
  and `queue::push` (`:1004-1013`) install the new buffer only after success.
  `list::push_*` allocates the node before changing links (`:806-838`).
  [VERIFIED] `dpbf_o1/dpff_o1/dgw_o1/stf_o1/stg_o1/qf_o1/qgw_o1` and
  `lpbe/lpbn/lpfe/lpfn` `_o1` checks (`ALLOC` oracle lines 113-156).
- **Leak check.** Telemetry enabled and reset *before* the container is built;
  after the injected failure and `drop()`, `alloc_count == free_count` and
  `alloc_failures >= 1` (`deep_collections_alloc.cp:97-100`;
  `DEEP_COLLECTIONS_ALLOC_CONTRACT.md:71-74,212-217`).
- **No wrong state after failure.** Each `_oN` check re-reads every pre-existing
  element and asserts the new element absent, plus exposed
  `size/capacity/valid` unchanged (`ALLOC_CONTRACT.md:62-70`;
  `deep_collections_alloc.cp:81-82,135-139`).
- **Retry.** After `stdlib_alloc_fail_reset()` the identical operation succeeds
  on a fresh identical container (`*_rN` checks).

---

## 5. Lifecycle and ownership

- Zero-initialised container is valid empty; each owns its storage and releases
  it exactly once in `drop()`; `drop()` is idempotent (`collections.hp:8-11`).
- Each `drop()` nulls all pointers and zeroes counters
  (`intmap :278-291`, `strmap :499-514`, `intset :654-664`,
  `deque :773-780`, `list :902-904`, `stack :967-973`, `queue :1041-1048`).
  `strmap::drop` first calls `clear()` to release each owned key (`:486-514`).
- `remove` on `strmap` releases the owned key and sets `NULL` (`:475-484`);
  `clear` releases all owned keys (`:486-497`).
- No iterators; element access by value, so nothing held by a caller dangles
  after growth (`collections.hp:20-22`). Cross-family CF1 verifies `strmap`
  key ownership against source `string` clear/realloc/drop
  (`DEEP_CROSS_FAMILY_CONTRACT.md:40-52`).
- Hash table keeps a 70% load threshold (`:68-72`) so empty slots remain and
  both `stdlib_probe` and the unbounded insert loops terminate; rebuild always
  into `cap > count` (`:154-160,356-362,558-564`).

---

## 6. Confirmed defects fixed (reproducer → fix → regression path)

**No library defect.** Every contract document explicitly records that
`collections.hp` was **not** modified
(`DEEP_COLLECTIONS_ALLOC_CONTRACT.md:5-7,191-198`;
`DEEP_COLLECTIONS_BOUNDARY_CONTRACT.md:16-18,251-263`;
`DEEP_COLLECTIONS_CONTRACT.md:14-16,184-189`).

All fixes were in the **test files only** (headers never edited to force a pass):

| # | Defect (test-side) | Reproducer | Fix | Regression path |
| --- | --- | --- | --- | --- |
| 1 | Wrong-variable method dispatch: reusing `s` for `intset` and `stack` in one function lowered `s.insert()` to `stack_insert` | strict compile error | unique container name per variable | `deep_collections_alloc.cp:37-39`; `ALLOC_CONTRACT.md:79-84,203-205` |
| 2 | `%ld` format with `int` argument in `snprintf` label | strict FFI check rejection | change to `%d` | `ALLOC_CONTRACT.md:206-208` |
| 3 | Uninitialized out-params passed by address | strict lifetime check flag | initialize all out-params to 0 | `ALLOC_CONTRACT.md:208-211`; `deep_collections_alloc.cp:167` (`v_dpbf = 0`) |
| 4 | Leak-check ordering: telemetry reset after pre-existing elements built → `free_count > alloc_count` (7 failing `*_lk*`) | 7 `*_lk*` checks failed | move reset before container build | `ALLOC_CONTRACT.md:212-217` |
| 5 | C+ safe-bounds/lifetime checker could not prove `shadow_n >= 1` after decrement through pointer | strict build failure | initialize scalars; guarded `bd_sh_back()` helper | `deep_collections_boundary.cp:81-86`; `BOUNDARY_CONTRACT.md:265-273` |
| 6 | Wrapped-deque case reused deque already at 9 elements from prior check → wrong expected size | boundary check mismatch | use fresh `bdw` deque | `BOUNDARY_CONTRACT.md:274-276` |
| 7 | Drop section shared one sentinel overwritten by an earlier successful `get` | drop check mismatch | reset sentinel before each type | `BOUNDARY_CONTRACT.md:276-278` |
| 8 | Wrong key expectation in growth suite | check failure | corrected expected key | `DEEP_COLLECTIONS_CONTRACT.md:205-209` |
| 9 | Wrong stored-value expectation | check failure | corrected expected value | same |
| 10 | Leak scenario injected failure never fired (table already grown to 256) | `g_leak` false | corrected scenario | same |

CF2/CF3/CF4 test-side fixes in `deep_cross_family.cp` (8-element array
out-of-range read, `stdlib_calloc` vs `stdlib_alloc` element-size assumption,
`file.hp` exact-fit defect) are documented in
`DEEP_CROSS_FAMILY_CONTRACT.md:68-72,75-87,140-163,242-245`; the one **library**
defect among them (`file::read_line`) belongs to `file.hp`, not this module.

---

## 7. Remaining untested paths

All are [UNTESTED] unless noted; none is claimed safe by this document.

1. `clear()` on **populated** `intset`, `deque`, `list`, `stack`, `queue`. The
   deep collections fixtures call `clear()` only on empty containers
   (`deep_collections_boundary.cp:105-142`). `collections_test.cp:31,71` covers
   populated `intmap`/`strmap` only. Node/key release on clear for the other
   types is not pinned by a deep fixture.
2. `strmap::stdlib_copy_key` with `len >= STDLIB_ALLOC_LIMIT`
   (`collections.hp:328-329`) — would require a ≥1 TiB key; only NULL and `""`
   and short keys are tested.
3. Doubling-overflow guards that return `-1` before allocation:
   `deque::stdlib_make_room` (`:715-716`), `stack::push` (`:930-931`),
   `queue::push` (`:1000-1001`) — `space > (LIMIT/sizeof(int))/2` cannot be
   reached without exhausting host memory.
4. `stdlib_map_rebuild_capacity` returning 0 on `capacity > SIZE_MAX>>2`
   (`collections.hp:81-82`).
5. Thread-safety / concurrent mutation; header explicitly delegates to external
   synchronisation (`collections.hp:23-24`), no fixture exercises contention.
6. Compile-time ownership gaps recorded in
   `tests/stdlib/compile_time/collections.json`: use-after-drop (ACCEPT),
   use-after-move (untagged → PARTIAL), dangling key (ACCEPT), mutable alias
   (ACCEPT). Only positive controls `collections_valid.cp` and
   `collections_valid_borrow.cp` are ACCEPT with clean diagnostics.
7. Branch coverage of every hash-probe/rebuild insertion under all possible
   tombstone layouts; the deterministic LCG stress
   (`boundary.cp` 6×3072 ops; `collisions.cp` 2000 ops) samples only fixed seeds
   (`BOUNDARY_CONTRACT.md:85-99`, `DEEP_COLLECTIONS_CONTRACT.md:63-65`).
8. No ASan run on this host; UBSan/bounds/null/alignment passing in three modes
   (`DEEP_COLLECTIONS_BOUNDARY_CONTRACT.md:236-247`) is not a memory-safety
   proof.

---

## 8. Compiler dependencies (C+ RAII lowering and tagged diagnostics)

- **RAII name de-duplication.** `RaiiLowering::registerOwned` keys ownership by
  variable name for a whole function, so a second owning local with the same
  name in a sibling scope gets no cleanup. The alloc test mandates unique
  container names (`deep_collections_alloc.cp:37-39`), and the contract explains
  the observed mis-dispatch/omitted-drop behaviour
  (`DEEP_COLLECTIONS_ALLOC_CONTRACT.md:79-84`). This is a **compiler** defect
  (root cause `compiler/Sema/RaiiLowering.cpp` in-tree / `c-plus-core` binary),
  reported for Agent 1, not patched here (`DEEP_THREAD_CONTRACT.md:128-145`).
  It is a **test-authoring constraint**, not a collections API guarantee.
- **Method-call lowering by owning-variable name.** The C+ compiler rewrites
  `this->method()` inside a class body; cross-object calls use lowered names
  (`stdlib/thread.hp:18-19` explains the convention). In collections this is
  why repeated variable names across types dispatch the wrong method (§6 #1).
- **Tagged diagnostics.** The compile-time manifests count only `[CPLUS-…]`
  rejections as C+ enforcement; backend warnings without the tag are never
  counted (`docs/stdlib/TASK6_FINAL_REPORT.md:84-87`). For collections,
  use-after-move/copy rejections are **untagged** → PARTIAL, and dangling-key /
  mutable-alias / use-after-drop are ACCEPT (no diagnostic)
  (`compile_time/collections.json` `gaps`). The only tagged lifetime evidence in
  this family comes from the explicit `cplus_check_live` probe
  (`[CPLUS-LIFETIME]`, noted in `collections_use_after_move.cp` entry).
- **Safe-mode checker.** Strict/lifetime checking requires out-parameters to be
  initialised before their addresses are taken (§6 #3) and cannot prove pointer
  decrements (§6 #5). `--strict` is accepted but the emitted backend command
  still carries `-DCPLUS_SAFE=1` without an extra strictness flag; the real
  difference in that configuration is `--sanitize=none` removing instrumentation
  (`DEEP_COLLECTIONS_CONTRACT.md:176-182`).
- **Alloc-injection harness dependency.** All atomicity evidence depends on the
  weak global `stdlib_state` (`stdlib/error.hp:124-125`), atomic counters
  (`__atomic_*`, `error.hp:127-145`) and `stdlib_alloc_seq/fail_at/fail_after`
  (`error.hp:226-251`). These are C+/GNU builtins, not ISO C; their correctness
  under the chosen backend is a compiler/runtime dependency, not something the
  collections header proves.
- **No ASan.** `docs/stdlib/TASK6_FINAL_REPORT.md:110-113` and
  `DEEP_COLLECTIONS_CONTRACT.md:176-178` record the backend rejecting
  `--sanitize=address`; absence of a report is not a memory-safety proof.

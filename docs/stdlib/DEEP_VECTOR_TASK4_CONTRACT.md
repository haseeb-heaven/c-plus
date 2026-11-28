# Deep contract: Task-4 vector cases and borrowed-view ownership safety

Per-family evidence for `tests/stdlib/deep_vector_task4.cp`. This test is a NEW,
independent test covering **only** the Task-4 cases not already covered by
`deep_error_vector.cp` (growth 0->4->8->16, exact/one-over reserve, SIZE_MAX and
alloc overflow shapes, empty get/pop, fault-injected reserve/push_back with state
preservation and recovery, slice freshness across non-reallocating push_back,
staleness after shape-changing mutations, alloc/free balance) and
`deep_string_view.cp` (view size/pointer/UTF-8 on live owners, invalidation via
metadata after reallocating append and drop, forked-child fatal on
`cplus_check_live`). Nothing in those two files is duplicated here.

Status at hand-off: **no confirmed defect** in `stdlib/vector.hp`,
`stdlib/string.hp` or `stdlib/error.hp`. No header was modified.

---

## 1. Scope and ownership

| Item | Path |
| --- | --- |
| Test | `tests/stdlib/deep_vector_task4.cp` |
| Contract | `docs/stdlib/DEEP_VECTOR_TASK4_CONTRACT.md` (this file) |
| Headers under test | `stdlib/vector.hp`, `stdlib/string.hp`, `stdlib/error.hp` |
| Modified headers | **none** |
| New/modified files | the two paths above only (no `git add`/commit) |

The test is self-checking, has no `.stdin` and no `.expected` file, and returns
`0` only when every check passes. Each per-check record is `<tag> <0|1>`
(`1` = check passed). The final record is `VECTOR_TASK4 <checks> <failures>`.

`STDLIB_ALLOC_LIMIT` is pinned to `4096` with a `#define` **before** any header
include, so the "push growth beyond half the element limit" boundary is
reachable with a 513-int (2052-byte) buffer. The pin is local to this
translation unit; each test is its own executable, so no other test is affected.

---

## 2. Scenario inventory (all 8 required areas)

| # | Tag | Required area | Asserted observable |
| --- | --- | --- | --- |
| 1 | `T4_INSERT_BOUNDARIES` | insert boundaries + atomicity | index 0 on empty, front, `index == size()` append, `index > size()` -> -1 with unchanged size/capacity/contents; element order preserved for every position; with `used == space`, `stdlib_alloc_fail_at(1)` makes the growth allocation fail -> -1 with capacity, size, order **and generation** unchanged (fresh slice stays valid); reset then retry succeeds (size 5, capacity 8, old slice stale). |
| 2 | `T4_ERASE_BOUNDARIES` | erase boundaries | middle erase returns correct value and preserves order; `out == NULL` accepted; last-element erase; `index >= size()` and index 99 refuse with unchanged state; repeated erases to empty then refusal on empty. |
| 3 | `T4_COPY_FROM` | copy_from | NULL source -> -1 unchanged; self-copy -> 0 no-op; overwrite shorter (`2`) and longer (`5`) sources with correct contents/capacity; failure atomicity at the single allocation via `fail_at(1)` -> -1 with size/capacity/contents/generation unchanged; retry succeeds. |
| 3b | `T4_COPY_FROM_BALANCE` | copy_from telemetry | isolated, telemetry-enabled scope: reserve/copy/shrink/drop sequence yields `alloc_count > 0` and `free_count == alloc_count`. |
| 4 | `T4_SHRINK_NOOP_AND_SUCCESS`, `T4_SHRINK_FAILED_ATOMIC`, `T4_SHRINK_TO_EMPTY` | shrink_to_fit | no-op when `space == used` (zero `stdlib_alloc_seq` delta); success reduces `capacity()` to `size()` and preserves elements (old slice stale); `fail_at(1)` leaves the larger buffer intact, readable and generation-unchanged, retry succeeds; shrink to empty releases storage (`capacity() == 0`), repeated shrink inert. |
| 5 | `T4_RESERVE_EXACT_AND_OVERFLOW`, `T4_PUSH_GROWTH_OVERFLOW` | exact capacity / overflow | `reserve(want == capacity)` no-op with allocation-sequence delta 0; `reserve(SIZE_MAX)` -> -1 with delta 0; after filling a 513-int buffer, `push_back` refuses (-1, delta 0, state intact), then a pop + push recovers. |
| 6 | `T4_NULL_INVALID_ARGS`, `T4_CLEAR_DROP_REUSE` | NULL/invalid args | `at_checked(big, NULL)`, `pop_back(NULL)`, `slice::at_checked(NULL out)`, `slice::at_checked(index >= size)` all recoverable with the caller's `out` value preserved; `at()` compat accessor valid value / 0 out-of-range; `clear()` x3, `drop()` x2 inert, then `push_back` after drop succeeds. |
| 7 | `T4_STALE_SLICE_REFUSAL`, `T4_STALE_SUB`, `T4_EMPTY_SUB` | stale slice safety, **no dereference** | after an owner `erase`, parent and sub-slice both report `stale()==1 && valid()==0` and `at_checked()==-1`; `sub()` on a stale slice returns an empty, non-stale slice; `sub()` on an empty view is empty. The stale `items` pointer is **never** read directly — only the library's refusal API is consulted. |
| 8 | `T4_STALE_VIEW_RESERVE`, `T4_STALE_VIEW_DROP` | stale string_view safety, **no dereference** | after `reserve()` reallocation and after owner `drop()`, `view.data()` still equals the captured old pointer, differs from `s.c_str()`, and `view.size()` is unchanged; `equals()`/`is_valid_utf8()` are deliberately **never** called on a stale view. |

16 per-check records + 1 summary record per run.

---

## 3. Documented vs undocumented guarantees

"Documented" means stated in the header comments of `vector.hp` / `string.hp` /
`error.hp` or in `DEEP_TESTS_HARNESS.md`. "Observed" means true of the current
implementation but not promised by the contract; the test still asserts it
because it is stable and useful, but it is **not** treated as a portability
guarantee.

| Behaviour | Class | Source / note |
| --- | --- | --- |
| Failure-atomic `reserve`, `insert`, `copy_from`, `shrink_to_fit` (-1 leaves the vector untouched, same buffer/contents/size/capacity) | **Documented** | `vector.hp` header comment and per-method comments. |
| Growth is overflow-checked; a rejected mutation is recoverable, never fatal | **Documented** | `vector.hp` header comment ("Growth is overflow-checked"). |
| `insert(index == size())` appends; `insert(index > size())` refuses | **Documented** | `insert` comment line 189 ("index == size() appends"). |
| `erase` writes `*out` only when `out != NULL` (NULL accepted) | **Documented** | `erase` comment line 212. |
| `copy_from` NULL -> -1; self-copy -> 0 no-op | **Documented** | `copy_from` comment lines 231-232 plus `stdlib_note` category INVALID. |
| `slice` invalidation: `stale()==1`, `valid()==0`, `at_checked` refuses after a shape-changing operation; `sub()` clamps and is empty when out of range | **Documented** | `vector.hp` header comment lines 9-19 and `slice` method comments. |
| `string_view` is a borrowed window "valid while its owner lives"; `data()`/`size()` are a snapshot | **Documented** (ownership lifetime; caller responsibility) | `string.hp` line 79 and `to_view()`; `DEEP_STRING_CONTRACT.md`. |
| `at()` returns 0 for an out-of-range index | **Documented** | `at` comment lines 153-154. |
| `stdlib_alloc_fail_at` / `stdlib_alloc_seq` / telemetry counters are test aids | **Documented** | `error.hp` comments lines 226-251, 69-95. |
| Failed `at_checked`/`slice::at_checked` leaves caller's `out` value unchanged | **Observed** | Code returns before writing `*out`; not spelled out in a comment. The test uses explicit sentinels to pin it. |
| `reserve(want == capacity)` and `reserve(SIZE_MAX)` perform zero allocation attempts (seq delta 0) | **Observed** | Follows from the early returns in `reserve`; "zero allocation" is a test-aid observable, not a container promise. |
| Exact refusal threshold `space > (STDLIB_ALLOC_LIMIT / sizeof(int)) / 2` in `push_back` | **Observed (implementation detail)** | Only "growth is overflow-checked" is documented; the exact 1/2 factor is not. Reached here with the pinned 4096-byte limit. |
| `copy_from` sets `capacity()` to the source's `size()`; `shrink_to_fit` sets `capacity()` to `size()` | **Observed** | Code sets `space = other->used` / `space = used`; header only promises the failure-atomicity and buffer validity. |
| `drop()` idempotent; `push_back` after `drop()` re-initialises storage; repeated `clear()` is safe | **Observed** | Follows from NULL/zero state after `drop`/`clear`. |
| `sub()` on a stale slice returns an empty, **non-stale** slice (generation_ref NULL) | **Observed** | `slice::sub` returns `slice(NULL,0)` on `stale()`. Consistent with invalidation; not explicitly documented. |
| New buffer allocated **before** the old one is released (so stale view pointer != `c_str()`) | **Observed** | `string::reserve`/`vector::reserve` allocate `fresh` first. Not a documented guarantee. |
| `string_view` has **no** generation tracking; staleness is caller responsibility, unlike `vector::slice`'s library-enforced refusal | **Observed (API-surface fact)** | `string_view` stores only `chars` + `count`; there is no `generation_ref`. The header promises validity only "while its owner lives". This is the key contrast documented for area 8. |

No undocumented behaviour was relied upon as a safety guarantee: even the
"Observed" rows are asserted only as stable current behaviour, and the safety
claims (areas 7 and 8) rest entirely on the **documented** slice refusal API and
on pointer/size comparisons that never dereference stale memory.

---

## 4. Exact commands and results

Run from the repository root with the checked-in `build/c+` driver. Flags are
separate argv entries (zsh does not word-split a single `$flags` string).
**STRICT `--sanitize=none` was run first**, then default, then UBSan, each build
and run wrapped in `timeout 30`.

```sh
# 1) strict, sanitizers explicitly off  (RUN FIRST)
timeout 30 ./build/c+ build tests/stdlib/deep_vector_task4.cp \
  --strict --sanitize=none -o build/stdlib-tests/deep_vector_task4.strict_none
timeout 30 ./build/stdlib-tests/deep_vector_task4.strict_none < /dev/null

# 2) default (Safe mode)
timeout 30 ./build/c+ build tests/stdlib/deep_vector_task4.cp \
  -o build/stdlib-tests/deep_vector_task4.default
timeout 30 ./build/stdlib-tests/deep_vector_task4.default < /dev/null

# 3) strict + explicit UBSan
timeout 30 ./build/c+ build tests/stdlib/deep_vector_task4.cp \
  --strict --sanitize=undefined -o build/stdlib-tests/deep_vector_task4.ubsan
timeout 30 ./build/stdlib-tests/deep_vector_task4.ubsan < /dev/null

# harness integration (picks up deep_*.cp automatically)
timeout 30 sh tests/stdlib/run_deep_tests.sh deep_vector_task4
```

Result summary:

| Mode (order run) | Build rc | Run rc | stderr | Final line | Result |
| --- | --- | --- | --- | --- | --- |
| `--strict --sanitize=none` (first) | 0 | 0 | empty | `VECTOR_TASK4 16 0` | PASS |
| default | 0 | 0 | empty | `VECTOR_TASK4 16 0` | PASS |
| `--strict --sanitize=undefined` | 0 | 0 | empty | `VECTOR_TASK4 16 0` | PASS |
| harness `run_deep_tests.sh deep_vector_task4` | — | — | — | 3 passed, 0 failed | PASS |

`--sanitize=address` is not used here (rejected by this host's driver probe;
ASan is delegated to the Linux CI path per `DEEP_TESTS_HARNESS.md` §3.3).

### Exact stdout (byte-identical in all three modes)

```
T4_INSERT_BOUNDARIES 1
T4_ERASE_BOUNDARIES 1
T4_COPY_FROM 1
T4_COPY_FROM_BALANCE 1
T4_SHRINK_NOOP_AND_SUCCESS 1
T4_SHRINK_FAILED_ATOMIC 1
T4_SHRINK_TO_EMPTY 1
T4_RESERVE_EXACT_AND_OVERFLOW 1
T4_PUSH_GROWTH_OVERFLOW 1
T4_NULL_INVALID_ARGS 1
T4_CLEAR_DROP_REUSE 1
T4_STALE_SLICE_REFUSAL 1
T4_STALE_SUB 1
T4_EMPTY_SUB 1
T4_STALE_VIEW_RESERVE 1
T4_STALE_VIEW_DROP 1
VECTOR_TASK4 16 0
```

---

## 5. Findings — no header defect

During development one check (`T4_NULL_INVALID_ARGS`) failed in the first
strict/no-sanitize run (`VECTOR_TASK4 16 1`). Root cause was a **test-expectation
mistake**, not a `vector.hp` defect: a refused `at_checked(index, &out)` or
`slice::at_checked(index, &out)` returns `-1` *without writing `*out`*, but the
initial assertion expected `out` to have been set to `33`. The header behaves
correctly (it preserves the caller's value on refusal); the test was corrected
to set an explicit `4242` sentinel and assert it is preserved. Re-running all
three modes produced `VECTOR_TASK4 16 0` with empty stderr.

No minimal repro of a real defect exists to report: every requested Task-4
status, capacity, size, element value, generation/slice state, telemetry
counter, refusal, and pointer/size snapshot matched the contract in default,
strict/no-sanitize and UBSan builds. No header was edited to force a pass.

| Item | Count / value |
| --- | --- |
| Per-check records per run | 16 (+1 summary) |
| Fully passing records per run | 17 |
| Confirmed header defects | **0** |
| Header files modified | **0** |
| Build/run modes verified | 3 (+ harness invocation) |
| Runs with sanitizer diagnostics or C+ runtime fatal | 0 |

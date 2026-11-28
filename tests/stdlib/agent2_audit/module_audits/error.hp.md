# Safety audit — `stdlib/error.hp`

Audit scope: `stdlib/error.hp` (626 lines) plus the committed deep fixtures
`tests/stdlib/deep_error_{task5,vector}.cp`, the cross-family fixture
`tests/stdlib/deep_cross_family.cp` (CF2/CF3/CF6), their contract documents under
`docs/stdlib/`, and the compile-time manifest
`tests/stdlib/compile_time/error.json`. `collections.hp` and `vector.hp` are
cited only as consumers of the checked allocator / error record. No file outside
this document was created or modified; **no test runner, compiler, or binary was
executed for this audit**.

## Evidence vocabulary

- **[VERIFIED]** — recorded as passing by a committed contract document with
  exact stdout/exit status, and the corresponding check exists in the committed
  `.cp` source read during this audit. Reported, not re-executed.
- **[DOCUMENTED-ONLY]** — promised by a header comment or contract but no
  committed fixture pins the specific branch.
- **[UNTESTED]** — code path exists in the header with no committed fixture.

> **Safety-claim boundary.** `ERROR_TASK5 22 0` and `DEEP_ERROR_VECTOR 1 F`
> (a *summary marker*, not a fatal invocation — `DEEP_ERROR_VECTOR_CONTRACT.md:172-176`)
> are contract-conformance evidence for the probed inputs only, **not proof of
> memory safety**. ASan is unavailable on this host
> (`docs/stdlib/TASK6_FINAL_REPORT.md:110-113`;
> `DEEP_ERROR_VECTOR_CONTRACT.md:147-150`). The header itself says telemetry is
> "diagnostic information, not proof of safety" (`error.hp:25-28`). Fatal checks
> deliberately excluded from these tests belong to `tests/runtime_fail/` and the
> forked-child pattern (`DEEP_ERROR_VECTOR_CONTRACT.md:178-195`).

**Defect count for this module:** **0 confirmed library defects fixed** —
`docs/stdlib/DEEP_ERROR_TASK5_CONTRACT.md:8` ("Status: no defect found.
`stdlib/error.hp` was not modified") and
`docs/stdlib/DEEP_ERROR_VECTOR_CONTRACT.md:8-9,205-217` (0 confirmed defects, 0
headers modified). **0 test-side defects** are recorded for the task-5 suite;
one **documented SKIP** exists (`err_code_name_mapping`). Two compile-time
capability gaps are recorded for this header (see §8).

---

## 1. Public API list

`stdlib/error.hp` defines value types, a checked allocator, telemetry and
diagnostic hooks. The static inventory counts **9 public callable entries** for
this header, 9 exercised, 0 unexercised
(`docs/stdlib/REGRESSION_INVENTORY.md:53,82,112,157,183`) — the inventory lists
only `error.*` methods plus `error_reset`; the `static inline` allocator and
telemetry helpers below are also callable and are exercised through consumers.

| Group | Symbols (header lines) | Contract |
| --- | --- | --- |
| Status constants | `STDLIB_OK=0`, `STDLIB_EOF=1`, `STDLIB_FAIL=-1` (`:39-41`) | return vocabulary |
| Categories | `STDLIB_CATEGORY_OK/INVALID/RANGE/IO/MEMORY/BOUNDS/OWNERSHIP/LIFECYCLE/UTF8` (`:43-53`); `STDLIB_ERROR_SIZE=256` (`:55`) | stable ABI codes |
| Allocation limit | `STDLIB_ALLOC_LIMIT` (`:57-67`): 1<<40 on 64-bit, SIZE_MAX>>1 on 32-bit; overridable | hard request ceiling |
| Telemetry | `STDLIB_TM_*` (`:71-81`), `stdlib_telemetry_stats` (`:83-94`), `stdlib_tm_add` (`:127-131`), `set_enabled/is_enabled/reset/snapshot` (`:133-161`) | atomic counters, opt-in |
| Diagnostics | `stdlib_error_callback` typedef (`:96-100`), `stdlib_set_error_callback` (`:163-165`), `stdlib_category_name` (`:167-190`), `stdlib_note` (`:192-222`) | category/code/static message only, never buffer contents; per-thread reentrancy guard at `:196` |
| Checked allocator | `stdlib_alloc_fail_at/fail_after/reset/alloc_seq` (`:226-251`), `stdlib_alloc` (`:365-392`), `stdlib_calloc` (`:395-412`), `stdlib_release` (`:517-524`) | NULL on failure, overflow-checked, injection |
| Metadata / ownership | `stdlib_meta_find/record/forget` (`:273-362`), `stdlib_alloc_size/elem_size/length/generation/check_generation/query_length` (`:415-492`), `stdlib_transfer_ownership` (`:494-515`), `STDLIB_META_TOMB` (`:253-254`) | length/generation queries, guarded transfer |
| `error` class | ctor, `ok/failed_now/code/what/category/clear/set/set_category` (`:532-579`) | plain value type; copy and move permitted (`DEEP_ERROR_TASK5_CONTRACT.md:59-65`) |
| Free helpers | `error_reset` (`:581-589`), `error_fail` (`:591-605`), `error_fail_cat` (`:607-624`) | null-safe; method sugar on parameters is not applied (header `:8-10`) |

---

## 2. Allocation paths and exact counts asserted by tests

`error.hp` performs no allocation in the error-record path (`stdlib_note` at
`:192-222` "allocates nothing, formats nothing"; `error_fail_cat` at `:607-624`
only copies bytes). All dynamic memory comes from the checked allocator:

| Path | Header lines | Count / assertion | Evidence |
| --- | --- | --- | --- |
| `stdlib_alloc(bytes)`: zero→1, limit check, injection (`fail_at`/`fail_from`), `cplus_alloc`, metadata record, telemetry | `:365-392` | `bytes==0`→1; `bytes > LIMIT` → NULL with `MEMORY` note; injected failure → NULL. `seq` counted per attempt (`:377`) | [VERIFIED] `deep_error_vector.cp` V6 (`stdlib_alloc(SIZE_MAX)` → NULL); `DEEP_ERROR_VECTOR_CONTRACT.md:51, V6`; `deep_collections_alloc.cp` `*_oN` injection paths |
| `stdlib_calloc(count, size)`: overflow check `size > LIMIT/count`, total, `memset`, metadata elem_size | `:395-412` | `calloc(SIZE_MAX,2)` and `calloc(SIZE_MAX/2+1,2)` → NULL | [VERIFIED] `deep_error_vector.cp` V6 (`DEEP_ERROR_VECTOR_CONTRACT.md:51`); CF3 uses `stdlib_calloc(8,sizeof(int))` and asserts `stdlib_alloc_length()==8` (`DEEP_CROSS_FAMILY_CONTRACT.md:76-87`) |
| Metadata table growth: `calloc(ncap, sizeof(slot))`, rehash, `free(old)` | `:289-323` | ncap starts 64; doubles when live×2 >= cap; guard `ncap <= SIZE_MAX/4/sizeof(slot)`. No direct count fixture | [UNTESTED for rehash/overflow] — exercised only indirectly by every `stdlib_alloc` |
| `stdlib_release`: metadata forget, `FREE` counter, `cplus_release` | `:517-524` | `alloc_count == free_count` after `drop()` in collection/vector tests | [VERIFIED] `deep_collections_alloc.cp:97-100` `*_lk*`; `deep_error_vector.cp` V7 (`DEEP_ERROR_VECTOR_CONTRACT.md:52, V7`); CF2 `CROSS_FAMILY` final balance (`DEEP_CROSS_FAMILY_CONTRACT.md:63-66`) |
| `stdlib_transfer_ownership`: validates handles, bumps generation, nulls source | `:494-515` | second transfer from NULL source → `-1`; alias generation stale | [VERIFIED] CF3 (`DEEP_CROSS_FAMILY_CONTRACT.md:74-87`; `deep_cross_family.cp:282-306`) |

**Exact test totals (from committed fixtures/contracts, not re-run here):**

- `tests/stdlib/deep_error_task5.cp` — **22 checks** (21 executed checks + 1
  documented SKIP `err_code_name_mapping`), final `ERROR_TASK5 22 0`, 24 stdout
  lines including `SKIP_REASON`
  (`DEEP_ERROR_TASK5_CONTRACT.md:24-49,67-96`;
  `tests/stdlib/deep_error_task5.expected`, 24 lines).
- `tests/stdlib/deep_error_vector.cp` — 5 `error` scenarios (E1–E5) + 7 `vector`
  scenarios (V1–V7) = **12 records + 1 summary**, stdout
  `DEEP_ERROR_VECTOR 1 F` (`DEEP_ERROR_VECTOR_CONTRACT.md:152-168,199-208`).
  The trailing `F` is the fatal-class *summary marker*, not an executed fatal.
- CF6 in `deep_cross_family.cp` — 19 checks: 9 notes → `api_failures==9`,
  callback calls 9, `invalid_ops==6`, `bounds_violations==1`, `alloc_failures==1`,
  `ownership_violations==1`, `lifecycle_failures==0`, category bitmask contains
  INVALID/BOUNDS/MEMORY/OWNERSHIP/IO
  (`DEEP_CROSS_FAMILY_CONTRACT.md:121-138,223-225`).

---

## 3. Input-validation matrix

| Input / state | Condition | Observable contract | Class | Evidence |
| --- | --- | --- | --- | --- |
| **message length 0/1/254/255** | ≤ 255 bytes | exact copy, `strlen` = n, `text[255]=='\0'` | [VERIFIED] | `deep_error_task5.cp:55-84` `err_bound_0/1/254/255`; `DEEP_ERROR_TASK5_CONTRACT.md:28-31` |
| message length **256/257** | > payload 255 | exact 255-byte prefix, NUL at index 255 | [VERIFIED] | `err_bound_256/257`; `DEEP_ERROR_VECTOR_CONTRACT.md:36,58-72` (255/256/288) |
| **NULL message** | `error_fail_cat(..., NULL)` | `what()==""` | [VERIFIED] | `deep_error_vector.cp:84-87` E1; `DEEP_ERROR_VECTOR_CONTRACT.md:36,69` |
| stale tail | 255-byte `Q` then `"s"` | reads back exactly `"s"`, no stale tail | [VERIFIED] | `err_no_stale_tail` (`deep_error_task5.cp:86-101`); E2 |
| **NULL error target** | `error_reset/fail/fail_cat(NULL,…)` | no crash; note path still runs (`api_failures>=2`) | [VERIFIED] | `deep_error_vector.cp:128-142` E3; `DEEP_ERROR_VECTOR_CONTRACT.md:38, E3` |
| **NULL callback** | `stdlib_set_error_callback(NULL)` then `error_fail` | no crash, error recorded | [VERIFIED] | `err_null_callback` (`deep_error_task5.cp:104-110`) |
| **reentrant callback** | callback calls `error_fail` | callback runs once, outer record wins; no stuck `reporting` latch on a later failure | [VERIFIED] | `err_reentrant_no_recursion/no_latch` (`deep_error_task5.cp:112-133`); `DEEP_ERROR_TASK5_CONTRACT.md:51-57` |
| reentrant telemetry | 2 outer + 2 suppressed nested notes | `api_failures==4`, `alloc_failures==2`, `invalid_ops==1` | [VERIFIED] | `err_reentrant_telemetry` (`deep_error_task5.cp:135-140`) |
| **category mapping** | every exit code through `error_fail` | IO→IO, RESOURCE→MEMORY, BOUNDS→BOUNDS, OWNERSHIP→OWNERSHIP, USE_AFTER_FREE/DOUBLE_FREE→LIFECYCLE, NULL_POINTER/TYPE_CONFUSION/UNSUPPORTED/OK→INVALID (default arm) | [VERIFIED] | `deep_error_vector.cp:144-207` E4; `DEEP_ERROR_VECTOR_CONTRACT.md:74-91` |
| category override | `error_fail_cat` stores caller category verbatim | RANGE with BOUNDS code; UTF8 with IO code | [VERIFIED] | E4 (`deep_error_vector.cp:181-187`) |
| **invalid category/code** | `error_fail_cat(999,7777,…)`, `(-5,-1,…)` | stored verbatim, no crash | [VERIFIED] | `err_invalid_category_code`, `err_negative_category` (`deep_error_task5.cp:205-216`) |
| invalid exit code | `error_fail((cplus_exit_code)4242,…)` | default INVALID category, code 4242 | [VERIFIED] | `err_invalid_exit_code` (`deep_error_task5.cp:217-223`) |
| unknown category name | `stdlib_category_name(999)` / `(-5)` | `"unknown"` | [VERIFIED] | `err_unknown_name`; E4 lists all nine names + fallback |
| **no exit-code→name API** | requested mapping | SKIP with reason | [VERIFIED (documented SKIP)] | `t5_skip` (`deep_error_task5.cp:224-228`); `DEEP_ERROR_TASK5_CONTRACT.md:98-102` |
| **overflow allocation** | `stdlib_alloc(SIZE_MAX)`, `calloc(SIZE_MAX,2)`, `calloc(SIZE_MAX/2+1,2)` | NULL, no OOB, vector still readable | [VERIFIED] | V6 (`DEEP_ERROR_VECTOR_CONTRACT.md:51`) |
| **injected allocation failure** | `stdlib_alloc_fail_after(0)` | `reserve(64)`/`push_back` → `-1`, state unchanged, `alloc_failures==1`; reset → success | [VERIFIED] | V4 (`DEEP_ERROR_VECTOR_CONTRACT.md:49`); CF2 (`deep_cross_family.cp:196-225`) |
| repeated reset/fail | 1000× `error_reset`+`error_fail(IO)` | `api_failures==1000`, `invalid_ops==1000`, final IO/109; final reset clean | [VERIFIED] | `err_1000_cycles`, `err_1000_reset_clean` (`deep_error_task5.cp:147-173`) |
| **copy / move of `error`** | `error b = a`, `cplus_move(a)` | copy independent; move transfers state and zeroes source (`ok()==1`) | [VERIFIED] | `err_copy_state/independent/move_state` (`deep_error_task5.cp:175-198`); `DEEP_ERROR_TASK5_CONTRACT.md:59-65` |
| method call on NULL/released `error *` | `e->ok()` after `cplus_release(e)` or on NULL | lowers to a free-function call and is **NOT** checked; direct member access **is** tagged `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME]` | [UNTESTED / compile-time GAP] | `tests/stdlib/compile_time/error.json` cases `error_invalid_lifecycle`, `error_null_pointer` residual notes |
| stale state after `error_reset` | read fields after reset | compiler tracks no reset-state; compiles clean | [UNTESTED / compile-time GAP] | `compile_time/error.json` `error_use_after_reset.cp` → ACCEPT |
| concurrent callback / telemetry | multi-thread `stdlib_note` | `reporting` is `static __thread`; counters atomic; no fixture | [UNTESTED] | `error.hp:196,127-145` |

---

## 4. Failure-atomicity evidence

- **`stdlib_note` ordering.** Counters first, then callback; allocates nothing,
  formats nothing, drops reentrant reports (`error.hp:192-222`). The outer
  `error_fail_cat` writes fields *after* `stdlib_note` returns, so a nested
  failure is counted but the outer record wins — asserted, not treated as a
  latch defect (`DEEP_ERROR_TASK5_CONTRACT.md:51-57`;
  `deep_error_task5.cp:118-140`).
- **`error_fail_cat` truncation is bounded.** Loop stops at
  `i+1 < STDLIB_ERROR_SIZE` (`:618-622`), so payload is 255 bytes and
  `text[255]` is always NUL; no off-by-one observed in default, strict/no-sanitize
  or UBSan modes (`DEEP_ERROR_VECTOR_CONTRACT.md:58-72`).
- **Allocation failure is recoverable and non-mutating.** `stdlib_alloc` returns
  NULL on limit/injection without calling `cplus_alloc` (`:372-384`);
  `stdlib_calloc` rejects overflowing products before allocating (`:399-403`).
  Consumers keep pre-state on `-1` — V4 (`DEEP_ERROR_VECTOR_CONTRACT.md:49`),
  CF2 (`DEEP_CROSS_FAMILY_CONTRACT.md:54-66`), and collection `*_oN` checks.
- **Balanced accounting.** With telemetry reset at the *start* of a scenario,
  `alloc_count == free_count` after all `drop()` calls
  (`deep_collections_alloc.cp:97-100`; V7; CF2). CF2 explicitly notes that
  resetting *after* allocations would erase matches and produce a false
  imbalance (`DEEP_CROSS_FAMILY_CONTRACT.md:63-66`).
- **Ownership transfer is validated.** `stdlib_transfer_ownership` rejects NULL
  handles, non-empty destination, `dst==src`, and a source that is not a live
  block (`:494-509`); CF3 pins the success path and a rejected second transfer
  from the now-NULL source (`deep_cross_family.cp:282-306`).

---

## 5. Lifecycle and ownership

- `error` is a plain class with a 256-byte inline `char text[]` and no owned
  resource; C+ therefore permits copy construction and `cplus_move()`
  (`DEEP_ERROR_TASK5_CONTRACT.md:59-65`). Move zeros the source.
- Free helpers are the supported null-safe API; method-call sugar on parameters
  is not applied, so `error_reset(err)` etc. work inside class methods
  (`error.hp:8-10`).
- `stdlib_state` is a weak definition — one shared instance per program
  (`error.hp:124-125`); counters use `__atomic_*` with relaxed ordering
  (`:127-161`), callback storage uses RELEASE/ACQUIRE (`:163-165,216`).
- `stdlib_meta_*` uses a spin lock `meta_lock` (`:256-263`), tombstone reuse
  (`STDLIB_META_TOMB`, `:253-254`) and per-block generation counters
  (`:329,341,510`). `stdlib_release` is NULL-safe (`:517-520`); blocks must
  never be handed to raw `free()` and raw `malloc()` blocks must never be
  released through it (`:22-23`).
- `stdlib_alloc_fail_at/after/reset/seq` (`:226-251`) are test-only injection
  controls that manipulate the same global state; tests must reset them to
  avoid cross-test interference.

---

## 6. Confirmed defects fixed (reproducer → fix → regression path)

**No defect in `stdlib/error.hp` was fixed by these tasks.** Both contracts
state the header was left untouched
(`DEEP_ERROR_TASK5_CONTRACT.md:8,16`;
`DEEP_ERROR_VECTOR_CONTRACT.md:8-9,205-217`). The task-5 contract records no
test-side defect either; its only non-pass scenario is a documented SKIP.

| Observation | Why it is **not** a defect | Where pinned |
| --- | --- | --- |
| `error_fail(CPLUS_EXIT_OK, …)` marks the record failed with `INVALID` | Documented default arm of the mapping switch; caller misuse, not changed | `DEEP_ERROR_VECTOR_CONTRACT.md:93-98` |
| `stdlib_note` buckets `RANGE`, `IO`, `OK`, `INVALID` and unknown categories under `invalid_ops` | Documented behaviour; tests rely only on `api_failures`/`alloc_failures` | `DEEP_ERROR_VECTOR_CONTRACT.md:99-102` |
| Nested failure increments telemetry but does not re-invoke callback; outer record wins | Documented `reporting` guard + write ordering, asserted as contract | `DEEP_ERROR_TASK5_CONTRACT.md:51-57` |
| No exit-code→name helper | API does not exist; SKIP with reason, not a silent pass | `DEEP_ERROR_TASK5_CONTRACT.md:98-102` |
| Earlier session added `error_reset`/`error_fail` free helpers | Header comment `:8-10` documents the rationale (method sugar not applied on parameters); not tied to a reproduced safety defect in these contracts | `error.hp:8-10` |

The one **library** defect fixed by the cross-family work (`file::read_line`
exact-fit handling, `DEEP_CROSS_FAMILY_CONTRACT.md:140-163`) is in `file.hp`, not
`error.hp`; its error *category* assertions (`BOUNDS` / `CPLUS_EXIT_BOUNDS`) are
CF4 evidence for the error record, not a change to this header.

---

## 7. Remaining untested paths

All [UNTESTED] unless noted.

1. Metadata-table rehash and overflow guard
   (`stdlib_meta_record` `:298-322`: `ncap` doubling and
   `ncap <= SIZE_MAX/4/sizeof(slot)` check). Exercised only indirectly; no
   fixture pins generation/rehash behaviour at scale.
2. `stdlib_alloc_size`, `stdlib_alloc_elem_size`, `stdlib_alloc_query_length`,
   `stdlib_alloc_check_generation` with mismatched `elem_size`
   (`:415-492`). CF3 uses `stdlib_alloc_length`, `stdlib_generation`,
   `check_generation` and `transfer_ownership`; the other query helpers and the
   `elem_size==0` fallback branches lack dedicated fixtures.
3. `stdlib_transfer_ownership` rejection branches with a **non-NULL but dead**
   source pointer (`:503-509`, "source is not a live block") and `dst==src`
   (`:496-500`). CF3 covers only the NULL-source retry.
4. `stdlib_telemetry_snapshot(NULL)` no-op (`:148-150`) and
   `stdlib_category_name` for categories outside the switch — covered for 999
   and -5, not exhaustively.
5. Multi-threaded callback/reentrancy: `reporting` is `static __thread`
   (`:196`), so cross-thread callback behaviour and contention on `meta_lock`
   are not exercised by any committed fixture.
6. Compile-time gaps in `tests/stdlib/compile_time/error.json`:
   `error_use_after_reset` (ACCEPT), plus the residual notes that method calls on
   NULL/released `error *` lower to free functions and are unchecked while
   direct member access is tagged `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME]`.
7. Fatal paths deliberately excluded: calling an `error` method through a null
   receiver, passing a non-stdlib pointer to `stdlib_release`, and passing a
   non-NUL-terminated buffer to `error_fail`
   (`DEEP_ERROR_VECTOR_CONTRACT.md:178-195`).
8. No ASan on this host; UBSan/bounds/null/alignment passing is not a
   memory-safety proof (`TASK6_FINAL_REPORT.md:110-113`).

---

## 8. Compiler dependencies (C+ RAII lowering and tagged diagnostics)

- **Weak symbol and GNU builtins.** `stdlib_state` uses
  `__attribute__((weak))` (`error.hp:124-125`) so every TU shares counters;
  telemetry/injection rely on `__atomic_load_n`, `__atomic_store_n`,
  `__atomic_add_fetch`, `__atomic_exchange_n` and `__thread` (`:127-161,196,377`).
  These are compiler/backend extensions, not ISO C, and are a dependency of
  every allocation-failure assertion in the suite.
- **Tagged diagnostics are the enforcement gate.** The compile-time manifests
  count only `[CPLUS-…]` rejections; untagged rejections are PARTIAL and backend
  warnings are never C+ enforcement (`docs/stdlib/TASK6_FINAL_REPORT.md:84-87`).
  For `error.hp`, `error_invalid_lifecycle` and `error_null_pointer` are tagged
  `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME]` REJECT for *direct member access*,
  but the manifest explicitly notes that method-call sugar (`e->ok()`) lowers to
  a free-function call and is **not** checked; `error_use_after_reset` is an
  ACCEPT capability gap. Positive controls `error_valid.cp` and
  `error_valid_reset.cp` compile clean.
- **Value-type copy/move is a compiler classification.** `error` is a plain
  class with no owned resource, so C+ permits both `error b = a` and
  `cplus_move(a)`; owning classes such as `json`/`file` have implicit copies
  rejected (`DEEP_ERROR_TASK5_CONTRACT.md:59-65`). The task-5 copy/move checks
  depend on this classification.
- **RAII / method lowering constraints in fixtures.** Out-parameters must be
  initialized before address-taking, and owning locals need unique names per
  function because `RaiiLowering::registerOwned` de-duplicates by name
  (`DEEP_COLLECTIONS_ALLOC_CONTRACT.md:79-84`;
  `DEEP_THREAD_CONTRACT.md:128-145`). These are compiler behaviours the tests
  work around, not guarantees of `error.hp`.
- **Strict-mode semantics.** `--strict` is the fail-closed contract mode
  (`docs/stdlib/DEEP_TESTS_HARNESS.md:140`), but the checked-in driver's emitted
  backend command still carries `-DCPLUS_SAFE=1` without an extra strictness flag;
  `--sanitize=none` merely removes instrumentation
  (`DEEP_COLLECTIONS_CONTRACT.md:176-182`). Driver flags must be passed as
  separate argv operands under zsh (`DEEP_ERROR_VECTOR_CONTRACT.md:110-115`).
- **No ASan** on this host; this family explicitly delegates ASan-class checks
  to Linux CI (`DEEP_ERROR_VECTOR_CONTRACT.md:147-150`), so absence of a report
  is not a memory-safety proof.

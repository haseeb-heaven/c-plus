# Function-by-function inventory: memory / container APIs

**Task:** Task 2 Phase 1 — function-by-function inventory of memory/container APIs.
**Branch / snapshot:** `agent/modern-stdlib`, 2026-11-23.
**File ownership:** this document only. No header, test, source, or git-index entry was created or modified by this author.

## 0. Scope, method, and reading rules

### 0.1 Files inspected (headers)

| Header | Role | Lines |
|---|---|---|
| `stdlib/error.hp` | `error` object, telemetry, failure notes, checked allocator + side table | 1–695 |
| `stdlib/memory.hp` | bounds-aware guarded byte operations and borrow/own helpers | 1–227 |
| `stdlib/string.hp` | `stdlib_utf8_validate`, `string_view`, `string` | 1–400 |
| `stdlib/vector.hp` | `slice`, `vector` | 1–308 |
| `stdlib/collections.hp` | `intmap`, `strmap`, `intset`, `deque`, `list`, `stack`, `queue` + helpers | 1–1083 |

Language/lowering semantics cited where relevant: `docs/LANGUAGE.md:160-200` (constructors, `drop()` destructor, no access control), `docs/stdlib/abi-preserving-runtime-safety.md:38-154,156-260` (metadata, ABI).

### 0.2 Files inspected (tests and evidence artifacts)

Primary fixtures: `tests/stdlib/deep_memory_bounds.{cp,expected}`, `deep_error_vector.{cp,expected}`, `deep_error_task5.{cp,expected}`, `deep_string_task4.{cp,expected}`, `deep_string_append.{cp,expected}`, `deep_string_view.{cp,expected}`, `deep_vector_task4.{cp,expected}`, `deep_collections_{alloc,boundary,growth,collisions}.{cp,expected}`, `deep_cross_family.{cp,expected}`, `containers_safety_test.{cp,expected}`, `collections_test.{cp,expected}`, `core_extras_test.{cp,expected}`, `safety_regressions.{cp,expected}`, `foundation.{cp,expected}`, `negative.{cp,expected}`.
Additional evidence: `tests/stdlib/regressions/*.cp`, `tests/stdlib/fuzz/f100_*.cp`, `tests/stdlib/agent2/*.cp` (+ `.expected`), `tests/stdlib/compile_time/*.cp`.
Coverage caveats adopted verbatim from `tests/stdlib/agent2/API_COVERAGE_REPORT.md:64-84,179-185` and `docs/stdlib/COVERAGE_TOOL_LIMITATIONS.md`: static call-site presence is **not** per-API execution, and function-level runtime attribution for `.hp` headers is unavailable on this host.

### 0.3 Evidence labels (three, and only three, statuses)

- **[D] DOCUMENTED** — guaranteed by an explicit header comment/contract at the cited `path:line`. This is a claim in the source, not a measurement.
- **[O] OBSERVED** — a checked-in test source asserts a result at a named tag and a checked-in `.expected` fixture pins that tag to `1`/a value. I write `direct` when the test calls the API, `indirect` when it asserts an effect produced through another entry point. This is behavioural evidence for that scenario only (no safety proof, no per-API execution count).
- **[U] UNTESTED** — no direct call/reference and no asserted effect was found in the inspected test tree. Where a header claim exists it is still recorded under [D]; [U] only says no test evidence was found.

An API can carry both [D] and [O]. Every "Evidence:" line lists all three slots, marking `--` where none was found. **No percentage is computed or invented anywhere in this document**; every figure is a count with its denominator.

### 0.4 Global rules referenced by per-API "Thread:" and "ABI:" lines

- **G-TS-P** — no shared mutable state in the function body; re-entrant/concurrent use is safe apart from any allocator it calls.
- **G-TS-M** — accesses `stdlib_state` side table under `stdlib_state.meta_lock` (spin, `stdlib/error.hp:116,256-263`) and/or atomic counters (`:127-146`). Synchronises the metadata only; it does **not** make a caller's container instance thread-safe.
- **G-TS-C** — container instance has no internal locking; concurrent access/mutation of the **same** object is not thread-safe; external synchronisation is required (`stdlib/collections.hp:23-24`; `stdlib/vector.hp` containers inherit the same rule).
- **G-TS-CB** — failure note invokes the optional callback under a thread-local re-entrancy guard (`stdlib/error.hp:96-100,192-222`); cross-thread callback behaviour **[U]**.
- **G-ABI-H** — header-only `static inline`; adds no exported ABI symbol and changes no C signature (`stdlib/error.hp:1-36`; `docs/stdlib/abi-preserving-runtime-safety.md:221-232`).
- **G-ABI-M** — class lowers to a plain C aggregate (no vtable, inheritance, access control, or inline metadata); methods lower to `Class_method(Class *self, ...)`, constructor to `Class_ctor`, and `void drop(void)` is the destructor lowering to `Class_drop(Class *self)` (`docs/LANGUAGE.md:160-200`). Adding/removing a method changes the C symbol surface.
- **G-ABI-T** — pointers remain one word; metadata is out-of-band in the side table (`stdlib/error.hp:1-10,102-125`; `docs/stdlib/abi-preserving-runtime-safety.md:55-79,158-165`).
- **G-OWN-RAII** — owning type: the compiler inserts `Class_drop(&local)` at scope exit; `drop()` must tolerate a zeroed handle (`docs/LANGUAGE.md:194-197`; `docs/MODERN_STDLIB.md:47-56`).
- **G-ALLOC** — allocates only via `stdlib_alloc`/`stdlib_calloc`, releases only via `stdlib_release`; failure-atomic by contract (`stdlib/error.hp:12-23`; `stdlib/collections.hp:8-24`).

### 0.5 Headline counts

Counting rule: one entry per declared function/method/constructor/`drop()` destructor plus the explicitly documented internal helpers. Free `stdlib_*` helpers are **included** here (the Agent-2 `381`-entry inventory excludes them, so its per-header numbers differ — see `tests/stdlib/agent2/API_COVERAGE_REPORT.md:60-61`).

| Header | Public API entries | Internal helpers (documented for completeness) | Total entries |
|---|---:|---:|---:|
| `stdlib/error.hp` | 36 | 6 | 42 |
| `stdlib/memory.hp` | 11 | 1 | 12 |
| `stdlib/string.hp` | 25 | 0 | 25 |
| `stdlib/vector.hp` | 21 | 0 | 21 |
| `stdlib/collections.hp` | 69 | 8 | 77 |
| **Total** | **162** | **15** | **177** |

Evidence status over all 177 entries. The three statuses are orthogonal dimensions of the same entry, so the first row is the whole inventory and the other rows partition it by the strongest test evidence found:

| Status | Entries | Denominator |
|---|---:|---|
| DOCUMENTED — explicit header contract at a cited `path:line` | 177 | 177 |
| OBSERVED — direct asserted call/effect in a checked-in fixture | 149 | 177 |
| OBSERVED — indirect effect only (no direct asserted call of that entry) | 27 | 177 |
| UNTESTED — no test reference and no asserted effect at all | 1 | 177 |

OBSERVED total = **176 / 177**; the single UNTESTED entry is `stdlib_safe_div` (§1.4). `stdlib_telemetry_is_enabled` is **not** UNTESTED: `containers_safety_test.cp:59` asserts its return and `containers_safety_test.expected:telemetry-ok 1 1` pins it. Destructors (`drop()`) are counted as entries and, where the test tree relies on compiler insertion, are marked OBSERVED *indirect* (e.g. `bd_drop_*`, `T4_CLEAR_DROP_REUSE`). The 27 indirect-only entries are enumerated in §7; the 1 UNTESTED entry in §8. `149 + 27 + 1 = 177`; no status row is a derived percentage.

---

## 1. `stdlib/error.hp` — allocator, metadata, telemetry, error object

### 1.1 Checked allocator (public)

#### `void *stdlib_alloc(size_t bytes)` — `stdlib/error.hp:364-392`
- **Ownership:** returns a freshly owned tracked block; caller must release with `stdlib_release()` or transfer via `stdlib_transfer_ownership()` (`:364,581-588`). Never `free()` it (`:22-23`).
- **Nullability:** never returns NULL for a successful small request; `bytes == 0` is remapped to `1` (`:370-371`). NULL on refusal.
- **Bounds:** request must be `<= STDLIB_ALLOC_LIMIT` (default `1<<40` on 64-bit, `SIZE_MAX>>1` otherwise; `:57-67`).
- **Allocation:** one `cplus_alloc(bytes)`; on success records metadata with `elem_size = 1` and bumps telemetry `ALLOC`/`BYTES` (`:385-390`).
- **Failure:** NULL with `STDLIB_CATEGORY_MEMORY` note when over-limit or when failure injection (`stdlib_alloc_fail_at/after`) trips (`:372-384`).
- **Cleanup:** `stdlib_release(p)`; second release of the same pointer is a runtime-detected invalid free, not a no-op.
- **Thread:** G-TS-M (atomic seq/counters, metadata lock in `stdlib_meta_record`); the injected-failure counters are process-wide.
- **ABI:** G-ABI-H, G-ABI-T — returns the same one-word pointer type as `cplus_alloc`.
- **Evidence:** D `:364-392`; O direct `deep_memory_bounds.expected:q_alloc_ok,b_alloc_ok,z_alloc_ok` and `agent2/abi_safety_meta_integrity.cp:asmi_a_raw_size`; U --

#### `void *stdlib_calloc(size_t count, size_t element_size)` — `stdlib/error.hp:394-414`
- **Ownership:** returns an owned tracked, zero-filled block; same release rule as `stdlib_alloc` (`:22-23,581-588`).
- **Nullability:** `count == 0 || element_size == 0` allocates one byte and returns non-NULL on success (`:406`). NULL on failure.
- **Bounds:** `__builtin_mul_overflow(count, element_size, &total)` and `total > STDLIB_ALLOC_LIMIT` are refused (`:400-405`).
- **Allocation:** delegates to `stdlib_alloc(total)`, then `memset(p,0,total)` and records `elem_size = element_size > 0 ? element_size : 1` (`:406-412`).
- **Failure:** NULL with `STDLIB_CATEGORY_MEMORY` note "allocation size overflow"; injected failure also applies through `stdlib_alloc`.
- **Cleanup:** `stdlib_release`.
- **Thread:** G-TS-M.
- **ABI:** G-ABI-H, G-ABI-T; `count*size` product is checked before any division (`:399`).
- **Evidence:** D `:394-414`; O direct `agent2/abi_safety_meta_integrity.cp:asmi_a_arr_size,asmi_a_arr_elem,asmi_a_arr_len,asmi_a_calloc_zeroed` and `agent2/abi_safety_injection_mem.cp:174`; U --

#### `void *stdlib_realloc(void *ptr, size_t bytes)` — `stdlib/error.hp:416-456`
- **Ownership:** `ptr` must be a live stdlib block or NULL; success returns the (possibly moved) owned block and retires the old identity (`:416-421`).
- **Nullability:** `ptr == NULL` delegates to `stdlib_alloc(bytes)` (`:431-432`); `bytes == 0` maps to one backing byte (`:423`).
- **Bounds:** `actual > STDLIB_ALLOC_LIMIT` refused (`:426-430`). Old pointer must be found in the metadata table (`:433-445`).
- **Allocation:** `cplus_realloc(ptr, actual)`; on move `stdlib_meta_forget(ptr)` then `stdlib_meta_record(fresh, actual, elem)` — the generation is bumped even for an in-place resize (`:446-454`).
- **Failure:** NULL if over-limit, if source is untracked (note `STDLIB_CATEGORY_INVALID`), or if the host realloc fails; on failure the original block and its metadata are untouched (`:420-421,437-448`).
- **Cleanup:** release the returned pointer with `stdlib_release`; do **not** also release the old address after a successful move.
- **Thread:** G-TS-M; concurrent stale-alias realloc is a documented untested boundary (ABI doc §1.4/F-04).
- **ABI:** G-ABI-H, G-ABI-T; C `realloc`-compatible signature, no new symbol.
- **Evidence:** D `:416-456`; O direct `regressions/adv_null_zero.cp`, `agent2/abi_safety_meta_integrity.cp:asmi_c_grow_nonnull,asmi_c_grow_new_pointer,asmi_c_grow_runtime_size`; U concurrent/stale-alias realloc (documented finding, ABI doc §1.4).

#### `void stdlib_release(void *ptr)` — `stdlib/error.hp:581-588`
- **Ownership:** consumes one owned block; must be called exactly once on a `stdlib_alloc`/`calloc`/`realloc` result. Never on raw `malloc` (`:22-23`).
- **Nullability:** `ptr == NULL` is an explicit no-op (`:583-584`).
- **Bounds:** n/a (base address only).
- **Allocation:** none; bumps `STDLIB_TM_FREE` telemetry (`:586`).
- **Failure:** cannot fail; passing an untracked/foreign pointer reaches `cplus_release` and is detected by the runtime.
- **Cleanup:** tombstones the metadata slot (`stdlib_meta_forget`, generation → 0, `:348-362`) then `cplus_release` (`:587`).
- **Thread:** G-TS-M.
- **ABI:** G-ABI-H; no signature change versus underlying release.
- **Evidence:** D `:581-588`; O direct `deep_memory_bounds.expected:c_release_nulls_handle,c_release_null_noop,c_single_free_counted` and `deep_memory_bounds.cp:378-384`; U literal double-release is intentionally not executed (fatal by design; `agent2/abi_safety_alloc_lifecycle.cp:109`).

### 1.2 Metadata queries and ownership transfer (public)

#### `size_t stdlib_alloc_size(const void *ptr)` — `stdlib/error.hp:476-486`
- **Ownership:** non-owning query; does not release or transfer.
- **Nullability:** NULL, untracked, and tombstoned pointers return `0` (`:477-485`, `stdlib_meta_find` `:273-287`).
- **Bounds:** returns the exact `byte_size` recorded at allocation; interior addresses are **not** accepted (keyed by exact base; `stdlib/memory.hp:27-29`).
- **Allocation:** none; no telemetry, no error note.
- **Failure:** no failure channel; `0` means "NULL/untracked", which is also a legitimate capacity for a not-yet-recorded block.
- **Cleanup:** none.
- **Thread:** G-TS-M (lock held for lookup).
- **ABI:** G-ABI-H, G-ABI-T; table is header-internal and not part of the ABI (ABI doc §1.2).
- **Evidence:** D `:476-486`; O direct `deep_memory_bounds.expected:q_cap_null,q_cap_tracked,u_stack_untracked` and `agent2/abi_safety_meta_integrity.cp:asmi_a_raw_size,asmi_a_null_size`; U --

#### `size_t stdlib_alloc_elem_size(const void *ptr)` — `stdlib/error.hp:488-497`
- **Ownership:** non-owning query.
- **Nullability:** NULL/untracked → `0`.
- **Bounds:** returns recorded `elem_size` (normally `1` for `stdlib_alloc`, the supplied size for `stdlib_calloc`; `:488-496`).
- **Allocation:** none.
- **Failure:** no failure channel; `0` ambiguous between untracked and unset.
- **Cleanup:** none.
- **Thread:** G-TS-M.
- **ABI:** G-ABI-H, G-ABI-T.
- **Evidence:** D `:488-497`; O direct `agent2/abi_safety_meta_integrity.cp:asmi_a_raw_elem,asmi_a_arr_elem` and `containers_safety_test.cp`; U --

#### `size_t stdlib_alloc_length(const void *ptr)` — `stdlib/error.hp:499-511`
- **Ownership:** non-owning query.
- **Nullability:** NULL/untracked → `0`; `elem_size == 0` is coerced to `1` (`:505-507`).
- **Bounds:** quotient `byte_size / elem_size` via `stdlib_safe_div` (`:507`); see §1.4 for that helper's documented quotient bound.
- **Allocation:** none.
- **Failure:** no failure channel.
- **Cleanup:** none.
- **Thread:** G-TS-M.
- **ABI:** G-ABI-H, G-ABI-T; deliberately avoids a variable division instruction (`:458-461`).
- **Evidence:** D `:499-511`; O direct `agent2/abi_safety_meta_integrity.cp:asmi_a_raw_len,asmi_a_arr_len`; U large quotients (no test beyond `STDLIB_ALLOC_LIMIT`-overridden small fixtures, §11).

#### `unsigned long long stdlib_alloc_generation(const void *ptr)` — `stdlib/error.hp:513-522`
- **Ownership:** non-owning query; the returned stamp is the lifetime token.
- **Nullability:** NULL/untracked → `0`, and `0` is reserved as "no token / invalid" (`:513-521`, `:528-529`).
- **Bounds:** n/a.
- **Allocation:** none.
- **Failure:** no failure channel.
- **Cleanup:** none; any previously captured token becomes stale on record/realloc/transfer.
- **Thread:** G-TS-M.
- **ABI:** G-ABI-H; generation is a 64-bit integer in a header-private slot (`:102-107`).
- **Evidence:** D `:513-522`; O direct `deep_memory_bounds.expected:q_token_nonzero,q_token_untracked,g_token_changed` and `fuzz/f100_mem_tokens.cp`; U wraparound (ABI doc §1.3).

#### `int stdlib_alloc_check_generation(const void *ptr, unsigned long long gen)` — `stdlib/error.hp:524-536`
- **Ownership:** non-owning validation.
- **Nullability:** `ptr == NULL || gen == 0` returns `0` without touching the table (`:528-529`).
- **Bounds:** n/a; requires exact base address.
- **Allocation:** none.
- **Failure:** returns `1` only when the slot exists and `generation == gen`; otherwise `0`. No error note.
- **Cleanup:** none.
- **Thread:** G-TS-M.
- **ABI:** G-ABI-H; token is opt-in and not enforced by free/transfer (ABI doc §1.3, finding F-01).
- **Evidence:** D `:524-536`; O direct `deep_memory_bounds.expected:g_current_token_ok,g_stale_write_refused` (`stdlib_mem_check_token` path) and `fuzz/f100_mem_tokens.cp`; U generation wrap (ABI doc §1.3).

#### `int stdlib_alloc_query_length(const void *ptr, size_t elem_size, size_t *out_count)` — `stdlib/error.hp:538-556`
- **Ownership:** non-owning query; caller owns `out_count`.
- **Nullability:** `ptr == NULL || out_count == NULL` → `-1`; `*out_count` is zeroed before any work when non-NULL, so it is never left stale (`:542-545`).
- **Bounds:** `elem_size == 0` means "use the recorded element size, else 1" (`:549`); quotient via `stdlib_safe_div`.
- **Allocation:** none.
- **Failure:** `-1` for NULL args or untracked pointer; `0` on success (`:555`). No error note.
- **Cleanup:** none.
- **Thread:** G-TS-M.
- **ABI:** G-ABI-H; out-param convention is plain C.
- **Evidence:** D `:538-556`; O direct `deep_memory_bounds.expected:q_count_null,q_count_null_error,q_count_bytes,q_count_elems,q_count_recorded` and `agent2/abi_safety_meta_integrity.cp:asmi_a_query_default,asmi_a_query_elem8,asmi_a_null_query`; U --

#### `int stdlib_transfer_ownership(void **dst, void **src)` — `stdlib/error.hp:558-579`
- **Ownership:** moves the block from `*src` to `*dst`; requires `*dst == NULL`, `*src` live, `dst != src` (`:560-565`).
- **Nullability:** any of `dst/src/*dst/*src` NULL → `-1` with `STDLIB_CATEGORY_OWNERSHIP` note.
- **Bounds:** n/a; `*src` must be a tracked live base address (`:566-573`).
- **Allocation:** none; bumps the block generation under the metadata lock (`:574`).
- **Failure:** `-1` for invalid handles or untracked source; both handles unchanged on failure (`:568-573`).
- **Cleanup:** on success writes `*dst = *src; *src = NULL` (`:575-576`); the new owner must release.
- **Thread:** G-TS-M.
- **ABI:** G-ABI-H, G-ABI-T; generation check is by table membership only, not a caller-supplied token (ABI doc §1.6/F-01).
- **Evidence:** D `:558-579`; O direct `deep_memory_bounds.expected:g_transfer_ok,g_transfer_dst_occupied,g_transfer_src_null,g_token_changed` and `deep_cross_family.cp` CF3; U stale alias at a recycled address (ABI doc §1.3/F-01).

### 1.3 Internal metadata helpers (public in C+ — no access control — but internal by convention)

All six are `static inline`, take/return no owned objects themselves, and operate on the weak `stdlib_state` (`:109-125`). They are **not** exported ABI symbols.

#### `void stdlib_meta_acquire(void)` / `void stdlib_meta_release_lock(void)` — `stdlib/error.hp:256-263`
- **Ownership:** none (lock guard only). **Nullability:** n/a. **Bounds:** n/a. **Allocation:** none.
- **Failure:** `acquire` spins until the lock is free; no timeout, no failure return (`:257-258`). `release` stores 0 with release semantics (`:262`).
- **Cleanup:** every `acquire` must be paired with `release`; all call sites in this header are paired.
- **Thread:** G-TS-M (this *is* the synchronisation primitive). **ABI:** G-ABI-H, G-ABI-T.
- **Evidence:** D `:256-263`; O indirect — asserted metadata balance in `agent2/abi_safety_meta_integrity.cp:asmi_a_balance,asmi_a_live_restored`; U direct contention/timeout measurement.

#### `size_t stdlib_meta_hash(const void *p)` — `stdlib/error.hp:265-271`
- **Ownership:** none. **Nullability:** tolerates NULL (hash of address bits). **Bounds:** n/a. **Allocation:** none.
- **Failure:** none. **Cleanup:** none. **Thread:** pure (G-TS-P). **ABI:** G-ABI-H; splitmix-style `uintptr_t` hash, no pointer mutation (G-ABI-T).
- **Evidence:** D `:265-271`; O indirect via table operations in `agent2/abi_safety_meta_integrity.cp`; U direct value assertion.

#### `stdlib_meta_slot *stdlib_meta_find(const void *p)` — `stdlib/error.hp:273-287`
- **Ownership:** borrowed pointer into `meta_tab`; must not be retained across a lock release or table growth.
- **Nullability:** NULL/`STDLIB_META_TOMB`/zero-capacity → NULL (`:275-276`).
- **Bounds:** probes at most `meta_cap` slots (`:279-285`).
- **Allocation:** none. **Failure:** NULL = not found. **Cleanup:** caller must not free the returned slot.
- **Thread:** caller must hold `meta_lock` (G-TS-M). **ABI:** G-ABI-H, G-ABI-T (private struct, ABI doc §1.2).
- **Evidence:** D `:273-287`; O indirect `agent2/abi_safety_meta_integrity.cp:asmi_b_tables_agree,asmi_b_runtime_get_meta`; U direct.

#### `void stdlib_meta_record(const void *p, size_t byte_size, size_t elem_size)` — `stdlib/error.hp:289-346`
- **Ownership:** registers a base address; does not own the block.
- **Nullability:** `p == NULL` returns immediately (`:293-294`); `elem_size == 0` coerced to 1 (`:295-296`).
- **Bounds:** rehash guarded by `ncap <= SIZE_MAX/4u/sizeof(slot)` (`:303-306`); if `calloc` for the new table fails, the old table stays and the insert is skipped (`:307-322`).
- **Allocation:** uses host `calloc`/`free` for the side table only (`:307,318`); no `stdlib_alloc` (no recursion).
- **Failure:** silent — a failed table growth means the block may not be registered.
- **Cleanup:** updates or inserts a slot and bumps `meta_gen` (`:324-344`); called under `meta_lock`.
- **Thread:** G-TS-M (acquires the lock itself). **ABI:** G-ABI-H, G-ABI-T.
- **Evidence:** D `:289-346`; O direct `agent2/abi_safety_meta_integrity.cp:asmi_a_raw_size,asmi_a_arr_elem` (effects), U direct call / table-growth-failure path.

#### `void stdlib_meta_forget(const void *p)` — `stdlib/error.hp:348-362`
- **Ownership:** deregisters a base address; does not free the block.
- **Nullability:** `p == NULL` no-op (`:350-351`).
- **Bounds:** n/a. **Allocation:** none.
- **Failure:** silently no-op if not found.
- **Cleanup:** tombstones the slot, zeroes `byte_size`/`elem_size`/`generation`, decrements `meta_live` (`:354-360`).
- **Thread:** G-TS-M (acquires the lock). **ABI:** G-ABI-H, G-ABI-T.
- **Evidence:** D `:348-362`; O indirect `deep_memory_bounds.expected:c_single_free_counted`, `agent2/abi_safety_meta_integrity.cp:asmi_c_*`; U direct call.

### 1.4 Allocation-sizing helper and failure injection (public)

#### `size_t stdlib_safe_div(size_t a, size_t b)` — `stdlib/error.hp:458-474`
- **Ownership:** none. **Nullability:** n/a. **Bounds:** `b == 0` → `0` (`:464-465`).
- **Allocation:** none; deliberately avoids a variable division/remainder for the strict checker (`:458-461`).
- **Failure:** no failure channel. The loop accumulates `q` from `1<<15` down to `1<<0`, so the representable quotient is at most `2^16-1`; the header claims this covers all `STDLIB_ALLOC_LIMIT` element-count queries on this host (`:460-461`). That claim was **not verified** here, and the default limit is `1<<40` (`:61-67`), so large-quotient behaviour is **[U]** (§11).
- **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-H; pure integer routine.
- **Evidence:** D `:458-474`; O indirect (called by `stdlib_alloc_length` / `stdlib_alloc_query_length`, both asserted in `agent2/abi_safety_meta_integrity.cp`); U direct call — zero `stdlib_safe_div` references anywhere under `tests/`.

#### `void stdlib_alloc_fail_at(long n)` / `void stdlib_alloc_fail_after(long n)` / `void stdlib_alloc_fail_reset(void)` / `long stdlib_alloc_seq(void)` — `stdlib/error.hp:226-251`
- **Ownership:** none (test-only global knobs on `stdlib_state.seq/fail_at/fail_from`; `:109-122`).
- **Nullability:** n/a. **Bounds:** `n > 0` for `fail_at`; `n >= 0` for `fail_after` (`:230,237`).
- **Allocation:** none; the next `stdlib_alloc` consults the counters (`:377-384`).
- **Failure:** `fail_at(n)` makes the n-th subsequent allocation fail once; `fail_after(n)` makes allocation `n+1` onward fail; `reset` clears all three fields (`:226-245`).
- **Cleanup:** tests must call `stdlib_alloc_fail_reset()` to avoid leaking the injection into later code.
- **Thread:** G-TS-M (all fields atomic; process-wide, so concurrent tests would interfere). **ABI:** G-ABI-H; test aid, not a production contract.
- **Evidence:** D `:226-251`; O direct `deep_collections_alloc.expected:ALLOC 59 0`, `deep_string_task4.expected:t4_copy_fail_rc`, `deep_vector_task4.expected:T4_SHRINK_FAILED_ATOMIC`, `deep_cross_family.cp` CF2, `agent2/abi_safety_injection_mem.cp:172-174`; U --

### 1.5 Telemetry and failure reporting (public)

#### `void stdlib_tm_add(int index, unsigned long long amount)` — `stdlib/error.hp:127-131`
- **Ownership:** none. **Nullability:** n/a; caller supplies a valid `STDLIB_TM_*` index (`:71-81`).
- **Bounds:** no index validation — an out-of-range index writes past `counters[10]`; all in-tree callers use the named constants.
- **Allocation:** none. **Failure:** no failure channel; no-op when telemetry disabled.
- **Cleanup:** none. **Thread:** G-TS-M (`__atomic_fetch_add`, relaxed). **ABI:** G-ABI-H; counters live in the weak header state.
- **Evidence:** D `:127-131`; O indirect `deep_memory_bounds.expected:p_telemetry_on_counts, c_single_free_counted`; U direct call / invalid index.

#### `void stdlib_telemetry_set_enabled(int enabled)` / `int stdlib_telemetry_is_enabled(void)` / `void stdlib_telemetry_reset(void)` — `stdlib/error.hp:133-146`
- **Ownership:** none. **Nullability:** n/a. **Bounds:** `enabled` coerced to 0/1 (`:134`).
- **Allocation:** none. **Failure:** none. **Cleanup:** `reset` zeroes all ten counters (`:142-146`).
- **Thread:** G-TS-M (atomic store/load); enable state is process-wide. **ABI:** G-ABI-H.
- **Evidence:** D `:133-146`; O direct `deep_memory_bounds.expected:p_telemetry_off_quiet,p_telemetry_on_counts,p_results_identical` and (`is_enabled`) `containers_safety_test.cp:telemetry-ok 1 1`; U cross-thread enable races.

#### `void stdlib_telemetry_snapshot(stdlib_telemetry_stats *out)` — `stdlib/error.hp:148-161`
- **Ownership:** caller owns `out`; the function only writes it.
- **Nullability:** `out == NULL` → immediate no-op (`:149-150`).
- **Bounds:** writes all ten `unsigned long long` fields of `stdlib_telemetry_stats` (`:83-94`).
- **Allocation:** none. **Failure:** none. **Cleanup:** none.
- **Thread:** G-TS-M (ten independent relaxed atomic loads; the snapshot is not a single atomic transaction). **ABI:** G-ABI-H; struct is a plain C aggregate.
- **Evidence:** D `:148-161`; O direct `deep_memory_bounds.cp:470-485`, `containers_safety_test.expected:telemetry-ok 1 1`; U --

#### `void stdlib_set_error_callback(stdlib_error_callback callback)` — `stdlib/error.hp:163-165`
- **Ownership:** stores a borrowed function pointer; caller must keep the target alive.
- **Nullability:** NULL is allowed and disables callbacks (`:217`).
- **Bounds:** n/a. **Allocation:** none. **Failure:** none.
- **Cleanup:** set to NULL before unloading a callback's module (not expressed in the API).
- **Thread:** G-TS-CB + atomic release store; cross-thread callback behaviour **[U]**. **ABI:** G-ABI-H; plain function-pointer typedef (`:99-100`).
- **Evidence:** D `:96-100,163-165,192-222`; O direct `deep_memory_bounds.expected:p_callback_off_silent,p_callback_on_fires`, `deep_cross_family.cp` CF6; U callback from multiple threads concurrently.

#### `const char *stdlib_category_name(int category)` — `stdlib/error.hp:167-190`
- **Ownership:** returns a borrowed pointer to a static string literal; must not be freed or modified.
- **Nullability:** never NULL; "unknown" fallback (`:187-188`).
- **Bounds:** n/a; any `int` accepted, including negative/unknown values.
- **Allocation:** none. **Failure:** none. **Cleanup:** none.
- **Thread:** G-TS-P (switch over immutable literals). **ABI:** G-ABI-H; `const char *` return.
- **Evidence:** D `:167-190`; O direct `deep_error_task5.expected:err_fresh_category_ok,err_unknown_name,err_negative_category`, `containers_safety_test.cp`; U -- (no exit-code-to-name function exists — recorded SKIP at `deep_error_task5.expected:SKIP_REASON ... err_code_name_mapping`).

#### `void stdlib_note(int category, cplus_exit_code code, const char *message)` — `stdlib/error.hp:192-222`
- **Ownership:** borrows `message`; copies nothing persistent (callback receives the pointer during the call only).
- **Nullability:** `message == NULL` is normalised to `""` (`:219`); category/code are validated only by the switch/default (`:198-214`).
- **Bounds:** `message` must be a valid NUL-terminated string for the callback's duration; no length cap here.
- **Allocation:** none — "Allocates nothing, formats nothing" (`:192-193`).
- **Failure:** none; increments the matching category counter plus `API_FAIL` (`:198-215`).
- **Cleanup:** none; re-entrant notes from inside the callback are dropped by the thread-local `reporting` guard (`:196,217-221`).
- **Thread:** G-TS-CB + G-TS-M (atomic callback load `:216`); cross-thread callback **[U]**. **ABI:** G-ABI-H; note carries only category/code/static message, never buffer contents (`:25-26`).
- **Evidence:** D `:192-222`; O indirect `deep_error_task5.expected:err_reentrant_no_recursion,err_reentrant_no_latch,err_1000_cycles`, `deep_memory_bounds.expected:p_callback_on_fires`; U cross-thread callback.

### 1.6 `error` object (constructor + 9 methods; no `drop()` → not an owning type)

All methods operate on a fixed `char text[STDLIB_ERROR_SIZE]` (`STDLIB_ERROR_SIZE = 256`, `:55,600`) and the free helpers `:645-693`.

#### `error::error(void)` — `stdlib/error.hp:602-607`
- **Ownership:** value-like, no heap; zeroed state (`failed=0`, `kind=OK`, `status=CPLUS_EXIT_OK`, `text[0]='\0'`).
- **Nullability:** n/a. **Bounds:** writes `text[0]` only. **Allocation:** none. **Failure:** cannot fail.
- **Cleanup:** none (no destructor; `clear()`/`error_reset()` restores the same state).
- **Thread:** G-TS-P (operates on the local object). **ABI:** G-ABI-M — lowers to `error_ctor` returning a zero-initialised aggregate by value (`docs/LANGUAGE.md:189-192`).
- **Evidence:** D `:602-607`; O indirect `deep_error_vector.cp:87-225` (E1–E5 construct `error` locals) and `fuzz/f100_error_obj.cp`; U --

#### `int error::ok(void)` / `int error::failed_now(void)` — `stdlib/error.hp:609-615`
- **Ownership:** non-owning reads. **Nullability:** n/a. **Bounds:** n/a. **Allocation:** none. **Failure:** none.
- **Cleanup:** none. **Thread:** G-TS-P on the local object. **ABI:** G-ABI-M.
- **Evidence:** D `:609-615`; O direct `deep_error_vector.expected:E2_reset_set,E3_null_handle`, `deep_error_task5.cp`, `negative.expected:error-methods`; U --

#### `int error::code(void)` / `const char *error::what(void)` / `int error::category(void)` — `stdlib/error.hp:617-629`
- **Ownership:** `code`/`category` return by value; `what()` borrows the internal `char[256]` — valid only while the `error` lives and is unchanged.
- **Nullability:** `what()` never returns NULL (`text[0]` is always initialised); `category()` returns `OK` when not failed (`:628`).
- **Bounds:** `code` is the stored `cplus_exit_code` cast to `int`; `category` is the stable category captured when set (`:625-629`).
- **Allocation:** none. **Failure:** none. **Cleanup:** caller must not free/modify `what()`'s result.
- **Thread:** G-TS-P on the local object. **ABI:** G-ABI-M.
- **Evidence:** D `:617-629`; O direct `deep_error_task5.expected:err_fresh_category_ok,err_invalid_category_code`, `deep_error_vector.expected:E4_categories,E5_status_map`, `deep_cross_family.cp` CF2 (`code()`); U --

#### `void error::clear(void)` / `void error::set(cplus_exit_code, const char *)` / `void error::set_category(int, cplus_exit_code, const char *)` — `stdlib/error.hp:631-642`
- **Ownership:** mutate the receiver; borrow `message`. **Nullability:** `message == NULL` handled by `error_fail_cat` (`:684-692`).
- **Bounds:** message truncated to 255 bytes + NUL (`:684-689`).
- **Allocation:** none. **Failure:** `set` derives a category from the code (`:655-669`); `set_category` uses the caller's category verbatim.
- **Cleanup:** `clear()` restores the ctor state.
- **Thread:** G-TS-P on the local object; `set*` also emit a note (G-TS-CB/G-TS-M). **ABI:** G-ABI-M.
- **Evidence:** D `:631-642,645-693`; O direct `deep_error_vector.expected:E2_reset_set,E4_categories`, `deep_error_task5.expected:err_bound_255,err_bound_256,err_bound_257,err_no_stale_tail`; U --

#### `void error_reset(void *target)` / `void error_fail(void *target, cplus_exit_code, const char *)` / `void error_fail_cat(void *target, int, cplus_exit_code, const char *)` — `stdlib/error.hp:645-693`
- **Ownership:** `target` is a borrowed `error*` (free helpers exist because method-call sugar does not apply to parameters; `:8-10`). Caller owns the object.
- **Nullability:** `target == NULL` is safe for all three: `reset`/`fail_cat` return after the note (`:647-648,675-676`), `fail` delegates. `message == NULL` → empty text (`:690-692`).
- **Bounds:** copy capped at `STDLIB_ERROR_SIZE - 1` = 255 bytes, NUL appended (`:684-689`).
- **Allocation:** none. **Failure:** `fail*` set `failed=1`, increment telemetry via `stdlib_note`, then optionally write the record (`:671-692`).
- **Cleanup:** `reset` zeroes the record (`:649-652`).
- **Thread:** G-TS-CB + G-TS-M for the note; receiver mutation is local. **ABI:** G-ABI-H (free `static inline` functions with `void *` C signatures).
- **Evidence:** D `:645-693`; O direct `deep_error_task5.cp:82-228`, `regressions/adv_null_zero.cp`, `fuzz/f100_error_obj.cp`, `negative.cp`; U --

---

## 2. `stdlib/memory.hp` — guarded byte operations

Global contract (`:1-36`): nothing is redefined, no fat pointers, no side table of its own; metadata comes from `error.hp`. Guarded operations are **fail-closed** — on any validation problem they set the supplied `error` and return `STDLIB_FAIL` without touching memory; `err == NULL` is allowed (the note still reaches telemetry/callback). Only `(base, offset)` is accepted, never `(base + offset)`.

#### `size_t stdlib_mem_capacity(const void *ptr)` — `stdlib/memory.hp:48-50`
- **Ownership:** none; non-owning query. **Nullability:** NULL/untracked → `0`.
- **Bounds:** returns recorded capacity; interior pointers report `0` (`:27-29`).
- **Allocation:** none. **Failure:** none. **Cleanup:** none.
- **Thread:** G-TS-M (delegates to `stdlib_alloc_size`). **ABI:** G-ABI-H, G-ABI-T.
- **Evidence:** D `:45-50`; O direct `deep_memory_bounds.expected:q_cap_null,q_cap_tracked,z_alloc_zero_cap1,u_stack_untracked`; U --

#### `int stdlib_mem_count(const void *ptr, size_t elem_size, size_t *out_count)` — `stdlib/memory.hp:56-59`
- **Ownership:** none; caller owns `out_count`. **Nullability:** NULL/untracked → `-1` and `*out_count = 0` (never stale); `elem_size == 0` uses the recorded size.
- **Bounds:** element count derived from tracked `byte_size`. **Allocation:** none.
- **Failure:** `-1` / `0` mirroring `stdlib_alloc_query_length` (`:52-59`).
- **Cleanup:** none. **Thread:** G-TS-M. **ABI:** G-ABI-H.
- **Evidence:** D `:52-59`; O direct `deep_memory_bounds.expected:q_count_null,q_count_null_error,q_count_bytes,q_count_elems,q_count_recorded`; U --

#### `unsigned long long stdlib_mem_token(const void *ptr)` — `stdlib/memory.hp:66-68`
- **Ownership:** none; returns an opaque lifetime stamp. **Nullability:** NULL/untracked → `0` ("no token").
- **Bounds:** n/a. **Allocation:** none. **Failure:** none.
- **Cleanup:** token goes stale on release/realloc/transfer by design (`:15-18`).
- **Thread:** G-TS-M. **ABI:** G-ABI-H; token is a 64-bit value from the side table.
- **Evidence:** D `:63-68`; O direct `deep_memory_bounds.expected:q_token_nonzero,q_token_untracked,g_token_changed`; U wrap.

#### `int stdlib_mem_check_token(const void *ptr, unsigned long long token, error *err)` — `stdlib/memory.hp:73-82`
- **Ownership:** validates a borrowed block; `token == 0` opts out of the generation check (`:70-72`).
- **Nullability:** NULL `ptr` is not validated here (it calls `stdlib_alloc_check_generation`, which returns 0 for NULL); `err` may be NULL.
- **Bounds:** n/a. **Allocation:** none.
- **Failure:** `STDLIB_FAIL` with category `LIFECYCLE` / `CPLUS_EXIT_USE_AFTER_FREE` on a stale token (`:76-80`); `STDLIB_OK` otherwise.
- **Cleanup:** none. **Thread:** G-TS-M; note may call back (G-TS-CB).
- **ABI:** G-ABI-H; `error*` out-param convention.
- **Evidence:** D `:70-82`; O direct `regressions/adv_null_zero.cp:569-573` (stale token), indirect via `stdlib_mem_copy/move/set/borrow` in `deep_memory_bounds.cp`; U direct NULL-`err` case.

#### `int stdlib_mem_check(const void *ptr, unsigned long long token, error *err)` — `stdlib/memory.hp:86-94`
- **Ownership:** validates a borrowed block + token. **Nullability:** `ptr == NULL` or capacity 0 → `STDLIB_FAIL` with `INVALID`/`CPLUS_EXIT_TYPE_CONFUSION` (`:88-92`); `err` may be NULL.
- **Bounds:** requires a tracked base address. **Allocation:** none.
- **Failure:** fail-closed as above; otherwise delegates to `stdlib_mem_check_token`.
- **Cleanup:** none. **Thread:** G-TS-M + G-TS-CB. **ABI:** G-ABI-H.
- **Evidence:** D `:84-94`; O direct `regressions/adv_null_zero.cp:499-503,573-574`, `fuzz/f100_mem_tokens.cp`; U --

#### `int stdlib_mem_check_range(const void *ptr, size_t off, size_t n, int is_dest, error *err)` *(internal)* — `stdlib/memory.hp:101-128`
- **Ownership:** validation only. **Nullability:** NULL `ptr` → `INVALID`/`CPLUS_EXIT_NULL_POINTER`; untracked → `INVALID`/`TYPE_CONFUSION` (`:104-116`); `err` may be NULL.
- **Bounds:** `off > cap` → `BOUNDS`; `n > cap - off` → `BOUNDS`; subtraction form prevents `off + n` overflow (`:98-126`). `off == cap, n == 0` is accepted.
- **Allocation:** none. **Failure:** `STDLIB_FAIL` with distinct dest/source messages selected by `is_dest`.
- **Cleanup:** none. **Thread:** G-TS-M + G-TS-CB. **ABI:** G-ABI-H.
- **Evidence:** D `:98-128`; O direct `regressions/adv_null_zero.cp:503`, indirect through every guarded op in `deep_memory_bounds.expected:o_copy_over,o_set_off_over,o_off_gt_cap,i_off_eq_cap_zero`; U --

#### `int stdlib_mem_copy(void *dst, size_t dst_off, const void *src, size_t src_off, size_t n, unsigned long long dst_token, error *err)` — `stdlib/memory.hp:137-150`
- **Ownership:** borrows both blocks; no transfer. **Nullability:** NULL dst/src refused (`:104-109` via `check_range`); `err` may be NULL.
- **Bounds:** both `[off, off+n)` ranges validated against tracked capacities; `n == 0` is a valid no-op **after** validation (`:134-147`).
- **Allocation:** none; uses `memcpy`.
- **Failure:** fail-closed — any range/token failure returns `STDLIB_FAIL` with **no write** (`:140-145`). Overlap is the caller's responsibility (use `mem_move`); UB if violated (`:135-136`).
- **Cleanup:** none. **Thread:** G-TS-M + G-TS-CB; concurrent mutation of the buffers is not synchronised. **ABI:** G-ABI-H; byte-count convention, no pointer representation change.
- **Evidence:** D `:132-150`; O direct `deep_memory_bounds.expected:z_copy_zero,x_copy_exact,o_copy_over,o_copy_over_no_write,n_dst_null,n_src_null,i_copy_interior,i_copy_neighbors_clean,i_over_from_interior,i_over_tail_clean,g_stale_write_refused`; U overlapping-use UB (documented, deliberately not exercised).

#### `int stdlib_mem_move(void *dst, size_t dst_off, const void *src, size_t src_off, size_t n, unsigned long long dst_token, error *err)` — `stdlib/memory.hp:153-166`
- **Ownership:** borrows both blocks. **Nullability:** NULL dst/src refused; `err` may be NULL.
- **Bounds:** same subtraction-form checks as `mem_copy`; `n == 0` no-op after validation.
- **Allocation:** none; uses `memmove`, so overlapping ranges are defined (`:152-164`).
- **Failure:** fail-closed, no write on any validation failure.
- **Cleanup:** none. **Thread:** G-TS-M + G-TS-CB. **ABI:** G-ABI-H.
- **Evidence:** D `:152-166`; O direct `deep_memory_bounds.expected:z_set_zero,x_move_exact,n_src_null`; U self-overlap with differing offsets (defined by `memmove` but no dedicated fixture found).

#### `int stdlib_mem_set(void *dst, size_t dst_off, int value, size_t n, unsigned long long dst_token, error *err)` — `stdlib/memory.hp:169-179`
- **Ownership:** borrows dst. **Nullability:** NULL dst → `STDLIB_FAIL`; `err` may be NULL.
- **Bounds:** `[dst_off, dst_off+n)` checked against capacity; `n == 0` no-op after validation (`value` is `int` narrowed to byte by `memset`).
- **Allocation:** none. **Failure:** fail-closed, no write on failure (`:171-174`).
- **Cleanup:** none. **Thread:** G-TS-M + G-TS-CB. **ABI:** G-ABI-H.
- **Evidence:** D `:168-179`; O direct `deep_memory_bounds.expected:z_set_zero,x_set_exact,o_set_off_over,o_set_off_over_no_write,n_set_null,i_set_interior,i_set_neighbors_clean,z_alloc_zero_set2_refused`; U --

#### `int stdlib_mem_borrow(const void *ptr, size_t off, size_t n, unsigned long long token, error *err)` — `stdlib/memory.hp:188-195`
- **Ownership:** **non-owning** validation of an interior sub-range; no transfer/release/generation change; caller keeps ownership and must not free during the borrow (`:183-187`).
- **Nullability:** NULL/untracked refused; `err` may be NULL. **Bounds:** `[off, off+n)` checked; `n == 0` accepted after validation.
- **Allocation:** none. **Failure:** fail-closed `STDLIB_FAIL`, no side effects.
- **Cleanup:** none — there is no "end borrow" call; correctness is by caller discipline (not enforced).
- **Thread:** G-TS-M + G-TS-CB; borrow does not lock the buffer. **ABI:** G-ABI-H; no new pointer type.
- **Evidence:** D `:183-195`; O direct `deep_memory_bounds.expected:z_set_zero,n_borrow_null,g_borrow_current_ok,g_borrow_over_refused,g_stale_borrow_refused`; U concurrent free while borrowed (caller-discipline violation).

#### `int stdlib_mem_transfer(void **dst, void **src, error *err)` — `stdlib/memory.hp:203-215`
- **Ownership:** transfers ownership: on success `*dst` gets the block, `*src` is set NULL, and the block's generation is bumped so pre-transfer tokens go stale (`:197-202`).
- **Nullability:** `dst == NULL || src == NULL || *dst != NULL || *src == NULL` → `STDLIB_FAIL`/`OWNERSHIP` (`:204-208`); `err` may be NULL.
- **Bounds:** `*src` must be a live tracked block. **Allocation:** none.
- **Failure:** both handles unchanged on failure (`:209-213`).
- **Cleanup:** new owner (`*dst`) must call `stdlib_mem_release`.
- **Thread:** G-TS-M + G-TS-CB. **ABI:** G-ABI-H, G-ABI-T; thin `void **` handles.
- **Evidence:** D `:197-215`; O direct `deep_memory_bounds.expected:g_transfer_ok,g_token_changed,g_transfer_dst_occupied,g_transfer_src_null`; U --

#### `void stdlib_mem_release(void **ptr)` — `stdlib/memory.hp:220-225`
- **Ownership:** consumes the block held in `*ptr` and NULLs the handle; a second call is a no-op (`:217-219`).
- **Nullability:** `ptr == NULL || *ptr == NULL` → immediate return (`:221-222`).
- **Bounds:** n/a. **Allocation:** none.
- **Failure:** none. **Cleanup:** `stdlib_release(*ptr)` then `*ptr = NULL` (`:223-224`). Never pass a raw `malloc` pointer.
- **Thread:** G-TS-M; the `*ptr` write itself is not atomic. **ABI:** G-ABI-H, G-ABI-T.
- **Evidence:** D `:217-225`; O direct `deep_memory_bounds.expected:c_release_nulls_handle,c_release_null_noop,c_single_free_counted`; U --

---

## 3. `stdlib/string.hp` — UTF-8 helper, owned string, borrowed view

Common contract (`:1-17`): `string` owns its buffer and frees it in `drop()`; `string_view` never frees anything. Storage is the `error.hp` checked allocator (G-ALLOC). Every fallible operation is **failure-atomic** (on `-1` the object is exactly as before). A constructor that could not allocate yields an *invalid* string (`valid() == 0`) that reads as empty and re-initialises on a later successful `append`/`reserve`/`copy_from`. NULL `const char *` is an invalid operation, except constructors treat it as `""`.

#### `int stdlib_utf8_validate(const char *data, size_t len)` — `stdlib/string.hp:30-77`
- **Ownership:** none; borrows `data`. **Nullability:** `len == 0` → `1` even for NULL; NULL with `len > 0` → `0` (`:33-36`).
- **Bounds:** reads exactly `len` bytes; truncation check `len - i - 1 < need` (`:63-64`) prevents over-read.
- **Allocation:** none. **Failure:** returns `1`/`0` — strict RFC 3629: overlongs, surrogates, > U+10FFFF, stray continuations rejected; NUL (U+0000) accepted (`:26-29`).
- **Cleanup:** none. **Thread:** G-TS-P (pure). **ABI:** G-ABI-H; byte-length convention, no pointer type change.
- **Evidence:** D `:26-77`; O direct `deep_string_task4.expected:t4_view_utf8_valid_bytes,t4_view_utf8_invalid_bytes,t4_invalid_utf8`, `fuzz/f100_strview_utf8.cp`, `containers_safety_test.expected:string-utf8-ok`; U --

### 3.1 `string_view` (borrowed, no `drop()` → not an owning type)

#### `string_view::string_view(const char *chars, size_t count)` — `stdlib/string.hp:84-87`
- **Ownership:** **borrows** `[chars, chars+count)`; never frees; valid only while the owner lives and does not reallocate (`:79`).
- **Nullability:** accepts NULL `chars` (e.g. an invalid string's `to_view()`); accessors must handle it.
- **Bounds:** caller-supplied `count` is not checked against the actual buffer.
- **Allocation:** none. **Failure:** cannot fail. **Cleanup:** none.
- **Thread:** G-TS-P on the view itself; owner mutation is not synchronised.
- **ABI:** G-ABI-M — lowers to `string_view_string_view(string_view self, const char *, size_t)` returning by value (`docs/LANGUAGE.md:189-192`).
- **Evidence:** D `:79-87`; O indirect `deep_string_task4.cp:383-388` builds views via `to_view()` (which zero-initialises and assigns fields rather than calling this ctor); U direct two-argument call — note `API_COVERAGE_REPORT.md:207` lists `json_view.json_view` but not this ctor as a spelling gap, and `deep_string_task4.expected:t4_view_size,t4_view_data_identity` cover the resulting object.

#### `size_t string_view::size(void)` / `const char *string_view::data(void)` — `stdlib/string.hp:89-95`
- **Ownership:** `size` by value; `data` borrows the owner's buffer. **Nullability:** `data()` may return NULL for a view of an invalid string.
- **Bounds:** `size()` is the unmodified stored count; `data()` has no length attached to the returned pointer.
- **Allocation:** none. **Failure:** none. **Cleanup:** caller must not free `data()`.
- **Thread:** G-TS-P on the view. **ABI:** G-ABI-M.
- **Evidence:** D `:89-95`; O direct `deep_string_task4.expected:t4_view_size,t4_view_data_identity`, `core_extras_test.expected:view 11 1`; U stale view dereference (documented caller duty).

#### `int string_view::equals(const char *other)` — `stdlib/string.hp:97-109`
- **Ownership:** borrows `other`. **Nullability:** `other == NULL` → `0`; `count > 0 && chars == NULL` → `0` (`:99-102`).
- **Bounds:** `strlen(other)` then `memcmp(..., n)` with `n == count`; length mismatch short-circuits (`:103-108`).
- **Allocation:** none. **Failure:** no error channel; `0` means "not equal".
- **Cleanup:** none. **Thread:** G-TS-P on the view. **ABI:** G-ABI-M.
- **Evidence:** D `:97-109`; O direct `deep_string_task4.expected:t4_view_equals,t4_view_equals_null,t4_view_empty_equals_null`; U --

#### `int string_view::is_valid_utf8(void)` — `stdlib/string.hp:111-113`
- **Ownership:** none. **Nullability:** delegates NULL handling to `stdlib_utf8_validate`. **Bounds:** exactly `count` bytes.
- **Allocation:** none. **Failure:** `1`/`0`. **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-M.
- **Evidence:** D `:111-113`; O direct `deep_string_task4.expected:t4_view_utf8_valid_bytes,t4_view_utf8_invalid_bytes`, `fuzz/f100_strview_utf8.cp`; U --

### 3.2 `string` (owning; `drop()` is the destructor)

#### `string::string(const char *text)` — `stdlib/string.hp:121-136`
- **Ownership:** allocates and owns `n+1` bytes; `text == NULL` treated as `""` (`:122, :14-16`).
- **Nullability:** NULL allowed by contract; `text` otherwise must be a valid NUL-terminated string (`strlen(text)`).
- **Bounds:** refuses `n >= STDLIB_ALLOC_LIMIT` (`:126`); allocates `n+1`.
- **Allocation:** one `stdlib_alloc(n+1)`; copies `n` bytes and writes the terminator (`:127-133`).
- **Failure:** allocation failure or over-limit leaves `chars == NULL`, `count == 0`, `space == 0` — an *invalid but readable* string (`valid() == 0`, `:8-12`).
- **Cleanup:** destructor `string_drop` releases the buffer (`:390-397`, G-OWN-RAII).
- **Thread:** G-TS-M during allocation; object local. **ABI:** G-ABI-M, G-ABI-T (owns a plain `char *`).
- **Evidence:** D `:121-136`; O direct `deep_string_task4.expected:t4_ctor_allocfail_valid,t4_ctor_allocfail_size,t4_ctor_allocfail_cap,t4_ctor_allocfail_cstr,t4_ctor_allocfail_at0`, `negative.cp:strings-ok`; U --

#### `size_t string::size(void)` / `size_t string::capacity(void)` / `int string::valid(void)` — `stdlib/string.hp:138-149`
- **Ownership:** non-owning reads. **Nullability:** n/a. **Bounds:** `size()` is logical length, `capacity()` is allocated bytes including terminator.
- **Allocation:** none. **Failure:** `valid()` is the explicit "owns a buffer" flag — false after a failed ctor or `drop()` (`:146-149`).
- **Cleanup:** none. **Thread:** G-TS-P on the object. **ABI:** G-ABI-M.
- **Evidence:** D `:138-149`; O direct `deep_string_task4.expected:t4_cap_initial,t4_cap_at_rc,t4_invalid_valid,t4_ctor_reinit_reserve_pre`, `foundation.expected:str 11 hello world 0 1 0 6`; U --

#### `char string::at(size_t index)` — `stdlib/string.hp:151-155`
- **Ownership:** none. **Nullability:** NULL buffer (invalid/dropped) → `'\0'`.
- **Bounds:** `index >= count` → `'\0'` (no telemetry, unlike `vector::at`); `index < count` reads `chars[index]`.
- **Allocation:** none. **Failure:** none. **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-M.
- **Evidence:** D `:151-155`; O direct `deep_string_task4.expected:t4_ctor_allocfail_at0,t4_drop3_at0`, `foundation.expected:str ... 0`; U --

#### `const char *string::c_str(void)` — `stdlib/string.hp:158-160`
- **Ownership:** returns a borrowed pointer; never NULL even after `drop()` (returns `""`).
- **Nullability:** never NULL. **Bounds:** NUL-terminated for the lifetime of the string state; pointer invalidated by `reserve`/`copy_from`/`drop`.
- **Allocation:** none. **Failure:** none. **Cleanup:** must not be freed.
- **Thread:** G-TS-P on the object. **ABI:** G-ABI-M.
- **Evidence:** D `:157-160`; O direct `deep_string_task4.expected:t4_ctor_allocfail_cstr,t4_drop3_cstr_nonnull,t4_drop3_cstr_empty`, `foundation.expected`; U stale `c_str()` after reallocation (caller duty).

#### `int string::equals(const char *other)` — `stdlib/string.hp:162-177`
- **Ownership:** borrows `other`. **Nullability:** NULL → note `INVALID`/`UNSUPPORTED` and `0` (`:164-168`).
- **Bounds:** `strlen(other)` then `memcmp` of `count` bytes; length mismatch → `0`; invalid string equals only `""` (`:169-176`).
- **Allocation:** none. **Failure:** no error channel. **Cleanup:** none.
- **Thread:** G-TS-P + note path (G-TS-CB). **ABI:** G-ABI-M.
- **Evidence:** D `:162-177`; O direct `deep_string_task4.expected:t4_invalid_equals_empty,t4_invalid_equals_nonempty`, `foundation.expected:str ... 1 0`; U --

#### `int string::compare(const char *other)` — `stdlib/string.hp:181-199`
- **Ownership:** borrows `other`. **Nullability:** NULL → note and `1` ("never equal", `:184-188`).
- **Bounds:** invalid-string path reads exactly 1 byte with `__builtin_memcpy` (`:189-192`); valid path uses `strcmp` (`:193`).
- **Allocation:** none. **Failure:** returns `-1/0/1` normalised from `strcmp` (`:194-198`).
- **Cleanup:** none. **Thread:** G-TS-P + note. **ABI:** G-ABI-M.
- **Evidence:** D `:179-199`; O direct `deep_string_task4.expected:t4_invalid_compare_empty,t4_invalid_compare_nonempty`, `foundation.expected`; U --

#### `long string::find(const char *needle)` — `stdlib/string.hp:201-209`
- **Ownership:** borrows `needle`. **Nullability:** NULL buffer or NULL needle → `-1` (`:203-204`).
- **Bounds:** `strstr` over the NUL-terminated buffer; returns byte offset or `-1`.
- **Allocation:** none. **Failure:** `-1` = not found / invalid input (no telemetry).
- **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-M; `long` return (LP64 64-bit, Windows x86 32-bit — never assume width).
- **Evidence:** D `:201-209`; O direct `deep_string_task4.expected:t4_invalid_find`, `foundation.expected:fields hello world -1`; U --

#### `int string::reserve(size_t want)` — `stdlib/string.hp:212-234`
- **Ownership:** reallocates the owned buffer; old buffer released only after the new one is populated.
- **Nullability:** n/a. **Bounds:** `want > STDLIB_ALLOC_LIMIT` → note `RANGE`/`BOUNDS` and `-1` (`:216-220`); `want <= space` is a no-op returning `0` (`:214-215`).
- **Allocation:** one `stdlib_alloc(want)`; growth telemetry `STDLIB_TM_GROWTH` (`:221-233`).
- **Failure:** `-1` on over-limit or allocation failure, string unchanged (failure-atomic, `:8-9`).
- **Cleanup:** releases the old buffer with `stdlib_release` (`:229`); invalid string re-initialises with `fresh[0]='\0'` (`:226-227`).
- **Thread:** G-TS-M during allocation; object local. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:211-234`; O direct `deep_string_task4.expected:t4_cap_at_rc,t4_cap_at_noop_seq,t4_cap_plusone_seq,t4_ovf_reserve_rc,t4_ovf_reserve_noalloc,t4_ovf_reserve_unchanged,t4_view_stale_reserve_rc`; U --

#### `int string::append(const char *text)` — `stdlib/string.hp:240-287`
- **Ownership:** borrows `text`; self-append and suffix-of-self are supported via integer-address alias detection and `memmove` (`:260-268,280-281`).
- **Nullability:** NULL → note `INVALID`/`UNSUPPORTED` and `-1` (`:246-250`); empty string is a successful no-op (`:252-253`).
- **Bounds:** checks `extra > LIMIT-1 || count > LIMIT-1-extra` (`:254-259`); doubling guarded by `space <= LIMIT/2` (`:272-276`).
- **Allocation:** at most one `stdlib_alloc` via `reserve` (`:277`); `STDLIB_TM_GROWTH` on growth.
- **Failure:** `-1` leaves the string unchanged (`:8-9`); no partial write (`:254-279`).
- **Cleanup:** old buffer freed by `reserve`.
- **Thread:** G-TS-M during growth; object local. **ABI:** G-ABI-M; uses `uintptr_t` comparisons specifically to avoid UB from relational pointer comparison (`:262-263`).
- **Evidence:** D `:236-287`; O direct `deep_string_append.expected:append-success 1,append-recoverable 1,append-fatal 1`, `deep_string_task4.expected:t4_ovf_append_rc,t4_ovf_append_noalloc,t4_ovf_append_unchanged`, `safety_regressions.expected:self-append 8 1,suffix-append xyzyz 1`; U --

#### `void string::clear(void)` — `stdlib/string.hp:289-293`
- **Ownership:** keeps the buffer; sets `count = 0` and `chars[0] = '\0'` when non-NULL.
- **Nullability:** NULL buffer (invalid/dropped string) still resets `count` safely. **Bounds:** writes index 0 of an owned buffer only.
- **Allocation:** none (capacity retained). **Failure:** none. **Cleanup:** none.
- **Thread:** G-TS-P on the object. **ABI:** G-ABI-M.
- **Evidence:** D `:289-293`; O indirect `deep_cross_family.cp` CF1 (source `clear()` must not change a copied key), `regressions/adv_containers.cp`; U direct fixture tag.

#### `int string::copy_from(string *other)` — `stdlib/string.hp:296-321`
- **Ownership:** replaces own buffer with a fresh copy of `other`; `other` is borrowed; `other == this` is a no-op returning `0` (`:304-305`).
- **Nullability:** `other == NULL` → note + `-1` (`:299-303`). Source with NULL `chars` copies an empty string (`:312-314`).
- **Bounds:** `n >= STDLIB_ALLOC_LIMIT` → `-1` (`:307-308`); allocates `n+1`.
- **Allocation:** one `stdlib_alloc(n+1)` (`:309`).
- **Failure:** `-1` leaves the receiver unchanged (failure-atomic; `:8-9`).
- **Cleanup:** releases the old buffer only after the new copy is complete (`:315-319`).
- **Thread:** G-TS-M during allocation; object local. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:295-321`; O direct `deep_string_task4.expected:t4_copy_null_rc,t4_copy_null_unchanged,t4_copy_self_rc,t4_copy_self_unchanged,t4_copy_shorter_state,t4_copy_longer_state,t4_copy_fail_rc,t4_copy_fail_unchanged,t4_copy_retry,t4_copy_telemetry_balanced`; U --

#### `int string::is_valid_utf8(void)` — `stdlib/string.hp:324-326`
- **Ownership:** none. **Nullability:** NULL buffer with `count == 0` validates as empty. **Bounds:** exactly `count` bytes.
- **Allocation:** none. **Failure:** `1`/`0`. **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-M.
- **Evidence:** D `:323-326`; O indirect `deep_string_task4.expected:t4_invalid_utf8`, `containers_safety_test.cp`; U --

#### `int string::substr(size_t start, size_t n, char *out, size_t out_size)` — `stdlib/string.hp:329-345`
- **Ownership:** caller owns `out`; source borrowed. **Nullability:** `out == NULL || out_size == 0` → `-1` (`:332-333`).
- **Bounds:** `start > count` → `-1`; `start == count` yields an empty result; `n` clamped to `count-start` then to `out_size-1`; always NUL-terminates (`:334-344`).
- **Allocation:** none. **Failure:** `-1` for the two input errors only; otherwise `0` with truncation, not an error.
- **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-M; uses `memmove` to permit `out` overlapping the source (`:341`).
- **Evidence:** D `:328-345`; O direct `deep_string_task4.expected:t4_sub_null_out,t4_sub_zero_outsize,t4_sub_start_gt_size,t4_sub_start_eq_size,t4_sub_clamp,t4_sub_exact,t4_sub_truncate`; U --

#### `size_t string::count_fields(char sep)` — `stdlib/string.hp:348-355`
- **Ownership:** none. **Nullability:** NULL buffer with `count == 0` → `1`. **Bounds:** iterates exactly `count` bytes.
- **Allocation:** none. **Failure:** none; returns `1` when the separator is absent. **Cleanup:** none.
- **Thread:** G-TS-P. **ABI:** G-ABI-M.
- **Evidence:** D `:347-355`; O direct `deep_string_task4.expected:t4_field_sep_first_count,t4_field_sep_last_count`, `foundation.expected:fields hello world -1`; U --

#### `int string::field(char sep, size_t index, char *out, size_t out_size)` — `stdlib/string.hp:358-381`
- **Ownership:** caller owns `out`; source borrowed. **Nullability:** `out == NULL || out_size == 0` → `-1` (`:363-364`).
- **Bounds:** index out of range → `-1` (`:380`); field length clamped to `out_size-1`; always terminates (`:369-374`).
- **Allocation:** none. **Failure:** `-1` for bad out-param or missing field. **Cleanup:** none.
- **Thread:** G-TS-P. **ABI:** G-ABI-M; `memmove` permits overlap.
- **Evidence:** D `:357-381`; O direct `deep_string_task4.expected:t4_field_null_out,t4_field_zero_outsize,t4_field_sep_first_0,t4_field_sep_first_1,t4_field_sep_last_0,t4_field_sep_last_1,t4_field_index_oor,t4_field_truncate`; U --

#### `string_view string::to_view(void)` — `stdlib/string.hp:383-388`
- **Ownership:** returns a **borrowed** snapshot (pointer + count) of the current buffer; no allocation, no lifetime extension.
- **Nullability:** may carry NULL `chars` for an invalid/dropped string. **Bounds:** captures `count` at call time; stale after any reallocation.
- **Allocation:** none. **Failure:** none. **Cleanup:** caller must not use the view after owner reallocation/drop.
- **Thread:** G-TS-P on the object; owner mutation not synchronised. **ABI:** G-ABI-M — returns the view aggregate by value.
- **Evidence:** D `:383-388`; O direct `deep_string_task4.expected:t4_view_size,t4_view_data_identity,t4_view_equals,t4_view_stale_reserve_rc,t4_view_stale_pointer_moved,t4_view_stale_data_unchanged,t4_view_stale_size_unchanged`, `fuzz/f100_strview_stale.cp`; U view used after owner `drop()`.

#### `void string::drop(void)` *(destructor)* — `stdlib/string.hp:390-397`
- **Ownership:** releases the owned buffer and zeroes `chars/count/space`.
- **Nullability:** NULL buffer → no-op (`:391`); safe on a zeroed/invalid handle (G-OWN-RAII).
- **Bounds:** n/a. **Allocation:** none (release only). **Failure:** none.
- **Cleanup:** `stdlib_release`, then all fields zeroed; idempotent, and the object can be re-initialised by a later `append`/`reserve`/`copy_from` (`:8-12`).
- **Thread:** G-TS-M during release; compiler insertion is per local object (G-OWN-RAII).
- **ABI:** G-ABI-M — lowers to exported `string_drop(string *self)`; adding/removing it changes the symbol surface.
- **Evidence:** D `:390-397`; O direct `deep_string_task4.expected:t4_drop3_state,t4_drop3_cstr_nonnull,t4_drop3_cstr_empty,t4_drop3_reinit_append,t4_drop3_reinit_reserve,t4_drop3_reinit_copy`, indirect via compiler-inserted `drop()` in `deep_cross_family.cp` CF1/CF5; U --

---

## 4. `stdlib/vector.hp` — growable `int` vector and generation-stamped `slice`

Contract (`:1-20`): checked allocator, overflow-checked growth, all fallible mutations failure-atomic. A `slice` records the owner's generation; reallocation, `pop_back`, `clear`, `insert`, `erase`, `copy_from`, `shrink_to_fit`, and `drop` bump it. `push_back` without reallocation does **not** invalidate slices. `slice::stale()` dereferences the owning vector, so a slice outliving the vector is an undetectable lifetime error (`:16-19`). All containers are not thread-safe (G-TS-C).

### 4.1 `slice` (borrowed, no `drop()`)

#### `slice::slice(int *items, size_t count)` — `stdlib/vector.hp:35-40`
- **Ownership:** borrows items; sets `generation_ref = NULL`, `generation_at = 0` (so a hand-built slice is never "stale" but also cannot detect change).
- **Nullability:** NULL `items` allowed (empty slice). **Bounds:** `count` is caller-supplied and unchecked.
- **Allocation:** none. **Failure:** none. **Cleanup:** none.
- **Thread:** G-TS-P on the slice. **ABI:** G-ABI-M — lowers to `slice_slice(slice self, int *, size_t)` returning by value.
- **Evidence:** D `:27-40`; O indirect via `vector::view()` / `slice::sub()` in `core_extras_test.expected:slice 3 1 1 1,sub 2 1` and `deep_vector_task4.expected:T4_STALE_SLICE_REFUSAL`; U direct two-arg call (spelling-only gap noted in `tests/stdlib/agent2/API_COVERAGE_REPORT.md:213`).

#### `size_t slice::size(void)` / `int slice::stale(void)` / `int slice::valid(void)` — `stdlib/vector.hp:42-54`
- **Ownership:** none. **Nullability:** `stale()` is `0` when `generation_ref == NULL` (`:48-49`); `valid()` requires non-NULL items, `count > 0`, and not stale (`:52-54`).
- **Bounds:** `stale()` dereferences `*generation_ref` — meaningful only while the owner vector is alive (`:16-19`).
- **Allocation:** none. **Failure:** no error channel. **Cleanup:** none.
- **Thread:** G-TS-P on the slice; the owner's generation is a non-atomic `size_t` (G-TS-C).
- **ABI:** G-ABI-M.
- **Evidence:** D `:42-54`; O direct `core_extras_test.expected:slice 3 1 1 1,sub-empty 0 1,after-clear 0`, `deep_vector_task4.expected:T4_STALE_SLICE_REFUSAL,T4_STALE_VIEW_RESERVE`, `fuzz/f100_vec_f072_stale.cp`; U slice outliving the vector (undetectable, documented).

#### `int slice::at_checked(size_t index, int *out)` — `stdlib/vector.hp:57-67`
- **Ownership:** caller owns `out`; items borrowed. **Nullability:** `out == NULL` → `-1` (`:58-59`).
- **Bounds:** `items == NULL || index >= count || stale()` → note `BOUNDS` and `-1`; otherwise writes `*out` (`:60-66`).
- **Allocation:** none. **Failure:** `-1` with a bounds note, no dereference of stale items.
- **Cleanup:** none. **Thread:** G-TS-P on the slice (G-TS-C for owner). **ABI:** G-ABI-M.
- **Evidence:** D `:56-67`; O direct `core_extras_test.cp:21-25` (`core_extras_test.expected:slice 3 1 1 1,sub 2 1`), indirect `deep_vector_task4.cp:73-113`; U --

#### `slice slice::sub(size_t start, size_t length)` — `stdlib/vector.hp:71-85`
- **Ownership:** returns a new borrowed sub-slice; both slices share the same `generation_ref`/`generation_at` (`:81-82`).
- **Nullability:** NULL items, `start >= count`, or stale → returns an empty slice `slice(NULL, 0)` (`:72-75`).
- **Bounds:** `length` clamped to `count - start` (`:76-78`); empty result has `size() == 0` and `valid() == 0`.
- **Allocation:** none. **Failure:** no error channel; "failure" is an empty slice.
- **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-M — returns aggregate by value.
- **Evidence:** D `:69-85`; O direct `core_extras_test.expected:sub 2 1,sub-empty 0 1`, `deep_vector_task4.expected:T4_STALE_SUB,T4_EMPTY_SUB`; U --

### 4.2 `vector` (owning; `drop()` is the destructor)

#### `vector::vector(void)` — `stdlib/vector.hp:94-99`
- **Ownership:** creates an empty owner (`items=NULL, used=0, space=0, generation=0`); zero-init safe.
- **Nullability:** n/a. **Bounds:** n/a. **Allocation:** none. **Failure:** cannot fail.
- **Cleanup:** destructor `vector_drop` (`:297-305`, G-OWN-RAII).
- **Thread:** G-TS-P on the object. **ABI:** G-ABI-M (lowers to `vector_vector` returning the aggregate by value).
- **Evidence:** D `:94-99`; O indirect every vector fixture, e.g. `deep_error_vector.expected:V1_growth`, `foundation.expected:vec 4 8 0 9 -1 16`; U --

#### `size_t vector::size(void)` / `size_t vector::capacity(void)` — `stdlib/vector.hp:101-107`
- **Ownership:** none. **Nullability:** n/a. **Bounds:** `used` elements, `space` allocated ints.
- **Allocation:** none. **Failure:** none. **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-M.
- **Evidence:** D `:101-107`; O direct `foundation.expected:vec 4 8 0 9 -1 16`, `deep_error_vector.expected:V1_growth`; U --

#### `int vector::reserve(size_t want)` — `stdlib/vector.hp:111-133`
- **Ownership:** grows the owned buffer; old buffer released only after copying.
- **Nullability:** n/a. **Bounds:** `want > STDLIB_ALLOC_LIMIT/sizeof(int)` → note `RANGE` and `-1` (`:116-120`); `want <= space` is a no-op `0` (`:114-115`).
- **Allocation:** one `stdlib_alloc(sizeof(int)*want)`; copies `used` elements; bumps `generation` and `STDLIB_TM_GROWTH` (`:121-132`).
- **Failure:** `-1` on over-limit or allocation failure, vector unchanged (failure-atomic).
- **Cleanup:** `stdlib_release` old buffer at `:127`.
- **Thread:** G-TS-M during allocation; object local (G-TS-C). **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:109-133`; O direct `deep_vector_task4.expected:T4_RESERVE_EXACT_AND_OVERFLOW,T4_STALE_VIEW_RESERVE`, `deep_error_vector.cp:462`, `deep_cross_family.cp` CF2; U --

#### `int vector::push_back(int value)` — `stdlib/vector.hp:135-151`
- **Ownership:** appends to owned buffer; may reallocate via `reserve`.
- **Nullability:** n/a. **Bounds:** growth guarded with `(space > (LIMIT/sizeof(int))/2)` → `-1` with note (`:139-143`); initial capacity 4, then doubling (`:136-147`).
- **Allocation:** zero or one `stdlib_alloc` (via `reserve`), telemetry on growth.
- **Failure:** `-1` on growth overflow/allocation failure, vector unchanged.
- **Cleanup:** handled by `reserve`; no per-element cleanup (`int` only).
- **Thread:** G-TS-M during growth; object local (G-TS-C). **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:135-151`; O direct `foundation.expected:vec 4 8 ...`, `deep_error_vector.expected:V1_growth,V6_overflow_shapes,V7_alloc_balance`, `deep_cross_family.cp` CF2, `fuzz/f100_vec_f070_capacity.cp`; U concurrent push_back.

#### `int vector::at(size_t index)` — `stdlib/vector.hp:155-162`
- **Ownership:** none. **Nullability:** n/a. **Bounds:** `index >= used` → note `BOUNDS` and return `0` (compatibility accessor; `0` is indistinguishable from a stored value — prefer `at_checked`, `:153-154`).
- **Allocation:** none. **Failure:** `0` on out-of-range. **Cleanup:** none.
- **Thread:** G-TS-P + note (G-TS-CB); object local. **ABI:** G-ABI-M.
- **Evidence:** D `:153-162`; O direct `foundation.expected:vec ... 9 -1 16` (`at` used with out-of-range), `benchmarks/safety_compare/modern.cp`; U --

#### `int vector::at_checked(size_t index, int *out)` — `stdlib/vector.hp:165-175`
- **Ownership:** caller owns `out`; items borrowed. **Nullability:** `out == NULL` → `-1` (`:166-167`).
- **Bounds:** `index >= used` → note `BOUNDS` and `-1`; otherwise `*out = items[index]` (`:168-174`).
- **Allocation:** none. **Failure:** `-1`, out untouched on failure. **Cleanup:** none.
- **Thread:** G-TS-P + note. **ABI:** G-ABI-M.
- **Evidence:** D `:164-175`; O direct `deep_vector_task4.cp:73-113`, `deep_error_vector.expected:V1_growth`, `fuzz/f100_vec_f070_capacity.cp`; U --

#### `int vector::pop_back(int *out)` — `stdlib/vector.hp:178-187`
- **Ownership:** removes the last element into `out`; no buffer release.
- **Nullability:** `out == NULL` or empty → `-1` (`:179-182`).
- **Bounds:** reads `items[used-1]` only when `used > 0`.
- **Allocation:** none. **Failure:** `-1`, vector unchanged.
- **Cleanup:** bumps `generation` (invalidating slices, `:185`), no `stdlib_release` (storage retained).
- **Thread:** G-TS-C + G-TS-M only if a growth occurred (none here). **ABI:** G-ABI-M.
- **Evidence:** D `:177-187`; O direct `deep_error_vector.expected:V5_empty_get_pop`, `fuzz/f100_vec_f071_erase.cp`, `foundation.expected:vec-clear 0 -1`; U --

#### `int vector::insert(size_t index, int value)` — `stdlib/vector.hp:190-210`
- **Ownership:** may reallocate; shifts elements right. **Nullability:** n/a.
- **Bounds:** `index > used` → `-1` (`:192-193`); `index == used` appends; growth overflow guarded (`:194-203`).
- **Allocation:** zero or one via `reserve`. **Failure:** `-1` leaves vector unchanged (failure-atomic).
- **Cleanup:** bumps `generation` (`:208`), invalidating slices.
- **Thread:** G-TS-C, G-TS-M during growth. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:189-210`; O direct `deep_vector_task4.expected:T4_INSERT_BOUNDARIES`, `deep_vector_task4.cp:73-113`; U --

#### `int vector::erase(size_t index, int *out)` — `stdlib/vector.hp:213-224`
- **Ownership:** removes element at index; `out` may be NULL to discard the value.
- **Nullability:** `out == NULL` allowed; `index >= used` → `-1` (`:215-217`).
- **Bounds:** shifts `items[i+1]` down; no buffer release.
- **Allocation:** none. **Failure:** `-1`, vector unchanged.
- **Cleanup:** bumps `generation` (`:222`).
- **Thread:** G-TS-C. **ABI:** G-ABI-M.
- **Evidence:** D `:212-224`; O direct `deep_vector_task4.expected:T4_ERASE_BOUNDARIES`, `fuzz/f100_vec_f071_erase.cp`; U --

#### `void vector::clear(void)` — `stdlib/vector.hp:226-229`
- **Ownership:** retains buffer, sets `used = 0`. **Nullability:** n/a. **Bounds:** n/a.
- **Allocation:** none. **Failure:** none. **Cleanup:** bumps `generation` (`:228`); no element release.
- **Thread:** G-TS-C. **ABI:** G-ABI-M.
- **Evidence:** D `:226-229`; O direct `foundation.expected:vec-clear 0 -1`, `deep_vector_task4.expected:T4_CLEAR_DROP_REUSE`, `deep_error_vector.expected:V3_stale_slices`; U --

#### `int vector::copy_from(vector *other)` — `stdlib/vector.hp:233-259`
- **Ownership:** replaces contents with a copy; `other` borrowed; `other == this` → `0` no-op (`:241-242`).
- **Nullability:** `other == NULL` → note + `-1` (`:236-240`).
- **Bounds:** `other->used > LIMIT/sizeof(int)` → `-1` (`:244-245`); allocates `sizeof(int)*other->used` (zero when empty → `fresh` stays NULL).
- **Allocation:** exactly one `stdlib_alloc` for non-empty source (`:246`).
- **Failure:** `-1` leaves receiver unchanged (failure-atomic).
- **Cleanup:** releases old buffer only after copy (`:252-253`); bumps `generation` (`:257`); sets `space = other->used` (no spare capacity).
- **Thread:** G-TS-C, G-TS-M during allocation. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:231-259`; O direct `deep_vector_task4.expected:T4_COPY_FROM,T4_COPY_FROM_BALANCE`, `deep_error_vector.cp`; U --

#### `int vector::shrink_to_fit(void)` — `stdlib/vector.hp:263-286`
- **Ownership:** reallocates to `used` ints (or releases entirely when empty).
- **Nullability:** n/a. **Bounds:** `space == used` → `0` no-op (`:266-267`); allocation guarded by `sizeof(int)*used`.
- **Allocation:** one `stdlib_alloc` when `used > 0` (`:276`).
- **Failure:** `-1` leaves the old, larger buffer valid (failure-atomic, `:261-262`).
- **Cleanup:** `stdlib_release(this->items)` at `:281` (or `:270` for empty); bumps `generation` (`:273,284`).
- **Thread:** G-TS-C, G-TS-M during allocation. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:261-286`; O direct `deep_vector_task4.expected:T4_SHRINK_NOOP_AND_SUCCESS,T4_SHRINK_FAILED_ATOMIC,T4_SHRINK_TO_EMPTY`; U --

#### `slice vector::view(void)` — `stdlib/vector.hp:290-295`
- **Ownership:** returns a borrowed slice stamped with the current generation pointer/value (`:292-293`); no allocation or lifetime extension.
- **Nullability:** `items` may be NULL for an empty vector → empty slice.
- **Bounds:** captures `used` as the slice count. **Allocation:** none. **Failure:** none.
- **Cleanup:** caller must not use a stale slice (`slice::stale()`); slice must not outlive the vector (`:16-19`).
- **Thread:** G-TS-C; reading `generation` is unsynchronised with concurrent mutation.
- **ABI:** G-ABI-M — returns the `slice` aggregate by value.
- **Evidence:** D `:288-295`; O direct `core_extras_test.expected:slice 3 1 1 1`, `deep_vector_task4.expected:T4_STALE_VIEW_RESERVE,T4_STALE_VIEW_DROP`, `fuzz/f100_vec_f072_stale.cp`; U slice outliving vector.

#### `void vector::drop(void)` *(destructor)* — `stdlib/vector.hp:297-305`
- **Ownership:** releases the owned buffer.
- **Nullability:** NULL `items` → no-op (`:298`); safe on a zeroed handle.
- **Bounds:** n/a. **Allocation:** none (release only). **Failure:** none.
- **Cleanup:** `stdlib_release`, zero `items/used/space`, bump `generation` (`:299-304`); idempotent.
- **Thread:** G-TS-M during release; compiler-inserted per local (G-OWN-RAII).
- **ABI:** G-ABI-M — lowers to exported `vector_drop(vector *self)`.
- **Evidence:** D `:297-305`; O direct `deep_vector_task4.expected:T4_CLEAR_DROP_REUSE,T4_STALE_VIEW_DROP`, `bd_drop_*` family in `deep_collections_boundary.expected` (cross-family RAII); U --

---

## 5. `stdlib/collections.hp` — maps, set, deque, list, stack, queue

Common contract (`:1-25`): explicit ownership, drop exactly once and idempotent, zero-initialised container is valid and empty; all allocating operations failure-atomic with rollback (`:12-15`); sizes overflow-checked against `STDLIB_ALLOC_LIMIT` (`:16-17`); open addressing + tombstones + power-of-two capacity, table never allowed to fill (`:18-19`); **no iterators** — access is by value so callers cannot hold a dangling reference (`:20-22`); **not thread-safe** (`:23-24`, G-TS-C). `drop()` is a destructor for every class here (G-OWN-RAII).

### 5.1 Free helpers

#### `unsigned long long stdlib_map_hash_int(long long key)` — `stdlib/collections.hp:37-45`
- **Ownership:** none. **Nullability:** n/a (any `long long`, including negative, cast to unsigned). **Bounds:** n/a.
- **Allocation:** none. **Failure:** none. **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-H; pure 64-bit mix.
- **Evidence:** D `:37-45`; O indirect via all map/set fixtures (`deep_collections_collisions.expected:c_map_probe_all,c_set_insert`); U direct value assertion.

#### `unsigned long long stdlib_map_hash_text(const char *text)` — `stdlib/collections.hp:47-54`
- **Ownership:** borrows `text`. **Nullability:** NULL → FNV offset basis (`:49`). **Bounds:** reads until NUL; caller must supply a terminated string.
- **Allocation:** none. **Failure:** none. **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-H.
- **Evidence:** D `:47-54`; O indirect `deep_collections_collisions.expected:c_str_probe_all,c_diff_model`; U direct value assertion.

#### `size_t stdlib_map_next_pow2(size_t want)` — `stdlib/collections.hp:57-65`
- **Ownership:** none. **Nullability:** n/a. **Bounds:** returns `0` on overflow (`cap > SIZE_MAX>>2`, `:60-61`); minimum 8.
- **Allocation:** none. **Failure:** `0` = overflow. **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-H.
- **Evidence:** D `:56-65`; O direct `deep_collections_boundary.expected:bd_mincap_intmap,bd_maxcap_*`; U --

#### `int stdlib_map_needs_room(size_t used_slots, size_t capacity)` — `stdlib/collections.hp:68-72`
- **Ownership:** none. **Nullability:** n/a. **Bounds:** 70% load test using 64-bit products (`:70-71`); `capacity == 0` → `1`.
- **Allocation:** none. **Failure:** none. **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-H.
- **Evidence:** D `:67-72`; O indirect via growth at the 6th insert in `deep_collections_growth.expected:g_grow3,g_grow7,g_grow9`; U direct call (0 references under `tests/`).

#### `size_t stdlib_map_rebuild_capacity(size_t count, size_t capacity)` — `stdlib/collections.hp:77-86`
- **Ownership:** none. **Nullability:** n/a. **Bounds:** `capacity == 0` → 8; `> SIZE_MAX>>2` → `0` (overflow); doubles when live entries exceed half, else same size to purge tombstones (`:78-86`).
- **Allocation:** none. **Failure:** `0` = overflow. **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-H.
- **Evidence:** D `:74-86`; O indirect `deep_collections_growth.expected:g_tomb_remove,g_reinsert`; U direct call.

#### `int *stdlib_ring_copy(const int *items, size_t space, size_t head, size_t used, size_t newspace)` — `stdlib/collections.hp:90-116`
- **Ownership:** returns a freshly owned linearised array; caller must `stdlib_release` it. Does not touch the source (`:88-89`).
- **Nullability:** `items == NULL` allowed when `space == 0` (`:104-105`); NULL return on failure.
- **Bounds:** `newspace > LIMIT/sizeof(int)` → note + NULL (`:94-98`); one `pos -= space` is sufficient given `head < space` and `i < used <= space` (`:109-111`).
- **Allocation:** one `stdlib_alloc(sizeof(int)*newspace)` (`:99`).
- **Failure:** NULL on over-limit or allocation failure; source unchanged.
- **Cleanup:** caller releases; on caller failure the fresh buffer must be released (call sites do so via their own failure-atomic discipline).
- **Thread:** G-TS-M during allocation; source not locked. **ABI:** G-ABI-H, G-ABI-T; plain `int *`.
- **Evidence:** D `:88-116`; O direct `regressions/adv_null_zero.cp`, indirect `deep_collections_alloc.expected:dpbf_probe,dpff_probe,dpgw_probe,qgw_probe,qf_probe`; U --

### 5.2 `intmap` (owning `int → int` hash map)

#### `intmap::intmap(void)` — `stdlib/collections.hp:128-135`
- **Ownership:** empty owner (all three buffers NULL, counters 0); zero-init safe. **Nullability:** n/a. **Bounds:** n/a.
- **Allocation:** none. **Failure:** none. **Cleanup:** `intmap_drop` (`:289-302`).
- **Thread:** G-TS-P on object. **ABI:** G-ABI-M.
- **Evidence:** D `:128-135`; O indirect `deep_collections_boundary.expected:bd_empty_intmap`; U --

#### `size_t intmap::size(void)` / `int intmap::valid(void)` — `stdlib/collections.hp:137-143`
- **Ownership:** none. **Nullability:** n/a. **Bounds:** `valid()` is `state != NULL` — false before first allocation and after `drop()`.
- **Allocation:** none. **Failure:** none. **Cleanup:** none. **Thread:** G-TS-P. **ABI:** G-ABI-M.
- **Evidence:** D `:137-143`; O direct `deep_collections_boundary.expected:bd_empty_intmap,bd_maxcap_intmap,bd_drop_intmap`, `collections_test.expected:empty 0 1 1 1`; U --

#### `long long intmap::stdlib_probe(int key)` *(internal)* — `stdlib/collections.hp:147-161`
- **Ownership:** none; returns slot index or `-1`. **Nullability:** `state == NULL` → `-1` (`:150-151`).
- **Bounds:** loop bounded by `capacity` and stops at `MAP_EMPTY`, so probing terminates because the table is never full (`:145-159`).
- **Allocation:** none. **Failure:** `-1` = not found. **Cleanup:** none. **Thread:** G-TS-P on object. **ABI:** G-ABI-M.
- **Evidence:** D `:145-161`; O indirect `deep_collections_collisions.expected:c_map_probe_all,c_map_removed`; U direct call.

#### `int intmap::stdlib_rebuild(size_t cap)` *(internal)* — `stdlib/collections.hp:165-210`
- **Ownership:** allocates three fresh buffers then swaps; releases the old three only after all succeed (`:172-205`).
- **Nullability:** n/a. **Bounds:** `cap < 8 || cap > LIMIT/sizeof(int)` → `-1` (`:170-171`); `cap` assumed power of two for the `& (cap-1)` mask.
- **Allocation:** three `stdlib_alloc` calls (keys, values, state); partial failure rolls back earlier ones (`:173-185`).
- **Failure:** `-1` with all earlier fresh buffers released, map unchanged.
- **Cleanup:** releases old buffers at `:197-202`; sets `used_slots = count`; `STDLIB_TM_GROWTH` (`:207-208`).
- **Thread:** G-TS-C + G-TS-M during allocation. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:163-210`; O indirect `deep_collections_growth.expected:g_grow3,g_grow7,g_grow9,g_grow_overflow,g_oom1,g_oom2,g_oom3`; U direct call.

#### `int intmap::grow(size_t want)` — `stdlib/collections.hp:214-221`
- **Ownership:** may rebuild owned buffers; never shrinks. **Nullability:** n/a.
- **Bounds:** rounded up by `stdlib_map_next_pow2`; `0` (overflow) → `-1`; `cap <= capacity` → `0` no-op.
- **Allocation:** via `stdlib_rebuild`. **Failure:** `-1`, map unchanged.
- **Cleanup:** old buffers released by `stdlib_rebuild`. **Thread:** G-TS-C, G-TS-M. **ABI:** G-ABI-M.
- **Evidence:** D `:212-221`; O direct `deep_collections_growth.expected:g_grow3,g_grow7,g_grow9,g_grow100,g_grow_noop,g_grow_big,g_grow_overflow`, `deep_collections_boundary.expected:bd_maxcap_intmap`; U --

#### `int intmap::put(int key, int value)` — `stdlib/collections.hp:223-255`
- **Ownership:** owns copied key/value storage; no caller memory retained. **Nullability:** n/a.
- **Bounds:** rebuild guarded by `stdlib_map_rebuild_capacity`; probing loop bounded by capacity (`:237-254`).
- **Allocation:** zero (overwrite) or three buffers via rebuild (`:232-236`).
- **Failure:** `-1` on rebuild failure, map unchanged (failure-atomic).
- **Cleanup:** tombstones reused via `first_deleted`; `count`/`used_slots` updated (`:239-248`).
- **Thread:** G-TS-C, G-TS-M. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:223-255`; O direct `collections_test.expected:empty 0 1 1 1,grown 200 1 1,overwrite 200 1 1`, `deep_collections_growth.expected:g_insert,g_reinsert_vals`, `deep_collections_boundary.expected:bd_repeat_intmap`; U --

#### `int intmap::get(int key, int *out)` — `stdlib/collections.hp:257-266`
- **Ownership:** caller owns `out`; map borrowed. **Nullability:** `out == NULL` → `-1` (`:259-260`).
- **Bounds:** probe bounded; writes `*out` only on hit. **Allocation:** none.
- **Failure:** `-1` when absent or `out == NULL`; `out` untouched on failure. **Cleanup:** none.
- **Thread:** G-TS-C. **ABI:** G-ABI-M.
- **Evidence:** D `:257-266`; O direct `collections_test.expected:grown 200 1 1`, `deep_collections_collisions.expected:c_map_values`, `deep_collections_boundary.expected:bd_missing_maps`; U --

#### `int intmap::contains(int key)` — `stdlib/collections.hp:268-271`
- **Ownership:** none. **Nullability:** n/a. **Bounds:** delegates to `get`. **Allocation:** none.
- **Failure:** `0`/`1`. **Cleanup:** none. **Thread:** G-TS-C. **ABI:** G-ABI-M.
- **Evidence:** D `:268-271`; O direct `collections_test.expected:remove 1 1 1 199`, `deep_collections_collisions.expected:c_map_kept`; U --

#### `int intmap::remove(int key)` — `stdlib/collections.hp:273-280`
- **Ownership:** marks the slot `MAP_DELETED`; no key storage to free (int keys).
- **Nullability:** n/a. **Bounds:** probe bounded. **Allocation:** none.
- **Failure:** `-1` when absent; map unchanged. **Cleanup:** decrements `count`; tombstone retained until rebuild (`:277-279`).
- **Thread:** G-TS-C. **ABI:** G-ABI-M.
- **Evidence:** D `:273-280`; O direct `collections_test.expected:remove 1 1 1 199`, `deep_collections_collisions.expected:c_map_removed`, `deep_collections_boundary.expected:bd_tomb_intmap`; U --

#### `void intmap::clear(void)` — `stdlib/collections.hp:282-287`
- **Ownership:** retains all three buffers; resets state to `MAP_EMPTY` and counters to 0.
- **Nullability:** `state == NULL` → skips memset, still zeroes counters (`:283-286`).
- **Bounds:** memsets exactly `capacity` state bytes. **Allocation:** none. **Failure:** none. **Cleanup:** none (deferred to `drop`).
- **Thread:** G-TS-C. **ABI:** G-ABI-M.
- **Evidence:** D `:282-287`; O direct `collections_test.expected:clear 0 1 1`, `agent2/extra_collections_clear.cp` (STDLIB_ALLOC_LIMIT=256); U --

#### `void intmap::drop(void)` *(destructor)* — `stdlib/collections.hp:289-302`
- **Ownership:** releases keys/values/state exactly once; idempotent.
- **Nullability:** each pointer NULL-guarded (`:290-295`); safe on zeroed handle.
- **Bounds:** n/a. **Allocation:** none (release only). **Failure:** none.
- **Cleanup:** releases all three buffers and zeroes all six fields (`:296-301`).
- **Thread:** G-TS-C + G-TS-M during release; compiler-inserted per local. **ABI:** G-ABI-M → exported `intmap_drop`.
- **Evidence:** D `:289-302`; O direct `collections_test.expected:dropped 0 1`, `deep_collections_boundary.expected:bd_drop_intmap`; U --

### 5.3 `strmap` (owning `char * → int` hash map with owned keys)

Same hash-map rules as `intmap`; keys are **deep-copied** into tracked storage, so the source string's later mutation/drop cannot change the map (`deep_cross_family.cp` CF1).

#### `strmap::strmap(void)` / `size_t strmap::size(void)` / `int strmap::valid(void)` — `stdlib/collections.hp:315-330`
- **Ownership:** empty owner; `size()` = live entries; `valid()` = `state != NULL`.
- **Nullability:** n/a. **Bounds:** n/a. **Allocation:** none at construction. **Failure:** none. **Cleanup:** `strmap_drop`.
- **Thread:** G-TS-P on object. **ABI:** G-ABI-M.
- **Evidence:** D `:315-330`; O direct `collections_test.expected:strmap-empty 0 1 1`, `deep_collections_boundary.expected:bd_empty_strmap,bd_invalid_strmap`; U --

#### `char *strmap::stdlib_copy_key(const char *text)` *(internal)* — `stdlib/collections.hp:333-346`
- **Ownership:** returns a new owned copy; caller (the map) must `stdlib_release` it.
- **Nullability:** `text == NULL` → NULL (`:336-337`). **Bounds:** `len >= STDLIB_ALLOC_LIMIT` → NULL; allocates `len+1` including NUL (`:339-344`).
- **Allocation:** one `stdlib_alloc(len+1)`. **Failure:** NULL on bad input/limit/allocation failure.
- **Cleanup:** caller must release on success (map does so in `remove`/`clear`/`drop`).
- **Thread:** G-TS-M during allocation. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:332-346`; O indirect `deep_collections_growth.expected:s_oom_keycopy,s_oom_keycopy_retry`, `deep_cross_family.cp` CF1; U direct call.

#### `long long strmap::stdlib_probe(const char *key)` *(internal)* — `stdlib/collections.hp:348-363`
- **Ownership:** none; borrows `key`. **Nullability:** `state == NULL || key == NULL` → `-1` (`:351-352`).
- **Bounds:** capacity-bounded probing with `strcmp` (`:354-361`). **Allocation:** none.
- **Failure:** `-1` = not found. **Cleanup:** none. **Thread:** G-TS-P on object. **ABI:** G-ABI-M.
- **Evidence:** D `:348-363`; O indirect `deep_collections_collisions.expected:c_str_probe_all`; U direct call.

#### `int strmap::stdlib_rebuild(size_t cap)` *(internal)* — `stdlib/collections.hp:367-414`
- **Ownership:** allocates three fresh buffers; **moves** key pointers (not copies), so no post-allocation failure (`:365-366`).
- **Nullability:** n/a. **Bounds:** `cap < 8 || cap > LIMIT/sizeof(char*)` → `-1` (`:372-373`).
- **Allocation:** three `stdlib_alloc`; partial failure rolls back (`:374-387`); fresh key array zeroed (`:388`).
- **Failure:** `-1` with rollback, map unchanged.
- **Cleanup:** releases the old three *arrays* but not the key strings (they are moved) (`:401-406`); `used_slots = count`, growth telemetry.
- **Thread:** G-TS-C, G-TS-M. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:365-414`; O indirect `deep_collections_growth.expected:g_oom1,g_oom2,g_oom3,g_oom_retry`; U direct call.

#### `int strmap::grow(size_t want)` — `stdlib/collections.hp:416-423`
- **Ownership:** may rebuild; never shrinks. **Nullability:** n/a. **Bounds:** power-of-two rounding; `0` → `-1`; no-op when `cap <= capacity`.
- **Allocation:** via `stdlib_rebuild`. **Failure:** `-1`, unchanged. **Cleanup:** as rebuild. **Thread:** G-TS-C, G-TS-M. **ABI:** G-ABI-M.
- **Evidence:** D `:416-423`; O direct `collections_test.expected:strmap-grown 1 1 1,strmap-grow 1 1`, `deep_collections_boundary.expected:bd_maxcap_strmap`; U --

#### `int strmap::put(const char *key, int value)` — `stdlib/collections.hp:425-468`
- **Ownership:** deep-copies the key into tracked storage; overwrite of an existing key does not copy (`:436-438`).
- **Nullability:** `key == NULL` → note `INVALID`/`UNSUPPORTED` and `-1` (`:430-434`).
- **Bounds:** key length checked in `stdlib_copy_key`; probing bounded by capacity.
- **Allocation:** rebuild buffers plus one key copy — the key copy is the **last** fallible step (`:445-449`).
- **Failure:** `-1` on rebuild or key-copy failure, map unchanged (failure-atomic).
- **Cleanup:** on success the map owns the copied key until `remove`/`clear`/`drop`.
- **Thread:** G-TS-C, G-TS-M. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:425-468`; O direct `collections_test.expected:strmap-put 1 1 1`, `deep_collections_growth.expected:s_empty_put,s_empty_overwrite,s_oom_keycopy,s_oom_keycopy_retry`, `deep_cross_family.expected:CF 1 13 0` (CF1); U --

#### `int strmap::get(const char *key, int *out)` / `int strmap::contains(const char *key)` — `stdlib/collections.hp:470-484`
- **Ownership:** caller owns `out`; key borrowed. **Nullability:** `key == NULL || out == NULL` → `-1` (no note here, unlike `put`, `:472-473`); `contains` passes a local `value`.
- **Bounds:** probe bounded. **Allocation:** none. **Failure:** `-1` when absent/bad args, `out` zero-initialized by `contains` only.
- **Cleanup:** none. **Thread:** G-TS-C. **ABI:** G-ABI-M.
- **Evidence:** D `:470-484`; O direct `collections_test.expected:strmap-get 1 1 1`, `deep_collections_boundary.expected:bd_missing_maps`, `deep_collections_growth.expected:s_null_queries`; U --

#### `int strmap::remove(const char *key)` — `stdlib/collections.hp:486-495`
- **Ownership:** releases the owned key string and tombstones the slot.
- **Nullability:** `key == NULL` → probe returns `-1` → `-1` (no explicit note).
- **Bounds:** probe bounded. **Allocation:** none. **Failure:** `-1` when absent, map unchanged.
- **Cleanup:** `stdlib_release(keys[found]); keys[found] = NULL; state = MAP_DELETED; count--` (`:490-493`).
- **Thread:** G-TS-C + G-TS-M during release. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:486-495`; O direct `collections_test.expected:strmap-remove 1 1 1`, `deep_collections_boundary.expected:bd_tomb_strmap`; U --

#### `void strmap::clear(void)` — `stdlib/collections.hp:497-508`
- **Ownership:** releases **every** owned key string, sets slots empty; buffers retained.
- **Nullability:** NULL-key guard at `:500`. **Bounds:** loops `capacity` slots. **Allocation:** none.
- **Failure:** none. **Cleanup:** each `keys[i]` released and NULLed (`:501-503`); counters zeroed.
- **Thread:** G-TS-C + G-TS-M. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:497-508`; O direct `collections_test.expected:strmap-clear 0 1`, `agent2/extra_collections_clear.cp`; U --

#### `void strmap::drop(void)` *(destructor)* — `stdlib/collections.hp:510-525`
- **Ownership:** calls `clear()` (freeing all key strings) then releases the three arrays; idempotent.
- **Nullability:** `state == NULL` skips `clear` (`:511-512`); each array pointer NULL-guarded.
- **Bounds:** n/a. **Allocation:** none (release only). **Failure:** none.
- **Cleanup:** all six fields zeroed (`:519-524`).
- **Thread:** G-TS-C + G-TS-M; compiler-inserted per local. **ABI:** G-ABI-M → `strmap_drop`.
- **Evidence:** D `:510-525`; O direct `collections_test.expected:strmap-dropped 0 1`, `deep_collections_boundary.expected:bd_drop_strmap`, `deep_cross_family.cp` CF1; U --

### 5.4 `intset`

#### `intset::intset(void)` / `size_t intset::size(void)` / `int intset::valid(void)` — `stdlib/collections.hp:537-551`
- **Ownership:** empty owner; `valid()` = `state != NULL`. **Nullability:** n/a. **Bounds:** n/a.
- **Allocation:** none. **Failure:** none. **Cleanup:** `intset_drop`. **Thread:** G-TS-P on object. **ABI:** G-ABI-M.
- **Evidence:** D `:537-551`; O direct `deep_collections_boundary.expected:bd_empty_intset,bd_maxcap_intset`; U --

#### `long long intset::stdlib_probe(int key)` / `int intset::stdlib_rebuild(size_t cap)` *(internal)* — `stdlib/collections.hp:553-603`
- **Ownership:** probe borrows; rebuild allocates two fresh buffers (keys, state) then swaps.
- **Nullability:** probe `state == NULL` → `-1` (`:556-557`); rebuild n/a.
- **Bounds:** `cap < 8 || cap > LIMIT/sizeof(int)` → `-1` (`:573-574`); probe bounded by capacity.
- **Allocation:** two `stdlib_alloc` in rebuild; second failure releases the first (`:578-582`).
- **Failure:** probe `-1` = absent; rebuild `-1` with rollback, set unchanged.
- **Cleanup:** rebuild releases old arrays, sets `used_slots = count`, growth telemetry (`:593-601`).
- **Thread:** G-TS-C, G-TS-M in rebuild. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:553-603`; O indirect `deep_collections_alloc.expected:isf_probe,isf_o1,isf_r1,isf_lk1`, `deep_collections_growth.expected:g_tomb_remove,g_oom1`; U direct calls.

#### `int intset::grow(size_t want)` — `stdlib/collections.hp:605-612`
- **Ownership:** may rebuild; never shrinks. **Nullability:** n/a. **Bounds:** power-of-two, `0` → `-1`.
- **Allocation:** via `stdlib_rebuild`. **Failure:** `-1`, unchanged. **Cleanup:** as rebuild. **Thread:** G-TS-C, G-TS-M. **ABI:** G-ABI-M.
- **Evidence:** D `:605-612`; O indirect `deep_collections_alloc.expected:isg_probe,isg_o1`, `deep_collections_growth.expected:g_grow3`; U direct call.

#### `int intset::insert(int key)` — `stdlib/collections.hp:616-643`
- **Ownership:** owns slot storage; no caller memory. **Nullability:** n/a.
- **Bounds:** rebuild capacity guarded; probe/insert loops bounded by capacity (`:626-642`).
- **Allocation:** two buffers via `stdlib_rebuild` when room is needed.
- **Failure:** `-1` only when memory cannot be obtained; set unchanged (`:614-615`).
- **Cleanup:** reuses the first tombstone; `used_slots`/`count` updated (`:628-636`).
- **Thread:** G-TS-C, G-TS-M. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:614-643`; O direct `deep_collections_alloc.expected:isf_probe,isf_r1`, `deep_collections_collisions.expected:c_set_insert,c_set_reinsert`, `deep_collections_boundary.expected:bd_bound_intset`; U --

#### `int intset::contains(int key)` / `int intset::remove(int key)` — `stdlib/collections.hp:645-656`
- **Ownership:** `contains` read-only; `remove` tombstones. **Nullability:** n/a.
- **Bounds:** probe bounded. **Allocation:** none. **Failure:** `remove` → `-1` when absent, set unchanged.
- **Cleanup:** `remove` decrements `count`, leaves tombstone (`:653-655`). **Thread:** G-TS-C. **ABI:** G-ABI-M.
- **Evidence:** D `:645-656`; O direct `deep_collections_collisions.expected:c_set_removed`, `deep_collections_boundary.expected:bd_tomb_intset`; U --

#### `void intset::clear(void)` / `void intset::drop(void)` *(destructor)* — `stdlib/collections.hp:658-675`
- **Ownership:** `clear` retains buffers and resets state/counters; `drop` releases keys + state and zeroes all fields; idempotent.
- **Nullability:** NULL guards throughout (`:659,666-669`). **Bounds:** memset of `capacity` state bytes. **Allocation:** none.
- **Failure:** none. **Cleanup:** as described. **Thread:** G-TS-C, G-TS-M in drop. **ABI:** G-ABI-M → `intset_drop`.
- **Evidence:** D `:658-675`; O direct `deep_collections_boundary.expected:bd_drop_intset`, `agent2/extra_collections_clear.cp`; U --

### 5.5 `deque` (ring buffer)

#### `deque::deque(void)` / `size_t deque::size(void)` / `size_t deque::capacity(void)` — `stdlib/collections.hp:686-699`
- **Ownership:** empty ring owner (`items=NULL, head=used=space=0`). **Nullability:** n/a. **Bounds:** n/a.
- **Allocation:** none. **Failure:** none. **Cleanup:** `deque_drop`. **Thread:** G-TS-P on object. **ABI:** G-ABI-M.
- **Evidence:** D `:686-699`; O direct `deep_collections_boundary.expected:bd_empty_deque,bd_mincap_deque`; U --

#### `int deque::reserve(size_t want)` — `stdlib/collections.hp:702-717`
- **Ownership:** replaces owned buffer with a linearised copy. **Nullability:** n/a.
- **Bounds:** `want <= space` → `0` no-op (`:704-705`); `stdlib_ring_copy` enforces `LIMIT/sizeof(int)`.
- **Allocation:** one fresh buffer via `stdlib_ring_copy` (`:706`).
- **Failure:** `-1` leaves deque unchanged (failure-atomic).
- **Cleanup:** releases the old buffer (`:710-711`), sets `head = 0`, `space = want`, growth telemetry.
- **Thread:** G-TS-C, G-TS-M. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:701-717`; O direct `deep_collections_boundary.expected:bd_maxcap_deque_reserve,bd_bound_deque_wrap`; U --

#### `int deque::stdlib_make_room(void)` *(internal)* — `stdlib/collections.hp:719-731`
- **Ownership:** may trigger reserve. **Nullability:** n/a. **Bounds:** `space == 0` → 8; otherwise doubling guarded by `(space > (LIMIT/sizeof(int))/2)` → `-1` (`:723-729`).
- **Allocation:** via `reserve`. **Failure:** `-1`, unchanged. **Cleanup:** via reserve. **Thread:** G-TS-C, G-TS-M. **ABI:** G-ABI-M.
- **Evidence:** D `:719-731`; O indirect `deep_collections_alloc.expected:dpbf_probe,dpff_probe,dgw_probe`; U direct call.

#### `int deque::push_back(int value)` / `int deque::push_front(int value)` — `stdlib/collections.hp:733-754`
- **Ownership:** append/prepend into owned ring; may reallocate. **Nullability:** n/a.
- **Bounds:** room via `stdlib_make_room`; index wrap uses a single subtraction (`:736-739,747-749`).
- **Allocation:** zero or one via reserve. **Failure:** `-1` on room/allocation failure, deque unchanged.
- **Cleanup:** via reserve; no element cleanup (`int`). **Thread:** G-TS-C, G-TS-M. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:733-754`; O direct `deep_collections_alloc.expected:dpbf_probe,dpff_probe,dpff_o1,dpff_r1`, `deep_collections_boundary.expected:bd_bound_deque_wrap,bd_stress_deque`; U --

#### `int deque::pop_front(int *out)` / `int deque::pop_back(int *out)` — `stdlib/collections.hp:756-777`
- **Ownership:** removes an element into `out`; buffer retained. **Nullability:** `out == NULL || used == 0` → `-1` (`:757-758,769-770`).
- **Bounds:** head/tail wrap with one subtraction (`:760-762,771-773`). **Allocation:** none.
- **Failure:** `-1`, deque unchanged. **Cleanup:** `used--`; no release. **Thread:** G-TS-C. **ABI:** G-ABI-M.
- **Evidence:** D `:756-777`; O direct `deep_collections_boundary.expected:bd_bound_deque,bd_nullout_deque`, `fuzz/f100_cont_f066_deque_ring.cp`; U --

#### `int deque::at_checked(size_t index, int *out)` — `stdlib/collections.hp:779-792`
- **Ownership:** caller owns `out`; items borrowed. **Nullability:** `out == NULL` → `-1` (`:780-781`).
- **Bounds:** `index >= used` → note `BOUNDS` and `-1`; wrapped index computed once (`:782-790`).
- **Allocation:** none. **Failure:** `-1`, out untouched. **Cleanup:** none. **Thread:** G-TS-C + note. **ABI:** G-ABI-M.
- **Evidence:** D `:779-792`; O direct `deep_collections_boundary.expected:bd_bound_deque`, `fuzz/f100_cont_f066_deque_ring.cp`; U --

#### `void deque::clear(void)` / `void deque::drop(void)` *(destructor)* — `stdlib/collections.hp:794-806`
- **Ownership:** `clear` retains buffer and resets `head/used`; `drop` releases buffer and zeroes all four fields; idempotent.
- **Nullability:** NULL guard in `drop` (`:800`). **Bounds:** n/a. **Allocation:** none. **Failure:** none.
- **Cleanup:** as described. **Thread:** G-TS-C + G-TS-M in drop. **ABI:** G-ABI-M → `deque_drop`.
- **Evidence:** D `:794-806`; O direct `deep_collections_boundary.expected:bd_drop_deque`, `agent2/extra_collections_clear.cp`; U --

### 5.6 `list` (doubly linked, per-node allocation)

`list_node` is a plain `struct` typedef (`:811-815`): `int value; list_node *prev; list_node *next;` — no methods; nodes are allocated with `stdlib_alloc(sizeof(list_node))`.

#### `list::list(void)` / `size_t list::size(void)` — `stdlib/collections.hp:822-830`
- **Ownership:** empty owner (`first=last=NULL, used=0`). **Nullability:** n/a. **Bounds:** n/a.
- **Allocation:** none. **Failure:** none. **Cleanup:** `list_drop` → `clear`. **Thread:** G-TS-P on object. **ABI:** G-ABI-M; node struct is a plain C `struct` (G-ABI-T).
- **Evidence:** D `:822-830`; O direct `deep_collections_boundary.expected:bd_empty_list`; U --

#### `int list::push_back(int value)` / `int list::push_front(int value)` — `stdlib/collections.hp:834-864`
- **Ownership:** allocates one owned node before touching any link, so failure is atomic (`:832-833`).
- **Nullability:** n/a. **Bounds:** n/a. **Allocation:** exactly one `stdlib_alloc(sizeof(list_node))` per call (`:835,851`).
- **Failure:** `-1` on allocation failure, list unchanged.
- **Cleanup:** links updated on success only (`:838-846,854-862`).
- **Thread:** G-TS-C, G-TS-M. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:832-864`; O direct `deep_collections_alloc.expected:lpbe_probe,lpbn_probe,lpfe_probe,lpfn_probe`, `deep_collections_boundary.expected:bd_bound_list,bd_stress_list`; U --

#### `int list::pop_front(int *out)` / `int list::pop_back(int *out)` — `stdlib/collections.hp:866-896`
- **Ownership:** unlinks one node and releases it. **Nullability:** `out == NULL || first/last == NULL` → `-1`.
- **Bounds:** reads `node->value` before unlinking. **Allocation:** none.
- **Failure:** `-1` on empty/bad out, list unchanged.
- **Cleanup:** `stdlib_release(node)` at `:878,894`; updates `first`/`last`, decrements `used`.
- **Thread:** G-TS-C + G-TS-M. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:866-896`; O direct `deep_collections_boundary.expected:bd_bound_list,bd_nullout_list`, `fuzz/f100_cont_f067_list_churn.cp`; U --

#### `int list::at_checked(size_t index, int *out)` — `stdlib/collections.hp:899-914`
- **Ownership:** caller owns `out`; traverses owned nodes. **Nullability:** `out == NULL` → `-1` (`:902-903`).
- **Bounds:** `index >= used` → note `BOUNDS` and `-1`; O(n) walk (`:898,904-911`).
- **Allocation:** none. **Failure:** `-1`, out untouched. **Cleanup:** none.
- **Thread:** G-TS-C + note. **ABI:** G-ABI-M.
- **Evidence:** D `:898-914`; O direct `deep_collections_boundary.expected:bd_bound_list`, `fuzz/f100_cont_f067_list_churn.cp`; U --

#### `void list::clear(void)` / `void list::drop(void)` *(destructor)* — `stdlib/collections.hp:916-930`
- **Ownership:** releases every node exactly once; `drop` simply calls `clear`.
- **Nullability:** starts from `first` which may be NULL (`:917-918`). **Bounds:** n/a. **Allocation:** none.
- **Failure:** none. **Cleanup:** saves `next` before each release (`:919-921`), then `first=last=NULL, used=0`; idempotent.
- **Thread:** G-TS-C + G-TS-M. **ABI:** G-ABI-M → `list_drop`.
- **Evidence:** D `:916-930`; O direct `deep_collections_boundary.expected:bd_drop_list`, `agent2/extra_collections_clear.cp`; U --

### 5.7 `stack`

#### `stack::stack(void)` / `size_t stack::size(void)` — `stdlib/collections.hp:940-948`
- **Ownership:** empty owner. **Nullability:** n/a. **Bounds:** n/a. **Allocation:** none.
- **Failure:** none. **Cleanup:** `stack_drop`. **Thread:** G-TS-P on object. **ABI:** G-ABI-M.
- **Evidence:** D `:940-948`; O direct `deep_collections_boundary.expected:bd_empty_stack`; U --

#### `int stack::push(int value)` — `stdlib/collections.hp:950-973`
- **Ownership:** grows owned array; old buffer released only after copying.
- **Nullability:** n/a. **Bounds:** initial capacity 8; doubling guarded by `space > (LIMIT/sizeof(int))/2` → `-1` with **no note** (unlike `vector::push_back`, `:955-959`).
- **Allocation:** one `stdlib_alloc(sizeof(int)*want)` on growth (`:960`).
- **Failure:** `-1` on overflow or allocation failure, stack unchanged.
- **Cleanup:** releases old buffer (`:965-966`), growth telemetry (`:969`).
- **Thread:** G-TS-C, G-TS-M. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:950-973`; O direct `deep_collections_alloc.expected:stf_probe,stg_probe`, `deep_collections_boundary.expected:bd_bound_stack,bd_stress_stack`; U --

#### `int stack::pop(int *out)` / `int stack::top_checked(int *out)` — `stdlib/collections.hp:975-987`
- **Ownership:** caller owns `out`; array borrowed. **Nullability:** `out == NULL || used == 0` → `-1`.
- **Bounds:** `pop` reads `items[used-1]` after decrementing; `top_checked` reads without removing.
- **Allocation:** none. **Failure:** `-1` on empty/bad out. **Cleanup:** `pop` decrements `used` only (no release).
- **Thread:** G-TS-C. **ABI:** G-ABI-M.
- **Evidence:** D `:975-987`; O direct `deep_collections_boundary.expected:bd_bound_stack,bd_nullout_stack`, `agent2/abi_safety_meta_integrity.cp:stg_probe`, `fuzz/f100_cont_f068_stack_growth.cp`; U --

#### `void stack::clear(void)` / `void stack::drop(void)` *(destructor)* — `stdlib/collections.hp:989-999`
- **Ownership:** `clear` retains buffer; `drop` releases it and zeroes all fields; idempotent.
- **Nullability:** NULL guard (`:994`). **Bounds:** n/a. **Allocation:** none. **Failure:** none.
- **Cleanup:** as described. **Thread:** G-TS-C + G-TS-M. **ABI:** G-ABI-M → `stack_drop`.
- **Evidence:** D `:989-999`; O direct `deep_collections_boundary.expected:bd_drop_stack`, `agent2/extra_collections_clear.cp`; U --

### 5.8 `queue` (FIFO ring buffer)

#### `queue::queue(void)` / `size_t queue::size(void)` — `stdlib/collections.hp:1010-1019`
- **Ownership:** empty ring owner. **Nullability:** n/a. **Bounds:** n/a. **Allocation:** none.
- **Failure:** none. **Cleanup:** `queue_drop`. **Thread:** G-TS-P on object. **ABI:** G-ABI-M.
- **Evidence:** D `:1010-1019`; O direct `deep_collections_boundary.expected:bd_empty_queue`; U --

#### `int queue::push(int value)` — `stdlib/collections.hp:1021-1047`
- **Ownership:** grows/linearises owned ring via `stdlib_ring_copy`.
- **Nullability:** n/a. **Bounds:** initial capacity 8; doubling guarded by `space > (LIMIT/sizeof(int))/2` → `-1` with **no note** (`:1025-1028`).
- **Allocation:** one `stdlib_alloc` on growth (through `stdlib_ring_copy`, `:1030-1033`).
- **Failure:** `-1` on overflow/allocation failure, queue unchanged (failure-atomic).
- **Cleanup:** releases old buffer (`:1034-1035`), sets `head = 0`, growth telemetry (`:1039`).
- **Thread:** G-TS-C, G-TS-M. **ABI:** G-ABI-M, G-ABI-T.
- **Evidence:** D `:1021-1047`; O direct `deep_collections_alloc.expected:qf_probe,qgw_probe`, `deep_collections_boundary.expected:bd_bound_queue_wrap,bd_stress_queue`; U --

#### `int queue::pop(int *out)` / `int queue::front_checked(int *out)` — `stdlib/collections.hp:1049-1066`
- **Ownership:** caller owns `out`; ring borrowed. **Nullability:** `out == NULL || used == 0` → `-1`.
- **Bounds:** head wrap with one subtraction (`:1053-1055`); `front_checked` does not remove.
- **Allocation:** none. **Failure:** `-1` on empty/bad out. **Cleanup:** `used--` on pop, no release.
- **Thread:** G-TS-C. **ABI:** G-ABI-M.
- **Evidence:** D `:1049-1066`; O direct `deep_collections_boundary.expected:bd_bound_queue,bd_nullout_queue`, `fuzz/f100_cont_f069_queue_ring.cp`; U --

#### `void queue::clear(void)` / `void queue::drop(void)` *(destructor)* — `stdlib/collections.hp:1068-1080`
- **Ownership:** `clear` retains buffer; `drop` releases it and zeroes all four fields; idempotent.
- **Nullability:** NULL guard (`:1074`). **Bounds:** n/a. **Allocation:** none. **Failure:** none.
- **Cleanup:** as described. **Thread:** G-TS-C + G-TS-M. **ABI:** G-ABI-M → `queue_drop`.
- **Evidence:** D `:1068-1080`; O direct `deep_collections_boundary.expected:bd_drop_queue`, `agent2/extra_collections_clear.cp`; U --

---

## 6. Cross-cutting ownership rules (consolidated)

| Rule | Citation | Evidence |
|---|---|---|
| One allocation model: `stdlib_alloc`/`stdlib_calloc` in, `stdlib_release` out; never mix with raw `free`/`malloc` | `stdlib/error.hp:12-23,364-414,581-588` | O: `deep_memory_bounds.expected:c_single_free_counted`; `agent2/abi_safety_meta_integrity.cp` |
| Failure-atomic mutation (return `-1` ⇒ object byte-for-byte unchanged, no leak) | `stdlib/string.hp:7-9`; `stdlib/vector.hp:5-7`; `stdlib/collections.hp:12-15` | O: `t4_copy_fail_unchanged`, `T4_SHRINK_FAILED_ATOMIC`, `ALLOC 59 0` |
| Overflow checks before multiply/double against `STDLIB_ALLOC_LIMIT` | `stdlib/error.hp:57-67`; `stdlib/vector.hp:116,139-143`; `stdlib/string.hp:254-259,272-276`; `stdlib/collections.hp:16-17,94-98` | O: `t4_ovf_reserve_rc`, `T4_PUSH_GROWTH_OVERFLOW`, `bd_maxcap_*`, `V6_overflow_shapes` |
| `drop()` is destructor; zero-init safe; compiler inserts it for locals | `docs/LANGUAGE.md:160-200`; `docs/MODERN_STDLIB.md:47-56` | O: `bd_drop_*`, `T4_CLEAR_DROP_REUSE`, `t4_drop3_*` |
| Borrows (`string_view`, `slice`, `stdlib_mem_borrow`) never free and are invalidated by owner mutation | `stdlib/string.hp:79,1-5`; `stdlib/vector.hp:9-19,27-28`; `stdlib/memory.hp:183-195` | O: `t4_view_stale_*`, `T4_STALE_*`, `g_stale_borrow_refused`; U: view/slice/borrow outliving owner |
| Generation tokens are opt-in and 0 means "no check" | `stdlib/error.hp:513-536`; `stdlib/memory.hp:63-94` | O: `g_current_token_ok`, `g_stale_write_refused`, `fuzz/f100_mem_tokens.cp`; U: wraparound / ABA at recycled address (ABI doc F-01) |
| Containers are not thread-safe; metadata is lock/atomic-synchronised | `stdlib/collections.hp:23-24`; `stdlib/error.hp:109-131,256-263` | O: metadata balance in `agent2/abi_safety_meta_integrity.cp`; U: cross-thread callback and `meta_lock` contention (ABI doc §7 item 5) |
| ABI is additive: thin pointers, header-private side table, no new exported C symbol | `stdlib/error.hp:1-36,102-125`; `docs/stdlib/abi-preserving-runtime-safety.md:156-260` | O (build/source audit, not per-API runtime): `70/70 layouts`, `abi_compat 69 passed/0 failed` as quoted in ABI doc §2; U: Windows x86 ABI |

---

## 7. OBSERVED only indirectly (27 entries)

These entries have a Header contract ([D]) and an asserted effect via another entry point ([O] indirect), but no direct asserted call of the entry itself. Their Evidence lines in §§1–5 say `O indirect` and name the effect-bearing fixture; this is not the same as "DOCUMENTED-only" — the header contract plus an indirect observation are both present, and none of these entries is UNTESTED.

| # | Entry | Header | Indirect evidence / residual gap |
|---:|---|---|---|
| 1 | `stdlib_meta_acquire` | `error.hp:256-259` | metadata balance in `agent2/abi_safety_meta_integrity.cp:asmi_a_balance`; no direct tag/contention test |
| 2 | `stdlib_meta_release_lock` | `error.hp:261-263` | same |
| 3 | `stdlib_meta_hash` | `error.hp:265-271` | table ops in `agent2/abi_safety_meta_integrity.cp`; no direct value assertion |
| 4 | `stdlib_meta_find` | `error.hp:273-287` | `asmi_b_tables_agree,asmi_b_runtime_get_meta`; no direct call |
| 5 | `stdlib_meta_forget` | `error.hp:348-362` | `c_single_free_counted`; direct call exists in `agent2/abi_safety_meta_integrity.cp:210,226,245` but is not a dedicated value fixture |
| 6 | `stdlib_tm_add` | `error.hp:127-131` | `p_telemetry_on_counts`; invalid-index behaviour unasserted |
| 7 | `stdlib_note` | `error.hp:192-222` | `err_reentrant_no_recursion`, `p_callback_on_fires`; cross-thread callback untested |
| 8 | `error::error` (constructor) | `error.hp:602-607` | E1–E5 construct `error` locals in `deep_error_vector.cp`; no dedicated ctor tag |
| 9 | `string_view::string_view` | `string.hp:84-87` | object produced by `to_view()`; two-arg ctor not called directly (spelling gap) |
| 10 | `string::clear` | `string.hp:289-293` | CF1 source-key `clear()`; no dedicated tag |
| 11 | `string::is_valid_utf8` | `string.hp:324-326` | `t4_invalid_utf8`, `containers_safety_test.cp`; no dedicated tag |
| 12 | `slice::slice` | `vector.hp:35-40` | produced by `view()`/`sub()`; spelling-only gap per `API_COVERAGE_REPORT.md:213` |
| 13 | `vector::vector` (constructor) | `vector.hp:94-99` | every vector fixture; no dedicated ctor tag |
| 14 | `stdlib_map_hash_int` | `collections.hp:37-45` | called to *build* collisions in `deep_collections_collisions.cp:49`; no hash-value assertion |
| 15 | `stdlib_map_hash_text` | `collections.hp:47-54` | same at `deep_collections_collisions.cp:63` |
| 16 | `stdlib_map_needs_room` | `collections.hp:68-72` | growth at 6th insert (`g_grow3,g_grow7`); no direct call |
| 17 | `stdlib_map_rebuild_capacity` | `collections.hp:77-86` | `g_tomb_remove,g_reinsert`; no direct call |
| 18 | `intmap::intmap` (constructor) | `collections.hp:128-135` | `bd_empty_intmap`; no dedicated ctor tag |
| 19 | `intmap::stdlib_probe` | `collections.hp:147-161` | `c_map_probe_all,c_map_removed`; no direct call |
| 20 | `intmap::stdlib_rebuild` | `collections.hp:165-210` | `g_grow3,g_oom1,g_oom2,g_oom3`; no direct call |
| 21 | `strmap::stdlib_copy_key` | `collections.hp:333-346` | `s_oom_keycopy,s_oom_keycopy_retry`, CF1; no direct value fixture |
| 22 | `strmap::stdlib_probe` | `collections.hp:348-363` | `c_str_probe_all`; no direct call |
| 23 | `strmap::stdlib_rebuild` | `collections.hp:367-414` | `g_oom1,g_oom2,g_oom3,g_oom_retry`; no direct call |
| 24 | `intset::stdlib_probe` | `collections.hp:553-567` | `isf_probe`, `c_set_insert`; no direct call |
| 25 | `intset::stdlib_rebuild` | `collections.hp:569-603` | `g_tomb_remove,g_oom1`; no direct call |
| 26 | `intset::grow` | `collections.hp:605-612` | `isg_probe`, `g_grow3`; no direct call |
| 27 | `deque::stdlib_make_room` | `collections.hp:719-731` | `dpbf_probe,dpff_probe,dgw_probe`; no direct call |

## 8. UNTESTED entry (1)

| Entry | Header | Why |
|---|---|---|
| `stdlib_safe_div` (direct) | `error.hp:458-474` | zero references anywhere under `tests/` (`grep -rw stdlib_safe_div tests/` → 0 files); reached only indirectly through `stdlib_alloc_length` / `stdlib_alloc_query_length`. The header's exactness claim at `:460-461` was not verified here (§9 item 1). Its only OBSERVED evidence is that indirect path; it is the one entry with no test reference of its own. |

## 9. Known boundary questions found while inventorying (not resolved here)

1. **`stdlib_safe_div` quotient range.** The loop at `error.hp:466-473` accumulates at most `2^16-1`, while the header at `:460-461` says this covers all `STDLIB_ALLOC_LIMIT` queries and the default limit is `1<<40` (`:61-67`). All in-tree allocation sizes are small (some fixtures override the limit to 4096/256), so no test can adjudicate this. **[U]** — recorded, not fixed.
2. **Generation/ABA.** `stdlib_transfer_ownership` and `stdlib_release` act on table membership, not a caller-supplied token; a stale alias at a recycled address can act on the new block (ABI doc §1.3, finding F-01). **[U]**
3. **Slice/view/borrow lifetime.** `slice` holds `&vector::generation`; using it after the vector's storage dies is undetectable (`vector.hp:16-19`). `string_view` and `stdlib_mem_borrow` are caller-disciplined. **[U]**
4. **Threading.** Container mutation is unsynchronised (`collections.hp:23-24`); the error callback's re-entrancy guard is thread-local only (ABI doc §7 item 5). **[U]**
5. **Non-local exits.** `longjmp`, cancellation, and signals skip the side-table cleanup path (ABI doc §3.4). **[U]**

## 10. Evidence index (files actually read)

Headers: `stdlib/{error,memory,string,vector,collections}.hp`.
Fixtures: `tests/stdlib/deep_{memory_bounds,error_vector,error_task5,string_task4,string_append,string_view,vector_task4,collections_alloc,collections_boundary,collections_growth,collections_collisions,cross_family}.{cp,expected}`, `containers_safety_test.{cp,expected}`, `collections_test.{cp,expected}`, `core_extras_test.{cp,expected}`, `safety_regressions.{cp,expected}`, `foundation.{cp,expected}`, `negative.{cp,expected}`.
Additional: `tests/stdlib/regressions/adv_null_zero.cp`, `regressions/adv_exact_fit.cp`, `regressions/adv_integer_bounds.cp`, `fuzz/f100_mem_tokens.cp`, `f100_mem_guarded.cp`, `f100_strview_utf8.cp`, `f100_strview_stale.cp`, `f100_error_obj.cp`, `f100_vec_f0{70,71,72,73}_*.cp`, `f100_cont_f06{1..9}_*.cp`, `f100_cont_f07{4,5}_*.cp`, `agent2/{abi_safety_meta_integrity,abi_safety_injection_mem,abi_safety_alloc_lifecycle,extra_error_metadata,extra_collections_clear,cross_module}.cp` (+ `.expected` where present), `compile_time/error_*.cp`.
Secondary docs: `docs/LANGUAGE.md:155-204`, `docs/MODERN_STDLIB.md:47-56`, `docs/stdlib/abi-preserving-runtime-safety.md`, `tests/stdlib/agent2/API_COVERAGE_REPORT.md`, `docs/stdlib/COVERAGE_TOOL_LIMITATIONS.md`.

## 11. Bottom line

- **177** entries inventoried across 5 headers: **162** public API entries + **15** internal helpers documented for completeness.
- Per header: `error.hp` 42, `memory.hp` 12, `string.hp` 25, `vector.hp` 21, `collections.hp` 77.
- Evidence status: **177 / 177** DOCUMENTED; **176 / 177** OBSERVED (**149** direct + **27** indirect-only); **1 / 177** UNTESTED (`stdlib_safe_div`). Definitions in §0.3; lists in §7 and §8; `149 + 27 + 1 = 177`.
- Every entry carries all eight requested dimensions (ownership, nullability, bounds, allocation, failure, cleanup, thread-safety, ABI) plus a DOCUMENTED / OBSERVED / UNTESTED evidence line with exact `path:line` and test-tag citations.
- No percentage was computed. No live-game, Windows-x86, or sanitizer claim is made here; passing fixtures are behavioural evidence for their scenarios only.

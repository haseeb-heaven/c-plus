# Memory and container safety — final synthesis (Task 2, Phase 6)

- **Branch / date:** `agent/modern-stdlib`, 2026-11-23.
- **File ownership for this task:** this document only. No header, test, build
  file, expectation fixture, or git index entry was created or modified by this
  author. No `git add` / commit.
- **Method:** synthesis of already-recorded evidence. This author did **not**
  run a test runner; every command, final line, hash, and count below is quoted
  from the named source file and attributed to it. Where the named sibling
  document was not present at authoring time, the source header, test, or test
  NOTES file is cited directly instead.
- **Evidence labels:** **[VERIFIED]** = directly supported by a checked-in
  source file (`path:line`), a recorded result artifact, or a named NOTES file
  that records an executed command. **[RECORDED]** = quoted from another
  agent's result document, not re-executed here. **[UNTESTED]** = designed or
  proposed but not exercised by a recorded command. **[DOCUMENTED]** = a
  documented behaviour or limitation, not a measurement.

## 0. Input availability and scope

The brief names these sibling outputs for synthesis:
`docs/stdlib/memory_ownership/{API_INVENTORY,MEMORY_SAFETY_AUDIT,STRING_VIEW_AUDIT}.md`
and `docs/stdlib/container_safety/{VECTOR_AUDIT,COLLECTIONS_AUDIT,UPSTREAM_REUSE,ABI_PERFORMANCE}.md`.
**None of those directories or files existed in this checkout when this
document was authored** (verified by `find docs/stdlib -maxdepth 2 -type d`).
Accordingly, no count, conclusion, or citation is attributed to them. The
substitute primary sources are:

| Available source | Role in this synthesis |
|---|---|
| `stdlib/memory.hp`, `stdlib/vector.hp`, `stdlib/collections.hp`, `stdlib/string.hp`, `stdlib/error.hp` | Public API inventory and safety contracts (read directly) |
| `docs/stdlib/MEMORY_BOUNDS_CONTRACT.md` | Memory wrapper contract and three-mode result |
| `docs/stdlib/DEEP_ERROR_VECTOR_CONTRACT.md`, `DEEP_VECTOR_TASK4_CONTRACT.md`, `DEEP_STRING_TASK4_CONTRACT.md`, `DEEP_COLLECTIONS_CONTRACT.md` | Per-module audit verdicts and final lines |
| `tests/stdlib/regressions/ADV_{CONTAINERS,INTEGER,LIFECYCLE,NULL_ZERO,SYSTEM}_NOTES.md` | New adversarial regression families (commands, stdout, findings) |
| `tests/stdlib/fuzz/F100_{CONTAINERS,STRING_FILE,SYSTEM,THREAD_JSON}_NOTES.md` | Fixed-seed fuzz configuration records |
| `docs/stdlib/abi-preserving-runtime-safety.md`, `docs/stdlib/upstream-reuse.md`, `benchmarks/safety_compare/RESULTS.md`, `docs/stdlib/TASK6_FINAL_REPORT.md`, `docs/FINAL_EVIDENCE.md` | ABI, upstream, performance, aggregate evidence |

This document covers the **C+ stdlib memory and container family only**:
bounds-aware memory wrappers, the `vector`/`slice` pair, the seven `collections`
containers, `string`/`string_view`, and the allocator metadata they depend on.
Other families (IO, file, thread, JSON, network) appear only where a recorded
adversarial test exercises a shared contract or where they bound a risk.

---

## 1. Modules inspected

**[VERIFIED]** — full text read for this document; line numbers are from the
checked-in files.

| # | Header | Lines / role | Safety mechanism relevant here |
|---|---|---|---|
| 1 | `stdlib/memory.hp` | 226 lines; thin additive wrappers | Capacity/length/generation queries; subtract-form range check `off > cap`, `n > cap - off`; fail-closed `error *` reporting; borrow/transfer/release helpers (`stdlib/memory.hp:48-224`) |
| 2 | `stdlib/vector.hp` | 306 lines; growable `int` vector | Overflow-checked doubling; failure-atomic mutation; generation-stamped `slice` with `stale()`/`valid()`/`at_checked()` (`stdlib/vector.hp:26-84,111-302`) |
| 3 | `stdlib/collections.hp` | 1074 lines; 7 containers | Open addressing + tombstones + power-of-two capacity + 70% load guard; allocate-all-then-commit rollback; by-value access (no iterators); idempotent `drop()` (`stdlib/collections.hp:6-20,120-1074`) |
| 4 | `stdlib/string.hp` | 390 lines; owning `string` + borrowed `string_view` | Failure-atomic operations; strict UTF-8 validation (RFC 3629); caller-responsible view with no generation counter (`stdlib/string.hp:1-30,80-114,116-388`) |
| 5 | `stdlib/error.hp` | metadata foundation | Open-addressed side table `stdlib_meta_slot{ptr,byte_size,elem_size,generation}`; `stdlib_alloc_size` / `_elem_size` / `_length` / `_generation` / `_check_generation` / `_query_length` (`:102-125,477-556`); `stdlib_transfer_ownership` bumps generation (`:558`); `stdlib_release` tombstones (`:582`); `stdlib_alloc_fail_at/after/reset` injection (`:227-241`) |

Corroborating audits read: `DEEP_ERROR_VECTOR_CONTRACT.md`,
`DEEP_VECTOR_TASK4_CONTRACT.md`, `DEEP_STRING_TASK4_CONTRACT.md`,
`DEEP_COLLECTIONS_CONTRACT.md`, `MEMORY_BOUNDS_CONTRACT.md` — all on branch
`agent/modern-stdlib`. The whole-stdlib static inventory (21 modules, not just
this family) is recorded in `docs/stdlib/TASK6_FINAL_REPORT.md:§3`.

---

## 2. Public APIs inventoried

### 2.1 Additive memory wrapper — 12 public helpers

**[VERIFIED]** — exact names/signatures from `stdlib/memory.hp`, each a
`static inline` (no exported ABI symbol):

| Group | Helpers |
|---|---|
| Capacity / length / token | `stdlib_mem_capacity` (:48), `stdlib_mem_count` (:56), `stdlib_mem_token` (:66) |
| Validation | `stdlib_mem_check_token` (:73), `stdlib_mem_check` (:86), `stdlib_mem_check_range` (:101) |
| Guarded byte ops | `stdlib_mem_copy` (:137), `stdlib_mem_move` (:153), `stdlib_mem_set` (:169) |
| Borrow / own | `stdlib_mem_borrow` (:188), `stdlib_mem_transfer` (:203), `stdlib_mem_release` (:220) |

All operations are fail-closed: any validation problem sets `error *` via
`error_fail_cat` and returns `STDLIB_FAIL` without touching memory; `n == 0`
is a no-op *after* validation; `memmove` is used for overlapping ranges. The
tolerated `token == 0` opts out of the generation check
(`stdlib/memory.hp:73-99,137-185`). The metadata foundation and the exact names
relied on are listed in `MEMORY_BOUNDS_CONTRACT.md:§2` and
`stdlib/error.hp:477-582`.

### 2.2 `vector` / `slice`

**[VERIFIED]** — `slice`: constructor, `size`, `stale`, `valid`, `at_checked`,
`sub` (`stdlib/vector.hp:26-84`). `vector`: constructor, `size`, `capacity`,
`reserve`, `push_back`, `at`, `at_checked`, `pop_back`, `insert`, `erase`,
`clear`, `copy_from`, `shrink_to_fit`, `view`, `drop`
(`stdlib/vector.hp:92-302`). `view()` returns a borrowed slice stamped with the
owner's `generation`; the counter bumps on `reserve`, `pop_back`, `insert`,
`erase`, `clear`, `copy_from`, `shrink_to_fit`, `drop`. `push_back` without
reallocation deliberately does **not** invalidate (`stdlib/vector.hp:1-24,290`).

### 2.3 `collections` — seven containers

**[VERIFIED]** — class declarations at `stdlib/collections.hp:120` (`intmap`),
`:307` (`strmap`, owned key copies), `:530` (`intset`), `:680` (`deque` ring),
`:811-817` (`list_node` / doubly linked `list`), `:935` (`stack`), `:1004`
(`queue` ring). Public surface per class: `size`/`valid`/`capacity` queries;
`put`/`get`/`contains`/`remove` (maps), `insert`/`contains`/`remove` (set),
`push_back`/`push_front`/`pop_front`/`pop_back`/`at_checked` (deque, list),
`push`/`pop`/`top_checked`/`front_checked` (stack, queue), `reserve`/`grow`,
`clear`, `drop`. Shared helpers `stdlib_map_hash_int`,
`stdlib_map_hash_text`, `stdlib_map_next_pow2`, `stdlib_map_needs_room`,
`stdlib_map_rebuild_capacity`, `stdlib_ring_copy` (`:30-114`). Documented
rules: failure atomicity, overflow checks, bounded probing, by-value access,
not thread-safe (`stdlib/collections.hp:6-20`).

### 2.4 `string` / `string_view`

**[VERIFIED]** — `stdlib_utf8_validate` (`:30`); `string_view`: constructor,
`size`, `data`, `equals`, `is_valid_utf8` (`:80-114`); `string`: constructor,
`size`, `capacity`, `valid`, `at`, `equals`, `compare`, `find`, `reserve`,
`append`, `copy_from`, `substr`, `count_fields`, `field`, `is_valid_utf8`,
`to_view`, `drop` (`stdlib/string.hp:116-388`). `substr`/`field` always
NUL-terminate and truncate to `out_size - 1` when one byte short; only
`out_size == 0` is refused (`:329-378`). `string_view` holds a raw `const char *`
and `size` with **no** generation field (`:80-87`) — it is caller-responsible.

### 2.5 Aggregate inventory count (whole stdlib, quoted method)

**[RECORDED]** — `docs/stdlib/TASK6_FINAL_REPORT.md:§3` reports 21
`stdlib/*.hp` modules, **381** public callable entries (232 distinct bare
names; 149 overload/cross-header repeats), **255** APIs exercised by the deep
tests (234 literal call sites + 21 implicit `drop()` credits), and **126** not
exercised. This is a *static textual call-site inventory*, not execution,
branch, effect, or safety coverage. No per-family split of 381/255 exists in
the cited source, so none is invented here.

---

## 3. Confirmed defects

**Zero confirmed library defects in the audited memory/container headers.**
**[VERIFIED by the named contract records]**

| Source | Verdict quoted from source |
|---|---|
| `DEEP_ERROR_VECTOR_CONTRACT.md:205-216` | `Confirmed defects: 0`; Finding F-EV-0: every requested boundary in `error.hp`/`vector.hp` matched the contract; no truncation off-by-one, missing NUL, wrong category, stale-slice false-positive, failed-mutation state change, or allocator leak |
| `DEEP_VECTOR_TASK4_CONTRACT.md:155-176` | `Confirmed header defects: 0`; no minimal repro of a real defect exists; `VECTOR_TASK4 16 0` in all three modes |
| `DEEP_STRING_TASK4_CONTRACT.md:24,280-286` | `DEFECT: None found`; `Defects found in string.hp: None` |
| `DEEP_COLLECTIONS_CONTRACT.md:14,186` | `stdlib/collections.hp unchanged — no defect was reproduced`; no defect reproduced for any requested scenario |
| `tests/stdlib/regressions/ADV_CONTAINERS_NOTES.md:§6.1` | `None found`; seven containers plus both view kinds held in all modes; no header edit made |
| `tests/stdlib/regressions/ADV_INTEGER_NOTES.md` (Defect status) | `No reachable defect was reproduced`; 66 checks pass in all three modes |
| `tests/stdlib/regressions/ADV_LIFECYCLE_NOTES.md` (Findings) | `No functional or memory-safety defect was found`; 603 + 65 checks pass in all three modes |
| `tests/stdlib/regressions/ADV_NULL_ZERO_NOTES.md` (Defect status) | Only a reproduced **compiler** defect (§4); no header defect |
| `tests/stdlib/regressions/ADV_SYSTEM_NOTES.md` (Observations) | No FATAL, crash, corruption, hang, or sanitizer report; two reproduced behaviours classified RECOVERABLE/documented, not defects |

**Outside this family but bounding its guarantees:** the allocator audits in
`docs/libc/AUDIT_alloc.md:194-215` record findings F-01…F-19 (generation check
opt-in, host-`free` bypass, address reuse, bounded recently-free cache, etc.).
Those are **documented risks** (§5), not defects fixed by this work.

---

## 4. Fixed defects

### 4.1 Library defects fixed

**None.** The four module contracts explicitly record that no header was
edited to force a pass (`DEEP_ERROR_VECTOR_CONTRACT.md:216`,
`DEEP_VECTOR_TASK4_CONTRACT.md:168,176`, `DEEP_STRING_TASK4_CONTRACT.md:284`,
`DEEP_COLLECTIONS_CONTRACT.md:14`). The adversarial notes likewise report no
header changes (`ADV_CONTAINERS_NOTES.md`, `ADV_INTEGER_NOTES.md`,
`ADV_LIFECYCLE_NOTES.md`, `ADV_NULL_ZERO_NOTES.md`, `ADV_SYSTEM_NOTES.md`).

### 4.2 Test-side issues found and corrected (not library defects)

**[VERIFIED from the named records]** — enumerating the explicitly listed
items (arithmetic sum of cited rows, **not** a measured defect metric):

| Record | Count | Items (abridged) |
|---|---:|---|
| `DEEP_VECTOR_TASK4_CONTRACT.md:157-165` | 1 | `T4_NULL_INVALID_ARGS` expected `out` to be written on a refused `at_checked`; header correctly preserves the caller's value — sentinel assertion fixed |
| `DEEP_STRING_TASK4_CONTRACT.md:121-130` | 3 | name-collision lowering (`t4_bad`→`t4_badview`), telemetry window, wrong `substr` expectation |
| `ADV_CONTAINERS_NOTES.md:§4.1,§6.2` | 5 | strict out-param initialisation; `ac_sm_values_after_null` shadow gating; `ac_q_wrap` shadow reset; `ac_q_wrap` loop bound; `av_vw_window_not_grown` stale-window misuse |
| `ADV_LIFECYCLE_NOTES.md` (Initial test mistakes) | 2 | `intmap::put` performs 3 allocations on an empty map; `vector::push_back` grows on the 5th push, not the 2nd |
| **Derived total** | **11** | Sum of the four cited rows |

`ADV_INTEGER_NOTES.md` and `ADV_SYSTEM_NOTES.md` describe earlier draft
assertions but record no additional fixed library defect; `ADV_SYSTEM_NOTES.md`
records two wrong draft expectations reclassified as RECOVERABLE/documented
behaviours, not fixes.

### 4.3 Working-tree header edits observed at authoring time

**[VERIFIED by `git diff`]** — at authoring time the following **uncommitted**
edits were present in the working tree. They are strict-checker / ARITH
compatibility rewrites (replacing variable division and `%` with proven
subtraction forms; replacing unverifiable pointer subscripting with
`memcpy`), plus the `stdlib_calloc` overflow check now using
`__builtin_mul_overflow` (`stdlib/error.hp:394-400`). Diffstat:

| File | Diffstat | Representative change |
|---|---|---|
| `stdlib/error.hp` | +37 / −10 | `__builtin_mul_overflow` in `stdlib_calloc`; `stdlib_safe_div` for length queries; `error_fail_cat` message copy capped at `STDLIB_ERROR_SIZE - 1` |
| `stdlib/collections.hp` | +40 / −8 | ring/deque/queue index `% space` → guarded single subtraction; `space == 0` guard in `stdlib_ring_copy` |
| `stdlib/string.hp` | +9 / −4 | `substr`/`field` NUL written via `memcpy`; `compare` reads one byte via `memcpy` |
| `stdlib/file.hp` | +41 / −18 | `read_line` NUL writes via `memcpy`; CRLF-truncation peek restructured |
| `stdlib/io.hp` | +46 / −22 | token/line writes via `memcpy`; CRLF-truncation peek restructured |
| `stdlib/log.hp` | +9 / −2 | `keys[i]`/`values[i]` read via `memcpy` |

These edits are **not** attributed to confirmed behavioural defect fixes: the
contract records in §3 say no header was modified for those tasks. This
author did not make, validate, or claim runtime results for them; they are
reported so the final document is accurate about the tree state.

---

## 5. Remaining risks

**[DOCUMENTED]** unless a recorded test is named. No item here is claimed fixed.

1. **Generation checks are opt-in.** A stale alias at a reissued address can
   free or transfer the *new* live block because checked free/transfer paths do
   not compare a caller-supplied generation (F-01,
   `docs/libc/AUDIT_alloc.md:196,249-265`;
   `docs/stdlib/abi-preserving-runtime-safety.md:§1.3`). Passing `token == 0`
   explicitly opts out (`stdlib/memory.hp:73-74,86-99`).
2. **Exact-base-address requirement.** Metadata is keyed by the allocation
   base; interior pointers must be passed as `(base, offset)`, never as a
   pre-summed pointer (`stdlib/memory.hp:16-27`). `cplus_alloc_remaining_capacity`
   accepts any interior address by range check — a diagnostic hint, not
   provenance proof (F-16/F-19, `AUDIT_alloc.md:66,211,214`).
3. **Only tracked blocks are protected.** Raw `malloc`/stack/static pointers
   and untracked precompiled C libraries are invisible: `stdlib_alloc_size`
   returns 0 and guarded helpers refuse them *by design*
   (`stdlib/memory.hp:19-25`). This is not protection of arbitrary memory.
4. **Host `free()` of a tracked pointer** leaves a false-live slot and inflated
   counters (F-02, `AUDIT_alloc.md:197,267-279`). `cplus_aligned_alloc` is
   untracked and must not be passed to `cplus_free` (F-03).
5. **Generation wrap unguarded and untested.** A full wrap makes a stale token
   compare equal again (`AUDIT_alloc.md:213`;
   `abi-preserving-runtime-safety.md:§1.3`). **[UNTESTED]** at wrap.
6. **Address-based recently-free cache is heuristic and bounded (1024).** A
   recycled address may report "freed"; after enough frees a real double free
   may report "not freed" (F-05/F-06, `AUDIT_alloc.md:200-201,311-325`).
7. **`realloc` ABA guard covers bookkeeping only**, not concurrent stale-pointer
   dereferences; it is **[UNTESTED]** under a hammering concurrency stress
   (F-04, `AUDIT_alloc.md:199,141-151`).
8. **`string_view` has no generation counter.** Invalidation detection in the
   adversarial test is test-side only: snapshot pointer/size plus the checked
   allocator's generation, consulted through a metadata gate that never
   dereferences the stale pointer (`ADV_CONTAINERS_NOTES.md:§7`,
   `ADV_STALE_VIEWS` cases). The header documents the view as caller-responsible
   (`stdlib/string.hp:1-5,79-80`; `upstream-reuse.md:103`).
9. **`vector::slice::stale()` dereferences the owning vector's generation
   field.** A slice outliving the *vector object itself* is undetectable by the
   library (`stdlib/vector.hp:17-24`; `ADV_CONTAINERS_NOTES.md:§7`).
10. **`strmap::put` storage-flag ambiguity.** When the 4th allocation (owned key
    copy) is injected to fail, logical contents are unchanged and memory is
    balanced, but `valid()` flips `0 → 1` because the three rebuild buffers
    were already committed. `ADV_LIFECYCLE_NOTES.md` records this as a
    contract-consistency observation (minimal repro included), asserts logical
    atomicity only, and leaves the decision to the header owner. No header
    change was made.
11. **Unreachable helper-level wrap.** `stdlib_map_rebuild_capacity(SIZE_MAX,
    SIZE_MAX)` returns `SIZE_MAX` because `((count+1)*2)` wraps to 0; it is
    **not reachable through the public container API** (callers pass
    `count <= capacity`, capacity a power of two ≤ `SIZE_MAX>>2`, and rebuild
    rejects caps above `STDLIB_ALLOC_LIMIT / sizeof(slot)`). `ADV_INTEGER_NOTES.md`
    pins only the reachable guard and does not assert a pass/failure for the
    unreachable call.
12. **Compiler lowering quirks (not header defects).** Method dispatch is
    keyed by variable name across a translation unit; member calls on a
    returned temporary are not lowered; strict mode requires explicit
    initialisation of out-parameters. Minimal repros and workarounds (globally
    unique class-typed variable names; named temporary; `int got = 0`) are in
    `ADV_NULL_ZERO_NOTES.md` (Defect reproduced) and
    `ADV_CONTAINERS_NOTES.md:§6.3`.
13. **No thread-safety proof for containers.** `collections.hp:18` requires
    external synchronisation; `adv_thread_contention.cp` proves no lost updates
    functionally under `guard`s but no ThreadSanitizer run was performed
    (`ADV_SYSTEM_NOTES.md:§Limitations`). `guard::holds()` tracks the owner
    pointer, not mutex liveness (RECOVERABLE case `mutex_double_drop`).
14. **Non-local exits bypass cleanup.** `setjmp`/`longjmp`, cancellation,
    signals, unwind, `abort`, `quick_exit`, `_Exit` do not run side-table
    cleanup; no unwind hook was added (`abi-preserving-runtime-safety.md:§3.4`).
15. **Sanitizer coverage is partial.** Only `none`, default, and
    `undefined` were exercised in the new families; `address`, `leak`, `thread`,
    `memory`, `cfi` are rejected by the driver probe, and `-fsanitize=integer`
    produces 4 genuine FAILs in `runtime/cplus_runtime.c:174` hashing
    (`SANITIZER_MATRIX.md:123-146`). Absence of a report is not a memory-safety
    proof.
16. **No interior-pointer owner query.** `stdlib_meta_find` matches exact base
    addresses only, so "which allocation owns this advanced pointer?" cannot be
    answered (`upstream-reuse.md:211-214`).

---

## 6. Exact test commands and results

All rows below are **[RECORDED]** from the named NOTES/contract file; none was
re-run by this author. Verification order was uniformly **strict/no-sanitizer
first**, then default, then UBSan. Compiler: `./build/c+` (`C+ 0.1.0`, host
Clang backend); PATH prefix
`export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH`; every
build and run under `timeout 30`.

### 6.1 Representative exact commands

```sh
# New adversarial families (per ADV_*_NOTES.md; strict first)
timeout 30 ./build/c+ build tests/stdlib/regressions/adv_containers.cp \
  --strict --sanitize=none -o build/adv-regressions/adv_containers.strict-none
timeout 30 build/adv-regressions/adv_containers.strict-none

timeout 30 ./build/c+ build tests/stdlib/regressions/adv_integer_bounds.cp \
  --strict --sanitize=none -o "$TD/aib_strict_none"
timeout 30 "$TD/aib_strict_none" > "$TD/aib_strict_none.out" 2> "$TD/aib_strict_none.err"

# Deep contract tests (per contract docs)
timeout 30 build/c+ build tests/stdlib/deep_memory_bounds.cp \
  --strict --sanitize=none -o <tmp>/deep_memory_bounds.strict_none
timeout 30 <tmp>/deep_memory_bounds.strict_none

# JSON operands must be separate argv operands (ADV_SYSTEM_NOTES.md)
timeout -k 5 30 ./build/c+ build tests/stdlib/regressions/adv_json_fs_net.cp \
  --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o build/adv-regressions/adv_json_fs_net_strict_none
```

### 6.2 New adversarial regression families (self-checking `<tag> <1|0>`)

| Program (source NOTES) | Checks / run | Modes | Final line (every mode) | Build/run rc; stderr |
|---|---:|---|---|---|
| `adv_containers.cp` (`ADV_CONTAINERS_NOTES.md`) | 70 | strict-none, default, strict-UBSan, default+UBSan | `ADV_CONTAINERS 70 0` | 0 / 0; empty |
| `adv_stale_views.cp` (same) | 70 | same four | `ADV_STALE_VIEWS 70 0` | 0 / 0; empty |
| `adv_integer_bounds.cp` (`ADV_INTEGER_NOTES.md`) | 26 | strict-none, default, strict-UBSan | `ADV_INTEGER_BOUNDS 26 0` | 0 / 0; empty |
| `adv_long_input.cp` (same) | 40 | same three | `ADV_LONG_INPUT 40 0` | 0 / 0; empty |
| `adv_lifecycle.cp` (`ADV_LIFECYCLE_NOTES.md`) | 603 | strict-none, default, strict-UBSan | `ADV_LIFECYCLE 603 0` | 0 / 0; empty |
| `adv_alloc_failure.cp` (same) | 65 | same three | `ADV_ALLOC_FAILURE 65 0` | 0 / 0; empty |
| `adv_null_zero.cp` (`ADV_NULL_ZERO_NOTES.md`) | 138 | strict-none, default, UBSan, explicit `--default` | `ADV_NULL_ZERO 138 0` | 0 / 0; empty |
| `adv_exact_fit.cp` (same) | 95 | same four | `ADV_EXACT_FIT 95 0` | 0 / 0; empty |

**Derived headline:** **1,107** self-checking checks across these eight programs
(70+70+26+40+603+65+138+95; arithmetic sum of the cited final lines, not a
measured coverage metric). No check line ended in `0` and no run timed out.

Determinism recorded: `adv_containers` stdout sha256
`007e70e79af7396cede8a357437b01d642bc31dcebef0b92f8106a946ad16cc3`,
`adv_stale_views` sha256
`0f004969b755737e71c97336b663aab0236ec4036b71dea56efa648f87b2562d` (all modes
plus 3 repeat runs); `adv_integer_bounds` sha256
`dcc12410c64ebfbc63dd11135f7377d6da29bfd7c914799890008ce47eac259e`,
`adv_long_input` sha256
`65c0adf7480871058ec1c8a285940412e89177ecb49cf3250e8effe3773d022a` (all three
modes).

`ADV_SYSTEM_NOTES.md` uses `CASE`/`SUMMARY` format instead:
`adv_thread_contention.cp` → `SUMMARY 10 1 0 0` / `RESULT PASS 0` and
`adv_json_fs_net.cp` → `SUMMARY 12 7 0 0` / `RESULT PASS 0` in all four modes;
**8/8 build+run combinations**, **30 CASE lines (22 SUCCESS, 8 RECOVERABLE,
0 FATAL)**, 0 SKIPs, 0 timeouts, 0 sanitizer reports. Its only build stderr is
two benign `-L`/`-l` "unused during compilation" driver warnings.

### 6.3 Fixed-seed fuzz configurations (source: F100 NOTES files)

| Note | Configs | Recorded result |
|---|---|---|
| `tests/stdlib/fuzz/F100_CONTAINERS_NOTES.md` | F-061…F-075 (15 configs; `intmap`/`intset`/`strmap`/`deque`/`list`/`stack`/`queue`/`vector`, collisions, tombstones, rings, fault injection, telemetry balance) | **45 build+run invocations** (15 × strict-none/default/UBSan) rc=0, 0 stderr; digests identical across modes; F-075 telemetry `alloc_count == free_count` (364/364), 126 growths |
| `tests/stdlib/fuzz/F100_STRING_FILE_NOTES.md` | F-076…F-085 (10 configs; `string`/`string_view` append/reserve/accessors/UTF-8/stale, file bytes/lines/lifecycle, `stdlib_mem_*` guarded ops and generation tokens) | 1,200 generated operations per config; digests and final lines recorded per config in the note |
| `tests/stdlib/fuzz/F100_THREAD_JSON_NOTES.md` | F-086…F-093 (8 configs) | digest + operation counts recorded per config |
| `tests/stdlib/fuzz/F100_SYSTEM_NOTES.md` | F-094…F-100 (7 configs) | fixed seeds, bounded ops and watchdog recorded in the note |

### 6.4 Deep contract tests (source: contract docs / recorded results)

| Test | Final line(s) | Modes / note |
|---|---|---|
| `deep_memory_bounds.cp` | `MEMORY_BOUNDS 61 0` | default, strict-none, UBSan (`MEMORY_BOUNDS_CONTRACT.md:220-267`) |
| `deep_vector_task4.cp` | `VECTOR_TASK4 16 0` | three modes (`DEEP_VECTOR_TASK4_CONTRACT.md:123-150`) |
| `deep_string_task4.cp` | 86 checks, 0 failures | three modes (`DEEP_STRING_TASK4_CONTRACT.md:278`) |
| `deep_collections_growth.cp` | `RESULT 32 0` | three modes (`DEEP_COLLECTIONS_CONTRACT.md:173`) |
| `deep_collections_collisions.cp` | `RESULT 23 0` | three modes (`DEEP_COLLECTIONS_CONTRACT.md:174`) |
| `deep_collections_alloc.cp` | `ALLOC 59 0` | every allocation point enumerated with `stdlib_alloc_seq()` (`TASK6_FINAL_REPORT.md:§7`) |
| `tests/libc/test_alloc_fault_matrix.c` | `294/294 checks ok` | plain + UBSan (`TASK6_FINAL_REPORT.md:§7`) |

Aggregates (each quoted with its own provenance; they come from different
runs and are **not** merged into a single invented number):

- `tests/stdlib/agent2/results/summary.txt` (current file, commit `487ab89`):
  `SUMMARY PASS=56 FAIL=0 BUILD_FAILURE=0 TIMEOUT=0 SKIPPED=0 BLOCKED=0 TOTAL=56`.
- `docs/stdlib/upstream-reuse.md:§7` and `abi-preserving-runtime-safety.md:§6.2`
  quote an earlier `50 PASS / 0 FAIL` aggregate (28 `deep_*` + 11 `agent2/*` +
  11 curated).
- `docs/stdlib/TASK6_FINAL_REPORT.md:§2`: deep runner `28 × 3 = 84 invocations,
  0 failed, 0 skipped`.
- `docs/FINAL_EVIDENCE.md` (run 2026-11-23T01:44Z): `stdlib-deep`
  `87 passed, 0 failed (29 file(s) x 3 modes)`; `stdlib-agent2` `56/0`. The
  file count difference is explained by the added `deep_memory_bounds` test.
- `docs/stdlib/upstream-reuse.md:§7`: `tests/run_all_evidence.sh` 23 suites,
  0 failures; `SANITIZER_MATRIX.md` 72 builds attempted / 27 executed /
  23 PASS / 4 genuine `-fsanitize=integer` FAILs, ASan/Leak/Thread/Memory/CFI
  unsupported.
- `docs/FINAL_EVIDENCE.md` overall run status was **FAIL** —
  `libc-tests` `87 passed, 3 failed` — a libc-family result outside this
  document's scope; it is cited so the aggregate is not misrepresented.

---

## 7. ABI preservation evidence

**[VERIFIED / RECORDED]** — quoted from `abi-preserving-runtime-safety.md:§2,§6`
and the recorded artifacts:

| Claim | Evidence |
|---|---|
| Thin pointers unchanged; no inline metadata | Baseline `sizeof(void*)=sizeof(size_t)=sizeof(unsigned long)=8` from both `build/abi/host.out:1` and `build/abi/cplus.out:1`; side table is out-of-band (`stdlib/error.hp:102-125`); `memory.hp` adds only `static inline` functions |
| Public layout unchanged | `sh tests/abi/run_abi_layouts.sh` → `abi_layouts: 70/70 layouts match` on both toolchains; `diff -u build/abi/host.out build/abi/cplus.out` byte-identical (72 lines each), 0 mismatches. N=70 = 36 size/alignment + 21 field-offset + 13 enumerator assertions |
| Symbol names / calling convention (arm64 only) | `sh tests/abi/run_abi_compat.sh` → `RESULT: abi_compat: 69 passed, 0 failed` (`build/abi/compat/results.log`); per-probe `pointers.out` 20/20, `layouts.out` 80/80, `c_driver.out` 13/13 (C→C+), `cplus_caller.out` 10/10 (C+→C); `nm -gU` allow-lists check no ` T __Z` mangling; header neutrality `diff` exit 0 |
| No new exported ABI symbol from this work | `stdlib/memory.hp:48-224` are all `static inline`; no new exported name; the only verbatim upstream body (`memset_explicit`) was renamed `cplus_memset_explicit` when vendored |
| Windows x86 ABI, varargs, disassembly-level call convention | **[UNTESTED]** — all measurements are Apple clang / arm64-apple-darwin (LP64); `docs/libc/ABI_EVIDENCE_PLAN.md:120-148` records the plan, not execution |

---

## 8. Performance measurement / status

**[RECORDED]** — `benchmarks/safety_compare/RESULTS.md`; host macOS arm64
(Apple M3 Pro), Apple clang 16, both C+ variants at `-O2 --release`.

| Workload | C+ safe (median) | C+ legacy (median) | Recorded ratio |
|---|---:|---:|---|
| loop sum (2e8) | 0.1075 s | 0.0038 s | safe 29.85× vs plain C; ~30× safe-vs-legacy |
| nested (4000²) | 0.0090 s | 0.0023 s | 3.90× vs plain C |
| modern stdlib (2,000,000 `push_back` + `at`, 200,000 `append`; 7 interleaved runs) | 0.0140 s (min 0.0134, max 0.0188) | 0.0061 s (min 0.0052, max 0.0077) | **2.31×** median safe vs legacy |

All variants printed checksum `2200000200000`. Source caveats: non-instrumented
compilers folded the integer loops into closed forms; Rust/Goose numbers are
cross-checks, not apples-to-apples comparisons; variance was material
(~1.8×–3.1× across three non-interleaved passes); Fil-C was **not measured**.

**Not measured [UNTESTED]:** no isolated overhead number exists for the side
table, generation counters, allocation registry, or recently-free cache; no
Windows x86 figure; no foreign-C-boundary figure; no `meta_lock` contention
figure. The recipe for the missing isolated measurement is in
`abi-preserving-runtime-safety.md:§7.3`, but no existing build flag disables
the metadata, so no number is reported.

---

## 9. Upstream reuse decisions

**[RECORDED]** — from `docs/stdlib/upstream-reuse.md:§§1,3-5` and
`abi-preserving-runtime-safety.md:§§4-5`. Nine study documents exist under
`docs/stdlib/upstream_study/`.

| Decision | Evidence |
|---|---|
| Only permissive licenses reusable; Cake rejected | MIT, BSL-1.0, Apache-2.0-with-LLVM-exception, BSD-2-clause accepted; Cake `GPL-3.0-or-later` + AI-training-use addendum rejected before the study; no Cake code copied |
| TrapC: no reuse authorized | No repository/commit/license/source exists; license **UNDETERMINED**; docs-only (`n3423`, trapc.org). The literal `"Open Source MIT"` in `n3423` is a Cmaker tutorial example, not TrapC's license |
| SoftBoundCETS: ideas only, no code copied | BSD-3-Clause tree with per-file MIT notices; pointer representation unchanged (single word) so layout-compatible, but shadow stack, `softboundcets_*` renaming, `main` wrapping, 8-byte slot granularity and 64-bit-only reservations make library-only equivalence impossible; its `abort()` policy, approximate `+1024` bounds and `size > dest_bound` `memcpy` typo are on the do-not-copy list |
| Fil-C: not copied | Fat/capability pointers, custom ABI, mandatory GC; whole-program compiler/loader feature, not ABI-preserving |
| Microsoft STL / GSL / SaferCPlusPlus: design references only | Templates, iterators, pointer-wrapper classes unavailable in C+; adapted ideas only |
| Memory wrapper (`stdlib/memory.hp`) | Adapted SoftBoundCETS spatial/range-predicate ideas, TrapC extent/`testof` ideas (docs only), and the local `stdlib/error.hp` side table into a thin additive layer: subtract-form range check, generation token, fail-closed `error *`. No new side table, no fat pointer, no ABI change |
| `vector`, `string`/`string_view`, `collections` | Subtract-form overflow checks, commit-last rollback, `at_checked` + generation-stamped `slice`; overlap-safe append; open addressing with tombstones, power-of-two capacity, 70% load guard. `string_view` deliberately remains caller-responsible (documented gap) |
| Only verbatim upstream code in scope | LLVM `memset_explicit`, renamed `cplus_memset_explicit`; vendored safeclib/Intel: 40 integrated, 10 adapter-only, 387 not integrated |
| Upstream tests | None copied verbatim or executed; cases adapted as ideas (MS STL `GH_005472_do_not_overlap`, GSL `span_tests.cpp`, SaferCPlusPlus `msevector_test.h`, Cake `defer` idea only) |

---

## 10. Explicit non-claims

1. **No arbitrary use-after-free proof.** All "stale" evidence uses metadata
   queries (generation/token, pointer/size snapshots) and refuses access
   *before* dereferencing; no arbitrary freed/raw pointer is ever shown safe.
   The wrappers protect only blocks tracked by `stdlib/error.hp`.
2. **No Rust equivalence.** Rust/Goose benchmark numbers are cross-checks from
   different standard-library implementations; 600 comparison cases with
   473 matches / 119 documented differences / 8 both-skip
   (`TASK6_FINAL_REPORT.md:§5`) are **outcome equality**, not safety
   equivalence. No "as safe as Rust" claim is made.
3. **No 100% safety claim.** Passing fixtures are contract-conformance
   evidence, not memory-safety proof. Generation wrap, address reuse, host-free
   bypass, interior provenance, non-local exits, uninstrumented C code, and
   Windows x86 ABI remain unverified or documented findings (§5).
4. **No claim of Windows x86, live-game, Wine, DLL-injection, or cross-host
   success.** All measurements are arm64-apple-darwin with the local `./build/c+`.
5. **No ASan/Leak/Thread/Memory/CFI evidence.** Only strict-none, default and
   UBSan were exercised in the new families; backend probing rejected the
   others; 4 `-fsanitize=integer` reports are recorded as FAIL, not suppressed.
6. **No compiled-runtime or compiler guarantee.** Fat pointers, capability
   metadata, full borrow checking, static `[CPLUS-*]` enforcement at
   `--sanitize=none`, and sanitizer runtime callbacks are compiler/runtime
   (Agent 1) ownership, explicitly out of scope
   (`abi-preserving-runtime-safety.md:§8`).
7. **No invented counts.** Every number above is quoted from a named source;
   the only arithmetic performed is the explicitly labelled 1,107 check sum
   and the 11-item test-side correction sum.

---

## 11. Headline counts

| Metric | Count | Source / method |
|---|---:|---|
| Core headers inspected | 5 | `memory.hp`, `vector.hp`, `collections.hp`, `string.hp`, `error.hp` (§1) |
| Public helpers in `memory.hp` | 12 | Exact enumeration, `stdlib/memory.hp:48-224` |
| Containers in `collections.hp` | 7 | Class declarations, `stdlib/collections.hp:120-1074` |
| Whole-stdlib modules / callable entries / exercised APIs | 21 / 381 / 255 | Static call-site inventory, `TASK6_FINAL_REPORT.md:§3` |
| Confirmed library defects (audited modules) | 0 | Four contracts + adversarial NOTES (§3) |
| Library defects fixed | 0 | Same records (§4.1) |
| Test-side issues corrected (enumerated) | 11 | Sum of 1+3+5+2 cited rows (§4.2) |
| New adversarial self-checks | 1,107 | Sum of eight final lines (§6.2) |
| Adversarial `CASE` lines | 30 (22 SUCCESS, 8 RECOVERABLE, 0 FATAL) | `ADV_SYSTEM_NOTES.md` (§6.2) |
| Fuzz configurations recorded (F-061…F-100) | 40 | F100 NOTES inventories (§6.3) |
| Deep memory-wrapper checks | 61 | `MEMORY_BOUNDS 61 0`, three modes |
| ABI layout assertions / abi_compat checks | 70 per toolchain × 2; 69 passed, 0 failed | `run_abi_layouts.sh`, `build/abi/compat/results.log` |
| Remaining risks recorded | 16 | §5 (not a defect count) |

---

## 12. Evidence inventory

Primary files read for this synthesis:
`stdlib/{memory,vector,collections,string,error}.hp`;
`docs/stdlib/{MEMORY_BOUNDS_CONTRACT,DEEP_ERROR_VECTOR_CONTRACT,DEEP_VECTOR_TASK4_CONTRACT,DEEP_STRING_TASK4_CONTRACT,DEEP_COLLECTIONS_CONTRACT,TASK6_FINAL_REPORT,abi-preserving-runtime-safety,upstream-reuse}.md`;
`docs/FINAL_EVIDENCE.md`;
`tests/stdlib/regressions/ADV_{CONTAINERS,INTEGER,LIFECYCLE,NULL_ZERO,SYSTEM}_NOTES.md`;
`tests/stdlib/fuzz/F100_{CONTAINERS,STRING_FILE,SYSTEM,THREAD_JSON}_NOTES.md`;
`benchmarks/safety_compare/RESULTS.md`;
`tests/stdlib/agent2/results/summary.txt`;
`build/abi/compat/results.log`, `build/abi/{host,cplus}.out`;
working-tree `git diff` of `stdlib/{error,collections,string,file,io,log}.hp`.

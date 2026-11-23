# Allocator architecture — Task 5 Phase 8 synthesis

- **Branch / date:** `agent/modern-stdlib`, 2026-11-23.
- **File ownership:** this document only. No header, source, test, build file,
  expectation fixture, or git index entry was created or modified by this
  author. No test runner was executed for this document; every command, final
  line, hash, and count below is **quoted** from a checked-in source or result
  artifact and attributed to it.
- **Purpose:** one synthesised account of the allocator stack — architecture,
  complete API inventory, diagnostic capabilities and their honest limits,
  failure-injection coverage (with denominators), integer-overflow protection,
  benchmark status, ABI-preservation evidence, upstream reuse decisions, known
  limitations, exact verification commands, and explicit non-claims.

Evidence labels used throughout:

| Label | Meaning |
| --- | --- |
| **[VERIFIED]** | Directly supported by a checked-in source file (`path:line`) or a recorded result artifact named in this document. |
| **[RECORDED]** | Quoted from a sibling agent's result document; not re-executed here. |
| **[PENDING]** | Named as an input but absent when this document was authored; no claim is made about it. |
| **[UNTESTED]** | Designed/documented but not exercised by a recorded command. |
| **[INFERENCE]** | Design reading, not a measurement or a quoted upstream fact. |

## 0. Input availability (synthesis inputs)

The brief names five sibling docs and two test-note directories. At authoring
time:

| Named input | Status | Used here as |
| --- | --- | --- |
| `docs/stdlib/allocator/INTERFACE_DESIGN.md` | **present** (§0–§8 read) | EXISTING/PROPOSED API split, constraints C1–C10, ownership domains, rejected ideas |
| `docs/stdlib/allocator/ABI_EVIDENCE.md` | **present** (§1–§4 read) | Exact host ABI commands/results, fingerprint SHA-256, header/struct inspection verdict |
| `docs/stdlib/allocator/UPSTREAM_RESEARCH.md` | **present** (§1–§11 read) | Pinned upstream sources, adaptability matrix, explicit rejections |
| `docs/stdlib/allocator/INFRASTRUCTURE_AUDIT.md` | **present** (§0–§6 read; landed during authoring) | Function-by-function allocation/deallocation audit, three-model decomposition, REPRO-1…REPRO-5, static findings (§1, §4, §9) |
| `docs/stdlib/allocator/ALLOCATOR_HP_CONTRACT.md` | **[PENDING]** | Not present; HP contract read directly from `stdlib/error.hp`, `stdlib/allocator.hp`, `stdlib/memory.hp` (§2–§4) |
| `tests/stdlib/allocator/` | **present** (sources + `.run` artifacts; no `*_NOTES.md`) | Result finals quoted directly from `.run/*.out`; notes **[PENDING]** |
| `tests/stdlib/allocation_failure/` | **present** (sources + `.run` artifacts; no `*_NOTES.md`) | Result finals quoted directly from `.run/*.out`; notes **[PENDING]** |

Corroborating checked-in documents read: `docs/stdlib/abi-preserving-runtime-safety.md`,
`docs/stdlib/memory-container-safety.md`, `docs/stdlib/upstream-reuse.md`,
`docs/stdlib/DEEP_COLLECTIONS_ALLOC_CONTRACT.md`, `docs/LIMITATIONS.md`,
`tests/stdlib/agent2/ABI_SAFETY_NOTES.md`, `tests/stdlib/agent1_handoff/performance/NOTES.md`,
`tests/abi/ABI_COMPAT_NOTES.md`, `docs/stdlib/container_safety/ABI_PERFORMANCE.md`
(inventory only). Where a named sibling doc was unavailable, source files are
cited directly and the gap is marked PENDING rather than filled by inference.

---

## 1. Architecture

The allocator is a **stack of four layers plus two independent sibling
domains**. The defining property is that all safety state is **out-of-band**:
public pointers stay one machine word, and no inline header, tag, or capability
is added to any block (`stdlib/error.hp:1-23`, `stdlib/memory.hp:1-9`,
`stdlib/allocator.hp:1-11`).

`INFRASTRUCTURE_AUDIT.md:22-37` adds an important correction to the "one
allocation model" claim in `error.hp:12-23`: there are **three** models —
(1) checked stdlib (`stdlib_alloc`/`release`, table `stdlib_state.meta_tab`),
(2) hardened libc (`cplus_malloc`/`free*`, table `g_tab`), and (3) upstream host
allocators (`yyjson_*` in `json.hp`, `fopen`/`tmpfile` in `file.hp`/`log.hp`,
`cplus_mutex_new`/`pthread_*` in `thread.hp`) with **no** table, injection, or
telemetry. The checked model is layered *on top of* the hardened-libc model, so
each `stdlib_alloc` produces **two** metadata entries with independently
advancing generation counters. Model 3's serialized buffers must be freed with
`json_free()`, not `stdlib_release` (`INFRASTRUCTURE_AUDIT.md:§5.7`).

### 1.1 Layer 0 — the C runtime ABI (system heap)

`runtime/include/cplus/cplus.h:93-155` declares the stable C entry points;
`runtime/cplus_runtime.c:189-239` implements them. `cplus_alloc(size)` is
literally `malloc(size)` plus an open-addressed live table
(`cplus_runtime.c:189-202`), guarded by a `pthread_mutex_t` on POSIX
(`:112-121`). `cplus_release` validates membership, tombstones the slot, records
a bounded recent-free address, and only then calls `free` (`:246-269`). Invalid
and double release go to `cplus_print_fatal` (`CPLUS_EXIT_TYPE_CONFUSION` /
`CPLUS_EXIT_DOUBLE_FREE`), i.e. fatal by design, not recoverable.

`cplus_realloc` is a *copy* operation: it allocates a fresh block, copies
`min(old,new)` bytes, and does **not** release the old pointer
(`cplus_runtime.c:223-239`). This matters to §6 and §9.

### 1.2 Layer 1 — the checked HP allocator (`stdlib/error.hp`)

Every `.hp` container allocates only through `stdlib_alloc` / `stdlib_calloc`
and releases only through `stdlib_release` (`error.hp:12-23`). `stdlib_alloc`
applies a hard request limit, consults the global injection window, calls
`cplus_alloc`, and records metadata (`error.hp:364-392`); `stdlib_calloc` adds a
multiplication-overflow check and zeroes (`error.hp:394-414`);
`stdlib_realloc` keeps the side table in sync and is failure-atomic
(`error.hp:416-456`). The side table is a weak, header-defined global
`stdlib_state_t` (`error.hp:102-125`) holding an open-addressed array of
`stdlib_meta_slot {ptr, byte_size, elem_size, generation}`, a spin mutex, live
counters, and the telemetry counters. Lookup is an exact-base-address hash
(table `:265-287`); release tombstones with `STDLIB_META_TOMB` and zeroes the
generation (`:348-362`). Generation is bumped on record and on ownership
transfer (`:329,341,574`).

**Important reconciliation:** `tests/stdlib/agent2/ABI_SAFETY_NOTES.md` records a
reproduced defect in which `cplus_realloc` leaves this side table stale (old
pointer still live, new pointer untracked), and states that `error.hp` has no
`stdlib_realloc`. **[VERIFIED]** The current checked-in `error.hp:422-456` *does*
define `stdlib_realloc`, which explicitly forgets the old address and records
the new one, so the note's premise is stale relative to HEAD. The recorded
defect is retained as a **characterised** risk for the `cplus_realloc` fallback
path; no header edit is attributed to this synthesis.

### 1.3 Layer 2 — fail-closed guarded operations (`stdlib/memory.hp`)

Thin `static inline` wrappers over the Layer 1 table: capacity/count/token
queries, generation validation, a subtract-form range check, and guarded
`memcpy`/`memmove`/`memset`/borrow/transfer/release (`memory.hp:68-252`). Every
operation is fail-closed: a validation failure sets `error *` and performs no
memory access. Crucially, the API takes `(base, explicit offset, length)`; it
never accepts a pre-summed interior pointer as the base (`:28-30, 121-153`).

### 1.4 Layer 3 — additive diagnostics and failure scopes (`stdlib/allocator.hp`)

A new, purely additive header (15,626 bytes) that defines **no new allocation
model**. It exposes captured identity (`stdlib_alloc_id {base, generation}`),
read-only views (bytes, elem bytes, generation, liveness, count, remaining
capacity, unchanged, transfer-ok), telemetry snapshot/delta/counter, and RAII
or explicit failure scopes (`allocator.hp:53-325`). It explicitly documents
that an address alone is not identity and that scopes are global, non-nestable,
non-reentrant, and not thread-safe (`allocator.hp:11-49`).

### 1.5 Independent sibling domains (not extendable from a `.hp` header)

- **Hardened libc:** `libc/src/stdlib/alloc.c` maintains its own
  `meta_slot` table, hash, generation, and recent-free cache; public surface in
  `libc/install-include/cplus_libc.h:31-84,467`. `cplus_malloc` / `cplus_calloc`
  / `cplus_realloc` / `cplus_free` keep standard C signatures; the
  `malloc`/`calloc`/`realloc`/`free` macros are opt-in remaps (`:81-84`).
- **SafeCRT:** `stdlib/safe/src/safe_alloc.c` keeps a separate table with its
  own mutex, generation, `g_recent[1024]` cache and per-API stats
  (`safe_alloc.c:13-32, 124-148`). Public API in `stdlib/safe/safe_alloc.h:51-101`.

The three tables are independent; there is no global cross-table registry and
no interposition. A block from one domain must never be released through
another (`error.hp:22-23`, `memory.hp:21-24`, `ABI_EVIDENCE.md:§3`).

### 1.6 Ownership domains that must not be mixed

From `INTERFACE_DESIGN.md:86-93`:

| Domain | Allocate | Release | Side-table query |
| --- | --- | --- | --- |
| Checked/managed | `stdlib_alloc`, `stdlib_calloc`, `stdlib_realloc` | `stdlib_release` / `stdlib_mem_release` | Yes |
| Raw host heap | `malloc`/`calloc`/`realloc` | `free` | No — never pass to `stdlib_release` |
| Arena/pool/fixed (proposed only, **NOT RUN**) | bump/slot from owned backing | `reset()`/`release()` | Only if explicitly registered; off by default |

---

## 2. API inventory

All counts are **enumerated from the named source files**; they are static
textual counts of callable entries, not execution or coverage metrics.

| Layer / header | Public callable entries | Enumeration basis |
| --- | ---: | --- |
| C runtime `runtime/include/cplus/cplus.h:93-155` | 11 | `cplus_alloc`, `cplus_zalloc`, `cplus_alloc_array`, `cplus_realloc`, `cplus_release`, `cplus_alloc_bytes`, `cplus_alloc_count`, `cplus_check_live`, `cplus_check_index`, `cplus_new`, `cplus_new_array` (plus `cplus_move`) |
| Checked allocator `stdlib/error.hp:226-588` | 19 | 4 injection/seq hooks + 4 telemetry controls + allocation/release/resize (4) + 6 metadata queries + transfer + `stdlib_note` + `stdlib_safe_div` |
| Guarded memory `stdlib/memory.hp:73-247` | 12 | `capacity`, `count`, `token`, `check_token`, `check`, `check_range`, `copy`, `move`, `set`, `borrow`, `transfer`, `release` — all `static inline` |
| Additive diagnostics `stdlib/allocator.hp:84-325` | 17 | 16 `static inline` functions (3 identity, 8 views, 3 telemetry, 2 scope) + 1 `alloc_fail_scope` class; plus `stdlib_alloc_id` struct and 3 `ALLOC_FAIL_SCOPE_*` constants |
| Hardened libc `libc/install-include/cplus_libc.h:31-84,467` | 21 | `cplus_malloc/calloc/realloc/free/free_checked`, meta/query/live/generation/capacity/transfer/live-count/bytes, double-free/invalid-free counters, `fail_after/reset`, `cplus_aligned_alloc` (plus 4 opt-in macros) |
| SafeCRT `stdlib/safe/safe_alloc.h:51-101` | 21 | `malloc_s`, `calloc_s`, `realloc_s`, `free_s`, `free_checked_s`, `aligned_alloc_s`, 8 metadata/liveness/size/count/query/generation helpers, `live_count/bytes`, `transfer_ownership`, `fail_after/reset/seq` |

**Key ABI-visible struct shapes** (identical across the two public metadata
headers): `cplus_alloc_meta` and `cplus_safe_alloc_meta` are `size 40`,
`align 8`, fields at offsets `0/8/16/24/32` (`tests/abi/abi_layouts.c:77-83,117-123`;
`tests/abi/ABI_COMPAT_NOTES.md:159-184`). The private runtime/meta slots are not
part of the public ABI.

`INTERFACE_DESIGN.md:96-110` rules out adding a *new* "system allocator" API:
the existing checked surface already is the system allocator; a raw host-heap
facade is "FEASIBLE BUT DISFAVOURED", and arena/pool/fixed/custom/tracking
proposals are all **PROPOSED / NOT RUN**.

---

## 3. Diagnostic capabilities and limits

### 3.1 What can be detected **[VERIFIED / RECORDED]**

- **Live-block size, element size, count, generation** for any exact base
  address registered by the checked allocator (`error.hp:477-556`). The
  `stdlib_alloc_id` pair `(base, generation)` distinguishes a recycled address
  from the original owner (`allocator.hp:11-18,68-141`).
- **Double free / invalid free** for tracked blocks: checked free refuses
  non-members and reports `EINVAL` without touching memory
  (`safe_alloc.c:319-350`; hardened `cplus_free_checked` in `alloc.c:356-388`).
  The runtime layer escalates the same conditions to fatal exits
  (`cplus_runtime.c:246-269`).
- **Leak indicators:** `cplus_safe_alloc_live_count/live_bytes`
  (`safe_alloc.h:86-87`), `cplus_alloc_live_count/live_bytes`
  (`cplus_libc.h`), and telemetry `alloc_count`/`free_count` parity in the deep
  fixtures (`DEEP_COLLECTIONS_ALLOC_CONTRACT.md:71-77`).
- **Interior capacity:** `cplus_safe_remaining_capacity` scans live ranges
  (`safe_alloc.c:367-393`); `cplus_alloc_remaining_capacity` does the same in
  hardened libc (`alloc.c:402-427`). `stdlib_alloc_view_remaining` is the
  `allocator.hp:192-207` view over the same idea.
- **Faithless reporting:** queries are pure and never dereference the supplied
  pointer, so asking about a stale pointer is itself safe
  (`allocator.hp:27-29`, `memory.hp:56-61`).

### 3.2 Hard limits (documented, not bugs) **[VERIFIED / DOCUMENTED]**

1. **Only tracked blocks.** Raw `malloc`, stack, static, and uninstrumented C
   libraries have no metadata: queries return `0`/`-1` and guarded helpers
   refuse them (`memory.hp:19-25`; `upstream-reuse.md:§8`). This is not
   protection of arbitrary memory.
2. **Exact base address required.** A pre-summed interior pointer is not found;
   callers must pass `(base, offset)` (`memory.hp:28-30`; `allocator.hp:21-24`).
   There is no "which allocation owns this advanced pointer?" query
   (`upstream-reuse.md:211-214`).
3. **Generation checks are opt-in.** `token == 0` disables the check; checked
   free/transfer paths do not compare a caller-supplied generation, so a stale
   alias at a reissued address can act on the *new* block (F-01,
   `abi-preserving-runtime-safety.md:§1.3`). `stdlib_mem_copy/move/set` validate
   only the **destination** token, not the source (`memory.hp:39-45,157-163`).
4. **Bounded recent-free caches.** SafeCRT uses a direct-mapped
   `g_recent[1024]` (`safe_alloc.c:21,30,332-338,426-433`); hardened libc uses a
   slightly stronger open-addressed form (`abi-preserving-runtime-safety.md:§1.5`).
   After enough frees a real double free can be classified "invalid", and a
   recycled address can be classified "freed" (F-05/F-06).
5. **Generation wrap unguarded and [UNTESTED]**; a full wrap makes a stale token
   compare equal again (`abi-preserving-runtime-safety.md:§1.3`).
6. **Host-`free` / cross-domain bypass** leaves a false-live slot and inflated
   counters (F-02); `cplus_aligned_alloc` is deliberately untracked (F-03)
   (`abi-preserving-runtime-safety.md:§3.1`).
7. **Address reuse after free is indistinguishable** without a previously
   captured token (`memory.hp:46-54`; `allocator.hp:11-18`).
8. **No provenance and no interior-object identity**; the metadata is a
   diagnostic hint, not a C abstract-machine capability
   (`abi-preserving-runtime-safety.md:§3.2`).
9. **Scope injection is a single global window.** `alloc_fail_scope` is
   non-nestable, non-reentrant, and not thread-safe (`allocator.hp:36-45,279-325`).
10. **Duplicate injection hooks are not interchangeable.** `stdlib_alloc_fail_at`
    is "n-th call fails once"; `cplus_safe_alloc_fail_after(n)` is "n successes,
    then latch", and `cplus_safe_alloc_fail_after(0)` is a *permanent* failure
    until reset (`test_alloc_fault_matrix.c:17-24`). Tests must map semantics
    explicitly.
11. **A reproduced tooling-level defect:** the Task 4 ABI fingerprint probe's
    documented `-Werror` build fails on `-Wcomment` in its own source; it ran
    only with `-Werror` removed (`ABI_EVIDENCE.md:§2.3, §4`).

`tests/stdlib/agent2/ABI_SAFETY_NOTES.md` additionally records that a literal
second `stdlib_release(stale)` terminates with `CPLUS_EXIT_DOUBLE_FREE` (104)
and `cplus_release` on a stack address with `CPLUS_EXIT_TYPE_CONFUSION` (105);
the self-checking suites therefore detect those cases through non-destructive
queries instead of invoking the fatal paths.

---

## 4. Failure-injection coverage (with denominators)

### 4.1 Hooks

| Domain | Hooks | Semantics | Source |
| --- | --- | --- | --- |
| Checked HP | `stdlib_alloc_fail_at(n)`, `stdlib_alloc_fail_after(n)`, `stdlib_alloc_fail_reset()`, `stdlib_alloc_seq()` | `at`: n-th following `stdlib_alloc` fails once; `after`: n successes then latch; consulted only inside `stdlib_alloc` | `error.hp:226-251,377-384` |
| Hardened libc | `cplus_alloc_fail_after(n)`, `cplus_alloc_fail_reset()` | latching; no one-shot `at` | `alloc.c:592-599` |
| SafeCRT | `cplus_safe_alloc_fail_after(n)`, `_fail_reset()`, `_seq()` | latching, with sequence counter | `safe_alloc.c:532-551` |
| Diagnostics | `stdlib_alloc_fail_begin(mode,count)`, `stdlib_alloc_fail_end()`, `alloc_fail_scope` (modes `NONE/AT/AFTER`) | RAII/explicit wrappers over the checked hooks | `allocator.hp:279-325` |

The standard method is: reset → probe the allocation count with `*_seq()` →
for `N = 1..count`, rebuild and arm `fail_at(N)` → assert `-1`/`NULL`, unchanged
state, balanced telemetry, and success after reset
(`DEEP_COLLECTIONS_ALLOC_CONTRACT.md:52-77`; `INTERFACE_DESIGN.md:555-564`).
No test exhausts real system memory.

### 4.2 Recorded coverage and exact denominators

| Suite / artifact | Denominator (checks) | Failures | Modes / provenance |
| --- | ---: | ---: | --- |
| `tests/libc/test_alloc_fault_matrix.c` (hardened libc **and** SafeCRT, incl. log-parity) | **294 / 294** | 0 | plain + UBSan; `tests/libc/run_alloc_fault_matrix.sh:2`; `TASK6_FINAL_REPORT.md:120` |
| `tests/stdlib/regressions/adv_alloc_failure.cp` | **65** | 0 | strict-none, default, UBSan; `ADV_LIFECYCLE_NOTES.md:36-59`; finals in `tests/stdlib/reports/logs/*/adv_alloc_failure.stdout` |
| `tests/stdlib/deep_collections_alloc.cp` (13 allocation points enumerated) | **59** | 0 | three modes; `DEEP_COLLECTIONS_ALLOC_CONTRACT.md:158,184-186`; `deep_collections_alloc.expected` |
| `tests/stdlib/agent2/abi_safety_injection_mem.cp` | **32** | 0 | strict-none + UBSan; `ABI_SAFETY_NOTES.md`; `results/abi_safety_injection_mem.stdout` |
| `tests/stdlib/allocation_failure/af_containers.cp` | **58** | 0 | strict-none; `.run/af_containers.out` final `ALLOC_FAIL 58 0` |
| `tests/stdlib/allocation_failure/af_hooks.cp` | **30** | 0 | strict-none; `.run/af_hooks.out` final `ALLOC_FAIL 30 0` |
| `tests/stdlib/allocation_failure/af_string.cp` | **26** | 0 | strict-none; `.run/af_string.out` final `ALLOC_FAIL 26 0` |

**Headline arithmetic (explicitly labelled, not a coverage metric):** the six
`.cp` suites above sum to **270 checks** (65+59+32+58+30+26), 0 failures; adding
the 294 C-level matrix checks gives **564**. The 294 and the `.cp` counts come
from different runners and are **not** merged into a single "safety percentage".

### 4.3 Reproduced blind spots in the injection hooks **[RECORDED from `INFRASTRUCTURE_AUDIT.md`]**

The denominators in §4.2 are real but they are **not exhaustive**. The
infrastructure audit records three new probes (§4.1–§4.3 of that document) that
bound the claims:

- **REPRO-1 — `stdlib_realloc` bypasses stdlib injection and `stdlib_alloc_seq`.**
  With `stdlib_alloc_fail_after(0)`, a `stdlib_realloc(p, 64)` still succeeds and
  `seq` stays 0 while a control `stdlib_alloc(16)` returns NULL. Cause:
  `stdlib_realloc` calls `cplus_realloc` directly (`error.hp:446`) and never runs
  the injection block that exists only in `stdlib_alloc` (`:377-384`). **Any
  `stdlib_alloc_seq()`-enumerated sweep silently omits every realloc call site.**
  Realloc injection is covered only by the libc-layer 294-check matrix.
- **REPRO-3 (benign) — `stdlib_calloc` burns two generations per block.** It
  records metadata in `stdlib_alloc` (`error.hp:387`) and again at `:411` to set
  the real `elem_size`; a block therefore has gen 3 where an `stdlib_alloc` block
  has gen 1. No memory-safety impact, but the two tables' generations diverge.
- **Metadata `calloc` bypass:** `stdlib_meta_record`'s table growth uses raw
  `calloc` (`error.hp:307`) outside injection and telemetry; on OOM it silently
  keeps the old table (`INFRASTRUCTURE_AUDIT.md:§5.6`).

### 4.4 Diagnostic-side results (new sibling tests) **[RECORDED]**

| Program | Final line | Build mode | Notes artifact |
| --- | --- | --- | --- |
| `tests/stdlib/allocator/diag_identity.cp` | `ALLOC_DIAG 18 0` | strict-none | `.run/diag_identity_strict_none.out` |
| `tests/stdlib/allocator/diag_liveness.cp` | `ALLOC_DIAG 14 0` | strict-none | `.run/diag_liveness_strict_none.out` |
| `tests/stdlib/allocator/diag_telemetry.cp` | `ALLOC_DIAG 16 0` | strict-none | `.run/diag_telemetry_strict_none.out` |

Sum: **48** diagnostic checks, 0 failures. `tests/stdlib/allocator/allocator_test.cp`
declares an `ALLOCATOR_TEST <checks> <failures>` contract (`:3,175`) but no
recorded output artifact was present at authoring time — **[PENDING]**.
`overflow_probe.cp` is a scratch probe, not a self-checking test.

**Honest UBSan caveat:** every `*_ubsan.build.log` in
`tests/stdlib/allocator/.run/` ends with `error: failed to compile instrumented
C+ runtime`, and the corresponding `.err` files contain
`timeout: exec(...): No such file or directory`. The recorded diagnostic checks
above are **strict-none only**; no UBSan pass is claimed for these three files.
No `*_NOTES.md` exists in either new test directory yet (**[PENDING]**).

---

## 5. Integer-overflow protection

| Guard | Implementation | Source |
| --- | --- | --- |
| Single-allocation hard ceiling | `STDLIB_ALLOC_LIMIT` = `1<<40` on 64-bit, `SIZE_MAX>>1` on 32-bit; over-limit returns `NULL` with `STDLIB_CATEGORY_MEMORY` | `error.hp:57-67,372-376` |
| Array product | `__builtin_mul_overflow(count, element_size, &total)`, then limit check; zero maps to 1 | `error.hp:394-414` |
| libc array product | `if (nmemb != 0 && size > SIZE_MAX / nmemb)`; table growth bounded by `ncap > SIZE_MAX/4/sizeof(meta_slot)` | `alloc.c:262,132` |
| SafeCRT product | `req_count > SIZE_MAX / req_size || req_count*req_size > RSIZE_MAX_MEM` → `errno = EOVERFLOW` | `safe_alloc.c:189-198`; `safe_alloc.h:9-11` |
| Over-limit / bad alignment | `size > RSIZE_MAX_MEM` → `EINVAL`; alignment must be power of two `>= sizeof(void*)` | `safe_alloc.c:158-166,219-226` |
| Runtime array product | `count > SIZE_MAX / element_size` → fatal "allocation overflow" | `cplus_runtime.c:211-220` |
| Table growth | `ncap <= SIZE_MAX / 4u / sizeof(stdlib_meta_slot)` before doubling | `error.hp:298-307` |
| Range arithmetic | subtraction form `off > cap`, `n > cap - off`; `off + n` can never wrap | `memory.hp:142-152` |
| Division without variable divisor | `stdlib_safe_div` shift-and-subtract; exact for quotients up to `2^16-1`, documented and honoured by `stdlib_alloc_length/query_length` | `error.hp:458-474,499-511,538-556`; `INTERFACE_DESIGN.md:82,152` |
| Realloc limit | `actual > STDLIB_ALLOC_LIMIT` → `STDLIB_CATEGORY_RANGE`, old block untouched | `error.hp:422-430` |

**Known gaps:** generation counters (`g_gen`, `meta_gen`) are `unsigned long
long` with no wrap guard **[UNTESTED]**; `stdlib_map_rebuild_capacity(SIZE_MAX,
SIZE_MAX)` wraps but is **not reachable** through the public container API
(`memory-container-safety.md:§5 item 11`). The `overflow_probe.cp` scratch file
tests strict-mode treatment of opaque `SIZE_MAX` arithmetic but has no recorded
result (**[PENDING]**).

---

## 6. Benchmark status (actual numbers)

### 6.1 Allocator microbenchmark — `alloc_churn` **[RECORDED]**

`benchmarks/alloc_churn.c` allocates and links two small objects per round for
`ROUNDS = 3000000`, with the safe build using the managed (side-table) allocator
and the plain build using `malloc`/`free`. Recorded table in
`docs/LIMITATIONS.md:235-244` and `tests/stdlib/agent1_handoff/performance/NOTES.md:196-210`
(arm64 macOS, Apple clang 16, `./benchmarks/run_bench.sh`, median of 5 runs via
`benchmarks/timeit.py`):

| Benchmark | C+ safe | C+ legacy | clang -O2 | safe/clang | legacy/clang |
| --- | ---: | ---: | ---: | ---: | ---: |
| `alloc_churn` | **0.1877 s** | **0.1834 s** | **0.0532 s** | **3.53x** | **3.45x** |

Interpretation recorded at source: the side-table bookkeeping is genuinely
~3.4x slower than raw `malloc`/`free` in this microbenchmark. Note that *safe*
and *legacy* are nearly identical here, so this specific cost is the managed
allocator, not UBSan instrumentation (`docs/LIMITATIONS.md:255-257`).

### 6.2 Whole-mode modern-stdlib workload **[RECORDED]**

`benchmarks/safety_compare/RESULTS.md:119-126` (7 interleaved runs, 2,000,000
`push_back` + `at` reads and 200,000 `append` calls; all variants printed
checksum `2200000200000`): C+ safe median **0.0140 s** (min 0.0134, max 0.0188)
vs C+ legacy **0.0061 s** (min 0.0052, max 0.0077) = **2.31x** median.
The source attributes this to safe-mode UBSan on lowered stdlib code, not to an
isolated metadata measurement (`abi-preserving-runtime-safety.md:§7.1`).

### 6.3 What is **not** measured **[UNTESTED]**

No isolated number exists for the side table, generation counters, allocation
registry, recent-free cache, `meta_lock` contention, Windows x86, foreign-C
boundaries, or the proposed arena/pool/custom allocators. Fil-C was not measured
(no toolchain). Rust/Goose figures in `RESULTS.md` are cross-checks from
different standard libraries, not apples-to-apples comparisons; variance on the
host was material (~1.8x–3.1x across three passes). No number is invented here.

---

## 7. ABI preservation evidence

All evidence is **Darwin/arm64, Apple clang 16 (LP64)**. No Windows x86 or Linux
ABI result is claimed.

### 7.1 Measured host suites **[VERIFIED / RECORDED]**

| Claim | Evidence |
| --- | --- |
| Thin pointers unchanged; no inline metadata | `abi_layouts: baseline sizeof(void*)=8 sizeof(size_t)=8 sizeof(unsigned long)=8` in `build/abi/host.out:1` and `build/abi/cplus.out:1`; out-of-band table at `error.hp:102-125` |
| Public layout unchanged | `CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh` → **`70/70 layouts match`**, RC=0, `diff -u host.out cplus.out` empty. N=70 = 36 size/alignment + 21 field-offset + 13 enumerator assertions (`abi-preserving-runtime-safety.md:§2.2,§6.1`) |
| Symbol names / boundary calls | `sh tests/abi/run_abi_compat.sh` → **`RESULT: abi_compat: 69 passed, 0 failed`**, RC=0 (`build/abi/compat/results.log`). Per-probe: `pointers.out` 20/20, `layouts.out` 80/80, `c_driver.out` 13/13 (C→C+), `cplus_caller.out` 10/10 (C+→C); `nm -gU` allow-lists; no ` T __Z` mangling; header-neutrality `diff` empty |
| Allocator metadata shape | `cplus_alloc_meta` and `cplus_safe_alloc_meta`: `size 40`, `align 8`, offsets `0/8/16/24/32`; fingerprint 101 records, SHA-256 `b9aff0a0a709fa5d2cfdeb28441ea3b92d0b35fe53e80821836a15863acc6d4f` (`ABI_EVIDENCE.md:§2.3`) |
| No new exported symbol from the `.hp` work | All `error.hp`/`memory.hp`/`allocator.hp` helpers are `static inline` (`error.hp:254-588`, `memory.hp:73-247`, `allocator.hp:84-325`); the only verbatim upstream body was renamed `cplus_memset_explicit` (`upstream-reuse.md:§4`) |

**Allocator-relevant symbol checks that passed** (`ABI_EVIDENCE.md:64-71`):
`cplus_malloc`, `cplus_calloc`, `cplus_realloc`, `cplus_free`, `cplus_free_checked`,
`cplus_alloc_get_meta`, `cplus_alloc_remaining_capacity`, `cplus_alloc_is_live`,
`cplus_alloc_size`, `cplus_alloc_length`, `cplus_alloc_query_length`,
`cplus_alloc_live_count`, `cplus_transfer_ownership`, `cplus_safe_alloc_get_meta`,
`cplus_memcpy_s`, `cplus_strcpy_s`, `cplus_sc_memcpy_s`.

### 7.2 Header/struct inspection **[VERIFIED]** (`ABI_EVIDENCE.md:§3`)

No working-tree `*.h`/`*.hp` diff; all allocator headers are new additive files
relative to `main` (status `A`); no pre-existing public struct had a member
added, removed, reordered, or retyped. `stdlib/memory.hp` declares no structs.
Commit `1df837f` added `memory.hp` and `error.hp`; commit `9a061ae` changed only
includes/declarations plus aligned-allocation registration — no struct
data-member edits. Verdict: **NO_ABI_IMPACT** on the host.

### 7.3 Unverified boundaries **[UNTESTED]**

Windows x86 (4-byte `long`/pointer/`size_t`, MSVC packing, ordinals,
`__cdecl`/`__stdcall`), varargs, disassembly-level calling convention, Linux,
symbol versioning, and the proposed `STDLIB_ALLOCATOR_ALIGN` constant have no
measured evidence (`ABI_EVIDENCE.md:§4`, `INTERFACE_DESIGN.md:586-613`).

---

## 8. Upstream reuse decisions

Synthesised from `UPSTREAM_RESEARCH.md` (§1 pins, §8 adaptability, §9 rejections)
and `docs/stdlib/upstream-reuse.md`. **Ideas only; no upstream source vendored**
(`third_party/` untouched).

| Upstream (pin) | Decision | Local analogue / reason |
| --- | --- | --- |
| SoftBoundCETS `master@9a9c09f…` (BSD-3-Clause w/ placeholder + per-file MIT) | **Ideas only** | Four-field `{base,bound,key,lock}`, fail-closed miss, subtraction-form range, O(1) lock-zeroing, tombstone free map → local side table/generation/`STDLIB_META_TOMB`. Shadow stack, `softboundcets_*` renaming, `mmap`'d tables and the `size > dest_bound` typo rejected |
| Fil-C `deluge@d3f9f822…` (LLVM-exception + per-file BSD) | **Not copied** | `zgetlower/upper`, `zhasvalidcap`, free-bit invalidation, fail-closed `zvalinbounds` → generation token/token check; capabilities, aux allocations, FUGC/GC and modified LLVM rejected (ABI + whole-program) |
| Microsoft STL `main@0cabaf3c…` (Apache-2.0 WITH LLVM-exception) | **Design reference** | `_Get_size_of_n` pre-multiply check, `_Big_allocation_threshold/in-band back-pointer`, guard objects, commit-last → `__builtin_mul_overflow`, `STDLIB_ALLOC_LIMIT`, explicit branches. Templates/RAII/exceptions not portable to C+ |
| mimalloc `main@c683b7c6…` (MIT) | **Not copied** | Sharded free lists, encoded free lists, `mi_count_size_overflow`, `EAGAIN/EFAULT/EOVERFLOW`, half-word overflow trick → failure taxonomy/limit ideas only; replaces host allocator |
| jemalloc `dev@b37b28d7…` (BSD-style, no SPDX asserted) | **Not copied** | `emap_t` fail-closed lookup returning `{size, size-class, slab?}`, `compute_size_with_overflow`, arena/slab bins → lookup-by-pointer shape only; arenas/rtree/tcache/HPA rejected |
| LLVM sanitizer runtime `main@1e0049e0…` | **Docs only** | `__sanitizer_annotate_contiguous_container` / poison APIs → annotation+verification *taxonomy* mirrored as checked APIs; calling the symbols would add a sanitizer dependency |
| Cake, TrapC (prior studies) | **Rejected** | Cake GPL-3 + AI-training addendum; TrapC has no repository/license/source — no reuse authorized |

Local convergence (from `UPSTREAM_RESEARCH.md:530-568`): the existing
side-table + generation + fail-closed-range design already covers the highest
value adaptable ideas. **Seven explicit rejections** at `UPSTREAM_RESEARCH.md:446-472`:
no allocator copying, no pointer-representation changes, no new dependencies,
no hard-abort policy, no huge virtual reservations/hard-coded masks, no copied
bug-for-bug, no upstream-equivalent safety claims.

---

## 9. Known limitations

**[DOCUMENTED]** unless a recorded test is named; none is claimed fixed here.

1. Generation checks are opt-in; stale aliases at recycled addresses are not
   detected without a captured token (§3.2 item 3).
2. Exact-base-address lookup: no interior-pointer owner query (§3.2 item 2).
3. Only tracked blocks are protected; uninstrumented C bypasses checks (§3.2 item 1).
4. Host-`free` bypass leaves false-live slots (§3.2 item 6). **DRIFT:** the older
   "aligned allocations are untracked" wording is stale on POSIX — REPRO-2 shows
   `cplus_aligned_alloc` *is* registered in `g_tab` and freeable via `cplus_free`,
   contradicting `stdlib_c17.c:13-23`; the Windows `_aligned_malloc` branch still
   carries a cross-platform hazard (`INFRASTRUCTURE_AUDIT.md:§4.2`).
5. Generation wrap unguarded and **[UNTESTED]** (§5).
6. Bounded, heuristic recently-free caches: 1024 direct-mapped slots in SafeCRT
   (§3.2 item 4).
7. `realloc` ABA guard protects bookkeeping only, not concurrent stale-pointer
   dereferences; hammering concurrency stress **[UNTESTED]**
   (`abi-preserving-runtime-safety.md:§1.4`).
8. The `cplus_realloc` / stdlib-side-table mismatch is recorded as reproduced by
   `ABI_SAFETY_NOTES.md`; current HEAD has `stdlib_realloc` (§1.2) but the
   fallback path remains a characterised risk.
9. `string_view` has no generation field; `vector::slice::stale()` reads the
   owner's generation and cannot detect a slice outliving the vector object
   (`memory-container-safety.md:§5 items 8-9`).
10. Non-local exits (`longjmp`, cancellation, signals, unwind, `abort`,
    `quick_exit`, `_Exit`) bypass side-table cleanup; no unwind hook
    (`abi-preserving-runtime-safety.md:§3.4`) **[UNTESTED]**.
11. No custom allocator plug-in point — explicit `SKIP abi_custom_alloc`, not a
    passing test (`INTERFACE_DESIGN.md:83`, `ABI_SAFETY_NOTES.md`).
12. Error callback has a per-thread re-entrancy guard but cross-thread behavior
    and `meta_lock` contention are untested (`abi-preserving-runtime-safety.md:§3.4`).
13. Sanitizer coverage partial: only `none`, default and UBSan exercised in the
    new families; `address`/`leak`/`thread`/`memory`/`cfi` unsupported by the
    backend; 4 genuine `-fsanitize=integer` FAILs in runtime hashing
    (`upstream-reuse.md:§7`). The new allocator diag tests' UBSan builds failed
    at toolchain level (§4.4).
14. New sibling test NOTE files are **[PENDING]**; `.run` artifacts carry no
    byte-exact `.expected` fixtures for the allocator/allocation_failure dirs.
15. ABI fingerprint tooling in progress: no `run.sh` and no committed baseline,
    so the recorded fingerprint is a current-state capture, not a no-drift proof
    (`ABI_EVIDENCE.md:§2.3,§4`).
16. Proposed arena/pool/fixed/custom/tracking allocators remain **PROPOSED /
    NOT RUN**; no existing container adopts them (`INTERFACE_DESIGN.md:568-613`).
17. All measurements are one macOS arm64 host; no Windows x86, Wine,
    DLL-injection, or live-game result exists.
18. **Two independent metadata tables can diverge** (`meta_tab` vs `g_tab`):
    a generation token from one is meaningless to the other, and a block
    registered via `cplus_alloc_meta_register` exists only in `g_tab`, so
    `stdlib_realloc` refuses it even though `cplus_realloc` accepts it
    (`INFRASTRUCTURE_AUDIT.md:§5.1`).
19. **No destructors and no copy control** on any owning `.hp` class despite
    "RAII" wording in headers; cleanup is manual `drop()`, and a value copy of
    an owning class would shallow-copy the pointer **[STATIC, not reproduced]**
    (`INFRASTRUCTURE_AUDIT.md:§5.3`).
20. **32-bit UB in both hash functions:** `x >> 33` on `uintptr_t` in
    `error.hp:267` and `alloc.c:53,55,57` exceeds a 32-bit type width; not
    reproduced on this 64-bit host but relevant to the supported `_WIN32` branch
    (`INFRASTRUCTURE_AUDIT.md:§5.4`).
21. **Inconsistent failure reporting:** several capacity-rejection paths return
    `-1`/`NULL` without `stdlib_note`, so they are absent from
    `STDLIB_TM_API_FAIL` and the callback; `stdlib_realloc` reports over-limit as
    `RANGE/BOUNDS` while `stdlib_alloc` uses `MEMORY/RESOURCE`
    (`INFRASTRUCTURE_AUDIT.md:§5.5`). `cplus_free_checked` also nulls `*inout`
    even on refusal (F-11, §5.8).

---

## 10. Exact verification commands

All commands run from the repository root with
`export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH` where the
C+ driver is used, and `CPLUS_CLANG=/usr/bin/clang` where noted.

### 10.1 ABI (host only) **[RECORDED]**

```sh
CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh
# expect: abi_layouts: 70/70 layouts match; RC=0; diff host.out/cplus.out empty
sh tests/abi/run_abi_compat.sh
# expect tail: RESULT: abi_compat: 69 passed, 0 failed
tail -1 build/abi/compat/results.log
```

### 10.2 Allocation fault matrix (294 checks) **[RECORDED]**

```sh
CPLUS_CLANG=/usr/bin/clang sh tests/libc/run_alloc_fault_matrix.sh
# expect: alloc_fault_matrix: 294/294 checks ok
```

### 10.3 Failure enumeration / deep allocation checks **[RECORDED]**

```sh
timeout 30 ./build/c+ build tests/stdlib/deep_collections_alloc.cp \
  --strict --sanitize=none -o build/dca_strict_none
timeout 30 build/dca_strict_none          # expect: ALLOC 59 0
timeout 30 ./build/c+ build tests/stdlib/regressions/adv_alloc_failure.cp \
  --strict --sanitize=none -o build/adv_alloc_failure_strict_none
timeout 30 build/adv_alloc_failure_strict_none   # expect: ADV_ALLOC_FAILURE 65 0
```

### 10.4 New sibling tests (strict-none; UBSan currently blocked) **[RECORDED]**

```sh
T=tests/stdlib/allocator/.run
for t in diag_identity diag_liveness diag_telemetry; do
  timeout 30 ./build/c+ build "tests/stdlib/allocator/$t.cp" \
    --strict --sanitize=none -o "$T/${t}_strict_none"
  timeout 30 "$T/${t}_strict_none" | tail -1 # ALLOC_DIAG 18/14/16 0
done
F=tests/stdlib/allocation_failure/.run
for t in af_containers af_hooks af_string; do
  timeout 30 ./build/c+ build "tests/stdlib/allocation_failure/$t.cp" \
    --strict --sanitize=none -o "$F/${t}_strict_none"
  timeout 30 "$F/${t}_strict_none" | tail -1 # ALLOC_FAIL 58/30/26 0
done
```

### 10.5 ABI-safety injection suite **[RECORDED]**

```sh
timeout 30 ./build/c+ build tests/stdlib/agent2/abi_safety_injection_mem.cp \
  --strict --sanitize=none -o /tmp/abi_safety_injection_mem
timeout 30 /tmp/abi_safety_injection_mem   # expect: ABI_SAFETY 32 0
```

### 10.6 Benchmarks **[RECORDED]**

```sh
./benchmarks/run_bench.sh alloc_churn   # median of 5 runs; see §6.1
sh benchmarks/safety_compare/run_bench.sh
```

The `INFRASTRUCTURE_AUDIT.md:§4` REPRO-1/REPRO-2/REPRO-3 probes used temporary
sources outside the repository and are **cited, not re-executed** here; their
recorded outputs are quoted in §4.3 and §9.

### 10.7 Not run / blocked (do not claim)

`STDLIB_ALLOCATOR_ALIGN` probe, arena/pool/custom allocation fixtures,
Windows x86 / clang-cl / MSVC golden table, `dumpbin /exports`, varargs and
disassembly-level calling-convention probes, ASan/Leak/Thread/Memory/CFI runs,
and the `-Werror` ABI-fingerprint probe build (source `-Wcomment` defect).

---

## 11. Explicit non-claims

1. **No arbitrary use-after-free proof.** All "stale" evidence uses metadata
   queries (base+generation identity, token checks, pointer/size snapshots) and
   refuses access *before* dereferencing. No arbitrary freed or raw pointer is
   ever shown safe; the wrappers protect only tracked blocks and only when the
   caller supplies the base and a captured token (`memory.hp:32-54`,
   `allocator.hp:11-29`).
2. **No ABI change.** This work neither makes nor claims one: pointers stay one
   word, no public struct layout changed, no symbol was renamed, and all new
   `.hp` helpers are `static inline` (§7). This is *host-only* evidence — it is
   **not** a Windows x86 or live-game ABI claim.
3. **No Rust equivalence.** Rust/Goose numbers in `benchmarks/safety_compare/RESULTS.md`
   are cross-checks from different standard libraries; outcome equality in
   comparison cases is not safety equivalence. No "as safe as Rust" claim is made.
4. **No 100% safety claim.** Passing fixtures are contract-conformance evidence,
   not a memory-safety proof. Generation wrap, address reuse, host-free bypass,
   interior provenance, non-local exits, uninstrumented C code, and Windows x86
   ABI remain unverified or documented findings (§9).
5. **No live-game / Wine / DLL-injection / cross-host result.** All measurements
   are arm64-apple-darwin with the local `./build/c+`.
6. **No invented counts.** Every number above is quoted from a named source; the
   only arithmetic performed is explicitly labelled (270 / 564 in §4.2, 48 in
   §4.4, API enumerations in §2).
7. **No compiler/runtime guarantee.** Fat pointers, capabilities, full borrow
   checking, static `[CPLUS-*]` enforcement at `--sanitize=none`, and sanitizer
   runtime callbacks are compiler/runtime (Agent 1) ownership and are out of scope.

---

## 12. Headline counts

| Metric | Count | Source / method |
| --- | ---: | --- |
| Named sibling docs present / PENDING | 4 / 1 | §0 (`INTERFACE_DESIGN`, `ABI_EVIDENCE`, `UPSTREAM_RESEARCH`, `INFRASTRUCTURE_AUDIT` present; `ALLOCATOR_HP_CONTRACT` PENDING) |
| Test note dirs present / `*_NOTES.md` files | 2 / 0 | §0, §4.4 |
| Architecture layers (+ independent domains) | 4 (+2) | §1.1–§1.5 |
| Public API entries by layer | 11 / 19 / 12 / 17 / 21 / 21 | §2 enumeration (runtime / HP / memory / diagnostics / libc / SafeCRT) |
| Failure-injection C matrix | 294 / 294, 0 fail | §4.2 |
| Failure-injection `.cp` suites | 270 checks (65+59+32+58+30+26), 0 fail | §4.2 |
| Combined injection checks (labelled sum) | 564 | §4.2 |
| New diagnostic checks (strict-none) | 48 (18+14+16), 0 fail | §4.4 |
| `alloc_churn` | 0.1877 s safe / 0.1834 s legacy / 0.0532 s plain; 3.53x / 3.45x | §6.1 |
| Modern workload | 0.0140 s vs 0.0061 s = 2.31x | §6.2 |
| ABI layouts / compat | 70/70 × 2; 69 passed, 0 failed | §7.1 |
| Fingerprint records / SHA-256 prefix | 101 / `b9aff0a0…acc6d4f` | §7.1 |
| Known limitations | 21 | §9 (not a defect count) |
| New reproduced injection blind spots | 3 (REPRO-1, REPRO-3, metadata `calloc`) | §4.3, `INFRASTRUCTURE_AUDIT.md:§4` |
| Explicit non-claims | 7 | §11 |

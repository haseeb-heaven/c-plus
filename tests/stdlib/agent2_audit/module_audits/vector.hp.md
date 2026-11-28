# Safety audit — `stdlib/vector.hp`

Audit scope: `stdlib/vector.hp` only, cross-referencing `stdlib/error.hp`,
`docs/stdlib/DEEP_ERROR_VECTOR_CONTRACT.md`,
`docs/stdlib/DEEP_VECTOR_TASK4_CONTRACT.md`, `docs/MODERN_STDLIB.md`, and the
committed fixtures `tests/stdlib/deep_error_vector.cp`,
`tests/stdlib/deep_vector_task4.cp`. No file outside this audit document was
modified; no tests were executed to produce this report — all findings are cited
from committed source/contract/test evidence.

## Evidence vocabulary

- **[VERIFIED]** — reproduced by a committed fixture with exact stdout/exit
  status pinned in a contract document.
- **[DOCUMENTED-ONLY]** — promised by a header comment or contract, not
  reproduced by a committed fixture.
- **[UNTESTED]** — code path exists but no committed fixture exercises it.
- **[OBSERVED]** — contract explicitly classifies an assertion as a stable
  implementation fact, not a guaranteed contract.

> **Safety-claim boundary.** Passing fixtures, generation/slice refusal and
> alloc/free telemetry are evidence of *contract conformance*, **not proof of
> memory safety**. ASan is rejected by this host's driver
> (`DEEP_ERROR_VECTOR_CONTRACT.md:147-150`;
> `DEEP_TESTS_HARNESS.md:186`), so out-of-bounds/use-after-free classes are not
> ruled out by these tests. The header itself declares that a slice outliving
> its owner is a lifetime error the library **cannot detect**
> (`vector.hp:17-19`).

---

## (1) Public API list

`stdlib/vector.hp` defines two classes (lines 29-306), lowered by C+ to structs
plus functions; internal calls use lowered names
(`vector_reserve(this, want)` at `vector.hp:146,201`).

| Symbol | Signature | Source | Return contract |
| --- | --- | --- | --- |
| `slice` ctor | `slice(int *items, size_t count)` | `vector.hp:35-40` | borrowed window, no generation until derived from `view()`/`sub()` |
| `slice::size` | `size_t size(void)` | `vector.hp:42-44` | recorded extent |
| `slice::stale` | `int stale(void)` | `vector.hp:47-50` | `1` when owner generation changed |
| `slice::valid` | `int valid(void)` | `vector.hp:52-54` | `items!=NULL && count>0 && !stale()` |
| `slice::at_checked` | `int at_checked(size_t index, int *out)` | `vector.hp:57-67` | `0`/`-1`; refuses stale/OOR; leaves `*out` untouched on failure |
| `slice::sub` | `slice sub(size_t start, size_t length)` | `vector.hp:71-85` | clamped sub-slice; empty when OOR or stale |
| `vector` ctor | `vector(void)` | `vector.hp:94-99` | `items=NULL`, `used=space=generation=0` |
| `size` / `capacity` | `size_t size(void)` / `size_t capacity(void)` | `vector.hp:101-107` | element count / capacity |
| `reserve` | `int reserve(size_t want)` | `vector.hp:111-133` | `0`/`-1`, failure-atomic, bumps generation |
| `push_back` | `int push_back(int value)` | `vector.hp:135-151` | `0`/`-1`; geometric growth 4→8→16 |
| `at` | `int at(size_t index)` | `vector.hp:155-162` | compat accessor; returns `0` for OOR (telemetry note) |
| `at_checked` | `int at_checked(size_t index, int *out)` | `vector.hp:165-175` | `0`/`-1`; `*out` untouched on failure |
| `pop_back` | `int pop_back(int *out)` | `vector.hp:178-187` | `0`/`-1`; bumps generation on success |
| `insert` | `int insert(size_t index, int value)` | `vector.hp:190-210` | `0`/`-1`; `index==size()` appends; failure-atomic |
| `erase` | `int erase(size_t index, int *out)` | `vector.hp:213-224` | `0`/`-1`; `out` may be NULL |
| `clear` | `void clear(void)` | `vector.hp:226-229` | `used=0`, generation++ |
| `copy_from` | `int copy_from(vector *other)` | `vector.hp:233-259` | `0`/`-1`, failure-atomic; self-copy no-op |
| `shrink_to_fit` | `int shrink_to_fit(void)` | `vector.hp:263-286` | `0`/`-1`, failure-atomic |
| `view` | `slice view(void)` | `vector.hp:290-295` | generation-tagged borrowed slice |
| `drop` | `void drop(void)` | `vector.hp:297-305` | releases buffer, zeroes state, bumps generation |

---

## (2) Allocation paths

All allocation goes through `stdlib_alloc`/`stdlib_release`
(`error.hp:12-23,365-392`); `sizeof(int)` products are overflow-checked before
allocation.

| Path | Source | Test evidence |
| --- | --- | --- |
| `reserve(want)`: `stdlib_alloc(sizeof(int)*want)` after `want > STDLIB_ALLOC_LIMIT/sizeof(int)` check | `vector.hp:116-127` | `reserve(SIZE_MAX)` / `reserve(LIMIT/sizeof(int)+1)` → `-1`, zero allocation seq delta (V6, `DEEP_ERROR_VECTOR_CONTRACT.md:51`); `t4` exact/overflow reserve |
| `push_back` growth via `vector_reserve(this, want)` (4, else doubling with half-limit guard) | `vector.hp:136-148` | growth 0→4→8→16 at 1st/5th/9th push (V1, `:46`); fail-at growth atomic, retry recovers (V4, `:49`); 513-int refusal (`T4_PUSH_GROWTH_OVERFLOW`) |
| `insert` growth via `vector_reserve(this, want)` | `vector.hp:194-203` | `fail_at(1)` → `-1` with capacity/size/order/**generation** unchanged, retry succeeds (`DEEP_VECTOR_TASK4_CONTRACT.md:43`) |
| `copy_from`: `stdlib_alloc(sizeof(int)*other->used)` | `vector.hp:243-251` | single-allocation failure atomic; retry (`:45`); telemetry alloc==free (`T4_COPY_FROM_BALANCE`, `:46`) |
| `shrink_to_fit`: `stdlib_alloc(sizeof(int)*used)`, or release on `used==0` | `vector.hp:266-282` | no-op delta 0, success, `fail_at(1)` atomic, shrink-to-empty (`:47`) |
| `drop` / `clear` / `pop_back` / `erase` / `insert` / `reserve` bump `generation` | `vector.hp:130,185,208,222,228,303` | V3 staleness after each shape change (`DEEP_ERROR_VECTOR_CONTRACT.md:48`); `T4_*` generation checks |

- **[VERIFIED]** Fault injection (`stdlib_alloc_fail_at/after`,
  `error.hp:226-251`) pins failure atomicity and no-allocation branches
  (`DEEP_ERROR_VECTOR_CONTRACT.md:49,51`; `DEEP_VECTOR_TASK4_CONTRACT.md:43-48`).
- `STDLIB_ALLOC_LIMIT` defaults to `1<<40` (`error.hp:61-67`) and is pinned to
  `4096` in `deep_vector_task4.cp` before any header include, so the half-limit
  growth boundary is reachable with a 513-int (2052-byte) buffer
  (`DEEP_VECTOR_TASK4_CONTRACT.md:32-35`). No request near 1 TiB is issued
  (`DEEP_ERROR_VECTOR_CONTRACT.md:51`).

---

## (3) Input-validation matrix

| Function | Condition | Behaviour | Code / category (`stdlib_note`) | Class | Evidence |
| --- | --- | --- | --- | --- | --- |
| `reserve` | `want <= capacity` | `0`, no allocation | n/a | [VERIFIED] | `vector.hp:114-115`; V1 (`DEEP_ERROR_VECTOR_CONTRACT.md:46`); `T4_RESERVE_EXACT_AND_OVERFLOW` |
| `reserve` | `want > LIMIT/sizeof(int)` (incl. `SIZE_MAX`) | `-1`, state unchanged, zero allocation | `RANGE`/`BOUNDS` (101) | [VERIFIED] | `vector.hp:116-120`; V6 (`:51`); `T4_RESERVE_EXACT_AND_OVERFLOW` |
| `push_back` | capacity full, space > half-limit | `-1`, state unchanged, zero allocation | `RANGE`/`BOUNDS` (101) | [VERIFIED] | `vector.hp:139-143`; `T4_PUSH_GROWTH_OVERFLOW` (`DEEP_VECTOR_TASK4_CONTRACT.md:48`) |
| `push_back` | growth allocation fails | `-1`, size/capacity/order/generation unchanged | `MEMORY`/`RESOURCE` (107) | [VERIFIED] | `vector.hp:146-147`; V4 (`:49`); `T4_INSERT_BOUNDARIES` |
| `at` | `index >= used` | returns `0`, counts bounds violation | `BOUNDS`/`BOUNDS` (101) | [VERIFIED] | `vector.hp:155-161`; V5 (`:50`); `T4_NULL_INVALID_ARGS` |
| `at_checked` | `index >= used` or `out==NULL` | `-1`, `*out` preserved | `BOUNDS` when OOR | [VERIFIED] | `vector.hp:165-175`; V5 (`:50`); sentinels in `T4_NULL_INVALID_ARGS` |
| `pop_back` | empty or `out==NULL` | `-1`, no generation bump | n/a | [VERIFIED] | `vector.hp:178-182`; V5 (`:50`); note `DEEP_ERROR_VECTOR_CONTRACT.md:103-105` |
| `insert` | `index > used` | `-1`, state unchanged, no generation bump | n/a | [VERIFIED] | `vector.hp:192-193`; `T4_INSERT_BOUNDARIES` (`DEEP_VECTOR_TASK4_CONTRACT.md:43`) |
| `erase` | `index >= used` | `-1`, state unchanged | n/a | [VERIFIED] | `vector.hp:215-216`; `T4_ERASE_BOUNDARIES` (`:44`) |
| `erase` | `out==NULL` | accepted, no write | n/a | [VERIFIED] | `vector.hp:217-218`; `T4_ERASE_BOUNDARIES` |
| `copy_from` | `other==NULL` | `-1`, unchanged | `INVALID`/`UNSUPPORTED` (108) | [VERIFIED] | `vector.hp:236-240`; `T4_COPY_FROM` (`DEEP_VECTOR_TASK4_CONTRACT.md:45`) |
| `copy_from` | `other==this` | `0`, no-op | n/a | [VERIFIED] | `vector.hp:241-242`; `T4_COPY_FROM` |
| `shrink_to_fit` | `space==used` / repeated on empty | `0`, no allocation / inert | n/a | [VERIFIED] | `vector.hp:266-275`; `T4_SHRINK_NOOP_AND_SUCCESS`,`T4_SHRINK_TO_EMPTY` |
| `slice::at_checked` | `out==NULL`, OOR index, or stale | `-1`, `*out` preserved | `BOUNDS` (101) | [VERIFIED] | `vector.hp:57-67`; `T4_STALE_SLICE_REFUSAL`,`T4_NULL_INVALID_ARGS` |
| `slice::sub` | stale slice | empty, non-stale slice (`generation_ref==NULL`) | n/a | [VERIFIED] / [OBSERVED] | `vector.hp:71-85`; `T4_STALE_SUB` (`DEEP_VECTOR_TASK4_CONTRACT.md:50,81`) |
| `slice::sub` | `start >= count` / length clamp | empty / clamped extent | n/a | [VERIFIED] | `vector.hp:74-79`; V2 (`DEEP_ERROR_VECTOR_CONTRACT.md:47`); `T4_EMPTY_SUB` |
| stale slice after shape change | `pop_back/insert/erase/clear/copy_from/reserve/shrink_to_fit/drop` | `stale()==1`, `valid()==0`, `at_checked()==-1` | `BOUNDS` on refused read | [VERIFIED] | `vector.hp:47-67,130,185,208,222,228,257,273,284,303`; V3 (`:48`); `T4_STALE_SLICE_REFUSAL` |
| fresh slice across non-reallocating `push_back` | size grows, recorded extent fixed | stays fresh; `at_checked(2)` refuses when extent was 2 | n/a | [VERIFIED] | V3 (`DEEP_ERROR_VECTOR_CONTRACT.md:48`); `vector.hp:149-150` (no generation bump) |

---

## (4) Ownership and lifecycle analysis

- **`vector` owns one `int` buffer**; `drop()` releases it (`stdlib_release`),
  zeroes `items`/`used`/`space` and bumps `generation`
  (`vector.hp:297-305`). The zero-initialised ctor makes the compiler-inserted
  destructor inert (`vector.hp:94-99`; `MODERN_STDLIB.md:53-58`).
- **`slice` is a borrowed, generation-tagged window.** `view()` records
  `generation_ref = &this->generation` and `generation_at`
  (`vector.hp:290-295`); `stale()` compares the live counter to the snapshot
  (`vector.hp:47-50`), and every shape-changing operation bumps the counter
  (§2 table). `sub()` propagates the same generation reference
  (`vector.hp:80-84`), so a sub-slice goes stale together with its parent
  (`DEEP_ERROR_VECTOR_CONTRACT.md:48`).
- **Library-enforced refusal [VERIFIED]:** a stale slice reports `valid()==0`
  and `at_checked()` returns `-1` without reading the stale pointer; tests
  never dereference it directly (`DEEP_VECTOR_TASK4_CONTRACT.md:50,87-88`).
- **Undetectable lifetime error [DOCUMENTED]:** the `generation_ref` points
  into the owning `vector`, so staleness is only meaningful while that object
  is alive; a slice outliving its owner is explicitly declared undetectable
  (`vector.hp:17-19`; `DEEP_ERROR_VECTOR_CONTRACT.md:184-187`). This is a
  caller lifetime rule, not a library guarantee.
- **Copy/move:** `copy_from` replaces contents via a fresh allocation; a
  compiler-inserted destructor calls `drop` on the owning class
  (`MODERN_STDLIB.md:53-58,148-152`). `[VERIFIED]` after `drop()` a second
  `drop()` and repeated `clear()` are inert, and `push_back` re-initialises
  storage (`T4_CLEAR_DROP_REUSE`, `DEEP_VECTOR_TASK4_CONTRACT.md:49,80`).

---

## (5) Error codes and failure atomicity

`vector`/`slice` methods have **no `error *` out-param**; they return `0`/`-1`
(or `0` sentinel for compat `at`) and call `stdlib_note`
(`stdlib/error.hp:194-222`).

| Code | Category | Trigger |
| --- | --- | --- |
| `CPLUS_EXIT_BOUNDS` (101) | `BOUNDS` (5) | `at`/`at_checked` OOR, `slice::at_checked` OOR/stale |
| `CPLUS_EXIT_BOUNDS` (101) | `RANGE` (2) | `reserve`/`push_back`/`insert` overflow or half-limit guard |
| `CPLUS_EXIT_RESOURCE` (107) | `MEMORY` (4) | injected/limit allocation failure |
| `CPLUS_EXIT_UNSUPPORTED` (108) | `INVALID` (1) | `copy_from(NULL)` |

- **Failure-atomicity [VERIFIED]** is the header's central promise
  (`vector.hp:5-7`): `reserve`, `push_back`, `insert`, `copy_from`,
  `shrink_to_fit` leave size/capacity/contents/**generation** unchanged on
  `-1`. Pinned in V4 (`DEEP_ERROR_VECTOR_CONTRACT.md:49`) and
  `T4_INSERT_BOUNDARIES`/`T4_COPY_FROM`/`T4_SHRINK_FAILED_ATOMIC`
  (`DEEP_VECTOR_TASK4_CONTRACT.md:43,45,47`).
- **Refusals do not bump generation** — `pop_back` on empty and `insert` with
  `index > size()` leave a previously taken slice fresh
  (`DEEP_ERROR_VECTOR_CONTRACT.md:103-105`). This is a documented
  observation, relevant to slice validity.
- **Compat `at()`** returns `0` for OOR instead of `-1`
  (`vector.hp:153-159`; `MODERN_STDLIB.md` pattern); `at_checked` is the
  preferred API.

---

## (6) Confirmed defects fixed

**Defect count for `vector.hp`: 0.** No reproduced library defect in this work;
no `vector.hp` (or `error.hp`) edit was made to obtain a pass.

Evidence:

- `docs/stdlib/DEEP_ERROR_VECTOR_CONTRACT.md:8-9`: "**no confirmed defect** in
  either header for any scenario below. Neither `stdlib/error.hp` nor
  `stdlib/vector.hp` was modified." §6 `:205-217` records
  "Finding F-EV-0 (no defect)" with **0 confirmed defects / 0 header files
  modified** across default, strict/no-sanitizer and UBSan modes.
- `docs/stdlib/DEEP_VECTOR_TASK4_CONTRACT.md:13-14,155-169`: "Status at
  hand-off: **no confirmed defect** … No header was modified." §5 documents
  that the single initial failing check (`T4_NULL_INVALID_ARGS`,
  `VECTOR_TASK4 16 1`) was a **test-expectation mistake** — a refused
  `at_checked`/`slice::at_checked` returns `-1` *without writing `*out`*, and
  the test wrongly expected `out==33`. The test was corrected to use a `4242`
  sentinel; re-runs gave `VECTOR_TASK4 16 0`. `:166-169`: "No minimal repro of
  a real defect exists to report … No header was edited to force a pass."
- Git history: `vector.hp` was last changed in `badd477`/`558bc36`
  (`git log --oneline -- stdlib/vector.hp`), not by the deep-audit commits
  `5fe387d`/`1438bf2` (which touched only `io.hp`/`file.hp`).

Regression tests covering the unchanged header:
`tests/stdlib/deep_error_vector.cp` (12 checks + summary;
`DEEP_ERROR_VECTOR_CONTRACT.md:152-168`) and
`tests/stdlib/deep_vector_task4.cp` (16 checks + summary;
`DEEP_VECTOR_TASK4_CONTRACT.md:131-151`).

---

## (7) Remaining untested paths and risks

**[UNTESTED / DOCUMENTED] — 5 items:**

1. **Slice outliving the owning `vector` object.** `generation_ref` points into
   the owner (`vector.hp:32,292`); once the owner's storage is gone,
   `stale()` dereferences a dangling reference. The header explicitly says
   this is a lifetime error the library cannot detect (`vector.hp:17-19`), and
   V3 checks refusals only while the owner is alive
   (`DEEP_ERROR_VECTOR_CONTRACT.md:184-187`). *Highest residual risk; by design
   not testable as a recoverable failure.*
2. **`copy_from` with an empty source (`used==0`).** The code leaves `fresh=NULL`
   and sets `items=NULL, space=0` (`vector.hp:243-256`); `T4_COPY_FROM` covers
   sources of length 2 and 5, not 0 (`DEEP_VECTOR_TASK4_CONTRACT.md:45`).
   *Risk:* empty-source copy/arm's release path unpinned.
3. **`generation` counter wraparound.** `size_t` increment has no guard
   (`vector.hp:130,185,208,222,228,257,273,284,303`); a full wrap would make a
   stale slice compare equal again. Unreachable in practice, untested.
4. **Concurrent mutation / thread safety.** No locking; concurrent
   `push_back`/`view()` is not documented as safe. No fixture exercises it.
   *Risk:* data race / stale-generation race outside the contract.
5. **Near-limit real-memory stress.** Allocations near the default 1 TiB limit
   are deliberately never issued (`DEEP_ERROR_VECTOR_CONTRACT.md:51`), so
   behaviour under genuine allocator exhaustion (as opposed to injection) is
   untested.

No item above is claimed safe because a passing fixture exists.

---

## (8) Compiler dependencies

- **Class lowering and lowered free-function calls:** `vector` is a `class`
  lowered to struct + functions. `push_back`/`insert` call
  `vector_reserve(this, want)` (`vector.hp:146,201`) — the lowered name of the
  `reserve` method — because inside a class body C+ only rewrites
  `this->method()` and internal cross-object calls use lowered names
  (`MODERN_STDLIB.md:34-38`; `vector.hp:111`). A compiler that does not emit
  `vector_reserve` would fail to link this header.
- **Pointer-to-member-state borrowing:** `slice` stores
  `const size_t *generation_ref` into the owning object (`vector.hp:32,292`).
  This relies on C++ class instances having stable addresses for their lifetime;
  it is the mechanism that makes `stale()` work, and the reason a slice cannot
  outlive its owner (§7 item 1).
- **Constructor / destructor lowering:** ctor assigns fields with no `return`
  (`vector.hp:94-99`); owning class receives a compiler-inserted `drop` call,
  inert on the zeroed state (`vector.hp:297-305`; `MODERN_STDLIB.md:53-58,
  168-171`).
- **Test aids in the compiler/runtime:** `stdlib_alloc_fail_at/after`,
  `stdlib_alloc_seq`, `cplus_check_live` and telemetry counters
  (`error.hp:226-251,69-95`; `DEEP_ERROR_VECTOR_CONTRACT.md:75`). Flaky
  compile-time lifetime checks are a documented Agent-1 concern, not a
  `vector.hp` defect (`docs/libc/COMPILE_TIME_GAPS.md:194-195`, cited here only
  as context).
- **Safe-mode / sanitizer dependencies:** default builds add
  `-fsanitize=undefined,bounds,null,alignment -fno-sanitize-recover=all` and
  `-DCPLUS_SAFE=1` (`compiler/Driver/Driver.cpp:516,519`). Both deep fixtures
  pass in default, `--strict --sanitize=none` and `--strict --sanitize=undefined`
  modes (`DEEP_ERROR_VECTOR_CONTRACT.md:138-145`;
  `DEEP_VECTOR_TASK4_CONTRACT.md:119-129`). **ASan unavailable** on this host
  (`DEEP_ERROR_VECTOR_CONTRACT.md:147-150`); no address-safety claim is made.
- **Driver invocation pitfall (test-side):** flags must be separate argv
  operands; zsh does not word-split an unquoted `$flags` string
  (`DEEP_ERROR_VECTOR_CONTRACT.md:111-114`;
  `DEEP_VECTOR_TASK4_CONTRACT.md:94-96`).

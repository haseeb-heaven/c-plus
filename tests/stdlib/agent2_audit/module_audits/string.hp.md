# Safety audit — `stdlib/string.hp`

Audit scope: `stdlib/string.hp` only, cross-referencing `stdlib/error.hp`,
`docs/stdlib/DEEP_STRING_CONTRACT.md`,
`docs/stdlib/DEEP_STRING_TASK4_CONTRACT.md`,
`docs/stdlib/DEEP_VECTOR_TASK4_CONTRACT.md`, `docs/MODERN_STDLIB.md`, and the
committed fixtures `tests/stdlib/deep_string_append.cp`,
`tests/stdlib/deep_string_view.cp`, `tests/stdlib/deep_string_task4.cp`.
No file outside this audit document was modified; no tests were executed to
produce this report — all findings are cited from committed source/contract/test
evidence.

## Evidence vocabulary

- **[VERIFIED]** — reproduced by a committed fixture with exact stdout/exit
  status pinned in a contract document.
- **[DOCUMENTED-ONLY]** — promised by a header comment or contract, not
  reproduced by a committed fixture.
- **[UNTESTED]** — code path exists but no committed fixture exercises it.
- **[DOCUMENTED-ONLY / OBSERVED]** — contract document explicitly classifies
  an assertion as an implementation fact, not a guaranteed contract.

> **Safety-claim boundary.** Passing fixtures, alloc/free telemetry and
> `stdlib_alloc_check_generation` probes are evidence of *contract conformance*,
> **not proof of memory safety**. ASan is unavailable on this host
> (`DEEP_STRING_CONTRACT.md:29-32`), so overlapping-`memcpy` and use-after-free
> classes are not ruled out; a known latent overlap case is recorded in §7.

---

## (1) Public API list

`stdlib/string.hp` defines one free validator and two classes (lines 30-393).
All methods lower to struct + functions; internal calls use lowered names
(e.g. `string_reserve(this, want)` at `string.hp:274`).

| Symbol | Signature | Source | Return contract |
| --- | --- | --- | --- |
| `stdlib_utf8_validate` | `int stdlib_utf8_validate(const char *data, size_t len)` | `string.hp:30-77` | `1` valid, `0` invalid (also for `NULL` with `len>0`); empty is valid |
| `string_view` ctor | `string_view(const char *chars, size_t count)` | `string.hp:84-87` | borrowed window; never frees |
| `string_view::size` / `::data` | `size_t size(void)` / `const char *data(void)` | `string.hp:89-95` | snapshot of `to_view()` time |
| `string_view::equals` | `int equals(const char *other)` | `string.hp:97-109` | `1`/`0`; `NULL` → `0` |
| `string_view::is_valid_utf8` | `int is_valid_utf8(void)` | `string.hp:111-113` | validator over the borrowed window |
| `string` ctor | `string(const char *text)` | `string.hp:121-136` | `NULL` text treated as `""`; may yield invalid object on alloc failure |
| `size` / `capacity` | `size_t size(void)` / `size_t capacity(void)` | `string.hp:138-144` | byte count / allocated bytes |
| `valid` | `int valid(void)` | `string.hp:147-149` | `chars != NULL` (false after `drop()` or failed ctor) |
| `at` | `char at(size_t index)` | `string.hp:151-155` | `'\0'` if invalid or out of range |
| `c_str` | `const char *c_str(void)` | `string.hp:158-160` | never NULL (`""` after `drop()`) |
| `equals` / `compare` | `int equals(const char*)`, `int compare(const char*)` | `string.hp:162-196` | `1`/`0`; `-1`/`0`/`1`; `NULL` → `0` / `1` |
| `find` | `long find(const char *needle)` | `string.hp:198-206` | index or `-1`; `NULL` → `-1` (silent) |
| `reserve` | `int reserve(size_t want)` | `string.hp:209-231` | `0`/`-1`, failure-atomic |
| `append` | `int append(const char *text)` | `string.hp:237-284` | `0`/`-1`, failure-atomic; self/overlap supported |
| `clear` | `void clear(void)` | `string.hp:286-290` | sets count 0, keeps buffer |
| `copy_from` | `int copy_from(string *other)` | `string.hp:293-318` | `0`/`-1`, failure-atomic; self-copy no-op |
| `is_valid_utf8` | `int is_valid_utf8(void)` | `string.hp:321-323` | validator over owned contents |
| `substr` | `int substr(size_t start, size_t n, char *out, size_t out_size)` | `string.hp:326-341` | `0`/`-1`, always NUL-terminates |
| `count_fields` / `field` | `size_t count_fields(char sep)`, `int field(char sep, size_t index, char *out, size_t out_size)` | `string.hp:344-376` | field count / `0`/`-1` |
| `to_view` | `string_view to_view(void)` | `string.hp:378-383` | borrowed snapshot |
| `drop` | `void drop(void)` | `string.hp:385-392` | releases buffer, zeroes state, idempotent |

---

## (2) Allocation paths

All allocation is through the checked allocator in `stdlib/error.hp`
(`stdlib_alloc`/`stdlib_release`; `error.hp:12-23,365-392`).

| Path | Source | Test evidence |
| --- | --- | --- |
| ctor: `stdlib_alloc(n+1)` when `n < STDLIB_ALLOC_LIMIT` | `string.hp:126-135` | injected ctor failure → `valid()==0`, later re-init (`DEEP_STRING_CONTRACT.md:114`; `deep_string_task4.cp` `t4_ctor_*`) |
| `reserve(want)`: single `stdlib_alloc(want)` | `string.hp:218-228` | `reserve(LIMIT+1)` → `-1`, no allocation (`DEEP_STRING_CONTRACT.md:108`; `t4_ovf_reserve_noalloc`) |
| `append`: growth via `string_reserve(this, want)` (single alloc; doubling capped at need) | `string.hp:266-275` | `fail_at(2)` succeeds → exactly one alloc; injected failure atomic (`DEEP_STRING_CONTRACT.md:110-113`) |
| `copy_from(other)`: single `stdlib_alloc(n+1)` | `string.hp:306-317` | `fail_at(1)` → `-1`, byte-exact unchanged state (`DEEP_STRING_TASK4_CONTRACT.md:48`; `t4_copy_fail_*`) |
| `drop`: `stdlib_release` | `string.hp:385-392` | `t4_copy_telemetry_balanced` (alloc==free), `t4_drop3_*` inertness (`DEEP_STRING_TASK4_CONTRACT.md:50,75`) |

- **[VERIFIED]** Telemetry and allocation-sequence probes (`stdlib_alloc_seq`,
  `stdlib_alloc_fail_at/after`, `error.hp:226-251`) pin "one allocation" and
  "no allocation" claims — `deep_string_task4.cp` §2.3 and §2.5,
  `DEEP_STRING_TASK4_CONTRACT.md:57-69,78-88`.
- `STDLIB_ALLOC_LIMIT` defaults to `1<<40` (`error.hp:61-67`) and is `#define`d
  to `4096` in the fixtures before including `error.hp`
  (`DEEP_STRING_CONTRACT.md:34-37`; `DEEP_STRING_TASK4_CONTRACT.md:13-16`),
  so limit branches are reached without stressing host memory.

---

## (3) Input-validation matrix

| Function | Condition | Behaviour | Code / category (`stdlib_note`) | Class | Evidence |
| --- | --- | --- | --- | --- | --- |
| ctor | `text==NULL` | treated as `""`; allocation still occurs | n/a | [VERIFIED] | `string.hp:121-135`; `DEEP_STRING_CONTRACT.md:71` |
| ctor | `n >= STDLIB_ALLOC_LIMIT` or alloc fails | `chars=NULL`, `valid()==0`, reads as empty | `MEMORY`/`RESOURCE` (107) on injected failure | [VERIFIED] | `string.hp:126-135`; `t4_ctor_allocfail_*`; `DEEP_STRING_TASK4_CONTRACT.md:34` |
| `append` | `text==NULL` | `-1`, byte-exact unchanged | `INVALID`/`UNSUPPORTED` (108) | [VERIFIED] | `string.hp:243-247`; `DEEP_STRING_CONTRACT.md:73` |
| `append` | `text==""` | `0`, no growth | n/a | [VERIFIED] | `string.hp:248-250`; `DEEP_STRING_CONTRACT.md:95` |
| `append` | `count+extra+1 > LIMIT` | `-1`, unchanged | `RANGE`/`BOUNDS` (101) | [VERIFIED] | `string.hp:251-256`; `t4_ovf_append_*` |
| `append` | allocation failure | `-1`, pointer/size/capacity/content unchanged | `MEMORY`/`RESOURCE` (107) | [VERIFIED] | `string.hp:267-276`; `DEEP_STRING_CONTRACT.md:110-113` |
| `append` | self / overlapping interior pointer | aliasing detected, `memmove` from saved offset | n/a | [VERIFIED] | `string.hp:261-265,277-280`; `DEEP_STRING_CONTRACT.md:89-94` |
| `equals` | `other==NULL` | `0` | `INVALID`/`UNSUPPORTED` (108) | [VERIFIED] | `string.hp:164-168`; `DEEP_STRING_CONTRACT.md:74` |
| `compare` | `other==NULL` | `1` ("never equal") | `INVALID`/`UNSUPPORTED` (108) | [VERIFIED] | `string.hp:183-187`; `DEEP_STRING_CONTRACT.md:75` |
| `find` | `needle==NULL` | `-1`, **no note** | n/a | [VERIFIED] | `string.hp:200-201`; `DEEP_STRING_CONTRACT.md:76` |
| `copy_from` | `other==NULL` | `-1`, unchanged | `INVALID`/`UNSUPPORTED` (108) | [VERIFIED] | `string.hp:296-300`; `t4_copy_null_*` |
| `copy_from` | `other==this` | `0`, no-op | n/a | [VERIFIED] | `string.hp:301-302`; `t4_copy_self_*` |
| `reserve` | `want <= capacity` | `0`, no allocation | n/a | [VERIFIED] | `string.hp:211-212`; `t4_cap_at_noop_seq` |
| `reserve` | `want > LIMIT` | `-1`, unchanged | `RANGE`/`BOUNDS` (101) | [VERIFIED] | `string.hp:213-217`; `t4_ovf_reserve_*` |
| `substr` / `field` | `out==NULL \|\| out_size==0` | `-1`, **no note** | n/a | [VERIFIED] | `string.hp:328-329,358-359`; `t4_sub_null_out`,`t4_field_null_out` (`DEEP_STRING_TASK4_CONTRACT.md:93`) |
| `substr` | `start > size()` | `-1` | n/a | [VERIFIED] | `string.hp:330-331`; `t4_sub_start_gt_size` |
| `substr` / `field` | requested length exceeds buffer | clamps to `out_size-1`, always NUL | n/a | [VERIFIED] | `string.hp:333-336,363-368`; `t4_sub_exact`,`t4_sub_truncate`,`t4_field_truncate` |
| invalid object (failed ctor / after `drop`) | `size/capacity/at/c_str/equals/compare/find/is_valid_utf8` | `0/0/'\0'/"" /equals("")==1 / compare("")==0 / -1 / 1` | n/a | [VERIFIED] | `string.hp:151-206,321-323`; `DEEP_STRING_CONTRACT.md:123-129`; `t4_invalid_*`,`t4_drop3_*` |
| repeated `drop` / re-init | `drop()` ×3 then `append`/`reserve(n>0)`/`copy_from` | drop inert; re-init restores `valid()==1` | n/a | [VERIFIED] | `string.hp:385-392`; `DEEP_STRING_CONTRACT.md:125-134`; `t4_drop3_reinit_*` |
| `string_view(NULL,3)` | `count>0`, `data==NULL` | `size()==3`, `equals()==0`, `is_valid_utf8()==0` | n/a | [VERIFIED] | `string.hp:84-113,99-102,30-36`; `DEEP_STRING_CONTRACT.md:79` |
| `stdlib_utf8_validate` | overlong/surrogate/>U+10FFFF/truncated/stray | returns `0`; embedded NUL and `F4 8F BF BF` accepted | n/a | [VERIFIED] | `string.hp:30-77`; `DEEP_STRING_CONTRACT.md:158-165`; `deep_string_view.cp` UTF-8 cases |

---

## (4) Ownership and lifecycle analysis

- **`string` owns one buffer** allocated with `stdlib_alloc`; `drop()` releases
  it, nulls the handle and zeroes `count`/`space`
  (`string.hp:117-119,385-392`). The compiler-inserted destructor calls `drop`
  on the owning class; a zero-initialised/failed object has `chars==NULL`, so
  the destructor is inert (`MODERN_STDLIB.md:53-58`; `string.hp:8-12`).
- **Invalid-object state [VERIFIED]:** a failed constructor yields `valid()==0`
  and behaves as empty for reads; `append`, `reserve(n>0)` and `copy_from`
  re-initialise it (`string.hp:8-12`; `DEEP_STRING_CONTRACT.md:114,123-129`).
  Nuance: `reserve(0)` after `drop()` is a no-op, so `valid()` stays `0` until
  an allocating operation (`DEEP_STRING_CONTRACT.md:131-134`).
- **`string_view` owns nothing** — it stores only `(const char *chars, size_t
  count)` (`string.hp:80-81`), has no `drop()` and never allocates. It is
  "valid while its owner lives" (`string.hp:79`); a reallocating
  `reserve`/`append` or `drop()` invalidates the borrowed pointer
  (`DEEP_STRING_CONTRACT.md:136-156`; `DEEP_VECTOR_TASK4_CONTRACT.md:112-119,
  83`). Unlike `vector::slice`, it has **no generation tracking and no
  library-enforced refusal** — detecting staleness is the caller's job
  (`DEEP_VECTOR_TASK4_CONTRACT.md:83`).
- **[VERIFIED, no dereference]** stale-view tests compare pointers/sizes only
  and never call `equals()`/`is_valid_utf8()` on a stale view
  (`DEEP_STRING_TASK4_CONTRACT.md:110,117-119`;
  `DEEP_VECTOR_TASK4_CONTRACT.md:51,88`). Fatal probes are forked children:
  double `stdlib_release` → `CPLUS_EXIT_DOUBLE_FREE` (104); stale-buffer
  `cplus_check_live` → `CPLUS_EXIT_TYPE_CONFUSION` (105)
  (`DEEP_STRING_CONTRACT.md:167-182`).

---

## (5) Error codes and failure atomicity

`string` methods have **no `error *` out-param**; they return `0`/`-1` or a
sentinel and call `stdlib_note(category, code, message)`
(`stdlib/error.hp:194-222`). Categories/codes:

| Code | Category | Trigger |
| --- | --- | --- |
| `CPLUS_EXIT_UNSUPPORTED` (108) | `INVALID` (1) | NULL `append`/`equals`/`compare`/`copy_from` argument |
| `CPLUS_EXIT_BOUNDS` (101) | `RANGE` (2) | `reserve(LIMIT+1)`, `append` result past limit |
| `CPLUS_EXIT_RESOURCE` (107) | `MEMORY` (4) | injected/limit allocation failure |
| n/a | — | `find(NULL)`, `substr`/`field` bad out-buffer, `string_view::equals(NULL)` are silent |

- **Failure-atomicity [VERIFIED]** is the header's central promise
  (`string.hp:7-8`): on `-1` the object is byte-for-byte unchanged. Pinned for
  `append` (`DEEP_STRING_CONTRACT.md:110-113`), `reserve`
  (`DEEP_STRING_TASK4_CONTRACT.md:82`), `copy_from`
  (`DEEP_STRING_TASK4_CONTRACT.md:48`), and overflow paths
  (`DEEP_STRING_TASK4_CONTRACT.md:84`).
- **Telemetry caveat [VERIFIED]:** `stdlib_note` buckets `RANGE` into
  `STDLIB_TM_INVALID`, so telemetry alone cannot distinguish a range failure
  from a null-argument failure; the callback's `(category, code)` pair and the
  `-1` return can (`DEEP_STRING_CONTRACT.md:116-121`; `error.hp:198-215`).
  This is a library-wide convention, not a `string.hp` defect.

---

## (6) Confirmed defects fixed

**Defect count for `string.hp`: 0.** There is no reproduced library defect in
this work; no `string.hp` (or `error.hp`) edit was made to obtain a pass.

Evidence (all independent sources agree):

- `docs/stdlib/DEEP_STRING_CONTRACT.md:12-13`: "No source change to
  `stdlib/string.hp` was required. All behaviour below was reproduced first;
  nothing in the library was relaxed to make a test pass."
- `docs/stdlib/DEEP_STRING_TASK4_CONTRACT.md:24` classifies `DEFECT` and
  reports **"None found"**; §6 (`:280-284`) states
  "**None.** Every Task-4 case passes against the unchanged header in all three
  modes. The three issues encountered were all test-side (section 3) and were
  fixed without touching `stdlib/string.hp` or `stdlib/error.hp`."
- `docs/stdlib/DEEP_VECTOR_TASK4_CONTRACT.md:13-14,25`: "**no confirmed
  defect** in `stdlib/vector.hp`, `stdlib/string.hp` or `stdlib/error.hp`. No
  header was modified."
- Git history: `string.hp` was last changed in `badd477` ("Nuklear GUI,
  multi-TU header linkage, fair benchmarks"), not by the deep-audit commits
  `5fe387d`/`1438bf2` (which touched only `io.hp`/`file.hp`, see
  `git log --oneline -- stdlib/string.hp`). The Task-4 fixture
  `tests/stdlib/deep_string_task4.cp` was added without a header change.

### Test-side issues encountered (NOT counted as defects)

1. **C+ name-based method resolution:** reusing identifier `t4_bad` for a
   `string_view` made the compiler infer `t4_bad.c_str()` as `int`, producing
   four `[CPLUS-FFI] incompatible integer to pointer conversion` errors;
   renaming to `t4_badview` fixed the build
   (`DEEP_STRING_TASK4_CONTRACT.md:121-129`).
2. **Telemetry window bookkeeping:** owner strings constructed before
   `stdlib_telemetry_reset()` produced `alloc=2, free=4`; constructing them
   inside the window fixed it (`DEEP_STRING_TASK4_CONTRACT.md:50-55`).
3. **`substr` expectation:** correct truncation of `"a,b,c"` into a 3-byte
   output is `"a,"`, not `"ab"`; fixture expectation corrected
   (`DEEP_STRING_TASK4_CONTRACT.md:131-133`).

Regression tests covering the unchanged header:
`tests/stdlib/deep_string_append.cp` (81 checks),
`tests/stdlib/deep_string_view.cp` (57 checks),
`tests/stdlib/deep_string_task4.cp` (86 checks) —
`DEEP_STRING_CONTRACT.md:39-63`, `DEEP_STRING_TASK4_CONTRACT.md:162-184,186-278`.

---

## (7) Remaining untested paths and risks

**[UNTESTED / DOCUMENTED-ONLY / OBSERVED] — 7 items:**

1. **Alias pointer past `count` after `clear()` → overlapping `memcpy`.**
   `append` detects overlap only in `[chars, chars+count]` (`string.hp:261-265`);
   after `clear()` an interior pointer past `count` takes the `memcpy` branch
   (`string.hp:280`). A direct probe produced the correct result on this
   backend, so **no defect was reproduced**, but the standard-level
   overlapping-`memcpy` UB is **not diagnosed** because ASan's
   `memcpy-overlap` check is unavailable. Recorded, not changed
   (`DEEP_STRING_CONTRACT.md:184-201`). *Highest residual risk in this module.*
2. **`reserve(0)` after `drop()` leaves `valid()==0`.** Documented nuance
   (`DEEP_STRING_CONTRACT.md:131-134`); no fixture explicitly asserts the
   "stays invalid" half. *Risk:* caller may assume any `reserve` re-initialises.
3. **`substr`/`field` with `out_size==1`.** The `n+1 > out_size` clamp
   (`string.hp:335-336,364-365`) yields an empty string; fixtures use
   `out_size==3`/`6` (`DEEP_STRING_TASK4_CONTRACT.md:98,101`). *Risk:*
   minimum-buffer edge unpinned.
4. **`find("")` (empty needle).** `strstr(chars,"")` should return `chars`
   (`string.hp:202-205`); no fixture checks the empty-needle result.
   *Risk:* unpinned boundary behaviour.
5. **`count_fields` with an absent separator.** Guaranteed "1 when sep is
   absent" (`string.hp:343`); fixtures pin first/last separator positions
   (`t4_field_sep_first/last_*`) but not the no-separator case.
6. **Embedded NUL in `equals`/`compare`/`find` arguments.** These take
   NUL-terminated `const char *` (`string.hp:169,190,202`); the contract notes
   a window with an embedded NUL cannot equal the full string
   (`DEEP_STRING_CONTRACT.md:163-165`). *Risk:* documented limitation, not a
   test-covered behaviour.
7. **Concurrent mutation / thread safety.** No locking; `string` is not
   documented as thread-safe. No fixture exercises concurrent access.

No item above is claimed safe because a passing fixture exists.

---

## (8) Compiler dependencies

- **Class lowering and lowered free-function calls:** `string` and
  `string_view` are `class`es lowered to struct + functions. `append` calls
  `string_reserve(this, want)` (`string.hp:274`) — the lowered name of its own
  `reserve` method — because inside a class body C+ only rewrites
  `this->method()`; internal cross-object calls use lowered names
  (`MODERN_STDLIB.md:34-38`; `string.hp:209`). This is a hard compiler
  dependency: a compiler that does not emit `Class_method` symbols would fail
  to link this header.
- **Descriptor safety of the alias test:** the self-append detection compares
  integer addresses via `(uintptr_t)` (`string.hp:261-264`) to avoid relational
  comparison of pointers into unrelated objects (UB); the comment at
  `string.hp:259-260` and `DEEP_STRING_CONTRACT.md:97-100` document this.
- **Constructor rules:** the ctor assigns fields with no `return`
  (`string.hp:121-136`), satisfying `MODERN_STDLIB.md:59-65`; `c_str()` is
  guaranteed non-NULL even after `drop()` (`string.hp:157-160`).
- **`STDLIB_ALLOC_LIMIT` override:** `#ifndef`-guarded build-time macro
  (`error.hp:57-67`); fixtures define `4096` before including `error.hp`
  (`DEEP_STRING_CONTRACT.md:34-37`). This is a compiler/preprocessor dependency
  of the overflow tests, not of production behaviour.
- **Safe-mode / sanitizer dependencies:** default builds add
  `-fsanitize=undefined,bounds,null,alignment -fno-sanitize-recover=all` and
  `-DCPLUS_SAFE=1` (`compiler/Driver/Driver.cpp:516,519`;
  `DEEP_STRING_CONTRACT.md:23-27`). All three deep fixtures pass in default,
  strict/no-sanitizer and UBSan modes
  (`DEEP_STRING_CONTRACT.md:15-21`, `DEEP_STRING_TASK4_CONTRACT.md:162-184`).
  **ASan unavailable** (`DEEP_STRING_CONTRACT.md:29-32`) — see §7 item 1.
- **Name-based method resolution quirk (test-side):** duplicate owned-variable
  names across functions can make the compiler infer a wrong type for a method
  call (`DEEP_STRING_TASK4_CONTRACT.md:121-129`). Driver flags must be
  separate argv operands (`DEEP_STRING_TASK4_CONTRACT.md:138-140`).
- **Multi-TU linkage:** `static inline` definitions per header
  (`string.hp:30,80,116`) and header-mode member lowering
  (`MODERN_STDLIB.md:100-117`); evidence `tests/stdlib/known_gaps/class_linkage/`
  includes `stdlib/string.hp` in two TUs and links (`MODERN_STDLIB.md:117`).

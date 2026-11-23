# BLUE-TEAM 8 — Hardening the checked allocation APIs

Branch `agent/modern-stdlib`. Owned files only:

| File | Change |
|---|---|
| `stdlib/safe/safe_alloc.h` | 2 additive declarations + 2 additive feature macros; honest-limits text updated |
| `stdlib/safe/src/safe_alloc.c` | internal recent-free cache rewritten; 2 additive helper implementations |
| `tests/defense/alloc/defense_alloc.c` | new before/after regression harness |
| `tests/defense/alloc/run.sh` | new build + `timeout 30` runner |
| `tests/defense/alloc/run_before_after.sh` | new HEAD-vs-working-tree evidence runner |
| `tests/defense/alloc/HARDENING.md` | this report |

No compiler, runtime, other header, existing test, or build file was touched. No
`git add` / `git commit` was performed.

---

## 1. Review of every requested area

| Area | Verdict | Evidence |
|---|---|---|
| `SIZE_MAX` / `count*size` overflow | **Already correct — no change.** `malloc_s` refuses `size > RSIZE_MAX_MEM` with `EINVAL`; `calloc_s` checks `count > SIZE_MAX/size` then `> RSIZE_MAX_MEM` before multiplying (`EOVERFLOW`); `realloc_s` checks before touching the owner; `aligned_alloc_s` checks size and alignment. | regression harness §1: `DEFENSE_ALLOC 4149 0`, guards asserted before and after |
| zero-size | **Already correct — no change.** All paths map 0 to a tracked 1-byte block, including `calloc_s(0,n)` (zeroed) and `realloc_s(p,0)`. | harness §1 |
| alignment constraints | **Already correct — deliberately not tightened.** Power-of-two `>= sizeof(void*)` is enforced. C11 `size % alignment == 0` is *not* added: `posix_memalign` accepts non-multiples, existing tests (`redteam/alloc/redteam_alloc.c:f_align_ok`, `test_alloc_fault_matrix.c:744`, `hundred_alloc.c:c091/c092`) treat e.g. `aligned_alloc_s(16,24)` as valid. Adding the C11 check would break valid-input semantics, which the task forbids. | harness §1 asserts `aligned_alloc_s(16,24)` still succeeds and is 16-byte aligned |
| double-free / invalid-free | **GENUINE GAP FIXED (F-06).** Baseline cache was 1024 direct-mapped single pointers; two distinct freed addresses colliding in one bucket made the second free evict the first, so a real double free was counted as `invalid_free` instead of `double_free` (both refuse — safety held, detection accuracy did not). | before: `RECENT_COLLISION legacy_misclassification 1`; after: `RECENT_COLLISION double_free_classified 1` |
| stale alias at reissued address (F-01, HIGH in tracker) | **GENUINE GAP FIXED additively.** No existing API receives a generation, so the legacy `free_s`/`free_checked_s`/transfer cannot be made generation-aware without breaking their signatures. Two new opt-in helpers now let callers refuse a stale alias explicitly. | before: `STALE_ALIAS legacy_stale_alias_frees_new_owner 1`; after: `STALE_ALIAS generation_free_REFUSED 1`, `GEN_TRANSFER stale_transfer_REFUSED 1` |
| realloc failure atomicity | **Already correct — no change.** Size/injection/table-reservation failures all return `NULL` before host `realloc`; host OOM leaves owner, generation, size and payload intact. ABA generation guard protects bookkeeping. | harness §2: owner/size/payload/generation unchanged on injected failure |
| metadata cache exhaustion | **Already correct — no change.** `tab_reserve_one()` handles growth/rehash and returns `-1` on allocation failure; `finish_alloc` frees the host block and returns `ENOMEM`; `realloc_s` refuses before moving the block. The recent cache is bounded and overwrites its home bucket when full (documented diagnostic limit). | harness §6: 4096 live blocks across table rehashes, exact counters, all metadata lookups valid, clean teardown |
| failure-injection atomicity | **Already correct — no change.** Injection is decided under `g_lock`; `fail_after(0)` fails the next attempt, `fail_after(1)` allows one success, `fail_reset()` restores. Refused `calloc` causes no live-byte drift. | harness §2 |
| null out-params | **Already correct — no change.** `get_meta`/`query_length` zero their out-struct and return `EINVAL` for NULL ptr/out; `transfer_ownership` rejects NULL and `dst==src`. | harness §1 |

Two further gaps were reviewed and **deliberately not changed** because a fix
would alter documented behavior or require changes outside this file ownership:

- **Host `free()` of a tracked pointer (F-02)** leaves a false-live metadata
  entry; detecting it requires interposing the host allocator, not a change to
  this library.
- **Host `realloc` runs with `g_lock` released (F-04, DOCUMENTED-CONTRACT)**:
  holding the lock across the host call would serialize all allocation and
  introduce re-entrancy deadlock risk with malloc hooks. The ABA generation
  guard already protects the metadata table.
- **Recent-cache false positive on a fresh host address reusing a just-freed
  cplus address (F-05)**: with only a pointer value, a recycled address is
  indistinguishable from a true double free. Still refused (safe, never freed
  twice) and documented as best-effort in `safe_alloc.h`.

---

## 2. What changed and why

### 2.1 Open-addressed, generation-tagged recent-free cache (F-06)

In `safe_alloc.c`:

- `static void *g_recent[1024]` → `static recent_slot g_recent[1024]` with
  `{void *ptr; unsigned long long generation;}`. All storage is `static`
  inside the translation unit.
- `hash_ptr` upgraded from a 2-round to the same 3-round mix used by
  `libc/src/stdlib/alloc.c`; distribution only, no exported value depends on it.
- Added internal helpers (all require `g_lock` held, matching the existing
  convention): `recent_find`, `recent_contains`, `recent_remove`, `recent_add`.
  Linear probing with deletion **tombstones** (`RECENT_TOMB`) keeps probe chains
  intact when a reissued address is removed, so a colliding neighbour is not
  lost. When the bounded table fills, the home bucket is overwritten (same
  bounded-diagnostic rule as the libc layer).
- `tab_insert` now calls `recent_remove(p)` instead of clearing one
  direct-mapped slot. `tab_remove` records the slot's generation *before*
  clearing it. `free_checked_s` and `cplus_safe_ptr_was_freed` now use
  `recent_contains`.

Result: two distinct freed addresses that hash to the same bucket no longer
evict one another; a real double free is still classified as `double_free`.

### 2.2 Additive generation-aware helpers (F-01)

Declared in `safe_alloc.h`, defined in `safe_alloc.c`:

```c
int free_checked_generation_s(void *ptr, unsigned long long generation);
int cplus_safe_transfer_ownership_generation(void **dst, void **src,
                                            unsigned long long generation);
```

- A block is released/transferred only when it is live **and** its generation
  equals the caller's captured value.
- Refusal is `CPLUS_SAFE_EINVAL` with `errno = EINVAL`; the live table, payload,
  generation and both out-params are untouched (atomic).
- `generation == 0`, NULL pointers, `*dst != NULL` and `dst == src` are refused.
- Refusals record `stale_free` / `stale_transfer` telemetry in addition to the
  existing `double_free` / `invalid_free` / `free_checked_generation_s` /
  `transfer_owner_generation` counters.
- Feature macros `CPLUS_SAFE_HAS_GENERATION_HELPERS` and
  `CPLUS_SAFE_HAS_TAGGED_RECENT_CACHE` let callers/tests detect availability at
  compile time.

These are **opt-in**. Callers who need stale-alias safety capture the generation
with `cplus_safe_alloc_get_meta` and use the new helpers; callers satisfied with
the legacy heuristics keep the original behavior.

---

## 3. ABI rationale (no ABI-visible change to existing symbols)

- **All 22 pre-existing exported symbols in `safe_alloc.c` are unchanged** —
  same names, C linkage, parameter lists, return types and semantics for valid
  inputs. Verified with `nm -gU` against a fresh build of `HEAD` source:
  `symbols added by hardening` = exactly
  `free_checked_generation_s`, `cplus_safe_transfer_ownership_generation`;
  `symbols removed by hardening` = empty.
- `cplus_safe_alloc_meta` layout is byte-identical:
  `sizeof=40`, offsets `byte_size=0, elem_size=8, elem_count=16, generation=24,
  is_live=32`.
- The recent cache, hash function and all new helpers are internal-to-TU
  statics or additive functions; no existing data structure visible to a caller
  changed size or alignment.
- The two new feature macros and two new declarations are additive source-level
  API; they do not alter any existing call site or link requirement (a caller
  that does not use the new symbols does not need to resolve them).
- Error contracts (`EINVAL` / `ENOMEM` / `EOVERFLOW`), zero-size normalization,
  injection semantics and the `cplus_safe_len` macro are untouched.

---

## 4. Exact commands and before/after results

Working directory: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.
Compiler: `/usr/bin/clang` (Apple clang 16). Every build and run bounded by
`timeout 30`. Work directory:
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/defense_alloc`.

### 4.1 Before/after evidence (single command)

```sh
timeout 30 sh tests/defense/alloc/run_before_after.sh
```

Output:

```
===== BEFORE (HEAD) =====
FEATURES tagged_recent=0 generation_helpers=0
RECENT_COLLISION legacy_misclassification 1
STALE_ALIAS legacy_stale_alias_frees_new_owner 1
GEN_TRANSFER legacy_transfer_generation_blind 1
DEFENSE_ALLOC 4144 0
===== AFTER (HARDENED) =====
FEATURES tagged_recent=1 generation_helpers=1
RECENT_COLLISION double_free_classified 1
STALE_ALIAS generation_free_REFUSED 1
GEN_TRANSFER stale_transfer_REFUSED 1
DEFENSE_ALLOC 4149 0
```

Interpretation:

- `RECENT_COLLISION`: before, a genuine double free after a bucket collision
  incremented `invalid_free` (misclassified); after, it increments
  `double_free`. In both builds the second free is **REFUSED** with
  `errno == EINVAL` and no crash.
- `STALE_ALIAS`: before, `free_checked_s(stale_alias)` released the *new* owner
  of the recycled address; after, `free_checked_generation_s(stale_alias, old_gen)`
  is **REFUSED**, the new block stays live with unchanged size and generation,
  and only a matching generation releases it.
- `GEN_TRANSFER`: before, transfer was generation-blind; after, a stale
  generation is **REFUSED** and both pointers plus metadata are unchanged.
- The 5 extra checks after hardening are the added REFUSED/atomicity
  assertions; the 4144 shared checks pass in both builds, so valid-input
  semantics are preserved.

### 4.2 Standalone hardened build/run

```sh
timeout 30 sh tests/defense/alloc/run.sh
```

```
FEATURES tagged_recent=1 generation_helpers=1
RECENT_COLLISION double_free_classified 1
STALE_ALIAS generation_free_REFUSED 1
GEN_TRANSFER stale_transfer_REFUSED 1
DEFENSE_ALLOC 4149 0
```

Build emits no warnings under `-Wall -Wextra -std=c11`.

### 4.3 UBSan

```sh
P=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/defense_alloc
for f in safe_alloc safe_log safe_stats; do
  /usr/bin/clang -O1 -g -std=c11 -fsanitize=undefined -fno-sanitize-recover=all \
    -Wall -Wextra -Wno-unused-parameter -I stdlib/safe \
    -c stdlib/safe/src/$f.c -o "$P/ubsan_$f.o"
done
/usr/bin/clang -O1 -g -std=c11 -fsanitize=undefined -fno-sanitize-recover=all \
  -Wall -Wextra -I stdlib/safe -I. tests/defense/alloc/defense_alloc.c \
  "$P/ubsan_safe_alloc.o" "$P/ubsan_safe_log.o" "$P/ubsan_safe_stats.o" \
  -pthread -o "$P/defense_alloc_ubsan"
timeout 30 "$P/defense_alloc_ubsan"
```

Result: `DEFENSE_ALLOC 4149 0`, exit 0, **empty sanitizer stderr**.

### 4.4 Existing-suite regression (built into temp only)

Recompiled the SafeCRT allocator objects from the working tree and linked the
existing tests against them (no build directory modified):

```sh
/usr/bin/clang -std=c11 -I stdlib/safe tests/crt/unit/<test>.c \
  $P/safe_alloc.o $P/safe_log.o $P/safe_stats.o \
  [$P/safe_error.o $P/constraint_stubs.o] -pthread -o "$P/<test>"
timeout 30 "$P/<test>"
```

| Test | Result |
|---|---|
| `test_malloc_s` | `unit malloc_s: ok`, rc 0 |
| `test_realloc_s` | `unit realloc_s: ok`, rc 0 |
| `test_free_s` | `unit free_s: ok`, rc 0 |
| `test_alloc_metadata_s` | `unit alloc_metadata_s: ok`, rc 0 |
| `test_calloc_s_overflow_uninit` | `test_calloc_s_overflow_uninit: ok`, rc 0 |
| `tests/scenarios/scenario_11.c` (1200 UAF/alias cases, linked with the existing archives) | `scenario_11: 1200 cases ok`, rc 0 |

### 4.5 ABI verification commands

```sh
/usr/bin/nm -gU "$P/safe_alloc.o" | awk '{print $2,$3}' | sort > new_syms.txt
/usr/bin/nm -gU "$P/head_safe_alloc.o" | awk '{print $2,$3}' | sort > head_syms.txt
comm -23 new_syms.txt head_syms.txt   # 2 additive symbols
comm -13 new_syms.txt head_syms.txt   # empty
```

Result: added = `_free_checked_generation_s`,
`_cplus_safe_transfer_ownership_generation`; removed = none. Struct layout
probe: `sizeof=40 byte_size=0 elem_size=8 elem_count=16 generation=24 is_live=32`.

---

## 5. Honest limitations retained

- The recent-free cache remains bounded (1024 entries) and heuristic: a very
  old freed address can still be reported as `invalid_free` rather than
  `double_free`; a fresh host address reusing a just-freed cplus address can
  still be reported as `double_free`. Both cases **refuse** the operation.
- Legacy `free_s`, `free_checked_s` and `cplus_safe_transfer_ownership` remain
  generation-blind by design; callers needing stale-alias safety must opt in to
  the new helpers.
- Host-side `free`/`realloc` of a tracked pointer still cannot be detected.
- `aligned_alloc_s` continues to accept sizes that are not a multiple of the
  alignment (POSIX `posix_memalign` semantics) because existing valid-input
  tests rely on it.

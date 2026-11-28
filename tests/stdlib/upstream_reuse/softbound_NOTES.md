# SoftBoundCETS check-predicate port into `stdlib/memory.hp`

Date 2026-11-23. Branch `agent/modern-stdlib`. This is a traceability record for
the actual port in `stdlib/memory.hp:122-249` and its regression test
`tests/stdlib/upstream_reuse/softbound.cp`. No compiler, runtime, ABI or
pointer-representation file was touched. Short predicate expressions are
quoted in the `memory.hp` comment block for attribution; no upstream source
file was vendored into this repository.

## 1. Source actually fetched

| Item | Exact URL / identifier | Result |
|---|---|---|
| Repository page | `https://github.com/santoshn/softboundcets-34` | Fetched. Default branch **master**, 28 commits, BSD-3-Clause. |
| API metadata | `https://api.github.com/repos/santoshn/softboundcets-34` | HTTP 403 (unauthenticated rate limit). SPDX/license data cross-checked against the committed study `docs/stdlib/upstream-reuse.md` §1 row 9. |
| Pin | `git ls-remote https://github.com/santoshn/softboundcets-34.git HEAD refs/heads/master` | Both `HEAD` and `refs/heads/master` = **`9a9c09f04e16f2d1ef3a906fd138a7b89df44996`** (verified this session). The existing study records this as the 2014-09-19 merge of PR #4; no tags exist. |
| Directory listing | `https://github.com/santoshn/softboundcets-34/tree/master/softboundcets-lib` | Fetched: `softboundcets.h`, `softboundcets.c`, `softboundcets-checks.c`, `softboundcets-wrappers.c`, MPX variants, `Makefile`. |
| Header | `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/softboundcets-lib/softboundcets.h` | Fetched in full via `webfetch` and again via `curl` (**1313 lines**) to obtain exact line numbers. This is the load-bearing source. |
| Checks file | `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/softboundcets-lib/softboundcets-checks.c` | Fetched in full; it only `#include`s the header, so all checks quoted below are from `softboundcets.h`. |
| LICENSE | `https://raw.githubusercontent.com/santoshn/softboundcets-34/master/LICENSE` | Fetched in full. |
| Commit patch | `https://github.com/santoshn/softboundcets-34/commit/9a9c09f04e16f2d1ef3a906fd138a7b89df44996.patch` | Fetched; parent commit `ccc8c30a` by Edward J. Schwartz, 2014-09-16. |

**License.** Top-level `LICENSE`: `Copyright (c) 2013, Santosh Nagarakatte`,
3-clause BSD text (GitHub SPDX `bsd-3-clause`; clause 3 still contains the
unfilled `{organization}` placeholder). Per-file notice in
`softboundcets.h`: MIT-style permission text,
`Copyright (c) 2014 Santosh Nagarakatte, Milo M. K. Martin. All rights
reserved.`, developed by Nagarakatte, Martin, Zhao and Zdancewic (UPenn).
Both families are permissive. Only ideas/short predicate quotes were adapted,
with attribution in the `memory.hp` comment block; no upstream header, runtime
file, test or trie code was copied.

## 2. Exact upstream source lines (softboundcets.h, pinned HEAD)

Metadata record (L61-105): four fields `void* base; void* bound; size_t key;
void* lock;` with `__BASE_INDEX 0`, `__BOUND_INDEX 1`, `__KEY_INDEX 2`,
`__LOCK_INDEX 3` in the default/spatial+temporal build.

Spatial dereference check — `softboundcets.h:539` (load) and `:555` (store):

```c
if ((ptr < base) || ((void*)((char*) ptr + size_of_type) > bound)) {
```

Overflow-aware bulk checks — `softboundcets.h:576` (dest), `:579` (src):

```c
if(dest < dest_base || dest > ((char*) dest_bound - size) || (size > (size_t) dest_bound))
    __softboundcets_abort();

if(src < src_base || src > ((char*) src_bound - size) || (size > (size_t) dest_bound))
    __softboundcets_abort();
```

The same subtract form recurs in the memset checks at `:620` and `:703`. The
src branch repeats `dest_bound` (`:579`, `:623`) — an upstream typo that was
**not** carried over.

Temporal key/lock check — `softboundcets.h:1004` (load) and `:1037` (store):

```c
size_t temp = *((size_t*)pointer_lock);
if(temp != key) { ... __softboundcets_abort(); }
```

Key allocation at `:1182` and `:1193` (`size_t temp_id =
__softboundcets_key_id_counter++;`, then `*lock = temp_id`), and invalidation
on free at `:1078`:

```c
*((size_t*)ptr_lock) = 0;
```

Also noted but deliberately not ported: `shrink_bounds` at `:509-515` is
`max(old/new base)`, `min(old/new bound)`.

## 3. What was ported, and how it stays ABI-compatible

Only the **check predicates** were ported. Upstream needs an instrumenting
compiler, an mmap'd two-level trie keyed by the pointer's storage address, a
shadow-stack calling convention, a temporal lock allocator, a free map and an
`abort()` on violation. None of that exists in C+ and none of it was copied:
it would change the pointer contract and require compiler/runtime work.

| Upstream predicate | C+ helper (current file lines) | Local substitution |
|---|---|---|
| `ptr < base \|\| ptr + size > bound` (`:539`, `:555`) | `stdlib_mem_span_ok` `stdlib/memory.hp:155`, `stdlib_mem_span_contains` `:171` | Caller supplies explicit `(base, size)`; the upper test is `off > size` then `n > size - off`, so no wrap and no `base+size` pointer arithmetic. Addresses are compared as `uintptr_t`, so relational comparisons are well defined even for distinct objects — the literal `ptr < base` is not strict-safe. |
| `dest > dest_bound - size` (`:576`, `:620`) | `stdlib_mem_span_ok` `:155`; already present in `stdlib_mem_check_range` `stdlib/memory.hp:256` | Capacity comes from the existing `stdlib/error.hp` side table, not a fat pointer. |
| `metadata_load(base, bound)` zero-on-miss | `stdlib_mem_bounds` `stdlib/memory.hp:193` | Returns explicit `(base, size)` from `stdlib_alloc_size`; `-1` + zeroed outputs on NULL/untracked instead of zeroed trie slots. |
| `if (*lock != key) abort()` (`:1004`); `*lock = 0` on free (`:1078`); `key = counter++` (`:1182`) | `stdlib_mem_key_current` `stdlib/memory.hp:216` | The captured **generation token** (`stdlib_mem_token` → `stdlib_alloc_generation`) is the key; the live generation in `stdlib/error.hp` metadata is the lock. `stdlib_transfer_ownership` bumps it and `stdlib_release` forgets the slot, so old keys go stale. `key == 0` opts out. |
| Spatial + temporal check combined | `stdlib_mem_query` `stdlib/memory.hp:229` | Pure query: derives `(base, size)` from metadata, applies the spatial predicate to `[off, off+n)`, then the key/lock check; reports through `error*()` / `stdlib_note` instead of aborting. |

**ABI / representation verdict:** unchanged. `sizeof(void *)` and all public
signatures stay the same; the additions are `static inline` helpers in an
existing header, reading the existing `stdlib_meta_slot` table. Interior
pointers still require the caller to pass `(base, offset)`; there is no
interior-pointer owner lookup and no new side table.

## 4. Regression test and results

`tests/stdlib/upstream_reuse/softbound.cp` is self-checking: 51 `<tag> 0|1`
lines plus `SOFTBOUND <checks> <failures>`; exit 0 only when failures == 0.

- `sb_bounds_query` (`:61`): tracked bounds query, NULL/untracked zeroing, NULL outputs.
- `sb_exact_fit` (`:100`): `n == size` and interior `off+n == size`; query OK; key stable across repeated queries.
- `sb_one_over` (`:143`): `n == size+1`, tail overrun, `off > size`, one-over interior pointer; each refusal reports `STDLIB_CATEGORY_BOUNDS`/`CPLUS_EXIT_BOUNDS` and leaves sentinel bytes intact; empty/NULL spans fail closed.
- `sb_interior_pointer` (`:187`): interior pointer has no metadata of its own; `contains(base, size, base+off, n)` accepts the exact remainder and rejects one-over; one-past with `n==0` accepted, `n==1` refused; below-base pointer (a second real allocation) refused.
- `sb_stale_generation` (`:242`): key current → transfer bumps generation → stale key refused with `STDLIB_CATEGORY_LIFECYCLE`/`CPLUS_EXIT_USE_AFTER_FREE` and no write; fresh key accepted; release/realloc also makes the old key stale.

**Verification actually run (2026-11-23):**

```
./build/c+ build tests/stdlib/upstream_reuse/softbound.cp --strict --sanitize=none -o <out>
  BUILD_RC=0  RUN_RC=0  stdout: SOFTBOUND 51 0  stderr: (empty)
```

Regression sanity under the same strict-none flags (no regressions from the
additive header change): `deep_memory_bounds` `MEMORY_BOUNDS 61 0`;
`memory_ownership/mo_ownership_lifetime` `MEMORY_OWNERSHIP 48 0`;
`fuzz/f100_mem_guarded` `1200 cases ok`. (`regressions/adv_exact_fit.cp` is a
known pre-existing strict-mode A1 FFI failure, documented in
`docs/libc/STRICT_HEADER_ADAPTATION_AUDIT.md`; it was not attributable to or
fixed by this work.)

Two strict-mode test-authoring constraints were hit and resolved locally:
`p[i]` on a pointer parameter is rejected (`[CPLUS-FFI] pointer parameter 'p'
has unverifiable extent`), so sentinel checks use a `memcmp` against a stack
pattern; manufacturing a pointer from an integer is rejected as unverifiable
provenance, so the below-base case uses a second real allocation and only
compares addresses as `uintptr_t`.

**UBSan: attempted, not runnable in this environment.** Both the new test and
the existing `tests/stdlib/deep_memory_bounds.cp` fail identically at build
time:

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:130:23:
  error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
error: failed to compile instrumented C+ runtime
```

This is the pre-existing external-runtime blocker documented in
`docs/security/verification-status.md` §3.1 and
`docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md` (the instrumented runtime path
compiles an external `cplus_runtime.c` against a local `cplus_exit_code` enum
that ends at `CPLUS_EXIT_IO = 109`). Setting `CPLUS_RUNTIME_LIB` to the
prebuilt non-instrumented runtime did not bypass it. Compiling the lowered C
directly with `clang -fsanitize=undefined` was also blocked because the
compiler removes its temporary `.h` includes after emitting. Fixing this
requires compiler/runtime edits, which are outside this task's file ownership;
no UBSan pass/fail claim is made here.

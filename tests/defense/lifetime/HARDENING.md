# BLUE-TEAM 10 — bounds/lifetime metadata hardening (DEFENSE)

Branch: `agent/modern-stdlib`. Files owned (PART A):
`stdlib/memory.hp`, `stdlib/lifetime_adapt.hp`, `tests/defense/lifetime/`.
No compiler/runtime edits, no git add/commit, no other files touched.

## Method

I audited every requested function against the listed attack scenarios
(interior pointer vs exact base, offset+size overflow, stale generation, double
release, use-after-release via metadata, recycled-address generation confusion,
null args). I only changed code where I could demonstrate a genuine gap with
evidence from the current headers, the existing tests, or the existing security
docs. Valid-input semantics, signatures and struct layouts are preserved.

## Gaps found and fixes

### GAP 1 — `stdlib_mem_key_current` failed OPEN for NULL/untracked/stale pointers with `key == 0`  (memory.hp)

Before the fix the body was effectively `if (key == 0) return 1;` with no
pointer check. Its documented contract is *"Returns 1 iff the key still matches
the block's lock"* and *"key == 0 opts out"*. Opting out of the key comparison
does not create a lock for a NULL, untracked or freed pointer, so returning 1
there is a fail-open liveness answer. This is directly reachable because callers
use 0 as a "no key captured" sentinel (e.g. `stdlib_mem_query(..., 0, ...)`).
Evidence: the redteam family `F_KEY_CURRENT` / `F_NULL_ARGS` in
`tests/redteam/bounds/bounds_redteam.cp` exercises stale/NULL pointers against
this predicate; the pre-fix contract contradiction is visible in
`memory.hp` doc lines for the predicate.

**Fix (additive guard, same signature):** check `ptr == NULL ||
stdlib_alloc_size(ptr) == 0` first and return 0; keep `key == 0 -> 1` for a
*tracked* block. Metadata lookup is exact-base (`stdlib_meta_find`,
`error.hp:279`), so an interior pointer is also correctly refused by this new
guard.

**Semantics preserved (verified):** `softbound.cp` (`stale_key_zero_opts_out`,
`exact_key_stable`, `stale_key_after_realloc`) and the 1000/10010 cases in
`adv1000_containers.cp` / `fuzz10k_containers.cp` still pass — they all use a
live tracked pointer with `key == 0`, whose result is unchanged.

### GAP 2 — token-less `stdlib_mem_release` is a double-free / address-recycling hazard on aliased handles  (memory.hp)

`stdlib_mem_release(void **ptr)` calls `stdlib_release(*ptr)` unconditionally
and only NULLs *that* handle. Two `void *` handles aliasing one block
(`void *b = a;`) let a second release reach `cplus_release()` with a dangling
pointer after the first freed it. This is already identified in the repo as a
real gap: `docs/stdlib/allocator/INFRASTRUCTURE_AUDIT.md` §5.2 — *"`stdlib_mem_release`
... calls `stdlib_release` unconditionally ... the stdlib-layer mirror of F-01:
a stale alias whose address was recycled can release a new live block."*
The task explicitly lists "double release" and "recycled-address generation
confusion" as in-scope scenarios.

**Fix (new additive helper, no change to the existing function):**
`stdlib_mem_release_checked(void **ptr, unsigned long long token, error *err)`
refuses to free anything that is not currently a live *tracked* stdlib block
(`stdlib_alloc_size() == 0` → stale alias / recycled / raw pointer) and refuses a
non-zero token whose generation no longer matches. It NULLs the handle before
the allocator call. `stdlib_mem_release` itself is untouched.

**Semantics preserved:** the existing helper keeps its exact documented
"release-once per handle, repeated call is a no-op" behaviour. The new helper
returns STDLIB_OK for a performed release or `*ptr == NULL` no-op, and
STDLIB_FAIL (LIFECYCLE / CPLUS_EXIT_USE_AFTER_FREE for a stale/untracked handle
or stale token; INVALID / CPLUS_EXIT_NULL_POINTER for a NULL handle-pointer)
otherwise. NULL `err` is allowed and still reports via `stdlib_note`.

### GAP 3 — owners had no non-dereferencing liveness query  (lifetime_adapt.hp)

`lifetime_borrow_is_live()` existed, but an owner could only be probed by
calling `lifetime_owner_release()` or `lifetime_transfer()` (both emit error /
telemetry side effects on refusal) or by inspecting `o->ptr` directly (which is
insufficient: a recycled address can be live under a *different* generation).
**Fix (purely additive):** `lifetime_owner_is_live(const lifetime_owner *o)`
mirrors the borrow predicate: NULL / empty / untracked / stale-token owners are
false; it consults metadata only and never touches the target.

### Reviewed and found NOT to be gaps (no change made, with evidence)

- **Interior pointers vs exact base:** `stdlib_meta_find` (`error.hp:279-293`)
  matches `meta_tab[i].ptr == p` exactly. `stdlib_alloc_size`,
  `stdlib_alloc_generation` and `stdlib_alloc_check_generation` therefore
  return miss for `base+off`. `stdlib_mem_bounds` returns `base = ptr` with the
  recorded capacity, so an interior pointer passed as base gets `cap == 0` and
  is refused (`softbound.cp: interior_no_metadata`; new `sem_*` checks).
  `stdlib_mem_span_contains` deliberately accepts an interior pointer as a
  *query subject* over an explicit (base,size) — that is its documented
  purpose, not a gap.
- **offset+size overflow:** `stdlib_mem_span_ok`, `stdlib_mem_check_range`,
  `lifetime_read`/`lifetime_write` all use the subtraction form
  (`off > cap` then `n > cap - off`); no `off + n` is ever computed.
  Regression checks `sem_span_overflow_rejected` and `sem_query_bounds` cover
  `off == SIZE_MAX`. Verified by `softbound.cp` one-over / tail cases.
- **Stale generations / recycled addresses:** generation is monotonically
  increased by `stdlib_meta_record`, `stdlib_realloc` and
  `stdlib_transfer_ownership` (`error.hp:347,459,580`) and zeroed on forget;
  a captured stale token cannot match a later block at the same address.
  `lifetime_transfer` invalidates old borrows; `lifetime_owner_release` refuses
  stale tokens and double release. Verified by `stale_key_after_transfer`,
  `stale_key_after_realloc`, `stale_query_after_transfer`, and the new
  `owner_*` / `sem_query_stale` checks. No change needed.
- **Use-after-release via metadata:** guarded helpers consult metadata before
  any byte access; a freed address has `stdlib_alloc_size() == 0` and is
  refused as INVALID rather than dereferenced. Documented limitation (a stale
  *source* pointer with no source-token parameter) is an API design limit and
  cannot be closed additively without changing signatures — reported openly,
  not silently "fixed".
- **Other null args:** `span_ok`/`span_contains`, `bounds`, `query`,
  `check_range`, copy/move/set/borrow, transfer, release and all lifetime
  entry points already fail closed for NULL inputs. The only exception was
  GAP 1 above.
- **TOCTOU between metadata check and memcpy:** a concurrent release between
  validation and the byte copy is not detected by these pure inline helpers.
  The library documents single-threaded use for `lifetime_adapt.hp` and fixing
  this would require holding the metadata lock across the copy (a runtime
  change, explicitly out of scope). Reported as a known limit, not changed.

## ABI rationale

- Every addition/modification is a `static inline` function in a header. No
  struct/union layout, enum value, macro value, function signature, global or
  exported symbol changed. `nm -g` on the test binary shows no external symbol
  for `stdlib_mem_release_checked`, `lifetime_owner_is_live` or
  `stdlib_mem_key_current`.
- `lifetime_owner` / `lifetime_borrow` structs are byte-for-byte unchanged
  (`{void *ptr; lifetime_token_t token;}`); no token/bound field was added to a
  pointer or descriptor.
- No raw game/runtime offsets, no allocator or compiler changes, no dependency
  added. All callers of the changed predicate with valid tracked input keep
  the exact return value they had before.

## Verification (all under `timeout 30`)

Build/run of the new regression test:

```
./build/c+ build tests/defense/lifetime/lifetime_hardening.cp --strict --sanitize=none -I. -o tests/defense/lifetime/out/lifetime_hardening
tests/defense/lifetime/out/lifetime_hardening
# LIFETIME_HARDEN 39 0   (exit 0)
```

Regression re-run of existing suites that include the two headers:

```
tests/stdlib/deep_memory_bounds.cp        -> MEMORY_BOUNDS 61 0
tests/adversarial_1000/containers/...     -> ADV1000 containers 1000 614 1000 0
tests/fuzz10k/containers/...              -> FUZZ10K containers 10010 0 0 0 6682
```

All passed with 0 failures / 0 crashes. `gcc`-style format check of first-party
C++ is not applicable here (these are C+ `.cp` headers/tests, no compiler
changes).

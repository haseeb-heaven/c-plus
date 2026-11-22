# Agent 1 compiler-capability handoff: concrete reproducers, proposed tags, and acceptance tests

Status: **report only**. This document does not edit `compiler/**` or `runtime/**`, does not
commit, and did not re-run the compiler or test suites for this write-up. Every "Observed"
statement is attributed to a manifest or an audit doc listed in §0.

## 0. Provenance and counting rules

Sources used:

- `docs/libc/COMPILE_TIME_GAPS.md` (46-case synthesis; classify it as a *derived* report, not a manifest).
- `tests/stdlib/compile_time/*.json` — **8 manifests**, not 7: `error.json` (5), `thread.json` (6),
  `collections.json` (6), `vector.json` (8), `net.json` (5), `string.json` (8), `file.json` (8),
  `json.json` (6) = **52 cases**. `json.json` is omitted from `COMPILE_TIME_GAPS.md` (see §11).
- `docs/libc/DISPATCH_BOUNDS_HANDOFF.md`, `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md`,
  `docs/libc/KNOWN_COMPILER_GAPS.md`, `docs/libc/WRAPPER_SAFETY_AUDIT.md`,
  `docs/libc/AUDIT_lifecycle.md`, `docs/libc/AUDIT_stdio_format.md`,
  `docs/libc/RUNTIME_RECOVERY.md`, `docs/libc/NORMAL_VS_CHECKED.md`,
  `docs/libc/WIDE_FORMAT_CONTRACT.md`, `docs/libc/API_STATUS_MATRIX.md`,
  `docs/patches/agent1-len-managed-allocators.md`.

Two rules are applied everywhere below:

1. **Only a diagnostic whose message contains a literal `[CPLUS-` token counts as enforcement.**
   Plain Clang warnings, UBSan aborts, linker/sysroot noise, and untagged C+ lowering errors are
   recorded as evidence of *rejection* but are **never** counted as tagged C+ enforcement. An
   untagged rejection is a `PARTIAL`, not a `REJECT`.
2. **"Compiler must reject" and "runtime mitigation already exists" are separate properties.**
   A header that returns `STDLIB_FAIL`, zeroes a handle, or aborts under UBSan is *not* a
   compile-time proof. Every capability below states both explicitly.

### Cross-tree caveat (read before using the implementation locations)

The authoritative tagged-diagnostic machinery is **not** in this checkout's reduced `compiler/`
mirror: `compiler/Sema/AstSafetyCheck.cpp` here is 257 lines and contains no literal `[CPLUS-`
string, whereas the sibling **`../c-plus-core/compiler/`** tree owns the production passes:

- `../c-plus-core/compiler/Sema/AstSafetyCheck.cpp` — category mapper and `report()` add
  `[CPLUS-...]` to `Diag::message` (~lines 3100–3168), including `CPLUS-OWNER` for
  `use-after-move`, `CPLUS-BORROW` for `borrow`, `CPLUS-ALIAS` for `alias state`,
  `CPLUS-BOUNDS`, `CPLUS-LIFETIME`, `CPLUS-RAII`, `CPLUS-LIBC`, `CPLUS-FFI`.
- `../c-plus-core/compiler/Sema/RaiiLowering.cpp` — the actual `use-after-move: use of
  moved-from resource '<var>'` diagnostic (~line 495) plus untagged borrow/copy/escape errors.
- `../c-plus-core/compiler/Analysis/OwnershipAnalysis.cpp` (743 lines) is the only non-placeholder
  analysis; `BoundsAnalysis.cpp`, `RaiiAnalyzer.cpp`, `Ast/ClassLowering.cpp`, and
  `Ast/ResourceLowering.cpp` are 1-line placeholders in **both** trees.

All implementation locations below are written as `compiler/...` paths; read them as
`../c-plus-core/compiler/...` for the production compiler, with this repository's `compiler/`
as the mirror that must be re-synced. This also qualifies the claim in
`docs/libc/COMPILER_CONTRACT_ALIGNMENT.md` that `compiler/Analysis/libc_contracts.json` is absent:
absent here, present in `c-plus-core`.

## 1. Summary

| # | Capability | Compiler must reject | Runtime mitigation already exists | Proposed stable code | Criticality |
|---|---|---|---|---|---|
| 1 | Class-field lifetime tracking (`drop()` / `close()`) | Yes — 8+ cases silently accepted | Yes (checked accessors / inert handles), incomplete for raw deref and views | `[CPLUS-LIFETIME]`, `[CPLUS-OWNER]` | **Critical** |
| 2 | Guard ownership state | Yes — 4 `thread` cases | Yes (NULL/owner guards in `thread.hp`) | `[CPLUS-GUARD]` | Medium |
| 3 | Alias / exclusivity analysis | Yes — 6 cases | Partial (generation-checked slices); none for dangling views | `[CPLUS-ALIAS]`, `[CPLUS-BORROW]` | **Critical** |
| 4 | Tagged use-after-move / owner-copy diagnostics | Already rejects, but untagged | N/A — rejection exists; only the tag is missing | `[CPLUS-OWNER]`, `[CPLUS-BORROW]` | **Critical** |
| 5 | Stdlib class bounds metadata | Yes — constant OOB accepted | Yes (`at_checked()` returns `-1`) | `[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS]` | High |
| 6 | Function-pointer array dynamic bounds | Dynamic index currently only UBSan-caught | Yes, but it is **Clang UBSan**, not C+ | `[CPLUS-SAFE-BOUNDS]` dynamic guard | High |
| 7 | RAII around `longjmp` | Yes (or auto-register cleanups) | Yes (`cplus_runtime_guard` + manual cleanup registry) | `[CPLUS-RAII]` | Medium |
| 8 | Stdlib class format attributes | Yes — method-call format checks missing | Yes for `_s`/`_checked` APIs only | `[CPLUS-FORMAT]` | Medium |
| 9 | Stdlib class attributes on methods | Yes — attribute parse + propagation | No generic mitigation | `[CPLUS-ATTR]` + semantic tags | Medium |

**Most critical three: #4, #1, #3** (rationale in §10).

---

## 2. Capability 1 — Class-field lifetime tracking for `drop()` / `close()`

**(1) Minimal reproducer** (trimmed from `tests/stdlib/compile_time/vector_use_after_drop.cp`;
same shape covers `net`, `file`, `collections`, `error`, `string`, `json`):

```c
#include "cplus/cplus.h"
#include "stdlib/vector.hp"

int main(void) {
    vector v = vector();
    v.push_back(11);
    v.drop();                    /* lowers to vector_drop(&v): plain method call */
    int out = -1;
    (void)v.at_checked(0, &out); /* use-after-drop */
    v.drop();                    /* double drop */
    return 0;
}
```

**(2) Observed diagnostic or silence.** Both `c+ check --strict` and
`c+ build --strict --sanitize=none` succeed with **no diagnostic at all** for
`vector_use_after_drop.cp`, `collections_use_after_drop.cp`, `net_use_after_close.cp`,
`net_double_close.cp`, `file_read_after_close.cp`, `file_write_after_close.cp`,
`file_double_close.cp`, `error_use_after_reset.cp`, `json_access_after_destroy.cp`, and
`json_builder_after_destroy.cp` (manifests cite `check rc=0, build rc=0, 0 [CPLUS- diagnostics`).
`string_use_after_drop.cp` and `string_double_drop.cp` *do* get tagged rejections only because
those fixtures release the raw managed buffer with `cplus_release()`; their notes explicitly say
literal `s.drop(); s.at(0);` / `s.drop(); s.drop();` are accepted.

**(3) Proposed stable diagnostic codes.**

```
[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: method call on dropped '<var>' (field '<field>')
[CPLUS-OWNER] [CPLUS-SAFE-FREE] double release of '<var>' via <Class>_drop/<Class>_close
[CPLUS-LIFETIME] stale reset-state read of '<var>' after error_reset/clear
```

Existing category strings from `AstSafetyCheck.cpp` are reused; `[CPLUS-SAFE-FREE]` is already
the compatibility twin of `CPLUS-FREE`.

**(4) Minimal implementation location (read-only research).**

- `compiler/Ast/ClassLowering.cpp` (placeholder in both trees) — emit owner metadata for
  pointer/field members of lightweight `class` declarations (`vector::items`, `file::stream`,
  `connection::fd`, `intmap::keys/values/state`).
- `compiler/Sema/CPlusRewriter.cpp` — method-call lowering (`p.method()` → `Class_method(&p, ...)`,
  ~lines 854–895 and 1518–1547) must preserve receiver + callee symbol so the lifetime pass can
  recognise `vector_drop` / `file_close` / `net_drop` as release points.
- `compiler/Sema/AstSafetyCheck.cpp` — model `drop()/close()/reset()` as release and check
  subsequent method calls/sub scripts on the invalidated receiver; emit the tagged diagnostics.
- `compiler/Sema/RaiiLowering.cpp` — treat explicit class `drop()/close()` as a release event in
  the same state lattice used for `cplus_release()`; this is what makes double-drop detectably a
  double release rather than two independent no-ops.
- `compiler/Analysis/OwnershipAnalysis.cpp` — natural home for the field-owner lattice.

**(5) Regression test that would prove the fix.** Add
`tests/stdlib/compile_time/class_field_lifetime_reject.cp` containing the reproducer above plus
the `file`/`net` double-close variants; require **both** commands to exit 1 with a literal
`[CPLUS-LIFETIME]` or `[CPLUS-OWNER]` token in stdout/stderr. Then flip the `expect` fields of
`vector_use_after_drop.cp`, `collections_use_after_drop.cp`, `net_use_after_close.cp`,
`net_double_close.cp`, `file_read_after_close.cp`, `file_write_after_close.cp`,
`file_double_close.cp`, `error_use_after_reset.cp`, `json_access_after_destroy.cp`, and
`json_builder_after_destroy.cp` from `RUNTIME` to `REJECT`. Positive controls
(`vector_valid.cp`, `collections_valid.cp`, `net_valid.cp`, `file_valid.cp`, `error_valid_reset.cp`,
`json_valid.cp`) must remain ACCEPT with zero `[CPLUS-` lines.

**(6) Acceptance criterion.** Ten named cases above each exit 1 from `check --strict` **and**
`build --strict --sanitize=none` with a tagged diagnostic; all ACCEPT controls still exit 0;
the existing `string_use_after_drop.cp` / `string_double_drop.cp` tags remain unchanged. A build
that merely returns `STDLIB_FAIL` at run time does not satisfy this.

**Compiler must reject:** use of a class owner after explicit `drop()/close()` and a second
`drop()/close()` on the same receiver.
**Runtime mitigation already exists (not enforcement):** `vector::at_checked()` returns `-1`;
`net` `drop()` sets `fd=-1, owns=0`; `file` returns `STDLIB_FAIL` with
`STDLIB_CATEGORY_LIFECYCLE`; `collections` checked accessors report the dead state;
`error_reset()` clears fields; `json` dropped handles become inert. Raw dereference of the stale
buffer is *silent* (`vector_use_after_drop.cp` note) and `string_dangling_view.cp` reads freed
memory with nondeterministic exit codes (2 and 5 observed) — runtime mitigation is incomplete.

## 3. Capability 2 — Guard ownership state

**(1) Minimal reproducer** (distilled from the four `thread.json` gap cases):

```c
#include "stdlib/thread.hp"

int main(void) {
    mutex m = mutex();
    guard g = guard(&m);
    g.unlock();
    g.unlock();          /* second unlock: runtime no-op, compile-time accepted */
    m.drop();            /* destroys the mutex handle */
    (void)g.holds();     /* guard outlives its mutex */
    guard bad = guard(NULL);
    bad.unlock();        /* unlock without ownership */
    return 0;
}
```

Plus `thread_invalid_mutex_owner.cp`: construct a guard over a zero-initialised/invalid `mutex`.

**(2) Observed diagnostic or silence.** All four unsafe shapes: `check rc=0, build rc=0`, **no
`[CPLUS-` diagnostic**, binaries exit 0 (`thread.json` cases and its global note). `thread.json`
also records a *removed* `[CPLUS-BORROW]` false positive at `thread.hp:159` that was worked around
by round-tripping `arg` through `uintptr_t` — proof that the header currently compiles cleanly is
not proof of a guard-state proof.

**(3) Proposed stable diagnostic codes.**

```
[CPLUS-GUARD] double unlock of guard '<g>'
[CPLUS-GUARD] unlock of guard '<g>' without ownership
[CPLUS-GUARD] guard '<g>' used after mutex '<m>' was dropped
[CPLUS-GUARD] guard '<g>' constructed over invalid mutex '<m>'
```

`[CPLUS-GUARD]` is new but follows the existing single-namespace scheme; a dynamic companion
`[CPLUS-SAFE-GUARD]` may be added for guard state that becomes invalid only at run time.

**(4) Minimal implementation location.**

- `compiler/Analysis/OwnershipAnalysis.cpp` — extend the ownership lattice with
  `(guard_var, mutex_var, state ∈ {locked, unlocked})` triples; invalidate on `unlock()` and on
  `mutex_drop()`/`m.drop()`; seed from `guard(&m)` / `mutex_lock(&m)` calls.
- `compiler/Ast/ClassLowering.cpp` — expose `guard::owner` and `mutex::handle` fields.
- `compiler/Sema/CPlusRewriter.cpp` — preserve the guard→mutex relation across method-call sugar.
- `compiler/Sema/AstSafetyCheck.cpp` — add `CPLUS-GUARD` to `diagnosticCategory()` and emit the
  tagged errors; `compiler/Diagnostics.h/.cpp` needs no change (tags live in `Diag::message`).

**(5) Regression test that would prove the fix.** Add
`tests/stdlib/compile_time/thread_guard_state_reject.cp` with one function per unsafe shape;
require four distinct `[CPLUS-GUARD]` tagged rejections from both commands. Keep
`thread_valid_guard.cp` and `thread_valid_lock.cp` as ACCEPT controls (they exercise `holds()`,
`unlock()` in valid order and `is_valid()`). Add a runtime suite asserting the no-op behavior
remains as defense in depth.

**(6) Acceptance criterion.** Four unsafe shapes exit 1 with `[CPLUS-GUARD]`; both valid controls
exit 0 with no `[CPLUS-`; no new false positive on `stdlib/thread.hp` itself.

**Compiler must reject:** double unlock, unlock by a non-owner, guard use after mutex destruction,
and guard construction over an invalid mutex.
**Runtime mitigation already exists (not enforcement):** `guard::unlock()` clears its owner
pointer so a second call is a no-op; `guard(NULL)` unlock is guarded; `mutex::drop()` NULLs the
handle and `mutex::unlock()` checks it before touching `pthread_*`. This is runtime-by-design,
not a static proof, and a raw unlocked pthread handle would be a genuine UAF.

## 4. Capability 3 — Alias / exclusivity analysis for class owners

**(1) Minimal reproducer** (combines `vector_mutable_alias.cp`, `vector_invalid_slice.cp`,
`collections_dangling_key.cp`, `string_dangling_view.cp`, `json_dangling_ref.cp`):

```c
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"

int main(void) {
    vector v = vector();
    vector *left  = &v;
    vector *right = &v;        /* two mutable aliases */
    left->push_back(1);
    right->push_back(2);

    vector *slice = v.slice(0, 1);
    v.push_back(3);            /* reallocation invalidates the slice */
    (void)slice->at(0);

    char *key = /* owned buffer */;
    strmap_put(&m, key, 1);
    stdlib_release(key);
    (void)strmap_get(&m, key, &out);   /* released pointer as ordinary argument */
    return 0;
}
```

**(2) Observed diagnostic or silence.** `vector_mutable_alias.cp`, `collections_mutable_alias.cp`,
`vector_invalid_slice.cp`, `string_dangling_view.cp`, `collections_dangling_key.cp`, and
`json_dangling_ref.cp` all compile cleanly (`check rc=0, build rc=0`, **no `[CPLUS-`**). The
manifests note that copying the *owner* (`vector b = a;`, `intmap b = a;`) is rejected, but only
with the untagged `resource ... cannot be copied from ...` message (capability 4).
`collections_dangling_key.cp` notes the asymmetry: a released pointer is tagged only when reached
through `p.len` or explicit `cplus_check_live(p)`, never as a plain function argument.

**(3) Proposed stable diagnostic codes.**

```
[CPLUS-ALIAS]  overlapping mutable borrows of '<v>' through '<left>' and '<right>'
[CPLUS-BORROW] view '<s>' used after source '<v>' was mutated/reallocated
[CPLUS-BORROW] view '<view>' escapes the function that owns '<owner>'
[CPLUS-LIFETIME] released pointer '<key>' passed as argument to '<api>'
```

`CPLUS-ALIAS` and `CPLUS-BORROW` are already recognised categories in `AstSafetyCheck.cpp`.

**(4) Minimal implementation location.**

- `compiler/Analysis/OwnershipAnalysis.cpp` — add pointer-alias sets and borrow/exclusivity state
  for class handles; invalidate all views derived from an owner on mutation/reallocation.
- `compiler/Sema/RaiiLowering.cpp` — existing `borrows_` / `moved_` maps (~lines 412–429) are the
  seed; extend them to method-returned views (`slice`, `strmap::key_view`, `json_view`) instead of
  only raw `T *p = &owner`.
- `compiler/Sema/CPlusRewriter.cpp` — method-call lowering must carry the receiver identity so a
  returned view can be linked to its source.
- `compiler/Sema/AstSafetyCheck.cpp` — check released-pointer arguments at call sites (currently
  only `p.len` / `cplus_check_live(p)` are tagged) and emit the tags.

**(5) Regression test that would prove the fix.** Add
`tests/stdlib/compile_time/class_alias_reject.cp` and `class_view_stale_reject.cp`; require tagged
`[CPLUS-ALIAS]` / `[CPLUS-BORROW]` rejections. Flip `vector_mutable_alias.cp`,
`collections_mutable_alias.cp`, `vector_invalid_slice.cp`, `string_dangling_view.cp`,
`collections_dangling_key.cp`, and `json_dangling_ref.cp` to `REJECT`. Guard against false
positives with the existing `vector_valid_slice.cp`, `collections_valid_borrow.cp`,
`string_valid_view.cp`, and `json_valid_ref.cp` (keys are copied; views consumed while owner lives).

**(6) Acceptance criterion.** Six cases above exit 1 from both commands with a tagged
alias/borrow/lifetime diagnostic; the four positive controls remain ACCEPT; no `[CPLUS-` line is
emitted for a view consumed before mutation or for copied keys.

**Compiler must reject:** two simultaneous mutable aliases of one class owner, use of a
slice/view after reallocation, view escape, and a released pointer passed as a normal argument.
**Runtime mitigation already exists (not enforcement, and partial):** `slice::stale()`/`valid()`
and generation bumps in `vector`; `string_view`/`json_view` have **no** drop and no runtime
liveness guard — `string_dangling_view.cp` reads freed memory, `json_dangling_ref.cp` dangles at
scope exit. `strmap` copies keys but the caller's stale key buffer is still hashed/compared.

## 5. Capability 4 — Tagged `[CPLUS-*` diagnostics for use-after-move and owner-copy

**(1) Minimal reproducer.**

```c
#include "stdlib/vector.hp"
#include "stdlib/file.hp"

int main(void) {
    vector a = vector();
    vector b = cplus_move(a);
    (void)a.size();       /* use-after-move: currently untagged */

    vector c = a;         /* owner copy: currently untagged */
    return (int)(b.size() + c.size());
}
```

Also: `file *p = &f; return p;` (escaping borrow) and
`connection *view = &a; cplus_move(a); view->descriptor();` (borrow after move).

**(2) Observed diagnostic or silence.** These cases are **already rejected** by both commands
(exit 1, no executable), but every rejection is untagged:

| Case | Verbatim untagged diagnostic |
|---|---|
| `collections_use_after_move.cp` | `use-after-move: use of moved-from resource a`; `borrow ... used after that resource was moved`; `resource b cannot be copied from a` |
| `vector_use_after_move.cp` | `use-after-move: use of moved-from resource 'a'` (help: `the handle was zeroed by cplus_move`) |
| `net_use_after_transfer.cp` | `use-after-move: use of moved-from resource 'a'` at 15:28; borrow variant `borrow 'view' of 'a' is used after that resource was moved` |
| `string_invalid_transfer.cp` | `borrow 'borrowed' of 'a' is used after that resource was moved` at 11:24 |
| `file_use_after_move.cp` | `error: use-after-move: use of moved-from resource a` |
| `file_invalid_borrow.cp` (residual variant) | `error: borrow p of local f cannot escape the function` |
| `json_invalid_transfer.cp` | `error: resource 'b' cannot be copied from 'a'` |

Under the counting rule all six are `PARTIAL`, and `net.json` / `file.json` contain contradictions
because their `observed` fields say `REJECT` while `tagged_cplus_diagnostics_observed` is 0 or the
summary counts disagree (§11).

**(3) Proposed stable diagnostic codes** (reuse existing categories, keep the existing help text):

```
[CPLUS-OWNER] use-after-move: use of moved-from resource '<a>'
[CPLUS-OWNER] owner-copy: resource '<b>' cannot be copied from '<a>'
[CPLUS-BORROW] borrow '<p>' of local '<f>' cannot escape the function
[CPLUS-BORROW] borrow '<v>' of '<a>' is used after that resource was moved
```

**(4) Minimal implementation location.**

- `compiler/Sema/RaiiLowering.cpp` — `errorAt()` (~line 189) and the four call sites:
  borrow-after-move (~425–428), escape (~562–564), owner-copy (~612–615), plus the
  `use-after-move` emission (~495 in `c-plus-core`). Prefix the message with the stable tag at
  the emission site, or route all diagnostics through a shared tag helper.
- `compiler/Sema/AstSafetyCheck.cpp` — `diagnosticCategory()` already maps `use-after-move` →
  `CPLUS-OWNER` and `borrow` → `CPLUS-BORROW`; a minimal alternative is to centralise tagging in
  `compiler/Diagnostics.cpp::DiagnosticEngine::report()` so **every** pass is tagged at once.
  The driver renderer (`Driver.cpp` ~340) already highlights `[CPLUS-`, so tags belong in
  `Diag::message`.

**(5) Regression test that would prove the fix.** For each of the six cases, update the expected
`.err` / manifest field to require the literal `[CPLUS-OWNER]` or `[CPLUS-BORROW]` token (an
untagged exit-1 must fail the fixture). Add a compile-fail fixture
`tests/compile_fail/owner_copy_tagged.c` with `error: [CPLUS-OWNER] owner-copy: ...` in its `.err`.
Re-classify the six `PARTIAL` outcomes as `REJECT` and correct `net.json`'s
`tagged_cplus_diagnostics_observed` from 0 to 1.

**(6) Acceptance criterion.** All six cases exit 1 with a `[CPLUS-`-prefixed message from both
commands; every fixture's expected output contains a literal `[CPLUS-` token; the 17 ACCEPT
controls remain clean. An untagged rejection no longer satisfies any REJECT expectation.

**Compiler must reject:** already done semantically — the missing piece is the stable tag, which
is required for machine-checkable enforcement.
**Runtime mitigation already exists:** none needed; this is a diagnostics-contract fix, not a
safety-behavior fix. Claiming "enforcement" before the tag is added would violate the counting rule.

## 6. Capability 5 — Stdlib class bounds metadata

**(1) Minimal reproducer.**

```c
#include "stdlib/vector.hp"

int main(void) {
    vector v = vector();
    v.push_back(1); v.push_back(2);        /* length 2 */
    int out = -1;
    (void)v.at_checked(9, &out);           /* constant index, accepted */
    return 0;
}
```

Companion: `vector_invalid_slice.cp` — index/dereference through a slice after `push_back`
reallocates the backing array.

**(2) Observed diagnostic or silence.** `vector_const_oob.cp`: `check rc=0, build rc=0`, program
prints `const-oob -1 size 2 out -1`, **no `[CPLUS-BOUNDS]`**. The manifest records the key probe:
the *same literal subscript on a local `int[3]`* **is** rejected with
`[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] array index 9 exceeds length 3`. `vector_invalid_slice.cp`
also compiles cleanly; the library reports `stale 1 valid 0 after -1` only at run time.

**(3) Proposed stable diagnostic codes.**

```
[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] array index 9 exceeds length 2 (v.at_checked, field 'used')
[CPLUS-BOUNDS] slice '<s>' index out of range after source '<v>' reallocated
```

**(4) Minimal implementation location.**

- `compiler/Analysis/BoundsAnalysis.cpp` — currently a 1-line placeholder in both trees; needs
  extent metadata for class fields and interprocedural propagation (see also
  `docs/libc/KNOWN_COMPILER_GAPS.md` §1: no interprocedural / pointer-extent / heap-capacity
  analysis; `staticBoundsCheck.cpp` is token-level and single-function).
- `compiler/Sema/StaticBoundsCheck.cpp` — already produces the tagged `int[3]` diagnostic
  (`c-plus-core` ~line 3074 and the tag guard at ~284); extend it to read container
  length/capacity metadata on method calls.
- `compiler/Ast/ClassLowering.cpp` — record `used`/`space` field extents from `vector.hp`.
- `compiler/Sema/CPlusRewriter.cpp` — method-call lowering must expose receiver + method to the
  bounds pass.

**(5) Regression test that would prove the fix.** Flip `vector_const_oob.cp` and
`vector_invalid_slice.cp` to `REJECT` with `[CPLUS-BOUNDS]` in expected output; add
`tests/stdlib/compile_time/class_bounds_metadata.cp` covering `at_checked`, `slice`, and a
checked string accessor. Keep `vector_valid.cp`, `vector_valid_slice.cp`, and
`tests/integration/expected_diag.txt` (`[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] array index 9 exceeds
length 4`) green; no false positive on in-range indices or on accessors whose contract is to
return `-1`.

**(6) Acceptance criterion.** A constant out-of-range index into a class container is rejected by
both commands with `[CPLUS-BOUNDS]`; a stale slice is rejected with a tagged bounds/borrow error;
all bounds ACCEPT controls remain clean; the `int[3]` behavior is unchanged.

**Compiler must reject:** statically knowable out-of-range indices into stdlib class containers
and stale views after reallocation.
**Runtime mitigation already exists (not enforcement):** `at_checked()` returns `-1` and records
bounds telemetry; slices expose `stale()`/`valid()` and return `-1`. Raw stale-buffer dereference
is silent. Separately, `KNOWN_COMPILER_GAPS.md` §1 shows raw `ptr[x]` on `malloc(1)` still has
**no** C+ bounds diagnostic and only optional `malloc_s`/`cplus_memcpy_s` runtime mitigation —
this capability does not claim to fix that wider gap.

## 7. Capability 6 — Function-pointer array dynamic bounds

**(1) Minimal reproducer.**

```c
typedef void (*handler_fn)(void);
void h0(void), h1(void), h2(void);
handler_fn dispatch[3] = { h0, h1, h2 };

int main(void) {
    volatile size_t cmd = 5556;
    dispatch[cmd]();        /* dynamic index, array extent 3 known at compile time */
    return 0;
}
```

Static twin: `dispatch[5556]()` with a literal index.

**(2) Observed diagnostic or silence.** Per `DISPATCH_BOUNDS_HANDOFF.md` and the fixtures:
the constant form is rejected with
`[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] array index 5556 exceeds length 3` (`case13_dispatch_const.c`).
The dynamic form compiles and aborts at run time with
`runtime error: index 5556 out of bounds for type 'handler_fn[3]'` — emitted by **Clang UBSan**,
not by C+. With `--sanitize=none` there is no C+ check at all. Note also that
`tests/compile_fail/case13_dispatch_const.err` contains only `array index 5556 exceeds length 3`
(untagged), so the fixture passing does **not** prove tagged enforcement.

**(3) Proposed stable diagnostic codes.**

```
/* static path, keep as-is */
[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS] array index 5556 exceeds length 3
/* dynamic path, new C+ emitted guard */
[CPLUS-SAFE-BOUNDS] array index out of bounds (dispatch[5556], length 3)
```

The dynamic message must be produced by C+ instrumentation and be distinguishable from UBSan.

**(4) Minimal implementation location.**

- `compiler/Sema/StaticBoundsCheck.cpp` — static rejection already lives here; the dynamic
  instrumentation hook should be added alongside it so the same extent logic is reused.
- `compiler/Analysis/BoundsAnalysis.cpp` — placeholder; natural home for "extent known at
  compile time but index not provable in range" state.
- `compiler/Driver/Driver.cpp` — pipeline step that inserts the pre-subscript guard; it already
  controls sanitizer flags (`-DCPLUS_SAFE=1`, etc.), so the C+ guard must work with
  `--sanitize=none`.

**(5) Regression test that would prove the fix.** Change
`tests/runtime_fail/case13_dispatch_runtime.stderr` to require the C+ tagged
`[CPLUS-SAFE-BOUNDS] ... out of bounds` line and run the fixture with sanitizers disabled; remove
the dependency on `-fsanitize=undefined`. Strengthen
`tests/compile_fail/case13_dispatch_const.err` to require the literal `[CPLUS-BOUNDS]` token.
Add a positive control where the index is provably in range and assert **no** guard diagnostic.

**(6) Acceptance criterion.** With `--sanitize=none`, the dynamic case exits non-zero with a C+
tagged bounds diagnostic naming the array and length; the static case still exits 1 with
`[CPLUS-BOUNDS]`; no check is emitted for a proven-safe index. A UBSan-only abort does not count.

**Compiler must reject / instrument:** a dynamic index that cannot be proven in range when the
array extent is known (or, conservatively, when safe mode requires it).
**Runtime mitigation already exists (not enforcement):** Clang UBSan `-fsanitize=undefined`
abort — explicitly identified in the handoff as *not* a C+ diagnostic.

## 8. Capability 7 — RAII around `longjmp`

**(1) Minimal reproducer** (shape from `AUDIT_lifecycle.md` F3 and `RUNTIME_RECOVERY.md:43`):

```c
#include <setjmp.h>
#include "stdlib/file.hp"
#include "stdlib/thread.hp"

static jmp_buf jb;
static void guarded_body(void) {
    file f = file_open("data", "r");   /* scoped owner: drop() at scope exit */
    mutex m = mutex();
    guard g = guard(&m);
    cplus_runtime_violation("api", "constraint");  /* longjmp past f/g scopes */
}

int main(void) {
    if (cplus_runtime_guard(guarded_body, NULL) != 0) return 1;
    return 0;
}
```

**(2) Observed diagnostic or silence.** No compile-time diagnostic. `AUDIT_lifecycle.md` F3:
`longjmp(g_top->jb,1)` skips all intermediate frames; only entries explicitly registered with
`cplus_runtime_push_cleanup()` run, via `cplus_runtime_cleanup_unwind_to(frame.base)`. The header
admits unregistered resources leak. `RUNTIME_RECOVERY.md:43`: after `longjmp` recovery, a second
UBSan fault on the same thread aborts with `UndefinedBehaviorSanitizer: nested bug in the same
thread, aborting.` `C_BUG_CLASS_MATRIX.md` row 32 marks this `NOT FIXABLE BY LIBC`.

**(3) Proposed stable diagnostic code.**

```
[CPLUS-RAII] longjmp crosses live scope with owner '<f>' lacking registered cleanup
```

Alternative (preferred when feasible): automatically emit `cplus_runtime_push_cleanup()` for
every compiler-owned destructor in a scope containing a `setjmp`/`longjmp` boundary; then no
diagnostic is needed in the fully-instrumented case. `CPLUS-RAII` is already a recognised category.

**(4) Minimal implementation location.**

- `compiler/Sema/RaiiLowering.cpp` — already computes scope-exit drops; extend it to register
  those drops with the runtime cleanup stack when the scope is inside a `cplus_runtime_guard`
  region, or reject when registration is impossible.
- `compiler/Sema/AstSafetyCheck.cpp` — detect `longjmp`/`cplus_runtime_violation` call sites with
  live unregistered owners and emit `[CPLUS-RAII]`.
- `compiler/Driver/Driver.cpp` — ensure the instrumentation runs in both `--strict` and default
  modes and is not elided by `--sanitize=none`.

**(5) Regression test that would prove the fix.** Add
`tests/runtime_fail/longjmp_raii_cleanup.c`: guarded body holds a mutex and a `file`, triggers a
recoverable violation, then assert leak counters and lock state are restored without manual
`cplus_runtime_push_cleanup()`. Add `tests/compile_fail/longjmp_raii_unregistered.cp` whose `.err`
requires `[CPLUS-RAII]`. Add a nested-UB test that documents the accepted abort under UBSan
instead of claiming recovery.

**(6) Acceptance criterion.** Either every compiler-owned owner in a longjmp-crossed scope is
released correctly, or the compiler emits `[CPLUS-RAII]` at the boundary; no double-cleanup when
the user registered cleanup manually; existing runtime recovery suites remain green.

**Compiler must reject or instrument:** non-local jumps that bypass live RAII scopes without
registered cleanups.
**Runtime mitigation already exists (not enforcement, incomplete):** `cplus_runtime_guard()` +
manual `cplus_runtime_push_cleanup()`; `failstop=1` is recommended outside isolated guards. The
UBSan nested-report limitation is documented and must not be claimed as fixed.

## 9. Capability 8 — Stdlib class format attributes

**(1) Minimal reproducer.**

```c
#include "cplus/cplus.h"

class reporter {
public:
    __attribute__((format(printf, 2, 3)))
    void emit(const char *fmt, ...);   /* method-call sugar -> reporter_emit(&r, fmt, ...) */
};

int main(void) {
    reporter r = reporter();
    const char *fmt = get_format_from_input();
    r.emit(fmt, 42);          /* non-literal format: no C+ check */
    r.emit("%d %d", 1);       /* count mismatch: no C+ check */
    return 0;
}
```

**(2) Observed diagnostic or silence.** No manifest case exercises a format attribute on a class
method; the capability is inferred from two documented facts: (a) `API_STATUS_MATRIX.md` line 981
says raw printf/fprintf/snprintf wrappers carry GCC/Clang `format(printf|scanf, m, n)` attributes
(`cplus_libc.h:120-132`) which are **host Clang** diagnostics, not `[CPLUS-`-tagged C+ enforcement;
(b) `error_invalid_lifecycle.cp` / `error_null_pointer.cp` notes say method-call sugar on a
`error*` lowers to a free-function call and is **not checked**, while direct member access is.
`WIDE_FORMAT_CONTRACT.md` F11 and `AUDIT_numeric_wide.md` F11 state Apple Clang ignores
`format(wprintf/wscanf,...)`, so no static wide-format checking exists on this host. Therefore:
**no tagged C+ diagnostic is emitted for method-call format violations.**

**(3) Proposed stable diagnostic codes.**

```
[CPLUS-FORMAT] non-literal format string passed to '<Class>.<method>'
[CPLUS-FORMAT] format argument mismatch in '<Class>.<method>' (expected %d, got <type/count>)
[CPLUS-FORMAT] format attribute on '<Class>.<method>' was ignored by lowering   (compiler bug class)
```

`[CPLUS-FORMAT]` is new; it must not be conflated with `[CPLUS-LIBC]` runtime log lines or with
untagged Clang `-Wformat` warnings.

**(4) Minimal implementation location.**

- `compiler/Sema/CPlusLexer.cpp` — already consumes encoding prefixes (~91–101); extend attribute
  token handling for in-class declarations if needed.
- `compiler/Sema/CPlusRewriter.cpp` — method declaration parsing (~379–418) and method-call
  emission (~854–895, ~1544) must carry `format(fmt_idx, args_idx)` from the method to the
  generated `Class_method` free function. This is the exact point where the attribute is lost.
- `compiler/Ast/ClassLowering.cpp` — placeholder; natural home for method attribute metadata.
- `compiler/Sema/AstSafetyCheck.cpp` — add `CPLUS-FORMAT` to `diagnosticCategory()` and implement
  literal-format / argument-shape checks on the lowered call; do not rely on host Clang warnings
  counting as enforcement.

**(5) Regression test that would prove the fix.** Add
`tests/compile_fail/class_method_format.cp` with a literal format/count mismatch and a
non-literal format argument; require `[CPLUS-FORMAT]` in `.err` from both `check` and `build`.
Add a positive control with `r.emit("%d", 1)` that must remain ACCEPT. Add a header-level test
that `stdlib/video.hp`'s `format(void)` method and any future attributed stdlib method still
compile. Do **not** claim wide-format coverage on Apple Clang; keep the documented limitation.

**(6) Acceptance criterion.** Both commands exit 1 with `[CPLUS-FORMAT]` for a class-method
format mismatch or non-literal format; free-function `printf` diagnostics are unchanged; no
untagged Clang warning is accepted as satisfying the fixture.

**Compiler must reject:** non-literal format strings and argument count/type mismatches on
attributed class methods.
**Runtime mitigation already exists (not enforcement):** `printf_s`/`fprintf_s`/`snprintf_s` and
the `*_checked` siblings reject non-literal formats and `%n`, and the plain wrappers carry host
`format(...)` attributes; neither produces a `[CPLUS-` diagnostic. Wide formats have no effective
checking on this host.

## 10. Capability 9 — Stdlib class attributes on methods

**(1) Minimal reproducer.**

```c
#include "stdlib/vector.hp"

class owner_box {
public:
    __attribute__((cplus_ownership_transfer(1))) void take(int *p);
    __attribute__((cplus_extent(1, 2)))        int  at(int i, int *out);
};

int main(void) {
    owner_box b = owner_box();
    int *p = cplus_alloc(4 * sizeof(int));
    b.take(p);          /* ownership transferred to b */
    p[0] = 1;           /* use-after-transfer: no diagnostic today */
    return 0;
}
```

Also relevant: `vector::at_checked(i, &out)` would be annotated with an extent/length attribute
so capability 5 can prove the index; `file::read` with a borrow attribute so capability 3 can
prove the buffer outlives the call. Currently a scan of `stdlib/*.hp` finds no such attributes —
the only `__attribute__` in these headers is `__attribute__((weak))` in `error.hp:125`.

**(2) Observed diagnostic or silence.** No tagged diagnostic. In-class attributes are either
ignored or rejected by parsing; method-call lowering (`Class_method(&obj, ...)`) discards any
method-level metadata before the safety passes run. This is the mechanism behind the residual
notes in `error_invalid_lifecycle.cp` and `error_null_pointer.cp` (method sugar not checked).
No manifest case currently covers this capability — it is a forward-looking compiler contract.

**(3) Proposed stable diagnostic codes.**

```
[CPLUS-ATTR] unsupported or malformed attribute on method '<Class>.<method>'
[CPLUS-ATTR] attribute '<name>' on '<Class>.<method>' was dropped during lowering
/* violations detected via the attribute then use the semantic category: */
[CPLUS-OWNER] use-after-transfer of '<p>' via '<Class>.<method>'
[CPLUS-BOUNDS] index exceeds extent declared on '<Class>.<method>'
```

**(4) Minimal implementation location.**

- `compiler/Sema/CPlusLexer.cpp` — attribute tokenisation for in-class method declarations.
- `compiler/Sema/TypeChecker.cpp` — validate the supported attribute set and arity; emit
  `[CPLUS-ATTR]` for malformed/unsupported forms.
- `compiler/Sema/CPlusRewriter.cpp` — method parse (~379–418) and call lowering (~854–895,
  ~1518–1547) attach metadata to the generated free function instead of dropping it.
- `compiler/Ast/ClassLowering.cpp` — placeholder; store method attributes with `MethodInfo`
  (currently `compiler/Sema/CPlusRewriter.cpp:71-72` keeps only `methods` / `methodNames`).
- `compiler/Analysis/OwnershipAnalysis.cpp`, `compiler/Analysis/BoundsAnalysis.cpp`,
  `compiler/Sema/AstSafetyCheck.cpp` — consume the propagated attributes and emit the semantic
  tags.

**(5) Regression test that would prove the fix.** Add
`tests/stdlib/compile_time/class_method_attrs.cp`: (a) a malformed method attribute must produce
`[CPLUS-ATTR]`; (b) `take()` + subsequent use of `p` must produce `[CPLUS-OWNER]`;
(c) `at(9, &out)` against declared extent 4 must produce `[CPLUS-BOUNDS]`. Positive control:
correctly attributed methods and an unattributed method must compile cleanly. Rebuild all
`stdlib/*.hp` consumers (`tests/stdlib/multi_tu`, `tests/stdlib/known_gaps/class_linkage`) to
confirm no linkage regression.

**(6) Acceptance criterion.** The compiler accepts the documented attribute set on class methods,
rejects malformed attributes with `[CPLUS-ATTR]`, and the ownership/bounds checks fire **through
method-call sugar** with the semantic tags; free-function behavior and `.hp` linkage are unchanged.

**Compiler must reject:** malformed/unsupported method attributes and semantic violations that
the attributes encode.
**Runtime mitigation already exists (not enforcement):** checked stdlib methods (`at_checked`,
`stale()`, `is_valid()`) still guard many of the same conditions, but there is no generic runtime
substitute for attribute propagation; this is primarily a compiler-only capability.

---

## 11. Most critical three — rationale

1. **Capability 4 (tagged use-after-move / owner-copy).** Highest leverage per unit of work: the
   semantic checks already reject six cases, so adding stable tags converts six `PARTIAL`s into
   genuine tagged `REJECT`s and resolves the `net.json`/`file.json` summary contradictions. Until
   this lands, none of those contract violations counts as enforcement under the project's own
   rule.
2. **Capability 1 (class-field lifetime tracking).** Largest silent-unsafe surface: ten cases
   across seven families (including the omitted `json.json`) accept use-after-`drop()` and
   double-`drop()` with **zero** diagnostics; raw stale-buffer dereferences are silent and one
   dangling view reads freed memory. This is where "compiles cleanly" most strongly diverges from
   memory safety.
3. **Capability 3 (alias/exclusivity).** Six cases compile cleanly and the two view-escape cases
   (`string_dangling_view.cp`, `json_dangling_ref.cp`) have **no runtime mitigation at all** — views
   have no `drop()` and no liveness guard. A container-aware borrow/exclusivity check is the only
   way to make those contracts provable.

Capabilities 5 and 6 are close behind but have effective runtime fallbacks (`at_checked()` and
UBSan, respectively); 2 and 7 are genuine gaps but currently contained by header guards and the
manual cleanup registry.

## 12. Handoff documents whose claims conflict with the current manifests

1. **`docs/libc/COMPILE_TIME_GAPS.md` vs `tests/stdlib/compile_time/file.json` (material).**
   - The report says `file_invalid_borrow.cp` should be recorded **PARTIAL (untagged)**, that
     "`file.json`'s summary says `observed_partial: 2, observed_reject: 0`", that the `file`
     family has **0** tagged REJECTs, and that the overall tagged total is **5**.
   - The **current** `file.json` says `file_invalid_borrow.cp` is `"observed": "REJECT"` with a
     tagged `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] null pointer dereference` on
     `borrowed->stream`; its summary is `observed_reject: 1, observed_partial: 1`; and the
     untagged escape variant is only a *residual note* inside that case, not the case outcome.
   - Consequence: the report's §2 resolution, §3 table row, §4 cross-tab (5 tagged), §5
     "`file` has zero tagged REJECTs", §6 diagnostic list, and §7 handoff counts are stale. On
     current manifests the tagged total is **6**, and `file` has **1**. Agent 1 should treat the
     manifest as authoritative and re-run the synthesis before using those counts.

2. **`docs/libc/COMPILE_TIME_GAPS.md` omits `tests/stdlib/compile_time/json.json` entirely
   (material).** The report says "All 7 manifests" and totals 46 cases; the directory has **8
   manifests / 52 cases**. `json.json` adds two more class-`drop()` gaps
   (`json_access_after_destroy.cp`, `json_builder_after_destroy.cp`), one view-escape gap
   (`json_dangling_ref.cp`), one untagged owner-copy gap (`json_invalid_transfer.cp`), and a
   cross-cutting claim that including `stdlib/json.hp` (via `third_party/yyjson/yyjson.h`)
   **suppresses the AST safety pass entirely** — a plain null dereference or malloc/free UAF in
   the same TU is then accepted with no `[CPLUS-` diagnostic. That last claim is a severity
   multiplier for capabilities 1, 3, and 4 and is absent from every synthesized count.

3. **`tests/stdlib/compile_time/net.json` is internally contradictory (manifest, flagged by the
   report but not corrected).** `net_use_after_transfer.cp` has `"observed": "REJECT"` with
   `"tagged": false`, and the summary says `"reject": 1` while
   `"tagged_cplus_diagnostics_observed": 0`. Under the counting rule this case is `PARTIAL`.
   `net.json` must be corrected as part of capability 4.

4. **`docs/libc/DISPATCH_BOUNDS_HANDOFF.md` fixture evidence is weaker than its claim (test
   design, not a numeric contradiction).** It says the const case "currently passes" and the
   compile-time path is complete, but `tests/compile_fail/case13_dispatch_const.err` contains only
   `array index 5556 exceeds length 3` with **no `[CPLUS-` token** — the fixture would also pass for
   an untagged rejection. It also correctly states that the runtime fixture passes only because of
   UBSan, i.e. it is not C+ enforcement. Both fixtures must be strengthened per capability 6.

5. **`docs/libc/COMPILER_CONTRACT_ALIGNMENT.md` is branch-accurate but cross-tree misleading.**
   Its statement that `compiler/Analysis/libc_contracts.json` "is not present on this branch" is
   true for this checkout, but the file **does** exist in the sibling `c-plus-core` tree, which
   also owns the `[CPLUS-` tag machinery. Its PR #16 re-validation checklist should be read against
   `c-plus-core`, not only against this mirror.

6. **`docs/libc/KNOWN_COMPILER_GAPS.md` §1 and `API_STATUS_MATRIX.md` line 981 describe
   host/sanitizer diagnostics, not tagged C+ enforcement** — consistent with the counting rule, but
   worth flagging because "compile-time `format(...)` attributes (COMPILER-ENFORCED)" and
   "UBSan aborts" can be misread as `[CPLUS-` enforcement. They are not; capabilities 6 and 8
   require C+-tagged diagnostics.

## 13. Global verification checklist for Agent 1

- Re-run the synthesis against all **8** manifests (52 cases), then fix the `file.json` / `net.json`
  contradictions and add `json.json` to the totals.
- For every capability, add both a tagged compile-fail fixture whose expected output contains a
  literal `[CPLUS-` token **and** a positive control that must remain ACCEPT with zero such tokens.
- Treat untagged exits 1 as `PARTIAL`, never as `REJECT`; do not let Clang/UBSan warnings satisfy a
  fixture.
- Keep runtime suites for `RUNTIME` cases; do not delete runtime mitigations when compile-time
  enforcement lands — they are layered defenses, not replacements.
- Re-sync this repository's reduced `compiler/` mirror with `c-plus-core` after each change.

# Porting a Fil-C liveness query into C+ allocator diagnostics

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Repo root:** `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
**Compiler:** `build/c+` (pre-existing binary, not rebuilt by this task)
**Timeout:** `/usr/bin/timeout -k 5 30` on every build and every run

## 1. Strict file ownership

Created/modified **only** these three files:

| File | Change |
| --- | --- |
| `stdlib/allocator.hp` | added the "Fil-C-style capability / liveness queries" section (lines 231–337) |
| `tests/stdlib/upstream_reuse/filc_diag.cp` | new regression test (26 checks) |
| `tests/stdlib/upstream_reuse/filc_diag_NOTES.md` | this file |

No other file was touched. No compiler, runtime, or `stdlib/error.hp` edit. No
`git add` / `git commit` was run.

## 2. Upstream provenance (fetched, with exact URLs / commit / license)

- Repository: `https://github.com/pizlonator/fil-c` (default branch **`deluge`**)
- Pinned commit: **`a80e32804fd44d295deb1ce1b0098962022da0a3`**
  ("Versioning", 2026-10-23). Obtained from the public Atom feed
  `https://github.com/pizlonator/fil-c/commits/deluge/filc/include/stdfil.h.atom`
  (the GitHub REST API returned HTTP 403 from this environment).
- Exact raw URLs fetched:
  - `https://raw.githubusercontent.com/pizlonator/fil-c/deluge/filc/include/stdfil.h`
  - `https://raw.githubusercontent.com/pizlonator/fil-c/deluge/filc/src/runtime.c`
  - same two pinned to commit `a80e328…2022da0a3` (re-fetched for line numbers)
  - `https://raw.githubusercontent.com/pizlonator/fil-c/deluge/libpas/LICENSE.txt`
- Directory layout confirmed via the GitHub tree pages for `deluge`:
  `filc/{include,src,main,tests,…}`, `libpas/{src,LICENSE.txt,…}`, `runtimes/`.
  The Yolo-C file `filc_runtime.c` referenced by `filc/src/runtime.c` is not
  shipped as a discrete source file in this tree; the stable public contract
  lives in `stdfil.h`, which is what this port uses.

**License (verbatim, from `stdfil.h` lines 1–24 and `runtime.c` lines 1–24):** a
BSD 2-Clause-style license, "Copyright (c) 2023-2026 Epic Games, Inc. All Rights
Reserved. / Copyright (c) 2026 Filip Pizlo. All Rights Reserved." with the two
redistribution conditions and the "AS IS" disclaimer. The libpas runtime is
covered by `libpas/LICENSE.txt` (same BSD 2-Clause terms; additionally
"Copyright (c) 2018-2022 Apple Inc. All rights reserved."). The repository
README states: "The runtime is covered by PAS-LICENSE.txt (see libpas/LICENSE.txt
in the source distribution)." Only the **contract/technique** was re-expressed;
no Fil-C code was copied, so this port inherits no licensing obligation beyond
documentation.

## 3. Exact upstream source lines (the technique identified)

`filc/include/stdfil.h` at pinned commit:

```
  46:#define FILC_VERSION 687u
 146:   Freeing an object in Fil-C does not cause memory to be reclaimed immediately. Instead, it changes
 147:   the upper bounds of the object to be the lower bounds and sets the free flag. This causes all
 148:   subsequent accesses to trap with a Fil-C panic.
 156:     address but use the free singleton as their capability. This allows the GC to reclaim memory for
 159:     the free singleton is not user-visible, except via ptr introspection like `%P` or
 213:/* Tells if the pointer has a capability and that capability is not free. */
 214:filc_bool zhasvalidcap(void* ptr);
 217:/* Tells if the pointer is in bounds of lower/upper. This is a guarantee that a 1 byte access will
 218:   succeed on this pointer. */
 219:static inline __attribute__((__always_inline__)) filc_bool zinbounds(void* ptr)
 220:{
 221:    return ptr >= zgetlower(ptr) && ptr < zgetupper(ptr);
 222:}
 224:static inline __attribute__((__always_inline__)) filc_bool zvalinbounds(void* ptr, __SIZE_TYPE__ size)
 225:{
 226:    if (!size)
 227:        return 1;
 228:    char* upper = (char*)zgetupper(ptr);
 229:    return (char*)ptr >= (char*)zgetlower(ptr) && (char*)ptr < upper
 230:        && size <= (__SIZE_TYPE__)(upper - (char*)ptr);
 231:}
```

**Concrete function identified and ported:** `zhasvalidcap(void* ptr)` — a
**liveness-query function** — together with the **free-bit / free-singleton
state** documented on `zgc_free` and the overflow-safe bounds predicate in
`zvalinbounds`. The reusable technique is: *answer "is this pointer still a
valid, non-free allocation?" from side metadata, never by dereferencing the
pointer; freed is a queryable state, not a property of the integer address.*
This matches the project's prior study
(`docs/stdlib/allocator/UPSTREAM_RESEARCH.md:78-97`, `docs/stdlib/upstream_study/
fil_c.md:141-159`), which marked the item **ADAPTABLE (contract only)** — this
task performs that adaptation.

## 4. The C+ state it maps onto (no representation or ABI change)

`stdlib/error.hp` already carries the exact analogue in its side table
(`stdlib_meta_slot`, lines 102–107):

| Fil-C concept | C+ side-table state |
| --- | --- |
| valid, non-free capability | live slot found by `stdlib_meta_find`, `generation != 0`, `byte_size > 0` |
| free flag / free singleton | `stdlib_meta_forget` (lines 354–368) sets `ptr = STDLIB_META_TOMB`, zeroes `byte_size`/`elem_size`/`generation`, decrements `meta_live` |
| capability-less value | no slot at all (`stdlib_meta_find` returns NULL) |
| bounds `upper - ptr` | recorded `byte_size`, queried as `offset <= cap && size <= cap - offset` |
| stale/free-singleton access traps | captured generation no longer matches after free/realloc/transfer |

The port adds **no field, no allocation, no global state, and no pointer change**;
it only reads that table under the existing `meta_lock`.

## 5. What was added to `stdlib/allocator.hp`

- `STDLIB_ALLOC_CAP_NONE` / `STDLIB_ALLOC_CAP_LIVE` (lines 272–273)
- `stdlib_alloc_cap_state(const void*)` (line 279) — one-lock triage of an
  address into LIVE/NONE, the direct analogue of `zhasvalidcap`.
- `stdlib_alloc_hasvalidcap(const void*)` (line 297) — semantic alias of
  Fil-C's `zhasvalidcap`: 1 only for a live, non-free exact-base slot.
- `stdlib_alloc_cap_covers(const void*, size_t offset, size_t size)` (line 310)
  — `(base, offset, size)` form of `zvalinbounds`, overflow-safe by subtraction,
  no pointer arithmetic.
- `stdlib_alloc_cap_check(const void*, unsigned long long gen, size_t offset,
  size_t size)` (line 330) — non-fatal "check-before-use" protocol: liveness +
  generation match + bounds, mirroring the guarantee that a stale/free-singleton
  capability traps on access.

All are `static inline`, fail-closed, never dereference `ptr`, and explicitly
documented as diagnostic hints rather than a safety proof (consistent with the
header's existing contract). Deliberate deviation from `zvalinbounds`: a
zero-length probe still requires a live base here, because this is an
allocation-STATE query; documented in the comment.

## 6. Regression coverage (`tests/stdlib/upstream_reuse/filc_diag.cp`)

| Function | Tags | Requirement |
| --- | --- | --- |
| `fd_live_state` | `fd_live_*` | live hasvalidcap / cap_state / bounds / cap_check, incl. NULL and untracked state |
| `fd_freed_state` | `fd_freed_*` | after `stdlib_release`: cap gone, generation 0, covers=0, old token refused, identity stale |
| `fd_recycled_state` | `fd_recycled_*` | same address reused with a NEW generation: bare address passes but captured generation fails |
| `fd_bounds_edges` | `fd_bounds_*` | offset==cap, offset>cap, range past end, SIZE_MAX size cannot wrap |
| `fd_invalid_refusal` | `fd_refusal_*` | untracked/NULL refused, `stdlib_realloc` refuses with `invalid_ops+1` and `free_count` unchanged, `stdlib_transfer_ownership` refuses untracked source |

Safety: a stale address is used only as an opaque metadata key; it is never
dereferenced and never released twice. Untracked storage is never passed to a
raw free path. Strict-safe patterns: unique `fd_` prefixes, no variable
division/modulo, no pointer arithmetic or integer→pointer casts, no pointer
subscripts, bounded straight-line code.

## 7. Verification

All commands run from the repo root with
`OUT=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/filc_diag_artifacts`.

### 7.1 Strict / no sanitizer (run FIRST)

```sh
timeout -k 5 30 build/c+ build tests/stdlib/upstream_reuse/filc_diag.cp \
  --strict --sanitize=none -o "$OUT/filc_diag_strict_none"
timeout -k 5 30 "$OUT/filc_diag_strict_none" </dev/null
```

**BUILD_RC=0, RUN_RC=0, stderr empty, final line `ALLOC_DIAG 26 0` (26/26 pass).**
Observed: `fd_recycled_same_addr 1` — this host allocator *does* reuse the
address, so the generation check is what distinguishes the recycled block.

### 7.2 UBSan (run AFTER strict/no-sanitizer)

Default UBSan build fails before reaching test code with the already-documented
cross-repo toolchain drift (not caused by, and not fixable within, this task):

```
c-plus-core/runtime/cplus_runtime.c:130:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
c-plus-core/runtime/cplus_runtime.c:137:21: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
error: failed to compile instrumented C+ runtime
BUILD_RC=1
```

Env-only workaround (no file edits), as used by the sibling allocator DIAG task:

```sh
CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include \
  timeout -k 5 30 build/c+ build tests/stdlib/upstream_reuse/filc_diag.cp \
  --strict --sanitize=undefined -o "$OUT/filc_diag_ubsan"
timeout -k 5 30 "$OUT/filc_diag_ubsan" </dev/null
```

**BUILD_RC=0, RUN_RC=0, stderr empty, final line `ALLOC_DIAG 26 0` (26/26 pass).**
This is evidence the *test program* and the new header functions are UB-free;
it does not claim the cross-repo build drift is fixed.

### 7.3 Determinism

`cmp -s` of the strict/no-sanitizer and UBSan stdout: **IDENTICAL** (byte for
byte). SHA-256 both modes:
`1c9ca50093f57769aa111490cfb586861a2553fe79d0ab4b9f53204ee72bf305`.

### 7.4 Existing header consumer (regression guard)

`tests/stdlib/allocator/allocator_test.cp` (the only other file including
`stdlib/allocator.hp`) still builds and passes under `--strict --sanitize=none`:
BUILD_RC=0, RUN_RC=0, final line `ALLOCATOR_TEST 21 0`, stderr empty.

## 8. Findings and limitations

1. The Fil-C **contract** ports cleanly onto the existing C+ side table:
   liveness/bounds become named, fail-closed queries answered without touching
   user bytes. No pointer representation, ABI, or allocator behaviour changed.
2. C+ cannot reproduce Fil-C's per-address *free singleton* exactly: because
   `stdlib_meta_forget` tombstones the slot, a freed address cannot be
   distinguished from a never-allocated address by the address alone. The port
   is therefore honest: `hasvalidcap == 0` covers both, and **generation-based
   identity** (`stdlib_alloc_id` / `cap_check`) is what detects freed→recycled
   transitions. This is documented in the header comment, not hidden.
3. As with all `stdlib/allocator.hp` views, these are diagnostic hints: they do
   not guard raw writes, are not thread-safe beyond the existing `meta_lock`
   (each call is a point-in-time snapshot), and are not safety proofs.
4. No defect was found in `stdlib/error.hp` or `stdlib/allocator.hp`; no fix to
   either was needed beyond the additive API. The UBSan toolchain drift is
   external to these three owned files and was not modified.

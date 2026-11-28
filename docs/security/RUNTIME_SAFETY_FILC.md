# Runtime-safety technique actually used: Fil-C liveness queries at the C+ ABI boundary

Date: 2026-11-23. Branch `agent/modern-stdlib`. This document records the
**one** Fil-C technique actually integrated into this repository — a metadata
*query* for liveness/bounds — and states precisely what was and was not copied.
It is deliberately narrow: it does **not** claim Fil-C-level memory safety, a
capability model, or any ABI change. Claims below are grounded in the files
listed in §0; no behavior is inferred from a build alone.

---

## 0. Evidence base (files read for this document)

- `docs/stdlib/upstream_study/fil_c.md` — upstream research, labelled
  `[OBSERVED]` / `[INFERENCE]` / `[OUT OF SCOPE]`.
- `docs/stdlib/upstream-reuse.md` §10, integration record **I3** (I3 row at
  line 289; per-record detail at lines 355–382; ABI ledger at 573–604).
- `stdlib/allocator.hp` — the actual C+ adaptation (lines 231–337, the
  "Fil-C-style capability / liveness queries" section).
- `stdlib/error.hp` — the side table it reads (`stdlib_meta_slot` line 102–107;
  `STDLIB_META_TOMB` line 260; `stdlib_meta_find` line 279;
  `stdlib_meta_forget` line 354; `stdlib_alloc_generation` / check lines
  519–537).
- `tests/stdlib/upstream_reuse/filc_diag.cp` — the regression test (26 checks).
- `tests/stdlib/upstream_reuse/filc_diag_NOTES.md` — sibling integration note
  with exact pins, license text, and recorded results.

---

## 1. Exact upstream source, commit and license

The technique was derived from the Fil-C project's **stable public introspection
API**, not from compiled code copied into this tree.

| Item | Exact value |
| --- | --- |
| Repository | `https://github.com/pizlonator/fil-c` |
| Branch | `deluge` (default) |
| Pinned commit | `a80e32804fd44d295deb1ce1b0098962022da0a3` ("Versioning", 2026-10-23) |
| Pin provenance | Public Atom feed `https://github.com/pizlonator/fil-c/commits/deluge/filc/include/stdfil.h.atom` (REST API returned HTTP 403 from this environment) |
| Source files consulted | `filc/include/stdfil.h`, `filc/src/runtime.c`, `libpas/LICENSE.txt` |
| Raw URLs | `https://raw.githubusercontent.com/pizlonator/fil-c/deluge/filc/include/stdfil.h` (and the same pinned to `a80e328…2022da0a3`); `…/filc/src/runtime.c`; `…/libpas/LICENSE.txt` |
| Identifiers in `stdfil.h` | `#define FILC_VERSION 687u` (line 46); `zhasvalidcap` (line 214); `zinbounds` (219–222); `zvalinbounds` (224–231); free-flag/free-singleton docs at 146–159 |
| License | **BSD-2-Clause-style**. Verbatim header in `stdfil.h`/`runtime.c` lines 1–24: "Copyright (c) 2023-2026 Epic Games, Inc. All Rights Reserved. / Copyright (c) 2026 Filip Pizlo. All Rights Reserved.", with the two redistribution conditions and the "AS IS" disclaimer. The runtime is `libpas/LICENSE.txt` (same BSD-2-Clause terms, additionally "Copyright (c) 2018-2022 Apple Inc. All rights reserved."); repository README: "The runtime is covered by PAS-LICENSE.txt (see libpas/LICENSE.txt in the source distribution)." |

Licensing position: only the **contract / technique** was re-expressed in C+
wording. **No Fil-C source code was copied** into this repository, so this port
inherits no redistribution obligation beyond documentation/ attribution. The
FILC_VERSION drift noted in the study (docs show 684, raw header shows 687) was
resolved by treating the raw `deluge` header at the pin as authoritative. The
Yolo-C file `filc_runtime.c` referenced by `filc/src/runtime.c` is not shipped as
a discrete source file in this tree; the stable public contract in `stdfil.h`
is what this port uses.

---

## 2. The technique that was actually adapted

### 2.1 What upstream does

From `stdfil.h` (quoted in `filc_diag_NOTES.md` §3 and `upstream_study/fil_c.md`
§§2–3):

```c
/* Tells if the pointer has a capability and that capability is not free. */
filc_bool zhasvalidcap(void* ptr);

static inline ... filc_bool zinbounds(void* ptr) {
    return ptr >= zgetlower(ptr) && ptr < zgetupper(ptr);
}

static inline ... filc_bool zvalinbounds(void* ptr, __SIZE_TYPE__ size) {
    if (!size) return 1;
    char* upper = (char*)zgetupper(ptr);
    return (char*)ptr >= (char*)zgetlower(ptr) && (char*)ptr < upper
        && size <= (__SIZE_TYPE__)(upper - (char*)ptr);
}
```

`zgc_free` documents the matching state change: freeing does **not** reclaim
memory immediately; it collapses `upper == lower`, sets a free flag so all later
accesses trap, and the GC may repoint dangling pointers to a "free singleton"
capability. The reusable idea is therefore: **"is this pointer still a valid,
non-free allocation?" is answered from metadata, never by dereferencing the
pointer; freed is a queryable state, not a property of the integer address.**
The overflow-safe form `size <= upper - ptr` (subtract, never add) is the second
reusable detail.

### 2.2 The existing C+ side table it maps onto

`stdlib/error.hp` already keeps an open-addressed, exact-base side table
(`stdlib_meta_slot` = `ptr`, `byte_size`, `elem_size`, `generation`). The port
adds **no field, no allocation, no global state, no new table, and no pointer
change**; it only reads that table under the existing `meta_lock`. The mapping:

| Fil-C concept | Existing C+ side-table state |
| --- | --- |
| Valid, non-free capability | exact-base live slot found by `stdlib_meta_find`, `generation != 0`, `byte_size > 0` |
| Free flag / free singleton | `stdlib_meta_forget` (called by `stdlib_release`) sets `ptr = STDLIB_META_TOMB`, zeroes `byte_size`/`elem_size`/`generation`, decrements `meta_live` — **this tombstone + zero generation is C+'s free bit** |
| Capability-less value | no slot at all (`stdlib_meta_find` returns NULL) |
| Bounds `upper - ptr` | recorded `byte_size`, queried as `offset <= cap && size <= cap - offset` |
| Stale / free-singleton access traps | captured generation no longer matches after free, realloc, or ownership transfer |

### 2.3 Functions added to `stdlib/allocator.hp` (lines 231–337), all `static inline`

- `STDLIB_ALLOC_CAP_NONE` / `STDLIB_ALLOC_CAP_LIVE` (272–273) — result of a
  metadata query; `LIVE` is the analogue of "has a non-free capability".
- `stdlib_alloc_cap_state(const void*)` (279) — one-lock triage of an address
  into LIVE/NONE; direct analogue of `zhasvalidcap`.
- `stdlib_alloc_hasvalidcap(const void*)` (297) — semantic alias of
  `zhasvalidcap`: 1 only for a live, non-free, exact-base slot.
- `stdlib_alloc_cap_covers(const void*, size_t offset, size_t size)` (310) —
  `(base, offset, size)` form of `zvalinbounds`. The predicate is
  `offset <= cap && size <= cap - offset`, so the sum cannot overflow (the
  subtract form of upstream's `size <= upper - ptr`). No pointer arithmetic or
  cast occurs. A zero-length probe still requires a live base — a deliberate
  deviation: this is an allocation-**state** query, not a pure arithmetic bounds
  check.
- `stdlib_alloc_cap_check(const void*, unsigned long long gen, size_t offset,
  size_t size)` (330) — a non-fatal **check-before-use** protocol: liveness +
  generation match + bounds, mirroring the guarantee that a stale/free-singleton
  capability traps on access.

All five are **fail-closed** and **read-only**: NULL, untracked/interior
addresses, freed blocks and recycled addresses all return 0/NONE. Every one
**never dereferences `ptr`** — it is used only as a lookup key — so querying a
stale pointer is itself safe. They add no allocator state and do not guard raw
memory writes. This is consistent with the header's standing contract that its
views are diagnostic hints, not a safety proof.

---

## 3. Exact test result and reproduction commands

Regression test: `tests/stdlib/upstream_reuse/filc_diag.cp` (26 checks across
`fd_live_state`, `fd_freed_state`, `fd_recycled_state`, `fd_bounds_edges`,
`fd_invalid_refusal`). Its output line is `ALLOC_DIAG <checks> <failures>`.

### Recorded result (sibling note §7, independently re-run in `upstream-reuse.md` §10.5)

- **Strict / no sanitizer** (`--strict --sanitize=none`): BUILD_RC=0, RUN_RC=0,
  stderr empty, final line **`ALLOC_DIAG 26 0`** (26/26 pass).
- **UBSan** (`--strict --sanitize=undefined`): BUILD_RC=0, RUN_RC=0, stderr
  empty, final line **`ALLOC_DIAG 26 0`** (26/26 pass).
- The default UBSan build fails before test code with an external cross-repo
  toolchain drift (`CPLUS_EXIT_ARITHMETIC` undeclared in
  `c-plus-core/runtime/cplus_runtime.c`); the env-only workaround
  `CPLUS_INCLUDE=.../c-plus-core/runtime/include` bridges it without editing any
  file or relaxing any check. The UBSan result is evidence that the test program
  and new header functions are UB-free; it does not claim that drift is fixed.
- Determinism: strict and UBSan stdout are byte-identical (`cmp -s`), SHA-256
  `1c9ca50093f57769aa111490cfb586861a2553fe79d0ab4b9f53204ee72bf305`.
- Regression guard: `tests/stdlib/allocator/allocator_test.cp` (the only other
  consumer of `stdlib/allocator.hp`) still builds and passes under strict:
  `ALLOCATOR_TEST 21 0`, stderr empty.

### Independent confirmation for this document

This author re-ran the strict/no-sanitizer case on 2026-11-23 from the repo
root with `OUT=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/filc_diag_artifacts`:
**BUILD_RC=0, RUN_RC=0, empty stderr, final line `ALLOC_DIAG 26 0`**, including
`fd_recycled_same_addr 1` (this host allocator reuses the address, so the
generation check — not address comparison — distinguishes the recycled block).

### Reproduce commands

```sh
# From the repo root. Run strict/no-sanitizer first.
OUT=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/filc_diag_artifacts
mkdir -p "$OUT"

timeout -k 5 30 build/c+ build tests/stdlib/upstream_reuse/filc_diag.cp \
  --strict --sanitize=none -o "$OUT/filc_diag_strict_none"
timeout -k 5 30 "$OUT/filc_diag_strict_none" </dev/null
# expect: ALLOC_DIAG 26 0   (rc 0, empty stderr)

# UBSan via the env-only bridge for the external runtime skew:
CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include \
  timeout -k 5 30 build/c+ build tests/stdlib/upstream_reuse/filc_diag.cp \
  --strict --sanitize=undefined -o "$OUT/filc_diag_ubsan"
timeout -k 5 30 "$OUT/filc_diag_ubsan" </dev/null
# expect: ALLOC_DIAG 26 0   (rc 0, empty stderr)
```

What the 26 checks prove (and what they do not): they confirm the *contract* —
live/freed/recycled classification, overflow-safe bounds edges (exact end,
one-past, `SIZE_MAX` size cannot wrap), and invalid-free refusal via
`stdlib_realloc`/`stdlib_transfer_ownership` with `invalid_ops + 1`,
`free_count` unchanged, and no host deallocation. The safety invariant in the
test is that a stale address is used **only as an opaque metadata key**; it is
never dereferenced and never released twice. These are deterministic, bounded,
contract-conformance results, not a proof of memory safety.

---

## 4. ABI boundary — explicit non-goals

The adaptation preserves the ordinary C pointer representation. Stated without
qualification:

- **NO fat pointers, NO capability pointers.** A pointer is still a single
  ordinary `T *` word. `sizeof`/alignment/register passing are unchanged. No
  InvisiCaps lower/intval pair, no atomic boxes, no pointer tagging.
- **NO custom ABI.** No `pizlonated_` symbol mangling, no getter indirection,
  no demotion of ODR to Any, no private *pizfix* header/library slice, no
  loader or "libc sandwich" changes. Public C signatures and exported symbol
  names are unchanged — this port adds only `static inline` helpers, no new
  global symbol.
- **NO mandatory GC.** No tracing collector, FUGC, free-singleton repointing,
  finalizer queues, weak references, safepoints, pollchecks, store barriers, or
  whole-program compilation requirement. C+ retains explicit
  `stdlib_alloc`/`stdlib_release` lifetime and the existing side table.
- **Fil-C pointer representation is deliberately NOT copied.** The upstream
  runtime docs state Fil-C has its own ABI ("not compatible with Yolo-C"),
  requires whole-program compilation, relies on ELF (Fil-C does not support
  32-bit systems), and uses a modified LLVM `FilPizlonator` pass. This
  repository builds 32-bit Windows x86 DLLs — outside Fil-C's supported pointer
  width. Porting the representation would break the C ABI and is recorded as
  REJECTED for port in `upstream-reuse.md` §10.4; only the liveness/bounds
  *query vocabulary* was adapted (integration I3).

Consistent with `upstream-reuse.md` §10.5: no ABI-visible API was changed by I3 —
all additions are read-only `static inline` helpers on the existing table; raw
`T *` stays one word, no pointer is widened or tagged, no public signature or
struct layout changes. The broader ABI evidence in the tree (70/70 layout
assertions for `tests/abi/abi_layouts.c` on both toolchains) is reported in
`docs/stdlib/abi-preserving-runtime-safety.md`; symbol export, calling
convention and Windows x86 ABI are explicitly **unverified** there, and this
document does not claim otherwise.

---

## 5. Honest limits

1. **The liveness query is metadata-based, not a dereference-time check.** The
   result answers "does an exact-base live slot exist right now?" as a
   point-in-time snapshot under `meta_lock`. It does not instrument loads or
   stores. Raw writes through an unchecked pointer are still unguarded; only
   operations issued through checked wrappers consult this metadata.
2. **Freed vs. never-allocated is indistinguishable by address alone.**
   Because `stdlib_meta_forget` tombstones the slot and zeroes its fields, a
   freed address returns `hasvalidcap == 0` exactly like a never-allocated or
   interior address. Fil-C's free-singleton *repointing* gives a per-address
   "freed" signal that C+ deliberately cannot reproduce with a tombstone; this
   is documented in the header comment, not hidden.
3. **Generation tokens detect recycled addresses.** An address reused at a new
   generation passes a bare address comparison but fails a generation
   comparison. Identity is the pair (exact base, current generation), captured
   via `stdlib_alloc_id` before a lifetime boundary and re-checked with
   `stdlib_alloc_id_matches` / `stdlib_alloc_cap_check` afterwards. This is the
   mechanism that closes the freed→recycled gap — it is a diagnostic token, not
   an unforgeable capability.
4. **Exact-base keying only.** `stdlib_meta_find` matches exact base addresses;
   an interior pointer has no slot and is reported untracked. Callers must pass
   `(base, offset)`, never a pre-summed `base + offset`.
5. **Diagnostic hints, not safety proofs.** These helpers are read-only,
   fail-closed, and safe to call on stale pointers, but they are not thread-safe
   beyond the per-call snapshot, establish no identity or liveness proof on
   their own, and do not terminate the process on violation (uncatchable panic
   is a Fil-C runtime policy that does not transfer to a header-only library).
   Telemetry counters and metadata views are signals, never proof of a safety
   property. No Fil-C-level memory-safety claim is made anywhere in this port.

---

## 6. Bottom line

The only Fil-C technique actually used in C+ is a **named, fail-closed,
metadata-backed liveness/bounds query** (`zhasvalidcap` / `zvalinbounds`
contract, re-expressed as `stdlib_alloc_cap_state` / `hasvalidcap` /
`cap_covers` / `cap_check` on the existing `stdlib/error.hp` side table). It is
ABI-neutral, copies no Fil-C code, adds no GC or fat pointers, and is verified
by `ALLOC_DIAG 26 0` (26/26) in both strict/no-sanitizer and UBSan modes.
Everything else in Fil-C — InvisiCaps, the custom ABI, GC/safepoints, the free
singleton — was studied and **not** ported.

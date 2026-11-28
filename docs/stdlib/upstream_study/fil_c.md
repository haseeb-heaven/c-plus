# Upstream study: Fil-C runtime allocation tracking & pointer validity

Status: upstream research only. This document records what the Fil-C project
actually does, derived from its official documentation and raw source. It does
**not** propose implementing Fil-C mechanisms inside this repository's C+
standard library, and it does not change any header.

Labels used throughout:

- **[OBSERVED]** — directly quoted or closely paraphrased from a fetched URL
  listed in the Source log. No invention.
- **[INFERENCE]** — my engineering interpretation; not stated by upstream.
- **[OUT OF SCOPE]** — compiler/runtime responsibility, explicitly excluded
  from Agent 2's header-only stdlib work.

---

## Source log (exact URLs and outcomes)

All fetches performed against the repository's default branch `deluge`
(confirmed from GitHub API `?ref=deluge` links).

| # | URL | Result |
|---|-----|--------|
| 1 | `https://fil-c.org/how` | SUCCESS (markdown). Overview: LLVM IR transform, InvisiCaps, FUGC, bigger runtime with safepoints. |
| 2 | `https://fil-c.org/invisicaps.html` | SUCCESS (markdown). Capability model, object header, aux allocation, free-bit, function/thread/special objects. |
| 3 | `https://fil-c.org/fugc.html` | SUCCESS (markdown). GC design, free semantics, finalizers, weak refs/maps, cycle API. |
| 4 | `https://fil-c.org/runtime.html` | SUCCESS (markdown). libc sandwich, `libpizlo.so`, `filc_crt.o`, custom ABI, mangling. |
| 5 | `https://fil-c.org/safepoints.html` | SUCCESS (markdown). Pollchecks, Pizderson frames, soft handshakes, enter/exit, store barrier. |
| 6 | `https://fil-c.org/stdfil.html` | SUCCESS (markdown). Full `stdfil.h` API reference. |
| 7 | `https://api.github.com/repos/pizlonator/fil-c/contents/` | SUCCESS but output truncated by size; enough to identify branch `deluge` and top-level files (`LLVM-LICENSE.txt`, `filc/`, `libpas/`, `README.md`, etc.). |
| 8 | `https://api.github.com/repos/pizlonator/fil-c/git/trees/deluge?recursive=1` | FAILURE — "Response too large (exceeds 5242880 byte limit)". Replaced by narrower directory queries. |
| 9 | `https://api.github.com/repos/pizlonator/fil-c/contents/libpas/src/libpas?ref=deluge` | SUCCESS (truncated preview). Confirmed real paths `libpas/src/libpas/fugc.c`, `fugc.h`, `filc_runtime.c`, `filc_runtime.h`, `filc_runtime_inlines.h`, `verse_heap.h`, `filc_default_settings.c`, `filc_dump_heap.c`, etc. |
| 10 | `https://api.github.com/repos/pizlonator/fil-c/contents/filc/include?ref=deluge` | SUCCESS. Confirmed `filc/include/stdfil.h`, `pizlonated_runtime.h`, `pizlonated_syscalls.h`, `unwind.h`, etc. |
| 11 | `https://raw.githubusercontent.com/pizlonator/fil-c/deluge/libpas/src/libpas/fugc.h` | SUCCESS (raw C, ~14 KB). GC marker/object/free-singleton internals. |
| 12 | `https://raw.githubusercontent.com/pizlonator/fil-c/deluge/filc/include/stdfil.h` | SUCCESS (raw C, ~38 KB). Introspection API definitions. |
| 13 | `https://raw.githubusercontent.com/pizlonator/fil-c/deluge/LICENSE` | FAILURE — HTTP 404. There is no root file literally named `LICENSE`. |
| 14 | `https://api.github.com/repos/pizlonator/fil-c/license` | SUCCESS. Detected license file is `LLVM-LICENSE.txt`; API reports `spdx_id: NOASSERTION`, `key: other`, `name: Other`. Decoded header text begins: "The LLVM Project is under the Apache License v2.0 with LLVM Exceptions". |

Local files consulted (verified to exist): `stdlib/error.hp`, `stdlib/vector.hp`,
`docs/stdlib/` directory listing, `AGENTS.md`.
Local files cited by task but **not present at study time**:
`docs/stdlib/agent2_audit/module_audits/error.hp.md` and
`docs/stdlib/agent2_audit/module_audits/vector.hp.md` (the `agent2_audit`
directory did not exist yet — parallel Agent 2 work). They are cited below as
forward references only; no content is attributed to them.

---

## 1. Runtime allocation tracking design

**[OBSERVED] `malloc`/`free` are thin forwarders to GC primitives.**
From `https://fil-c.org/stdfil.html`: "`malloc` in the Fil-C libc is implemented
as just a call to `zgc_alloc`. Similarly, `free` is just `zgc_free`." From raw
`stdfil.h`: "libc's malloc just forwards to this. There is no difference between
calling `malloc` and `zgc_alloc`."

**[OBSERVED] Allocation API (raw `filc/include/stdfil.h`).**
`void* zgc_alloc(__SIZE_TYPE__ count)` — zero-initialized; "May allocate
slightly more than `count`, based on the runtime's minalign (which is currently
16)". `zgc_aligned_alloc`, `zgc_realloc`, `zgc_aligned_realloc`,
`zgc_realloc_preserving_alignment` also exist. Realloc/free require
`ptr == zgetlower(ptr)` or the runtime panics.

**[OBSERVED] Every object has metadata outside the program-visible payload.**
From `invisicaps.html`: a flight pointer's *lower* points "right above the
object header", which contains the *upper bound ptr*, the *aux word*, and
flags. The aux word "uses only its lower 48 bits for storing the aux allocation
pointer; the upper 16 bits are used for flags and additional data". If any
pointer is stored into an object, an *aux allocation* of the same size as the
payload is created; "all pointers at rest in the object have their *lower* in
the aux allocation and their *intval* in the object payload". If the aux word
is NULL, the GC treats the object as a leaf.

**[OBSERVED] Allocation is heap-config driven.** Raw `fugc.h` includes
`"verse_heap.h"` and declares multiple heaps: `fugc_default_heap`,
`fugc_destructor_heap`, `fugc_census_heap`, `fugc_finalizer_heap`, plus
`verse_heap_object_set` instances (`fugc_destructor_set`, `fugc_census_set`,
`fugc_finalizer_set`, `fugc_scribble_set`). `https://fil-c.org/fugc.html`
describes a "bitvector SIMD" sweeping algorithm from the "Verse heap config",
typically "<5% of its time sweeping".

**[OBSERVED] GC trigger/cycle API.** Raw `fugc.h`: `fugc_request`,
`fugc_request_fresh`, `fugc_wait`, `fugc_completed_cycle`,
`fugc_requested_cycle`. Docs note GC is normally triggered by the verse heap's
live-bytes callback, not by user code.

**[OBSERVED] Alignment/special-object metadata.** `invisicaps.html`: extra
aux-word flags encode alignment for `posix_memalign`/`mmap`/Sys-V shm, and
mark special objects (functions, `zthread`, `jmp_buf`-like internals) whose
upper bound equals the lower bound so all data accesses are prohibited.

**[INFERENCE]** The tracking model is "object header + side allocation for
interior pointer metadata + flags in unused high bits", i.e. a hybrid of inline
header and sidecar table. This is qualitatively similar in *shape* to this
repo's `stdlib_meta_slot` table in `stdlib/error.hp` (lines 102-107:
`ptr`, `byte_size`, `elem_size`, `generation`), but Fil-C's metadata is trusted
by hardware-adjacent check code on every access, whereas ours is a diagnostic
hash table consulted only by explicit query functions.

---

## 2. Pointer validity checks (fat pointers / capabilities)

**[OBSERVED] The capability model is InvisiCaps ("Invisible Capabilities").**
From `invisicaps.html`: "each pointer dynamically tracks what object in memory
it should be allowed to access, and using that pointer to access any memory
that is not that object is dynamically prohibited." It forbids OOB access,
access to freed objects, writes to readonly data, reads/writes of non-data or
runtime-special objects, and corruption of capability metadata.

**[OBSERVED] Pointers are NOT classic fat pointers.** `invisicaps.html`:
"preserving `sizeof(T*)` to be the same as what you'd expect — i.e. 8 bytes on
64-bit platforms." A *flight pointer* is conceptually a pair:

- *lower* (trusted; the capability; points just above the object header),
- *intval* (untrusted raw integer value the C program sees).

Every access uses untrusted *intval* plus trusted *lower*. A NULL *lower* means
the pointer cannot be accessed. Pointers at rest keep *lower* in the aux
allocation and *intval* in the payload, so "no capability has the locations
used for storing *lower*s (or any other Fil-C metadata) within its bounds" —
this is the "invisible" property. Atomic pointers use 16-byte-aligned *atomic
boxes* stored with 128-bit atomics (low bit of an aux entry distinguishes
*lower* from box pointer).

**[OBSERVED] Prior models rejected (useful design history).** `invisicaps.html`:
PLUT (256-bit pointers, no GC, UAF not detected), SideCaps (128-bit atomic
protocol, ~200x slowdown, no unions), MonoCaps (128-bit, ~10x, deterministic
UAF panic, required GC). InvisiCaps restore 64-bit pointers and meaningful
unions; overhead "about 4x in the bad cases". Described as a thread-safe
variant of SoftBound and a software CHERI with more deterministic UAF.

**[OBSERVED] Introspection API (raw `stdfil.h`), exact signatures:**

```c
void* zgetlower(void* ptr);
void* zgetupper(void* ptr);
filc_bool zis_readonly(void* ptr);
#define zlength(ptr) /* (upper - ptr) in units of typeof(*ptr) */
filc_bool zhasvalidcap(void* ptr);   /* "has a capability and that capability is not free" */
static inline filc_bool zinbounds(void* ptr) {
    return ptr >= zgetlower(ptr) && ptr < zgetupper(ptr);
}
static inline filc_bool zvalinbounds(void* ptr, __SIZE_TYPE__ size) { /* lower <= ptr, ptr+size <= upper */ }
static inline void zcheck(void* ptr, __SIZE_TYPE__ size);          /* panics via zsafety_errorf */
static inline void zcheck_readonly(void* ptr, __SIZE_TYPE__ size);
```

Also: `zmkptr`/`zorptr`/`zandptr`/`zxorptr`/`zretagptr` for safe pointer tagging,
`zsetcap` to set a capability over a range, `zptr_to_new_string`/`%P` to dump a
full pointer, and `zptrtable`/`zexact_ptrtable` (with weak variants) to encode
pointers as integers while retaining/repuriating capabilities.

**[OBSERVED] Docs/source drift (minor).** `https://fil-c.org/stdfil.html` shows
`#define FILC_VERSION 684u` and defines `zvalinbounds` via
`zinbounds(ptr) && zinbounds((char*)ptr + size - 1)`; raw `stdfil.h` on branch
`deluge` has `#define FILC_VERSION 687u` and an explicit
`size <= (upper - ptr)` check. Treat the raw header as current.

**[INFERENCE]** The reusable idea is not "copy capabilities" but "make
validity/bounds/liveness *queryable and named* in the public API, with a
distinct panic path for safety failures". This maps cleanly onto the existing
C+ `error` categories and metadata query helpers (see §6).

---

## 3. Memory lifetime and use-after-free detection

**[OBSERVED] `free` is logical invalidation, not immediate reclamation.**
Raw `stdfil.h` comment on `zgc_free`: "Freeing an object in Fil-C does not
cause memory to be reclaimed immediately. Instead, it changes the upper bounds
of the object to be the lower bounds and sets the free flag. This causes all
subsequent accesses to trap with a Fil-C panic." `invisicaps.html` states the
same: "Freeing sets the pointer's upper bound to be equal to *lower*, thus
preventing all accesses. A *free* bit is set in the aux word so that the
diagnostic message ... indicates that it was because of use after free."

**[OBSERVED] Free-singleton repointing gives deterministic UAF beyond
reclamation.** `fugc.html`: "FUGC will redirect all capability pointers
(*lower*s) to free objects to point at the free singleton object instead. This
allows freed object memory to really be reclaimed." Consequence: "you will trap
even after memory is reclaimed (due to *lower* repointing to the free
singleton)." Raw `fugc.h` implements exactly this in
`fugc_mark_or_free_flight()` / `fugc_mark_or_free_lower_or_box()`: check
`FILC_OBJECT_FLAG_FREE`, skip `filc_free_singleton`, otherwise weak-CAS the
lower to `filc_object_lower_not_null(&filc_free_singleton)`.

**[OBSERVED] Guarantee summary from `fugc.html`:**
"Freeing an object and then accessing it is guaranteed to result in a trap";
"Freeing an object twice is guaranteed to result in a trap"; "Failing to free
an object means the object gets reclaimed for you." `free` also stops the GC
scanning outgoing references, avoiding GC-induced leaks from dangling
pointers.

**[OBSERVED] Object flags in raw `fugc.h`.** `filc_object_flags`,
`filc_aux_get_flags(aux)`, `FILC_OBJECT_FLAG_GLOBAL`, `FILC_OBJECT_FLAG_FREE`,
`FILC_OBJECT_FLAGS_SPECIAL_MASK`, `FILC_OBJECT_FLAG_WEAK_KEY`,
`filc_object_flags_log_align(flags)` (aligned objects take the slow mark path
because they may carry a finalizer queue or JIT module edge).

**[OBSERVED] Lifetime support beyond plain free.** `zgc_finq_new/poll/wait/
alloc` finalizer queues ("Java-style ... you get to set up your own finalizer
queues and choose which thread processes them"), `zweak_new/zweak_get`
(weak pointers become NULL when dead **or freed**), `zweak_map_*`, and a
GC-cycle API used to decide when to sweep weak references.

**[INFERENCE]** The key property to borrow conceptually is *one action at
lifetime end that both (a) flips a liveness bit and (b) makes every existing
alias fail closed*. This repo already has a weaker analogue:
`stdlib_release()` → `stdlib_meta_forget()` sets `ptr = STDLIB_META_TOMB` and
`generation = 0` (`stdlib/error.hp` lines 348-362, 517-524) and
`vector::drop()` bumps `generation` (`stdlib/vector.hp` lines 297-305). Unlike
Fil-C, our raw `int*` copies are not checked on dereference, so this remains a
diagnostic/generation scheme, not a memory-safety guarantee.

---

## 4. Cleanup semantics

**[OBSERVED] Optional explicit free plus automatic GC.**
`stdfil.html`: "Freeing objects is optional in Fil-C, since Fil-C is garbage
collected." `zgc_free` requires a base pointer (`ptr == zgetlower(ptr)`),
must be live, and is idempotence-checked (double free traps). `free` is a
one-way transition to the free state, not a deallocation.

**[OBSERVED] Reclamation is a separate, later phase.** `fugc.html` collector
loop: trigger → store barrier on → black allocation → mark globals → soft
handshake/stack scan → tracing fixpoint → store barrier off → sweep (SIMD
bitvector) → repeat. `stdfil.h`: `zgc_request_and_wait()` does not decommit;
call `zscavenge_synchronously()` afterwards to return memory to the OS. Cycle
observability via `zgc_completed_cycle()` / `zgc_requested_cycle()`.

**[OBSERVED] Finalizer semantics are one-shot and queue-owned.**
`stdfil.html`: "Finalizable objects get one shot at being on the finalizer
queue"; an object is reclaimed only after being dequeued and not otherwise
reachable; finalizable objects hold a *weak* reference to their `zgc_finq`.

**[OBSERVED] Panic is the failure path, not a return code.** `zerror`,
`zsafety_error`, `ZSAFETY_CHECK`, `zcheck`: "shuts the program down using the
Fil-C panic mechanism (prints a stack trace, kills the program in a way that
the program cannot intercept)"; `zset_quiet_panic()` can silence output.

**[INFERENCE]** Cleanup in this repo is deliberately different and should stay
so: `stdlib_release()` is recoverable/diagnostic, and `error.hp` line 6 states
"Nothing in this header terminates the process; fatal safety violations stay
the runtime's job (`cplus_fatal`)." Fil-C's uncatchable panic is a runtime
policy that does not transfer to a header-only library.

---

## 5. Compiler/runtime responsibilities — explicitly OUT OF SCOPE for Agent 2

Every item in this section **[OUT OF SCOPE]**. These require a modified LLVM,
a custom runtime/loader, and whole-program compilation; none can or should be
reimplemented in `.hp` headers.

**[OBSERVED] Compiler pass.** `how.html`: Fil-C "transform[s] all unsafe
operations in LLVM IR into code that does dynamic checking ... Most of that is
about transforming all operations involving `ptr` type to use InvisiCaps."
Named pass: `llvm::FilPizlonator` (`fugc.html`, `safepoints.html`).

**[OBSERVED] ABI changes** (`runtime.html`): (1) symbol mangling with
`pizlonated_` prefix; (2) getter indirection returning a Fil-C flight pointer;
(3) demotion of ODR to Any; (4) compiler/loader search a private *pizfix*
header/library slice. "Fil-C has its own ABI ... not compatible with Yolo-C."

**[OBSERVED] Runtime and loader** (`runtime.html`): `libpizlo.so` (libpas +
FUGC + safepoints + memory-safe threading/syscalls/signals + capability slow
paths), `filc_crt.o`, `ld-fil1-x86_64.so`/`ld-fil1-aarch64.so`, `libyoloc.so`,
and a "libc sandwich" with two copies of musl/glibc (Yolo-C and Fil-C).
Symbols with `filc_` prefix are imported by every Fil-C-compiled module.

**[OBSERVED] Safepoints** (`safepoints.html`): pollchecks inserted at every
backward CFG edge (`testb $0xe,0x8(%rbx)` fast path), Pizderson frames
(`struct filc_frame { parent; origin; void* lowers[]; }`) mirroring live
pointers, soft handshakes, `filc_enter`/`filc_exit` around native calls, GC
state bits, Dijkstra store barrier, weak load barrier, thread-local cache
resets, deferred-signal delivery at pollchecks, and stop-the-world for
`fork(2)`/`FUGC_STW=1`.

**[OBSERVED] Whole-program requirement** (`runtime.html`): "There is no
interoperability with Yolo-C"; "Your whole program must be compiled with
Fil-C. Your programs dependencies must be compiled with Fil-C as well." C++
support requires modified `libc++`, `libc++abi`, and a Fil-C `libunwind`.

**[INFERENCE]** Do not propose to Agent 2 (or any stdlib-only change): pointer
tagging in the ABI, per-load bounds checks, GC, store barriers, safepoint
polls, custom panics, or loader/ABI work. The correct response to these is
"belongs to the compiler runtime", exactly as `error.hp` already delegates
fatal safety violations to `cplus_fatal`.

---

## 6. Ideas that can inform C+ stdlib contracts

These are *concept transfers only* (API shape and guarantee discipline), not
code ports. Audit forward references: `docs/stdlib/agent2_audit/module_audits/error.hp.md`
and `docs/stdlib/agent2_audit/module_audits/vector.hp.md` (not yet present at
study time). Verified source: `stdlib/error.hp`, `stdlib/vector.hp`.

| Fil-C concept (observed) | Existing C+ analogue (verified) | Contract idea / gap |
|---|---|---|
| `zlength`, `zgetlower`, `zgetupper`, `zvalinbounds` | `stdlib_alloc_size`, `stdlib_alloc_elem_size`, `stdlib_alloc_length`, `stdlib_alloc_query_length` (`error.hp` 415-492) | **Already convergent.** Promote "size/length query for any live allocation" from incidental helper to a documented contract; specify behaviour for unknown/freed pointers (ours returns 0/-1; Fil-C queries trap or report invalid capability). Audit path: `.../error.hp.md`. |
| `zhasvalidcap` = "has capability and it is not free"; `FILC_OBJECT_FLAG_FREE` | `stdlib_alloc_generation`, `stdlib_alloc_check_generation`, `STDLIB_META_TOMB`, `stdlib_meta_forget` (`error.hp` 253-254, 348-362, 450-473) | **Strongest transfer.** Treat generation as a *liveness token*: define a documented "check-generation-before-use" protocol and record which APIs bump it. Note the gap: ours cannot invalidate arbitrary copied raw pointers; it only detects staleness through a queried generation. Audit path: `.../error.hp.md`. |
| `vector::stale()`/`generation_ref`, bumped on reallocation/shorten (`vector.hp` 9-19, 46-54, 290-295) | Fil-C free-bit + free-singleton repointing (§3) | **Generation counters in `vector::slice`.** Already implemented; the contract to tighten is the explicit caveat that `slice` dereferences the owning vector, so a slice outliving the vector is an undetectable lifetime error. Consider documenting `valid()` as "owner alive AND generation matches", and keeping `stale()` side-effect free. Audit path: `.../vector.hp.md`. |
| `zcheck`, `zcheck_readonly`, `zsafety_errorf`, `%P` dump | `at_checked` on `vector` and `slice` (`vector.hp` 56-67, 164-175); `stdlib_note` + `STDLIB_CATEGORY_BOUNDS/LIFECYCLE` (`error.hp` 43-53, 192-222) | **Checked-access-first API.** Prefer `_checked` variants that return -1 + note a category; keep a separate non-fatal diagnostic path. Ensure lifecycle failures (`CPLUS_EXIT_USE_AFTER_FREE`, `CPLUS_EXIT_DOUBLE_FREE` → `STDLIB_CATEGORY_LIFECYCLE`, `error.hp` 602-603) are never conflated with range/bounds. Audit paths: both. |
| `zgc_completed_cycle()` as "safe time to sweep weak refs" | `stdlib_alloc_seq()` / `stdlib_alloc_fail_at` test hooks (`error.hp` 226-251) | **Epoch observation.** A read-only epoch/counter is a clean, cheap contract for tests and diagnostics; consider documenting that `stdlib_alloc_generation` is an epoch, not a timestamp. Audit path: `.../error.hp.md`. |
| `zptrtable` auto-purges freed pointers; weak exact tables drop unreachable entries | `stdlib_meta_tab` with `STDLIB_META_TOMB` and `meta_live` (`error.hp` 102-122, 265-362) | **Handle-table discipline.** Already tombstone-based; document that querying a forgotten pointer returns 0/-1 and that ownership transfer (`stdlib_transfer_ownership`, 494-515) bumps generation. Audit path: `.../error.hp.md`. |
| `zchecked_add`, `zchecked_mul` (raw `stdfil.h`) | `stdlib_calloc` overflow guard (`error.hp` 394-412); `vector::reserve` guard (`vector.hp` 111-133) | **Already convergent.** Keep "overflow → recoverable -1/NULL + note", never UB. Audit paths: both. |

**[INFERENCE] Top three reusable concepts (ranked):**

1. **Liveness token exposed as a query** — `zhasvalidcap`/free-bit ↔
   `stdlib_alloc_generation`/`STDLIB_META_TOMB`. Cheap, header-compatible, and
   already partly present; the win is *contract discipline* (who bumps it, when
   to check it, what happens on mismatch), not new machinery.
2. **First-class metadata queries with explicit unknown/freed results** —
   `zlength`/`zvalinbounds` ↔ `stdlib_alloc_length`/`query_length`. Makes
   bounds/liveness a named, testable contract instead of an ad-hoc helper.
3. **Generation-stamped borrowed views with failure-atomic mutation** —
   `vector::slice::stale()` + `vector`'s failure-atomic `-1` paths. Fil-C gets
   this from capabilities; we get an honest weaker version and must state its
   lifetime precondition openly.

---

## 7. Licensing

**[OBSERVED] No root `LICENSE` file** — `https://raw.githubusercontent.com/pizlonator/fil-c/deluge/LICENSE`
returned 404. The GitHub license API names `LLVM-LICENSE.txt` as the detected
license file: `spdx_id: NOASSERTION`, `key: other`, `name: Other`.

**[OBSERVED] Identifier quoted from `LLVM-LICENSE.txt`** (base64 decoded via
GitHub API content field), first substantive line:

> "The LLVM Project is under the Apache License v2.0 with LLVM Exceptions"

The file then contains the full Apache License 2.0 text, an "LLVM Exceptions to
the Apache 2.0 License" section, and a "Legacy LLVM License" (UIUC/NCSA-style
3-clause BSD). This covers LLVM-derived compiler portions.

**[OBSERVED] Identifier quoted from the runtime/API source files actually
fetched** (`fugc.h`, `stdfil.h`) — a per-file BSD-style 2-clause header:

> "Copyright (c) 2024-2025 Epic Games, Inc. All Rights Reserved.
> Copyright (c) 2026 Filip Pizlo. All Rights Reserved."
> ("Copyright (c) 2023-2026 Epic Games, Inc." in `stdfil.h`)
> "Redistribution and use in source and binary forms, with or without
> modification, are permitted provided that the following conditions are met:
> 1. Redistributions of source code must retain the above copyright notice ...
> 2. Redistributions in binary form must reproduce the above copyright notice
> ... THIS SOFTWARE IS PROVIDED BY FILIP PIZLO ``AS IS'' ..."

**[INFERENCE]** This study transfers *ideas and API shapes*, not source text;
no Fil-C code was copied into this repository. If any Fil-C source were ever
vendored, the BSD-style notice above must be preserved verbatim, and the
LLVM-derived compiler pieces would carry the Apache-2.0-with-LLVM-exceptions
terms. Do not modify `third_party/` under this task (per `AGENTS.md`).

---

## 8. What is infeasible on this host/toolchain

**[OBSERVED] Platform mismatch.** Fil-C runtime docs describe an ELF-based
Linux stack (`ld-fil1-x86_64.so`, `ld-fil1-aarch64.so`, glibc 2.40 as
`libc.so.6666`, musl, cosmo/peregrine, pizfix, /opt/fil, Pizlix). `runtime.html`:
"Fil-C relies on ELF. I have also previously demonstrated it working on
Mach-O." The host here is Darwin/macOS, and this repository builds **32-bit
Windows x86 DLLs** for the retail IGI.EXE via `tools/build-win32-dll.sh`
(`AGENTS.md`).

**[OBSERVED] No 32-bit support.** `invisicaps.html`: InvisiCaps "allow for
64-bit pointers on 64-bit systems (and would allow for 32-bit pointers if
Fil-C supported 32-bit systems)." The conditional is explicit: Fil-C does not
currently support 32-bit systems. This repo's x86 Windows ABI target is
therefore outside Fil-C's supported pointer width.

**[OBSERVED] ABI and whole-program requirements are incompatible with DLL
injection.** `runtime.html`: Fil-C has its own ABI, mangled `pizlonated_`
symbols, getter indirection, and "There is no interoperability with Yolo-C";
the whole program and all dependencies must be compiled with Fil-C. An
unmodified retail executable plus an injected DLL cannot satisfy this.

**[OBSERVED] Toolchain components absent here.** No `llvm::FilPizlonator`
pass, no `libpizlo.so`, no `filc_crt.o`, no Fil-C loader, no pizfix slice, and
no rebuilt libc/libc++ exist in this repository. Raw `stdfil.h` itself
`#error`s unless `__PIZLONATOR_WAS_HERE__` is defined by the Fil-C compiler.

**[INFERENCE] Practical conclusion.** We cannot build, run, or validate
Fil-C on this host, and we cannot transplant its checks into `.hp` headers:
they are a compiler+loader+runtime property. The only actionable output of
this study is the contract-level guidance in §6, which stays within the
existing checked allocator / error categories / generation-stamped slice
design. Any claim of "Fil-C-level safety" in this repo would be unsupported.

---

## Failures / caveats (summary for the parent session)

1. Root `LICENSE` fetch: **404**; used GitHub license API + `LLVM-LICENSE.txt`
   instead (identifier quoted in §7).
2. Full recursive GitHub tree: **too large (>5 MB)**; used per-directory API
   queries. No invented paths — every source path cited above appeared in an
   API response or doc page.
3. Cited audit files `docs/stdlib/agent2_audit/module_audits/error.hp.md` and
   `.../vector.hp.md`: **not present at study time** (parallel Agent 2). Cited
   as forward references; conclusions grounded in `stdlib/error.hp` and
   `stdlib/vector.hp` instead.
4. `filc_runtime.c` (619 KB) and `filc_runtime.h` (192 KB) were **not fetched**;
   safepoint/frame details come from the official `safepoints.html` page, and
   object/free-singleton details from raw `fugc.h`.
5. Docs `FILC_VERSION` is stale (684 vs raw 687); raw header treated as
   authoritative.
6. No files outside `docs/stdlib/upstream_study/fil_c.md` were created or
   modified; no git add/commit was performed.

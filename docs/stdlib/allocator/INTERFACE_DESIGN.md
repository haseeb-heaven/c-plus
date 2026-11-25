# Backward-compatible allocator interfaces — Task 5 Phase 2 design

- **Branch / date:** `agent/modern-stdlib`, 2026-11-23.
- **File ownership for this task:** this document only. No `.hp` header, test,
  build file, runtime source, git index entry or commit was created or modified
  by this author. No build or test command was executed for this document.
- **Status:** DESIGN ONLY. Every signature in §3 is **PROPOSED** (new, not
  implemented, not compiled, not tested). Existing APIs are labelled
  **EXISTING** and carry a `path:line` citation that was read before any
  proposal was written.
- **Question answered:** can system, arena, pool, fixed-capacity, custom and
  debug/tracking allocators be added as *purely additive, header-only*
  interfaces — no change to existing signatures, struct layouts, or the C ABI?

Evidence labels used here:

| Label | Meaning |
| --- | --- |
| **EXISTING** | Already in this checkout; cited at `path:line` and read during this task. |
| **PROPOSED** | New surface designed here. Nothing is implemented or verified. |
| **VERIFIED** | A property of the existing architecture supported by a cited source line or recorded artifact. |
| **NOT RUN** | A proposed validation step; this author did not execute it. |

---

## 0. Architecture checked before any API was proposed

This section exists so that no API is invented against an imagined architecture.
All files below were read in full or in the cited ranges before §3 was written.

### 0.1 Existing allocation surface (do not duplicate)

| File | What is already there | Citation |
| --- | --- | --- |
| `runtime/include/cplus/cplus.h` | Stable **C ABI**: `cplus_alloc`, `cplus_zalloc`, `cplus_alloc_array`, `cplus_realloc`, `cplus_release`, `cplus_alloc_bytes`, `cplus_check_live`, `cplus_alloc_count`, `cplus_new`, `cplus_new_array`, `cplus_move`. `cplus_exit_code` enumerators (`CPLUS_EXIT_RESOURCE`, `CPLUS_EXIT_UNSUPPORTED`, `CPLUS_EXIT_OWNERSHIP`, `CPLUS_EXIT_BOUNDS`, …). | `cplus.h:68-80,93-155` |
| `runtime/cplus_runtime.c` | `cplus_alloc` is `malloc(size)` plus bookkeeping — the "system heap" underneath everything. | `cplus_runtime.c:189-192` |
| `stdlib/error.hp` | The checked allocator: `stdlib_alloc`, `stdlib_calloc`, `stdlib_realloc`, `stdlib_release`; side-table queries `stdlib_alloc_size/elem_size/length/generation/check_generation/query_length`; `stdlib_transfer_ownership`. All `static inline`. | `error.hp:364-456,476-588` |
| `stdlib/error.hp` | Failure reporting `stdlib_note(category, code, message)` — allocates nothing, formats nothing, increments telemetry, optional callback with re-entrancy guard. | `error.hp:96-100,192-222` |
| `stdlib/error.hp` | Telemetry: `STDLIB_TM_*` indices, `stdlib_telemetry_{set_enabled,is_enabled,reset,snapshot}`, `stdlib_tm_add`. | `error.hp:69-94,127-161` |
| `stdlib/memory.hp` | Bounds-aware **guarded** byte ops and borrow/own helpers built on the `error.hp` side table: `stdlib_mem_capacity/count/token/check/check_token/check_range/copy/move/set/borrow/transfer/release`. | `memory.hp:62-254` |
| `stdlib/safe/safe_alloc.h`, `libc/install-include/cplus_libc.h` | Independent SafeCRT and hardened-libc allocation tables with their own metadata/generation APIs (Agent 2 / Agent 1 slices, not extendable from a `.hp` header). | `docs/patches/agent1-len-managed-allocators.md:24-37`; `docs/stdlib/abi-preserving-runtime-safety.md:44-53` |
| `stdlib/allocator.hp` | Read-only allocation **diagnostics** (`stdlib_alloc_identity`, `stdlib_alloc_view_*`, `stdlib_alloc_tm_snapshot/delta/counter`) and RAII/explicit failure-injection scopes (`alloc_fail_scope`, `stdlib_alloc_fail_begin/end`). No new allocation model. | `stdlib/allocator.hp:1-51,60-275,279-335` |

**Consequence:** a "system allocator", "metadata query", "generation token",
"ownership transfer", telemetry-delta or failure-scope API already exists.
Re-implementing any of them in a new header would duplicate the surface and
split ownership domains. Note this file landed in the checkout while this
document was being authored by a parallel agent; it is treated as EXISTING
here, and §3.6/§5 were adjusted so the proposals do not duplicate it.

### 0.2 Existing failure-injection hooks (must be coordinated, not replaced)

**EXISTING**, all `static inline`, process-wide, atomic
(`stdlib/error.hp:109-125,226-251`):

```c
void stdlib_alloc_fail_at(long n);     /* n-th following stdlib_alloc fails once */
void stdlib_alloc_fail_after(long n);  /* allocation n+1 and later fail */
void stdlib_alloc_fail_reset(void);    /* clear all injection state */
long stdlib_alloc_seq(void);           /* attempts since last reset */
```

They are consulted only inside `stdlib_alloc` (`error.hp:377-384`); `stdlib_calloc`
inherits them through its `stdlib_alloc` call (`error.hp:408`). The deep-test
method — probe the count with `stdlib_alloc_seq()`, enumerate `fail_at(N)` for
`N = 1..count`, assert `-1` + unchanged state + balanced telemetry + reset retry —
is recorded in `docs/stdlib/DEEP_COLLECTIONS_ALLOC_CONTRACT.md:52-77`.

**Existing wrappers, not new hooks:** `stdlib/allocator.hp:279-335` already adds
`stdlib_alloc_fail_begin(mode, count)`, `stdlib_alloc_fail_end()` and the
`alloc_fail_scope` RAII class (ctor arms the injection, `drop()` resets it).
That file documents them as a **single global, non-nestable, non-re-entrant,
not-thread-safe window** (`stdlib/allocator.hp:35-44,283-305`). Proposals below
must neither duplicate those helpers nor claim nesting/thread safety they do
not have.

### 0.3 Constraints every proposal must satisfy

These are properties of the existing architecture, not preferences:

| # | Constraint | Evidence |
| --- | --- | --- |
| C1 | New `.hp` functions must be `static inline`; no new exported ABI symbol. | `docs/MODERN_STDLIB.md:105-113`; `docs/stdlib/abi-preserving-runtime-safety.md:221-232` |
| C2 | Pointers stay one word. No length/capability/generation word in the pointer or before the block. Metadata is out-of-band. | `stdlib/error.hp:1-23`; `stdlib/memory.hp:3-10`; `docs/stdlib/abi-preserving-runtime-safety.md:55-79,158-165` |
| C3 | No public `sizeof`/alignment/field-offset change; ABI probe is `70/70` on both toolchains and byte-identical. | `docs/stdlib/abi-preserving-runtime-safety.md:156-219` |
| C4 | "No second allocator" is a stated library rule: containers use `cplus_alloc` through `stdlib_alloc`. | `docs/MODERN_STDLIB.md:69-70` |
| C5 | Agent 2 has stopped adding new stdlib surface; anything new must not disturb the recorded 50/50, 84/84, 23/23 results. | `docs/MODERN_STDLIB.md:3-7,296-335` |
| C6 | Classes lower to plain C structs; methods lower to `Class_method(Class *self, ...)`; constructor is `Class_ctor` returning an aggregate by value and **may not return early**; `void drop(void)` is the destructor and makes the type owning; destructors must tolerate a zeroed handle. No inheritance, interfaces, virtual dispatch, access control, or out-of-body methods. | `docs/LANGUAGE.md:150-200`; `docs/MODERN_STDLIB.md:54-68` |
| C7 | Method-call sugar is resolved from simple declarations of one variable name per type in a scope; reuse of a name for two container types lowers to the wrong method. | `docs/stdlib/DEEP_COLLECTIONS_ALLOC_CONTRACT.md:79-84`; `docs/LANGUAGE.md:155-158` |
| C8 | Strict mode rejects variable division with an unprovable divisor and subscripting an unverified pointer parameter; existing workarounds are `stdlib_safe_div` and `strlen`+`memcpy`. | `error.hp:458-474,680-690`; `docs/stdlib/memory_ownership/API_INVENTORY.md:245-250` |
| C9 | No custom allocator plug-in point exists today; it is an explicit `SKIP`, not a passing test. | `tests/stdlib/agent2/abi_safety_alloc_lifecycle.cp:200-206`; `docs/stdlib/abi-preserving-runtime-safety.md:523-543`; `tests/crt/fuzz/F100_ALLOC_LIFE_NOTES.md:62-64` |
| C10 | Failure is a value (`STDLIB_OK`/`STDLIB_EOF`/`STDLIB_FAIL`); fatal safety violations stay `cplus_fatal`'s job. Constructors that fail yield an invalid, readable object. | `docs/MODERN_STDLIB.md:43-49,72-89`; `stdlib/error.hp:39-41` |

### 0.4 Ownership domains that must not be mixed

| Domain | Allocate with | Release with | Side-table query works? |
| --- | --- | --- | --- |
| Checked/managed (existing) | `stdlib_alloc`, `stdlib_calloc`, `stdlib_realloc` | `stdlib_release` / `stdlib_mem_release` | Yes |
| Raw host heap (**proposed system facade**) | `malloc`/`calloc`/`realloc` | `free` | No — and must never be given to `stdlib_release` (`error.hp:22-23`) |
| Arena / pool / fixed (**proposed**) | bump or slot from an owned backing block | `reset()` / `release()` / no-op; backing via `stdlib_release` | Only if explicitly registered (§3.3 tracked pool); off by default |

---

## 1. Feasibility verdicts at a glance

| Allocator family | Purely additive header-only? | Changes existing signature / struct layout / C ABI? | Verdict |
| --- | --- | --- | --- |
| **System** — existing checked allocator | N/A — already exists | No | **NO NEW API REQUIRED.** Document the existing surface. |
| **System** — raw host-heap facade for C interop | Yes | No | **FEASIBLE BUT DISFAVOURED.** New isolated ownership domain; no existing code may adopt it automatically. |
| **Arena** (bump / reset) | Yes | No | **FEASIBLE.** New class type only. |
| **Pool** (fixed-slot free list) | Yes | No | **FEASIBLE.** New class type only; no division in hot paths. |
| **Fixed-capacity** (caller-supplied storage) | Yes | No | **FEASIBLE.** Non-owning; no `drop()`. |
| **Custom interface** (vtable passed per call) | Yes, **for new call sites only** | No for the interface itself; **yes** if retrofitted into existing container structs | **FEASIBLE as new opt-in API; RETROFIT REJECTED.** Resolves C9 without touching existing types. |
| **Debug / tracking** (caller-owned counters) | Yes | No | **FEASIBLE.** No new global state, no new telemetry enum values. |

Common qualifier: **FEASIBLE means "can land as new `static inline` headers
without touching existing files"**, not "tested". Every proposal is **NOT RUN**.

---

## 2. Global design rules for all proposed headers

1. **Proposed file layout** (new files only). Directory `docs/stdlib/allocator/`
   holds this design; headers would go under `stdlib/allocator/`, satisfying
   `docs/STRUCTURE.md:32-39`. The name `stdlib/allocator.hp` is **already taken**
   by the diagnostics/injection-scope header, so the umbrella must not reuse it:
   - `stdlib/allocator/interfaces.hp` — umbrella: shared constants only; may
     include the existing `stdlib/allocator.hp` for callers that want diagnostics.
   - `stdlib/allocator/system.hp`, `arena.hp`, `pool.hp`, `fixed.hp`,
     `custom.hp`, `tracking.hp` — one family each.
2. **Includes:** each header includes at most `"stdlib/error.hp"` (and
   `"stdlib/memory.hp"` where a guarded query is reused). No runtime, compiler
   or vendored dependencies. No new exported C symbol (C1).
3. **Status channel:** reuse `STDLIB_OK` / `STDLIB_FAIL` (`error.hp:39-41`).
   Allocation-shaped functions return `void *` with `NULL` on failure, like
   `stdlib_alloc`. Validating functions return `int` and take an optional
   `error *err`, like `stdlib_mem_*` (`memory.hp:56-61`). No new status namespace.
4. **Failure notes:** on any recoverable failure call
   `stdlib_note(STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE, "...")`. That path
   already bumps `STDLIB_TM_ALLOC_FAIL` and `STDLIB_TM_API_FAIL`
   (`error.hp:199-215`) — so new allocators feed existing telemetry without new
   counters. Invalid handles use `STDLIB_CATEGORY_OWNERSHIP` /
   `CPLUS_EXIT_OWNERSHIP`, matching `stdlib_transfer_ownership`
   (`error.hp:562-572`). Over-limit/overflow uses `STDLIB_CATEGORY_RANGE` /
   `CPLUS_EXIT_BOUNDS`, matching `stdlib_realloc` (`error.hp:426-429`).
5. **No fatal paths.** A bad pointer to a proposed allocator returns `FAIL` and
   notes an error; it never reaches `cplus_release` on an interior address.
6. **Alignment policy (PROPOSED constant):**
   `#define STDLIB_ALLOCATOR_ALIGN ((size_t)16)`. Bump allocators round the
   cursor up with `(off + (A - 1)) & ~(A - 1)` where `A` is this compile-time
   constant — no variable division (C8). Rationale: `cplus_alloc` wraps host
   `malloc` (`cplus_runtime.c:189-192`), which supplies fundamental alignment on
   the supported hosts. **Over-aligned types are out of scope** and must be
   stated in every header that uses the constant. Verifying this constant with
   the ABI probe is a **NOT RUN** validation step (§7).
7. **Overflow policy:** use the subtraction form `n > cap - off` from
   `stdlib_mem_check_range` (`memory.hp:142-151`) so `off + n` cannot wrap.
   Allocation-size products use `__builtin_mul_overflow`, as `stdlib_calloc`
   does (`error.hp:400-405`). Never call variable `/` or `%` on a runtime value
   (C8); where an index is unavoidable, use the existing `stdlib_safe_div`
   **and honour its documented `2^16-1` quotient bound** (`error.hp:458-461`,
   `API_INVENTORY.md:245-250`).
8. **Constructor discipline (C6/C10):** a failed constructor leaves
   `base == NULL`, `cap == 0`, `off == 0`; every method and `drop()` is a no-op
   or returns `NULL`/`FAIL` on such a handle. Constructors contain no `return`.
   Every class that owns a `stdlib_alloc` block defines `drop()`; types over
   borrowed storage deliberately do **not** define `drop()` so the compiler does
   not misclassify them as owning (§3.4).
9. **Variable-name discipline (C7):** each allocator instance gets a unique
   variable name in a scope; callers use `a.method()` sugar or the lowered
   `Class_method(&a, ...)` form.
10. **No ABI-visible metadata** (§4): a proposed allocator may call the
    existing header-internal `stdlib_meta_record` / `stdlib_meta_forget`
    (`error.hp:289-362`) only as an explicitly opt-in, diagnostic-mode feature;
    the default is off.

---

## 3. Per-family designs

### 3.1 System allocator

#### 3.1.1 Reading A — "expose the existing checked system allocator"

**VERDICT: no new API required.** `stdlib_alloc`/`stdlib_calloc`/
`stdlib_realloc`/`stdlib_release` plus `stdlib_mem_capacity` are already the
public, checked, system-heap-backed interface (`error.hp:364-456,581-588`;
`memory.hp:73-75`). A new "system allocator" header would be a third name for
the same functions and would split the documentation, not add capability. The
additive action is a paragraph in this design, not a header.

#### 3.1.2 Reading B — "raw host-heap facade for C-interop boundaries"

**VERDICT: FEASIBLE (purely additive), DISFAVOURED.** The only legitimate use is
handing a pointer to a precompiled C API that will `free()` it, or receiving one
from such an API — a boundary the checked allocator cannot serve because
`stdlib_release` on a raw `malloc` block is an invalid free (`error.hp:22-23`).

**PROPOSED signatures** — `stdlib/allocator/system.hp`:

```c
static inline void *stdlib_system_alloc(size_t bytes);
static inline void *stdlib_system_calloc(size_t count, size_t element_size);
static inline void *stdlib_system_realloc(void *ptr, size_t bytes);
static inline void  stdlib_system_release(void *ptr);
```

- **Semantics:** thin `malloc`/`calloc`/`realloc`/`free` wrappers. `bytes == 0`
  maps to 1 byte (mirrors `error.hp:370-371`). `count * element_size` uses
  `__builtin_mul_overflow` and is refused above `STDLIB_ALLOC_LIMIT`
  (`error.hp:57-67,400-405`). `release(NULL)` is a no-op.
- **Ownership contract:** a block from these functions belongs to the **raw
  domain** forever. Never pass it to `stdlib_release`, `stdlib_mem_release`,
  `stdlib_realloc` (checked), `stdlib_transfer_ownership` or any guarded
  `stdlib_mem_*` operation; conversely never pass a checked block to
  `stdlib_system_release`. No conversion between domains except an explicit
  copy. `stdlib_mem_capacity()` returns `0` for raw blocks by design
  (`memory.hp:21-24`).
- **Failure contract:** `NULL` on refusal or host failure, with the standard
  note (`STDLIB_CATEGORY_MEMORY`, `CPLUS_EXIT_RESOURCE`). No fatal exit.
- **Adoption later:** **none.** `stdlib/async.hp:101-106`,
  `stdlib/thread.hp:95-129`, `stdlib/string.hp:127,221,319`,
  `stdlib/vector.hp:121,246,270` and `stdlib/collections.hp` all pair
  `stdlib_alloc` with `stdlib_release`; switching them would change both the
  allocation call and the release call inside existing functions (C5) and split
  their telemetry. Vendored yyjson/stb already use the host allocator and must
  keep being freed exactly as they allocate (`docs/MODERN_STDLIB.md:69-70`).
  New FFI-only code may opt in.

---

### 3.2 Arena allocator (bump / reset)

**VERDICT: FEASIBLE — a new class type only; no existing signature, layout or
ABI change.**

**PROPOSED signatures** — `stdlib/allocator/arena.hp`:

```c
class arena {
    void  *base;        /* owned: one stdlib_alloc backing block */
    size_t cap;
    size_t off;
    int    failed;      /* last operation's failure reason, for diagnostics */

    arena(size_t capacity);                       /* ctor -> arena_ctor */
    void  *alloc(size_t bytes);
    void  *alloc_zeroed(size_t count, size_t elem_size);
    void   reset(void);                           /* off = 0; no per-block frees */
    int    valid(void);
    size_t capacity(void);
    size_t used(void);
    int    empty(void);
    void   drop(void);                            /* stdlib_release(base) */
};

static inline const char *stdlib_arena_failure_name(const arena *a);
```

- **Backing:** ctor calls `stdlib_alloc(capacity)` (so
  `STDLIB_ALLOC_LIMIT`, failure injection and telemetry all apply once, at
  construction). `capacity == 0` yields the invalid state. `failed` is a plain
  `int` tag; the human-readable name is a borrowed static string, never a
  formatted buffer.
- **Alignment / overflow:** round `off` up to `STDLIB_ALLOCATOR_ALIGN` with the
  constant mask; refuse when `bytes > cap - off - slack` using the subtraction
  form (§2 rules 6–7).
- **Ownership contract:** the arena owns `base`; each returned address is a
  **borrowed sub-range**, valid until `reset()` or `drop()`. Individual blocks
  must **never** be given to `stdlib_release`/`free` — they are interior
  addresses and would trigger a runtime invalid free. `reset()` invalidates all
  outstanding borrows at once; it does not shrink or free `base`. `drop()` is
  a no-op on a zeroed/invalid handle and calls `stdlib_release(base)` exactly
  once otherwise.
- **Failure contract:** `NULL` from `alloc`/`alloc_zeroed` for an invalid arena,
  size overflow, over-limit or exhaustion; note
  `STDLIB_CATEGORY_MEMORY`/`CPLUS_EXIT_RESOURCE` so `STDLIB_TM_ALLOC_FAIL`
  increments. No fatal exit. `reset`/`capacity`/`used`/`empty`/`valid` cannot
  fail.
- **Metadata:** off by default. An arena sub-allocation is not registered in
  the side table, so `stdlib_mem_capacity()` reports `0` for it (document this
  as intended, like a stack pointer, `memory.hp:21-24`).
- **Adoption later:** only **new** types. `stdlib/collections.hp:835,851`
  (`list_node`) and the scratch buffers in `stdlib/string.hp:221,319` are the
  natural future users, but adopting an arena requires adding an `arena` member
  to those existing structs and rewriting their allocation sites — a struct
  layout and signature change, explicitly out of scope (C5). No existing code
  can adopt this automatically.

---

### 3.3 Pool allocator (fixed-size slots, free list)

**VERDICT: FEASIBLE — new class type only.**

**PROPOSED signatures** — `stdlib/allocator/pool.hp`:

```c
class pool {
    void  *base;         /* owned backing block: slot_bytes * slot_count */
    void **slots;        /* owned directory of slot base addresses */
    size_t slot_bytes;   /* rounded up to STDLIB_ALLOCATOR_ALIGN */
    size_t slot_count;
    size_t total;
    void  *free_head;    /* intrusive `void *next` stored in a free slot */
    size_t live;
    int    failed;

    pool(size_t slot_size, size_t slot_count);   /* -> pool_ctor */
    void  *alloc(void);
    int    release(void *p);                     /* STDLIB_OK / STDLIB_FAIL */
    int    owns(const void *p);
    size_t slot_size(void);
    size_t live_count(void);
    int    valid(void);
    void   drop(void);                           /* releases slots[] then base */
};

/* Opt-in diagnostic variant: same shape, plus per-slot side-table records. */
class pool_tracked { /* same members and methods as pool; ctor calls
                      * stdlib_meta_record on each slot and stdlib_meta_forget
                      * on release/drop. Diagnostic only. */ };
```

- **Why a directory, no division:** membership is an exact-pointer linear scan
  of `slots[]`, and the free list stores a `void *next` in each free slot. No
  address-to-index conversion, so no variable `/` or `%` (C8) and no dependence
  on `stdlib_safe_div`'s `2^16-1` quotient bound. Cost: `owns`/`release` are
  `O(slot_count)`; state that in the header comment. An index-based variant
  would be viable only with a documented `slot_count <= 65535` cap.
- **Backing:** ctor makes two `stdlib_alloc` calls — `base` and `slots` — so the
  existing injection hooks see allocation points 1 and 2 (§5). Product overflow
  is checked with `__builtin_mul_overflow`.
- **Ownership contract:** the pool owns both blocks; a handed-out slot is
  borrowed until `release(p)` returns it. A slot is never `free`d or
  `stdlib_release`d. `drop()` frees both blocks once; any slots still live
  become dangling — callers must release all first (leak, not corruption, if
  they do not). `owns()` never transfers.
- **Failure contract:** `alloc` returns `NULL` with the standard memory note
  when the free list is empty, when uninitialised or on product overflow.
  `release(p)` returns `STDLIB_FAIL` with `STDLIB_CATEGORY_OWNERSHIP` /
  `CPLUS_EXIT_OWNERSHIP` for a foreign address or a double release (detected by
  scanning the free list); it never terminates. All failure paths leave the
  free list unchanged.
- **`pool_tracked` caveat:** registering each slot via `stdlib_meta_record`
  makes `stdlib_mem_capacity(slot)` work, but `stdlib_release(slot)` is still
  fatal (interior address). Growth of the side table can fail silently
  (`error.hp:307-322`), so the diagnostic is best-effort. Off by default to
  avoid changing existing telemetry/generation behaviour.
- **Adoption later:** **new types only**, for the same reason as arena —
  `list_node` in `stdlib/collections.hp:835-878` is the obvious candidate but
  cannot be switched without editing that header (C5).

---

### 3.4 Fixed-capacity allocator (caller-supplied storage)

**VERDICT: FEASIBLE — smallest surface of the six; no allocation, no
destructor.**

**PROPOSED signatures** — `stdlib/allocator/fixed.hp`:

```c
class fixed {
    void  *base;   /* borrowed: storage supplied by the caller */
    size_t cap;
    size_t off;
    int    failed;

    fixed(void *storage, size_t bytes);            /* -> fixed_ctor */
    void  *alloc(size_t n);
    void  *alloc_zeroed(size_t count, size_t elem_size);
    void   reset(void);
    int    valid(void);
    size_t capacity(void);
    size_t used(void);
    /* Deliberately NO drop(): base is borrowed, not owned. */
};

/* Optional, compiler-validation-gated typed form (NOT RUN): */
#define STDLIB_FIXED_TYPE(Name, Element, Count) /* generates a struct with
   unsigned char data[(Count) * sizeof(Element)] and Element *Name##_alloc(Name *) */
```

- **Ownership contract:** caller owns `storage` (stack, static, or a checked
  block). The `fixed` value only borrows it. There is intentionally **no**
  `drop()`: C6 makes any class with `drop()` an owning type with an inserted
  cleanup, which would be wrong here. `reset()` rewinds the cursor; the caller
  frees the storage when appropriate.
- **Failure contract:** `NULL` on a null storage pointer, zero capacity,
  alignment slack overrun, product overflow, or exhaustion; standard
  `STDLIB_CATEGORY_MEMORY`/`CPLUS_EXIT_RESOURCE` note. No fatal exit.
- **Injection hooks:** none apply — there is no `stdlib_alloc` call. Tests must
  exhaust the buffer deterministically rather than call
  `stdlib_alloc_fail_at` (§5).
- **Typed macro:** macro-generated classes are expected to work because the
  driver preprocesses headers before lowering (`docs/MODERN_STDLIB.md:191-203`),
  but this is **NOT RUN**. Until validated, use the `void *storage` form above.
- **Adoption later:** the `error` object's inline `char text[STDLIB_ERROR_SIZE]`
  (`error.hp:600`) is the existing example of this pattern; no existing
  allocator call site can adopt `fixed` without edits, and none should.

---

### 3.5 Custom allocator interface (per-call vtable)

**VERDICT: FEASIBLE as a new opt-in interface; RETROFIT INTO EXISTING TYPES
REJECTED.** This is the additive answer to the recorded `SKIP abi_custom_alloc`
(C9). It does **not** make existing containers custom-allocatable — that would
require a vtable pointer or template parameter in their structs (layout change,
C3) or a changed constructor signature (C5).

**PROPOSED signatures** — `stdlib/allocator/custom.hp`:

```c
typedef struct stdlib_allocator_vtable {
    void  *(*alloc)(void *ctx, size_t bytes);              /* required */
    void  *(*alloc_zeroed)(void *ctx, size_t count,
                           size_t elem_size);             /* required */
    void  *(*resize)(void *ctx, void *ptr, size_t bytes);  /* required */
    void   (*release)(void *ctx, void *ptr);              /* required */
    size_t (*capacity)(void *ctx, const void *ptr);       /* optional; NULL = unknown */
} stdlib_allocator_vtable;

static inline int    stdlib_allocator_valid(const stdlib_allocator_vtable *vt);
static inline void  *stdlib_alloc_with(const stdlib_allocator_vtable *vt,
                                       void *ctx, size_t bytes);
static inline void  *stdlib_alloc_zeroed_with(const stdlib_allocator_vtable *vt,
                                              void *ctx, size_t count,
                                              size_t elem_size);
static inline void  *stdlib_realloc_with(const stdlib_allocator_vtable *vt,
                                         void *ctx, void *ptr, size_t bytes);
static inline void   stdlib_release_with(const stdlib_allocator_vtable *vt,
                                         void *ctx, void *ptr);
static inline size_t stdlib_capacity_with(const stdlib_allocator_vtable *vt,
                                          void *ctx, const void *ptr);
/* Adapter that restores checked-allocator semantics (and all its hooks): */
static inline const stdlib_allocator_vtable *stdlib_checked_vtable(void);
```

- **Shape:** a plain C struct of function pointers plus a caller-supplied
  `ctx`. No templates, overloading, virtual dispatch or inheritance — all
  unsupported by C+ (C6). No global state: vt and context are passed per call.
  `stdlib_checked_vtable()` returns a pointer to a function-local `static
  const` table whose functions delegate to `stdlib_alloc`, `stdlib_calloc`,
  `stdlib_realloc` and `stdlib_release`; because it is `static inline`, it adds
  no exported symbol and keeps the existing injection/telemetry behaviour.
- **Ownership contract:** blocks returned by a vtable belong to that vtable
  instance and must be released through the **same** `(vt, ctx)` pair. Mixing
  vtables, or releasing a vtable block with `stdlib_release`/`free`, is a caller
  error. `capacity` is diagnostic only (`0` = unknown, mirroring
  `stdlib_alloc_size`, `error.hp:477-486`).
- **Failure contract:** `stdlib_allocator_valid` returns `STDLIB_OK` only when
  the four required pointers are non-NULL; `NULL` vt is invalid. Allocation
  functions return `NULL` and note `STDLIB_CATEGORY_MEMORY`/
  `CPLUS_EXIT_RESOURCE` on invalid vt or vtable refusal. `resize` failure must
  leave the original block untouched, matching `stdlib_realloc`
  (`error.hp:420-421`). `stdlib_realloc_with` on a vt with no `resize` returns
  `NULL` with `CPLUS_EXIT_UNSUPPORTED` — never a silent cross-allocator
  fallback. `capacity_with` returns `0` when the optional callback is absent.
- **Hooks:** delegate to the vtable, not to global injection state. Tests use a
  deliberately failing test vtable, or `stdlib_checked_vtable()` to exercise
  the existing hooks (§5).
- **Adoption later:** **new code only.** `stdlib/async.hp:101`,
  `stdlib/thread.hp:95`, `stdlib/string.hp:127`, `stdlib/vector.hp:121` and
  `stdlib/collections.hp` call `stdlib_alloc` directly; changing them to
  `stdlib_alloc_with` changes existing function bodies and would need new
  allocator state stored in their structs — both prohibited here (C5, C3).
  The existing `SKIP` fixtures may be extended with a *new* test that builds a
  vtable, but their expected output must not be changed to claim the SKIP is
  gone.

---

### 3.6 Debug / tracking allocator (caller-owned counters)

**VERDICT: FEASIBLE — purely additive, no new global state.**

**PROPOSED signatures** — `stdlib/allocator/tracking.hp`:

```c
typedef struct stdlib_allocator_tracking {
    unsigned long long allocs;
    unsigned long long frees;
    unsigned long long bytes;        /* current outstanding bytes */
    unsigned long long peak_bytes;
    unsigned long long failed;       /* failures seen through this wrapper */
    size_t             live;         /* current outstanding blocks */
} stdlib_allocator_tracking;

static inline void  stdlib_tracking_init(stdlib_allocator_tracking *t);
static inline void *stdlib_tracking_alloc(stdlib_allocator_tracking *t, size_t bytes);
static inline void *stdlib_tracking_calloc(stdlib_allocator_tracking *t,
                                           size_t count, size_t elem_size);
static inline void *stdlib_tracking_realloc(stdlib_allocator_tracking *t,
                                            void *ptr, size_t bytes);
static inline void  stdlib_tracking_release(stdlib_allocator_tracking *t, void *ptr);
static inline void  stdlib_tracking_snapshot(const stdlib_allocator_tracking *t,
                                             stdlib_allocator_tracking *out);
static inline int   stdlib_tracking_balanced(const stdlib_allocator_tracking *t);
```

- **Delegation:** every call wraps the corresponding checked allocator, so
  metadata registration, double-free detection, injection hooks and global
  telemetry are unchanged. `realloc` reads the old size with
  `stdlib_alloc_size(ptr)` (`error.hp:477-486`) and the new size after success,
  leaving counters untouched on failure (`error.hp:420-421`).
- **Counters:** updated **only on success**. `failed` increments on `NULL`.
  `init(NULL)` and `snapshot(NULL, ...)` are no-ops, following
  `stdlib_telemetry_snapshot` (`error.hp:148-150`). `balanced()` is
  `live == 0 && bytes == 0`; it is a **leak indicator for this wrapper's
  call sites**, not a process-wide memory-safety proof.
- **Ownership contract:** the caller owns the `stdlib_allocator_tracking`
  struct; blocks are checked-allocator blocks and are released through
  `stdlib_tracking_release(t, p)` (or, by deliberate bypass, `stdlib_release` —
  but then the wrapper's counters drift). No new global counter, no new
  `STDLIB_TM_*` index (adding one would change `STDLIB_TM_COUNT` and the
  `stdlib_state_t` layout; §4).
- **Failure contract:** identical to the checked allocator it wraps; the
  wrapper adds no new failure mode and no fatal path.
- **Non-duplication:** `stdlib/allocator.hp:229-275` already provides
  `stdlib_alloc_tm_snapshot`, `stdlib_alloc_tm_delta` and
  `stdlib_alloc_tm_counter` for the **process-wide** counters. This proposal
  must not reimplement them; `stdlib_tracking_snapshot` is deliberately a
  **per-subsystem** accumulator over the call sites that opt into the wrapper.
- **Adoption later:** new tests/tools only. The deep tests already assert
  `alloc_count == free_count` from `stdlib_telemetry_snapshot`
  (`DEEP_COLLECTIONS_ALLOC_CONTRACT.md:71-77`), so this adds a **per-subsystem**
  view, not a replacement. No existing header may switch to it (C5).

---

## 4. Explicitly rejected ideas

Each rejection is about preserving C1–C10; none is a judgment about the upstream
technique in general.

| Idea | Verdict | Why (evidence) |
| --- | --- | --- |
| **Fat pointers** — replace `T *` with `{base, size, token}` | **REJECT** | Thin-pointer invariant is measured and byte-identical across toolchains; no pointer widening or tagging is permitted (`docs/stdlib/abi-preserving-runtime-safety.md:55-79,158-165`); `stdlib/memory.hp:3-10` explicitly says pointers are not redefined. Would change every existing signature. |
| **Capabilities / tagged high bits / shadow-stack calling convention** | **REJECT** | Compiler/loader work; would need whole-program instrumentation and change the C ABI (`docs/stdlib/abi-preserving-runtime-safety.md:674-688`, SoftBoundCETS/Fil-C rows). |
| **ABI-visible metadata** — inline header before each block, or length/generation fields in public structs | **REJECT** | Changes `sizeof`/offsets asserted by `tests/abi/abi_layouts.c` (70/70; §2.2) and breaks the free protocol: a pointer returned to C must be the pointer `free`/`cplus_release` expects. Existing metadata is out-of-band for exactly this reason (`error.hp:1-23`). |
| **New exported runtime C allocator symbols** from a `.hp` header | **REJECT** | C1/C5; `.hp` functions are `static inline`; runtime changes are Agent 1's area. |
| **Global interposition / weak-symbol override of `stdlib_alloc` or `stdlib_release`** | **REJECT** | Silently changes behaviour of every existing container, bypasses `stdlib_alloc_fail_*` and telemetry, violates "no second allocator" (`docs/MODERN_STDLIB.md:69-70`) and the recorded fixtures (C5). |
| **Macro-redefining `stdlib_alloc`/`free` in a public header** | **REJECT** | Same behavioural hijack, invisible at call sites; breaks `error.hp:12-23` contract. |
| **Adding fields to `stdlib_state_t`, `stdlib_telemetry_stats`, `error`, or `stdlib_allocator_*` public structs after landing** | **REJECT** | Struct-layout change (C3). `stdlib_state` is a weak definition shared per program (`error.hp:124-125`); a size change desynchronises separately compiled consumers. |
| **Vtable/allocator field in an existing container struct** | **REJECT** | Changes `sizeof(vector)`/`string`/`intmap` etc. and their lowered C layout (C3/C6); method sugar and constructor signatures change too (C7). |
| **Templates, overloading, `operator new`, virtual dispatch, interfaces** | **REJECT** | Unsupported by C+ (`docs/LANGUAGE.md:199-200`; `docs/stdlib/abi-preserving-runtime-safety.md:701-711`). |
| **Storing `__FILE__`/`__LINE__` inside each allocation** | **REJECT** | An inline metadata header (above) and it changes the free protocol; an optional side-table tag is fine, a prefix is not. |
| **Per-call `stdlib_alloc_fail_*`-style global injection knobs for each new allocator** | **REJECT** | Duplicates process-wide state and cross-test interference; use per-instance state / a failing vtable / deterministic exhaustion instead (§5). |
| **Promoting the existing `SKIP abi_custom_alloc` to PASS without a real plug-in point** | **REJECT** | Would be a false claim; the SKIP is recorded evidence of absence (`abi_safety_alloc_lifecycle.cp:200-206`). |

---

## 5. Coordination with the existing `stdlib_alloc_fail_*` hooks

The four hooks are **EXISTING** and unchanged (`error.hp:226-251`). Proposed
behaviour, to be stated verbatim in each proposed header:

| Allocator | Hooks apply? | Exact interaction |
| --- | --- | --- |
| Checked (`stdlib_alloc`) | Yes — already | `stdlib_alloc:377-384`. |
| System facade (raw `malloc`) | **No** | Deliberately outside the checked domain; a test expecting injection here is wrong. Use the checked allocator instead. |
| Arena | **Once**, at ctor | Backing `stdlib_alloc(capacity)` is allocation point 1; per-`alloc` bump is not hook-driven. `stdlib_alloc_seq()` counts the ctor only. |
| Pool | **Twice**, at ctor | `base` then `slots` are points 1 and 2; per-slot `alloc` exhaustion is a free-list state, not an injected failure. |
| Fixed | **Never** | No `stdlib_alloc` call; exhaust the buffer deterministically. |
| Custom interface | Only via the vtable | A failing test vtable is the injection mechanism; `stdlib_checked_vtable()` delegates to `stdlib_alloc` and therefore honours all four hooks. |
| Tracking | Yes, normally | Wraps checked calls; `failed` also increments on `NULL`. |

**Test recipe (reuse, do not reinvent):** for any constructor that calls
`stdlib_alloc`, reset with `stdlib_alloc_fail_reset()`, probe the successful
allocation count with `stdlib_alloc_seq()`, then for `N = 1..count` rebuild the
object and call `stdlib_alloc_fail_at(N)` just before construction. Assert
invalid-but-readable state, `-1`/`NULL`, `alloc_failures >= 1`, balanced telemetry
after cleanup, and success after reset — exactly the method in
`DEEP_COLLECTIONS_ALLOC_CONTRACT.md:52-77`. Never exhaust real system memory.
Where a test already uses `alloc_fail_scope`, keep using it (its `drop()`
guarantees reset) but do not claim nesting or thread safety — the underlying
window is one global state (`stdlib/allocator.hp:35-44,283-305`). Existing hook
callers (`deep_string_task4`, `deep_vector_task4`,
`agent2/abi_safety_injection_mem.cp`; `API_INVENTORY.md:252-259`) must remain
unchanged.

---

## 6. Adoption map — who could use these later

"No automatic adoption" is the headline. Every row below requires a source edit
in a file this task does not own, and none changes an existing signature or
struct layout.

| Existing code | Candidate | What an adoption would require | Allowed now? |
| --- | --- | --- | --- |
| `stdlib/async.hp:101-106` (`uv_timer_t`) | custom interface / system facade | Change ctor to accept a vtable and pair it with a matching release | **No** — signature change |
| `stdlib/thread.hp:95-129` (`pthread_cond_t`) | custom interface | Same as above | **No** — signature change |
| `stdlib/string.hp:127,221,319,402` | arena / pool / custom | Add allocator state to `string` + rewrite every `stdlib_alloc`/`stdlib_release` | **No** — layout change |
| `stdlib/vector.hp:121,246,270` | arena / custom | Same | **No** — layout change |
| `stdlib/collections.hp:99-1075` (`intmap`, `strmap`, `intset`, `deque`, `list`, `stack`, `queue`) | pool for `list_node` (`:835-878`), arena for rebuild buffers | New members + rewritten growth/rollback paths; must preserve the failure-atomic counts pinned by `DEEP_COLLECTIONS_ALLOC_CONTRACT.md:31-45` | **No** — layout and behaviour change |
| `stdlib/json.hp`, `stdlib/image.hp` (vendored yyjson/stb) | system facade | They already allocate/free with the host allocator; must not be forced through `stdlib_release` (`docs/MODERN_STDLIB.md:69-70`) | **No** — not needed |
| New `.cp` code, new tests, new library types | any of the six | Include the new header, pick one ownership domain, obey §3 contracts | **Yes**, once implemented and validated (§7) |

---

## 7. Validation required before any proposed header lands — **NOT RUN**

This author ran no build, test or ABI probe. The following must be executed by
the owning agent before any of §3 is called feasible in practice:

1. **Strict + default + UBSan builds** using the recorded deep-test pattern,
   e.g. `./build/c+ build <new-test>.cp --strict --sanitize=none` and the
   `--sanitize=undefined` variant (`DEEP_COLLECTIONS_ALLOC_CONTRACT.md:165-189`).
   Strict mode is the real gate for the division/subscript workarounds (C8).
2. **Multi-TU link check** — two TUs including each new header must link and run
   (`docs/MODERN_STDLIB.md:115-121`), proving every function is `static inline`.
3. **ABI probes unchanged** — `sh tests/abi/run_abi_layouts.sh` (expect 70/70,
   byte-identical `host.out`/`cplus.out`) and `sh tests/abi/run_abi_compat.sh`
   (expect the recorded `69 passed, 0 failed`);
   `docs/stdlib/abi-preserving-runtime-safety.md:198-219,476-497`.
4. **Injection/atomicity matrix** from §5 for arena and pool constructors, plus
   a failing-vtable test for the custom interface and a balanced-counters test
   for tracking.
5. **Separation fixtures** — assert that an arena/pool/fixed sub-allocation is
   refused by `stdlib_mem_capacity()` (returns `0`) and that a raw-system block
   is never passed to `stdlib_release`.
6. **Alignment probe** for `STDLIB_ALLOCATOR_ALIGN` before any over-aligned use;
   over-aligned types stay out of scope until then.
7. **Regression baseline** — the recorded 50/50 aggregate, 84/84 deep and
   23/23 evidence suites must remain green (`docs/MODERN_STDLIB.md:307-313`).
8. **No fixture lying** — the `SKIP abi_custom_alloc` lines stay as they are
   until a real plug-in point is wired into a new call site; §3.5 does not by
   itself remove the SKIP.

---

## 8. Evidence inventory

Files read for this document:

- `stdlib/error.hp:1-695` — checked allocator, side table, telemetry, `error`,
  `stdlib_alloc_fail_*` hooks.
- `stdlib/memory.hp:1-254` — guarded ops, ownership rules, no-fat-pointer contract.
- `stdlib/allocator.hp:1-335` — existing read-only diagnostics, telemetry helpers
  and injection scopes (parallel agent's header, present at final read).
- `runtime/include/cplus/cplus.h:1-398` — C ABI and `cplus_exit_code`;
  `runtime/cplus_runtime.c:189-192` — `cplus_alloc` → `malloc`.
- `docs/MODERN_STDLIB.md:1-335` — no-second-allocator rule, no-new-surface rule,
  ownership states, multi-TU and recorded suite results.
- `docs/stdlib/abi-preserving-runtime-safety.md:1-776` — metadata stores, ABI
  evidence, non-goals, custom-allocator SKIP.
- `docs/stdlib/memory_ownership/API_INVENTORY.md:1-533` — function-by-function
  inventory of the existing allocator/memory surface and global rules G-*.
- `docs/stdlib/DEEP_COLLECTIONS_ALLOC_CONTRACT.md:1-219` — injection method and
  variable-name/method-dispatch constraint.
- `docs/LANGUAGE.md:150-209` — class lowering, constructors, destructors,
  unsupported features; `docs/STRUCTURE.md:1-43` — header placement rules.
- `docs/patches/agent1-len-managed-allocators.md:1-50` — existing SafeCRT/libc
  metadata APIs (do not duplicate).
- `tests/stdlib/agent2/abi_safety_alloc_lifecycle.cp:195-210` — explicit
  custom-allocator SKIP; `tests/crt/fuzz/F100_ALLOC_LIFE_NOTES.md:62-64`.

No file other than this document was created or modified. No git index entry was
changed. No test, build or ABI command was executed, and no live-game,
Windows-x86 or injected-DLL success is claimed.

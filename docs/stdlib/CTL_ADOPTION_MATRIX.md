# CTL component adoption matrix (issue #24)

**Scope.** Component-by-component decision for the vendored [rurban/ctl](https://github.com/rurban/ctl)
headers under `third_party/ctl/`, measured against the existing C+ container and
allocator APIs in `stdlib/{vector,string,collections,alloc_iface,arena,mempool,datastack}.hp`.
This document is an adoption analysis only: it does **not** change, remove, or
replace any C+ container, and it does **not** change the compiler.

Decisions are one of:

- **ADOPT** — safe to use now as-is *behind a thin C+ checked adapter*, with no
  edit to the vendored header and no removal of a C+ container.
- **DEFER** — potentially useful, but blocked on concrete adapter/contract work
  listed in the row; not adopted now.
- **REJECT** — do not use as a C+ container implementation in its current form;
  revisiting requires a new issue with a substantive rewrite.

## 1. Pin, license, provenance (recorded exactly)

From `third_party/ctl/PROVENANCE.md` (lines 1–16) and
`third_party/ctl/UPSTREAM_COMMIT`:

| Item | Exact recorded value | Source |
| --- | --- | --- |
| Upstream | `https://github.com/rurban/ctl` | `third_party/ctl/PROVENANCE.md:3` |
| Commit (pin) | `ede5f455392f2380dd16aead26835eece6cd20e4` | `third_party/ctl/PROVENANCE.md:4`; `third_party/ctl/UPSTREAM_COMMIT:1` |
| License | MIT (see LICENSE; Gustav Louw 2020-2021, Reini Urban 2020-2021) | `third_party/ctl/PROVENANCE.md:5`; `third_party/ctl/LICENSE:1-4` |
| Vendored by | Agent 2 (PR #17, agent/modern-stdlib) | `third_party/ctl/PROVENANCE.md:6` |
| Scope | `ctl/` headers copied verbatim (header-only typed C containers: vector, string, deque, list, map, set, ...). LICENSE/README/SECURITY preserved. | `third_party/ctl/PROVENANCE.md:7-8` |
| 2026-11-22 | vendored upstream headers unchanged; added UPSTREAM_COMMIT | `third_party/ctl/PROVENANCE.md:12` |
| Consumer requirement | include `<string.h>/<stdlib.h>` and, for POD element types, `#define POD` before the container include (upstream requirement) | `third_party/ctl/PROVENANCE.md:13-14` |
| Local hardening | vector: empty underflow guard in `pop_back`, size-multiplication overflow check in `fit`, `realloc` failure preservation so `self->vector` is not lost on OOM in `fit`/`push_back`/`emplace_back` | `third_party/ctl/PROVENANCE.md:15` |
| Local hardening | string: NULL pointer guards in `str_init`, `str_append`, `str_find`, `str_count`, `str_rfind`, plus integer-overflow and OOM post-resize verification in `str_append` | `third_party/ctl/PROVENANCE.md:16` |

MIT grant text and warranty disclaimer: `third_party/ctl/LICENSE:6-22`.

## 2. Safety boundary (must be read with the matrix)

**CTL is plain C with raw `realloc`, `calloc` and `malloc`; it has no built-in
memory safety.** Ownership is a convention implemented by function pointers in
each container struct (e.g. `ctl/vector.h:26-35`: `free`, `copy`, `compare`,
`equal`), POD mode is selected by a preprocessor macro
(`ctl/vector.h:190-196`), and allocation failure is frequently ignored or
silently returned as `void` (§3.2). Memory from CTL never passes through
`stdlib_allocate` / `stdlib_reallocate` / `stdlib_deallocate`
(`stdlib/alloc_iface.hp:30-37,40-107`), so failure injection, generation
metadata and the checked-heap policy of C+ do not reach it.

Therefore **C+ contracts/adapters are required for every non-trivial CTL use**:
status-returning wrappers (`int` result + `error*`), checked allocator
injection, explicit owner/borrowed typing, generation-based view invalidation
and external synchronisation. No claim is made that CTL containers are
memory-safe, failure-atomic, bounds-safe or thread-safe.

## 3. C+ container API inventory (what an adapter must match)

| C+ header | API surface (exact file:line) | Contract to preserve |
| --- | --- | --- |
| `stdlib/vector.hp` | helper `vector_allocation_bytes` `:48`, `vector_calculate_growth` `:72`; class `slice` `:88` (`size` `:101`, `stale` `:106`, `valid` `:111`, `at_checked` `:116`, `sub` `:130`); class `vector` `:147` (`size` `:170`, `capacity` `:174`, `reserve` `:180`, `push_back` `:206`, `at` `:231`, `at_checked` `:241`, `pop_back` `:254`, `insert` `:266`, `erase` `:294`, `clear` `:307`, `copy_from` `:314`, `shrink_to_fit` `:345`, `view` `:375`, `drop` `:382`); `vector_in(allocator)` `:395` | Failure-atomic mutations; overflow checks against `STDLIB_ALLOC_LIMIT`; generation-stamped borrowed `slice` (`stdlib/vector.hp:1-19`) |
| `stdlib/string.hp` | `stdlib_utf8_validate` `:31`, `stdlib_string_calculate_growth` `:102`; class `string_view` `:120`; class `string` `:156` (`valid` `:201`, `reserve` `:266`, `append` `:309`, `copy_from` `:369`, `substr` `:402`, `field` `:431`, `to_view` `:456`, `drop` `:463`); `string_in(allocator,text)` `:477` | Owned string vs borrowed view; failure-atomic operations; `valid()` distinguishes "empty" from "failed"; strict UTF-8 and documented NULL policy (`stdlib/string.hp:1-17,27-31`) |
| `stdlib/collections.hp` | helpers `:37-116`; `intmap` `:120`, `strmap` `:307`, `intset` `:530` (`put/get/contains/remove/clear/drop`), `deque` `:680`, `list` `:817` (`push_front/push_back/pop_front/pop_back/at_checked`), `stack` `:935` (`push/pop/top_checked`), `queue` `:1004` (`push/pop/front_checked`) | Explicit ownership, failure atomicity, overflow-checked sizing, no exposed iterators, not thread-safe (`stdlib/collections.hp:1-25`) |
| `stdlib/alloc_iface.hp` | `stdlib_allocator` vtable (alloc/realloc/release/reset) `:30-37`; dispatch `stdlib_allocate` `:40`, `stdlib_reallocate` `:65`, `stdlib_deallocate` `:88`, `stdlib_allocator_reset` `:95`, `stdlib_heap_allocator` `:100` | `realloc` gets `old_size`; allocation failure reported via NULL + `error*`, never abort (`stdlib/alloc_iface.hp:1-15`) |
| `stdlib/arena.hp` | `stdlib_arena_init` `:100`, `stdlib_arena_from_buffer` `:123`, `stdlib_arena_alloc` `:179`, `stdlib_arena_realloc` `:216`, `stdlib_arena_release` `:260`, mark/handle validation `:302-306`, allocator adapter `:314-333` | O(1) generation-invalidating reset; stale handles fail closed via `error*` (`stdlib/arena.hp:1-24`) |
| `stdlib/mempool.hp` | `stdlib_mem_pool_init` `:61`, `add_slab` `:81`, `alloc` `:146`, `release` `:166`, `destroy` `:210`, `stdlib_mem_pool_as_allocator` `:260`; slot refs `:355-411`; scope guard `:422` | O(1) slot allocation with double-free/foreign-free/stale-pointer refusal in safe builds (`stdlib/mempool.hp:1-22`) |
| `stdlib/datastack.hp` | `stdlib_datastack_init` `:140`, `destroy` `:159`, `ensure` `:176`, `stdlib_thread_datastack` `:183`, scratch enter/leave `:194-204`, `alloc` `:212`, `realloc` `:239`, `release` `:276`, scope `:309`, allocator `:333` | Thread-local bump stack; one-store scope exit; top-of-stack `realloc` grows in place (`stdlib/datastack.hp:1-12,25-37`) |

## 4. Vendored CTL audit (claims with exact file:line)

Paths below are relative to `third_party/ctl/`.

### 4.1 Ownership, copy, destructor conventions

- Every container struct carries optional `free`/`copy`/`compare`/`equal`
  function pointers; the vector layout is `ctl/vector.h:26-35`. POD mode
  installs `implicit_copy` and default methods (`ctl/vector.h:190-196`,
  `ctl/bits/container.h:53-56,88-89`); non-POD mode binds `JOIN(T, free)` and
  `JOIN(T, copy)` (`ctl/vector.h:194-195`).
- Mutation goes through the callbacks: `set` frees the replaced element before
  overwrite (`ctl/vector.h:211-218`), `erase_index` frees before `memmove`
  (`ctl/vector.h:522-540`), `erase_range` frees each element and memmoves the
  tail (`ctl/vector.h:543-576`), `clear` is a series of `pop_back`/`wipe`
  (`ctl/vector.h:271-288`).
- `free` releases the buffer and reinitialises the container
  (`ctl/vector.h:290-299`); `copy` builds a new container with the same
  callbacks and copies element-by-element (`ctl/vector.h:724-731`).
- Hash containers own flat entry arrays: `hashmap` allocates/copies entries and
  frees them in `free` (`ctl/hashmap.h:401-418`); `swisstable` does the same
  (`ctl/swisstable.h:287-299`). Node containers own one allocation per node:
  list (`ctl/list.h:256-262`), forward_list (`ctl/forward_list.h:44-50`),
  set (`ctl/set.h:311-318`), unordered_set (`ctl/unordered_set.h:414-423,936`).
- There is **no** allocator injection: all storage comes from raw
  `malloc`/`calloc`/`realloc` (`ctl/vector.h:245,263`; `ctl/deque.h:71,74,257,280`;
  `ctl/list.h:258`; `ctl/set.h:313`; `ctl/hashmap.h:152,404`;
  `ctl/swisstable.h:123,290`; `ctl/unordered_set.h:539,546`), never from
  `stdlib_allocator` (`stdlib/alloc_iface.hp:30-37`).

### 4.2 realloc and failure behavior

- The locally hardened vector `fit` checks `max_size` and `SIZE_MAX / sizeof(T)`,
  uses a temporary for `realloc`/`calloc` and returns without publishing a lost
  pointer on OOM (`ctl/vector.h:229-269`). Corresponding hardening is recorded
  in `third_party/ctl/PROVENANCE.md:15`.
- Nevertheless vector mutation returns `void` and silently no-ops on OOM:
  `push_back` returns when the buffer is missing/full (`ctl/vector.h:346-357`,
  especially `:350-351`); `emplace_back` does the same (`:359-367`);
  `reserve`/`fit` return no status (`:301-344`, `:229-269`).
- Deque `alloc` assigns `realloc` directly to `self->pages` with no NULL check
  and immediately writes the shifted page pointers (`ctl/deque.h:277-290`), and
  bucket creation dereferences an unchecked `malloc` result
  (`ctl/deque.h:255-260`).
- Unordered_set `_reserve` assigns `realloc` directly to `self->buckets`, calls
  `memset` on it, and only *asserts* non-NULL afterwards
  (`ctl/unordered_set.h:536-553`); with `ASSERT` compiled out (§4.5) OOM leaves
  a lost table pointer and a NULL dereference.
- List/forward_list/set node constructors dereference unchecked `malloc`
  (`ctl/list.h:256-262`, `ctl/forward_list.h:44-50`, `ctl/set.h:311-318`).
- Hash map/swisstable `_resize` do at least NULL-check `calloc`
  (`ctl/hashmap.h:150-154`, `ctl/swisstable.h:121-125`), but commit the new
  table to `self` **before** rehashing and can return `false` after mutation
  (`ctl/hashmap.h:155-175`, `ctl/swisstable.h:125-136`). Their `copy` silently
  skips the `memcpy` when `malloc` fails, and `assign` frees the old table
  before copying — not failure-atomic (`ctl/hashmap.h:401-413`,
  `ctl/swisstable.h:287-298`).
- String: `str_append` guards NULL and `SIZE_MAX` overflow and checks the
  post-resize size (`ctl/string.h:86-99`, hardening in
  `third_party/ctl/PROVENANCE.md:16`), but on failure it simply returns after a
  possibly successful `str_resize`, leaving padding bytes instead of the
  appended text; `str_insert_str` and `str_replace` have no equivalent guards
  (`ctl/string.h:101-122`).
- `bvector` uses `assert(p)` on the result of `realloc` and then writes through
  it (`ctl/bvector.h:15`).

### 4.3 Bounds and overflow

- Vector `at` uses `ASSERT(index < self->size ...)` and otherwise returns NULL
  (`ctl/vector.h:53-57`); `front` is explicitly "not bounds-checked"
  (`ctl/vector.h:59-62`), as is `set` (`ctl/vector.h:211-212`). `erase_index`
  computes `self->size - 1` without an empty check (`ctl/vector.h:522-541`).
- `max_size` is a fixed `4294967296 / sizeof(T)` cap even on 64-bit
  (`ctl/bits/container.h:37-40`); this is a different policy from C+
  `STDLIB_ALLOC_LIMIT` (`stdlib/vector.hp:48-57`).
- String `str_rfind` indexes from `self->size` via `strstr(&c_str[i], ...)`
  (`ctl/string.h:151-162`), relying on the vector's implicit NUL terminator
  (`ctl/vector.h:235-259`, `:354-356`).
- `span::at` uses raw `assert(index < self->size)` and a ternary fallback
  (`ctl/span.h:38-42`); `bvector::at` likewise (`ctl/bvector.h:14`).
- `array` compile-time checks bound `N` to `<= 4294967296/8`
  (`ctl/array.h:5-9`) but its heap path `calloc(N, sizeof(T))` is unchecked
  (`ctl/array.h:194-202`).

### 4.4 Iterator invalidation and aliasing

- Vector iterators hold a raw `T *ref` into the buffer
  (`ctl/vector.h:172-184`, fields via `CTL_T_ITER_FIELDS` at `:41-44`). `fit`
  may `realloc` that buffer (`ctl/vector.h:243-248`), and `insert_index`/
  `erase_index` memmove without re-pointing other live iterators
  (`ctl/vector.h:508-541`). There is no generation/stale-token concept; contrast
  C+ `slice` (`stdlib/vector.hp:9-19,106-113,375-379`).
- Node containers keep element addresses stable until that node is erased:
  chained unordered_set states this explicitly (`ctl/unordered_set.h:3-6`);
  list/forward_list/set allocate nodes (`ctl/list.h:256-262`,
  `ctl/forward_list.h:44-50`, `ctl/set.h:311-318`). C+ deliberately exposes no
  iterators at all (`stdlib/collections.hp:20-22`).
- `priority_queue`, `queue` and `stack` are macro aliases over `vector.h` /
  `deque.h` (`ctl/priority_queue.h:9-38`, `ctl/queue.h:8-32`,
  `ctl/stack.h:8-37`), so they inherit the underlying invalidation and OOM
  behavior verbatim.
- `hashmap::copy`/`swisstable::copy` shallow-copy the struct before replacing
  the entry pointer (`ctl/hashmap.h:401-407`, `ctl/swisstable.h:287-292`), so
  an OOM leaves two owners of the same buffer.

### 4.5 Error semantics and thread-safety

- Most mutation APIs return `void`; allocation failure is a silent no-op
  (`ctl/vector.h:229-269,346-367`; `ctl/deque.h:277-290`;
  `ctl/hashmap.h:401-413`; `ctl/string.h:86-99`). There is no `error*`
  equivalent to `stdlib/alloc_iface.hp:40-85`.
- Assertion macros are conditional: `ASSERT(x)` is `assert(x)` only when
  `_ASSERT_H` is defined and `NDEBUG` is not; otherwise it expands to nothing
  (`ctl/ctl.h:31-39`). Comparator/equality preconditions rely on these macros
  (`ctl/ctl.h:42-46`; `ctl/priority_queue.h:55-57`).
- There is **no synchronisation anywhere** in the vendored headers; a source
  search for `thread|mutex|pthread|lock|atomic` finds only a comment in deque —
  "It should be possible to do it thread-safe. Not yet."
  (`ctl/deque.h:5`) — and an unrelated hash-seeding note
  (`ctl/unordered_set.h:25`). C+ likewise requires external synchronisation
  (`stdlib/collections.hp:23-24`); an adapter must not relax that contract.

## 5. Component-by-component adoption matrix

| CTL component (header) | Decision | Rationale (evidence) | C+ API replaced / complemented | Required C+ adapter / contract work |
| --- | --- | --- | --- | --- |
| `vector` (`ctl/vector.h`) | **DEFER** | Only locally hardened component (`PROVENANCE.md:15`; `ctl/vector.h:223-247`), but OOM is a silent `void` no-op (`ctl/vector.h:346-367`), bounds `ASSERT` is release-disabled (`ctl/vector.h:53-57`; `ctl/ctl.h:31-39`), raw `realloc` bypasses the checked allocator, and iterators dangle across `fit` (`ctl/vector.h:172-184,243-248`). | Complements (reference/benchmark) C+ `vector` (`stdlib/vector.hp:147-391`); not a replacement — C+ is failure-atomic with generation-stamped `slice` (`:1-19,106-113`). | Inject `stdlib_allocator` for alloc/realloc/release; translate all `void` mutators into `int` status + `error*`; map `max_size` (`ctl/bits/container.h:37-40`) to `STDLIB_ALLOC_LIMIT`; reissue borrowed views with generation tokens. |
| `string` (`ctl/string.h`) | **REJECT** | CTL_STR specialization of vector (`ctl/string.h:15-34`) with implicit NUL (`ctl/vector.h:354-356`), silent post-resize partial append (`:86-99`) and unguarded insert/replace (`:101-122`); `str_rfind` depends on NUL termination (`:151-162`). | Rejected as replacement for C+ `string` (`stdlib/string.hp:156-477`); may complement only as upstream algorithm reference. | Would require a full re-engineering (checked allocator, error* status, strict UTF-8 per `stdlib/string.hp:27-31`, NULL policy `:14-16`) — effectively a new implementation, not an adoption. |
| `deque` (`ctl/deque.h`) | **DEFER** | Unchecked page `malloc` and `realloc` that loses `self->pages` on OOM (`:255-260,277-290`); explicitly not thread-safe (`:5`). | Complements C+ ring-buffer `deque` (`stdlib/collections.hp:680-816`) with `push_front`/iterator access; not a replacement. | Checked page allocation, overflow guard on `capacity * sizeof(B*)`, status-returning alloc, external lock contract. |
| `list` (`ctl/list.h`) | **DEFER** | Unchecked node `malloc` (`:256-262`); stable addresses only until erase; callback-based ownership (`:250-254`). | Complements C+ `list` (`stdlib/collections.hp:817-934`), which exposes no iterators (`:20-22`). | Checked node allocator, `free`/`copy` callbacks bound to C+ lifetime hooks, status returns, iterator lifetime contract. |
| `forward_list` (`ctl/forward_list.h`) | **DEFER** | No C+ equivalent, but unchecked node `malloc` (`:44-50`) and O(n) `tail` traversal (`:52-60`). | Complements C+ `list`/`deque`; new capability, not a replacement. | Same checked-node/status adapter as `list`, plus a documented cost model for tail operations. |
| `priority_queue` (`ctl/priority_queue.h`) | **DEFER** | Macro alias over `vector.h` (`:9-38`), so inherits vector OOM behavior; comparator only precondition-asserted (`:55-57`, `ctl/ctl.h:31-39`). | Complements C+ `stack` (`stdlib/collections.hp:935-1003`) with O(log n) heap operations. | Validated comparator, checked engine, status-returning push/pop, no exposed invalidatable iterators. |
| `queue` (`ctl/queue.h`) | **REJECT** | Thin macro rename of `deque.h` (`:8-32`), inheriting the unchecked page realloc (`ctl/deque.h:277-290`) with no added safety. | Replaced by C+ `queue` (`stdlib/collections.hp:1004-1083`, failure-atomic per `:12-17`). | Revisit only if the deque engine is first rewritten with checked allocation and status returns. |
| `stack` (`ctl/stack.h`) | **REJECT** | Thin macro rename of `deque.h` (`:8-37`) with the same defects. | Replaced by C+ `stack` (`stdlib/collections.hp:935-1003`). | Same precondition as `queue`; no independent adoption path. |
| `set` (`ctl/set.h`) | **DEFER** | Red-black tree complements int-only C+ set, but node `malloc` unchecked (`:311-318`) and comparator precondition is a release-disabled assert (`ctl/ctl.h:42-46`). | Complements `intset` (`stdlib/collections.hp:530-679`) with generic keys and ordered traversal. | Checked node allocation, comparator validation via `error*`, status returns, stable-iterator policy. |
| `map` (`ctl/map.h`) | **DEFER** | Macro wrapper over `set.h` (`:1-19`), inheriting its defects; key+value pair ownership needs explicit callbacks. | Complements `intmap`/`strmap` (`stdlib/collections.hp:120-306,307-529`) with ordering. | Same as `set`, plus pair `free`/`copy` contract and failure-atomic `insert_or_assign`. |
| `unordered_set` (`ctl/unordered_set.h`) | **REJECT** | `_reserve` assigns `realloc` straight to `self->buckets`, memsets past it, and only asserts non-NULL (`:536-553`); with `NDEBUG` this loses the table and dereferences NULL (`ctl/ctl.h:31-39`). | Replaced by C+ `intset` (`stdlib/collections.hp:530-679`): open addressing, tombstones, never-full table, probing terminates (`:16-19,67-71`). | Would require rewriting growth/allocation to checked allocator + failure atomicity — a fork, not an adoption. |
| `unordered_map` (`ctl/unordered_map.h`) | **REJECT** | Derived from unordered_set by macros (`:1-19`) so inherits its OOM defect; own TODO notes missing pair-free handling (`:5-6`). | Replaced by `intmap`/`strmap` (`stdlib/collections.hp:120-529`). | Same blocker as `unordered_set`, plus key/value pair ownership contract. |
| `hashmap` (`ctl/hashmap.h`) | **DEFER** | Useful auto-selected hash policy (`:1-24`), but `_resize` commits before rehash and can return `false` after mutating (`:150-175`), and `copy`/`assign` are not failure-atomic or double-owner-safe (`:401-413`). | Complements `strmap` (`stdlib/collections.hp:307-529`) with open addressing + auto hash selection. | Rework `_resize` to commit-last with `stdlib_allocator`, status-returning insert/rehash, safe `copy`/`assign`, seeded hash policy review. |
| `swisstable` (`ctl/swisstable.h`) | **DEFER** | Same open-addressing defects as hashmap (`:121-137,287-298`); DoS knobs are documented (`:4-23`) and need policy review. | Complements `strmap`/`intmap` with Swiss-style probing and configurable security knobs. | Same adapter work as `hashmap`, plus an explicit audit of `CTL_USET_SECURITY_COLLCOUNTING` defaults before any untrusted-key use. |
| `array` (`ctl/array.h`) | **DEFER** | Compile-time `N` capped by `#error` (`:5-9`), but heap path uses unchecked `calloc(N, ...)` (`:194-202`) and stack/heap cutoff is implicit (`:14`). | Complements C+ (no fixed-capacity container today); closest C+ analogue is `slice` (`stdlib/vector.hp:88-145`) as a borrowed view. | Checked heap path, explicit heap/stack policy, `at_checked` status wrapper; avoid for untrusted/oversized `N`. |
| `span` (`ctl/span.h`) | **ADOPT** | Non-owning `{T *data; size_t size}` view with no allocation and no destructor (`:1-25`); borrow-only semantics match C+ views. Caveat: raw `assert` in `at` is release-disabled (`:38-42`, `ctl/ctl.h:31-39`). | Complements `slice` (`stdlib/vector.hp:88-145`) and `string_view` (`stdlib/string.hp:120-155`); replaces nothing. | Thin C+ wrapper with checked `at_checked`-style access and a generation/lifetime token; **no edit** to `ctl/span.h`; never treat it as owning. |
| `algorithms` (`ctl/algorithm.h`) | **DEFER** | Generic range algorithms over CTL iterators (`:32-45`), include-once-only (`:1-8`); no checked status and no stable C+ surface. | Complements C+ by-value access (`stdlib/collections.hp:20-22`); replaces nothing. | Define a small checked C+ algorithm facade (status-returning, no raw iterator escape), map CTL iterators to borrowed views, and fix include-once integration per translation unit. |

Out of scope for issue #24 (present in `third_party/ctl/ctl/` but not in the
matrix): `bvector.h`, `hive.h`, `btree_set.h`, `flat_{set,map,multiset,multimap}.h`,
`inplace_vector.h`, `strv.h`, `u8str.h`, `numeric.h`, `generic.h`. They carry the
same raw-allocation/assert caveats (e.g. `ctl/bvector.h:14-15`) and would need
their own audit before any decision.

## 6. Verification (exact commands and observed results)

Environment note: the default `clang` on `PATH` is Homebrew clang 23, which in
this session defaults to a missing `MacOSX27.sdk` (`clang: warning: no such sysroot
directory: '/Library/Developer/CommandLineTools/SDKs/MacOSX27.sdk'`, then
`fatal error: 'assert.h' file not found`). The documented suite result is
reproduced with the Apple toolchain compiler selected through the script's own
`CPLUS_CC` override (`tests/ctl/run_ctl_tests.sh:5`); `/usr/bin/gcc` works too.

### 6.1 CTL suite

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
CPLUS_CC=/usr/bin/clang sh tests/ctl/run_ctl_tests.sh
```

Result (exit 0):

```
ok test_ctl_deque.c
ok test_ctl_forward_list.c
ok test_ctl_list.c
ok test_ctl_map.c
ok test_ctl_priority_queue.c
ok test_ctl_queue.c
ok test_ctl_set.c
ok test_ctl_stack.c
ok test_ctl_string.c
ok test_ctl_unordered_set.c
ok test_ctl_vector.c
ctl tests: 11 passed, 0 failed
```

### 6.2 C+ include proof (`-Ithird_party/ctl`, prints `CTL 2 9`)

No repository source file is needed — feed the translation unit on stdin:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
/usr/bin/clang -O2 -Wall -Wextra -std=c11 -Ithird_party/ctl -x c - \
  -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/ctl_include_proof <<'EOF'
#include <stdio.h>
#define POD
#define T int
#include <ctl/vector.h>
int main(void) {
    vec_int v = vec_int_init();
    vec_int_push_back(&v, 2);
    vec_int_push_back(&v, 9);
    printf("CTL %d %d\n", *vec_int_at(&v, 0), *vec_int_at(&v, 1));
    vec_int_free(&v);
    return 0;
}
EOF
/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/ctl_include_proof
```

Result (warning-free, exit 0):

```
CTL 2 9
```

The same source compiled with `/usr/bin/gcc -O2 -Wall -Wextra -std=c11
-Ithird_party/ctl -x c -` also prints `CTL 2 9`. Note `vec_int_at` returns
`T *`, so the proof dereferences it; the local `#define POD` / `#define T int`
sequence follows `third_party/ctl/PROVENANCE.md:13-14`.

## 7. Explicit non-claims

- **No existing C+ container is removed or replaced by this document.**
  `stdlib/vector.hp`, `string.hp`, `collections.hp` and the allocator headers
  remain the supported APIs; the matrix only proposes future adapter work.
- **No compiler/frontend file is changed.** This analysis touches only this
  documentation file; the vendored CTL headers stay pinned and unchanged apart
  from the local hardening already recorded in `PROVENANCE.md:15-16`.
- **No blanket CTL safety claim.** CTL is plain C with raw `realloc`/`calloc`/
  `malloc`, no allocator injection, no `error*` status, no failure-atomicity,
  release-disabled assertions, and no thread safety (§4). Every use needs a C+
  contract/adapter as specified in the matrix.
- The 11/11 suite and the `CTL 2 9` prove only header compilability, basic
  functionality and C+ include compatibility — **not** live-game, production,
  or full-project correctness. Full gates must be run separately.

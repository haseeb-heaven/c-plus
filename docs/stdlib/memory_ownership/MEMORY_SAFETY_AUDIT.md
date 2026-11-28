# Task 2 Phase 2 — Implementation-level memory-safety audit and fixes

Branch: `agent/modern-stdlib`. Audit date: 2026-11-23.
Toolchain: `/usr/bin/clang` (Apple clang 16.0.0), `build/c+` (in-tree C+ compiler),
macOS/arm64. No sanitizer for the primary runs; UBSan runs are listed separately.

## 0. Scope and strict file ownership

This agent was only permitted to create/modify:

| File | Change made |
| --- | --- |
| `stdlib/memory.hp` | Documentation only (UAF detection limits). No functional or ABI change. |
| `libc/src/stdlib/alloc.c` | **No change** — see §4 for why no reproduced defect was fixable under the minimal/ABI-preserving constraint. |
| `docs/stdlib/memory_ownership/MEMORY_SAFETY_AUDIT.md` | This document. |

No test files, headers, build scripts, compiler or runtime sources were touched.
All investigation probes were written **outside the repository** under the
approved temp root
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/agent2baseline/`.

Sources inspected: `libc/src/stdlib/alloc.c` (hardened libc allocator +
metadata side table), `stdlib/memory.hp` (additive bounds-aware wrappers),
`stdlib/error.hp` (stdlib-side meta table used by `memory.hp`),
`libc/install-include/cplus_libc.h` (C ABI surface), `runtime/cplus_runtime.c`
(the managed allocator behind `stdlib_alloc`), plus the four tests named in the task.

### Enforcement layers (do not conflate)

There are **two independent metadata tables** and one managed allocator:

1. **`g_tab` in `alloc.c`** — records `cplus_malloc/calloc/realloc` blocks
   (`byte_size`, `elem_size`, `generation`). All `cplus_alloc_*` queries read it.
2. **`stdlib_state.meta_tab` in `error.hp`** — records `stdlib_alloc/calloc/realloc`
   blocks. All `stdlib_mem_*` wrappers in `memory.hp` read **this** table via
   `stdlib_alloc_size` / `stdlib_alloc_check_generation`.
3. **`cplus_live` in `runtime/cplus_runtime.c`** — the managed allocator `stdlib_alloc`
   delegates to; it has its own live/recent tables and its own `cplus_realloc`
   (which allocates a fresh block rather than resizing in place).

A defect demonstrated in layer 1 with `cplus_free` is not automatically a defect
in the `memory.hp` wrappers; this distinction is kept explicit below.

---

## 1. Verification baseline (before any change)

Exact commands, run from the repository root. `TMPBASE` is the temp dir above.

```sh
TMPBASE=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/agent2baseline

# 1. test_alloc_metadata.c  (plain clang, no sanitizer)
/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  tests/libc/test_alloc_metadata.c build/libc/libcplus_hardened.a \
  -lm -pthread -o "$TMPBASE/test_alloc_metadata"
"$TMPBASE/test_alloc_metadata"
# -> test_alloc_metadata: ok                         (exit 0)

# 2. test_alloc_fault_matrix.c  (plain clang, no sanitizer)
/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
  -Ilibc -Ilibc/include -Ilibc/install-include -I. \
  tests/libc/test_alloc_fault_matrix.c \
  build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
  -lm -pthread -o "$TMPBASE/test_alloc_fault_matrix"
"$TMPBASE/test_alloc_fault_matrix"
# -> alloc_fault_matrix: 294/294 checks ok           (exit 0)

# 3. test_stdlib_realloc.cp  (strict, no sanitizer)
build/c+ build tests/libc/test_stdlib_realloc.cp --strict --sanitize=none \
  -o "$TMPBASE/test_stdlib_realloc"
"$TMPBASE/test_stdlib_realloc"
# -> stdlib_realloc: 12/12 checks ok                 (exit 0)

# 4. deep_memory_bounds.cp  (strict, no sanitizer)
build/c+ build tests/stdlib/deep_memory_bounds.cp --strict --sanitize=none \
  -o "$TMPBASE/deep_memory_bounds"
"$TMPBASE/deep_memory_bounds" | tail -1
# -> MEMORY_BOUNDS 61 0                              (exit 0)
```

**Baseline result: all four tests pass** (0 compile warnings surfaced for the
C tests with `-Wall -Wextra -pedantic`).

### UBSan / strict variants (additional)

```sh
# alloc.c compiled directly with UBSan, linked with the test sources
/usr/bin/clang -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined \
  -Wall -std=c99 -Ilibc -Ilibc/include -Ilibc/install-include \
  tests/libc/test_alloc_fault_matrix.c libc/src/stdlib/alloc.c libc/log/log.c \
  build/safe/libcplus_safecrt.a -lm -pthread -o "$TMPBASE/ubsan_fault"
"$TMPBASE/ubsan_fault"
# -> alloc_fault_matrix: 294/294 checks ok  (no UBSan diagnostic)

/usr/bin/clang -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined \
  -Wall -std=c99 -Ilibc -Ilibc/include -Ilibc/install-include \
  tests/libc/test_alloc_metadata.c libc/src/stdlib/alloc.c libc/log/log.c \
  libc/src/string/extra2.c libc/src/string/strndup.c -lm -pthread \
  -o "$TMPBASE/ubsan_meta"
"$TMPBASE/ubsan_meta"
# -> test_alloc_metadata: ok  (no UBSan diagnostic)

build/c+ build tests/libc/test_stdlib_realloc.cp --strict --sanitize=undefined \
  -o "$TMPBASE/realloc_ubsan" && "$TMPBASE/realloc_ubsan" | tail -1
# -> stdlib_realloc: 12/12 checks ok

build/c+ build tests/stdlib/deep_memory_bounds.cp --strict --sanitize=undefined \
  -o "$TMPBASE/deep_ubsan" && "$TMPBASE/deep_ubsan" | tail -1
# -> MEMORY_BOUNDS 61 0
```

`alloc.c` also compiles clean standalone:

```sh
/usr/bin/clang -O2 -Wall -Wextra -Wpedantic -std=c99 \
  -Ilibc -Ilibc/include -Ilibc/install-include \
  -c libc/src/stdlib/alloc.c -o "$TMPBASE/alloc.o"    # -> compile OK
```

---

## 2. Concern-by-concern findings

Legend: **PASS** = exercised by test/probe with no defect; **LIMIT** = behavior is
intentional and documented; **REPRO** = defect reproduced by probe; **LATENT** =
theoretical / requires caller contract violation, not reachable in normal use.

| Concern | Verdict | Evidence (source / test / probe) |
| --- | --- | --- |
| Invalid free | **PASS** | `cplus_free_checked` (`alloc.c:356-387`) calls host `free` only when `meta_find` succeeds. Probe: `cplus_free_checked` on `(char*)a+4` and on a raw `malloc(32)` pointer were both refused, `invalid_free_count` +1, owner still live, raw payload intact. `deep_memory_bounds.cp:n_dst_null/n_src_null/n_set_null/u_stack_refused` cover the `memory.hp` side. |
| Double free | **PASS** (normal) / **REPRO** (saturated) | Normal: `test_alloc_metadata.c:74-76` and probe (double free refused, `double_free_count` +1). Saturated cache misclassification: §4.2. |
| UAF detection limits | **REPRO** at libc layer / **LIMIT** in `memory.hp` | Stale-alias free reproduced in §4.1. `memory.hp` never claimed arbitrary UAF detection; its header comment under-specified *which* token is checked, now corrected (§3). |
| Allocation-size overflow | **PASS** | `cplus_calloc` guard `nmemb!=0 && size>SIZE_MAX/nmemb` (`alloc.c:262-265`). Probe: `cplus_calloc(SIZE_MAX,2)` and `(2,SIZE_MAX)` both return NULL with `errno==ENOMEM`; `test_alloc_fault_matrix` and `test_alloc_metadata.c:92-93` agree. `stdlib_calloc` uses `__builtin_mul_overflow` (`error.hp:400`). |
| Incorrect size calculations | **PASS** | `meta_insert` normalizes `elem_size==0` to 1 (`alloc.c:160-161`); `get_meta`/`length`/`query_length` use `elem_size?elem_size:1` (`alloc.c:445,496,515`); `stdlib_safe_div` (`error.hp:462`) avoids variable division. `test_alloc_metadata.c:27,31,87-89` and `deep_memory_bounds.cp:q_count_*` pass; UBSan reports no signed/unsigned issues. |
| realloc failure metadata consistency | **PASS** | `alloc.c:294-354`: injection/`meta_reserve_one` failure and host `realloc` NULL all return before any metadata mutation. Probe: with `fail_after(0)`, `cplus_realloc(r,4096)` returns NULL/`ENOMEM`, owner still live, size still 24, **generation unchanged**. `test_alloc_metadata.c:51-55`, fault matrix `realloc injected`, `test_stdlib_realloc.cp:overflow_refused/metadata_preserved_on_refusal` agree. |
| Zero-size allocations | **PASS** | `cplus_malloc(0)`, `cplus_calloc(0,0)`, `cplus_realloc(p,0)` all normalize to one tracked byte (`alloc.c:236,266-268,302`; `error.hp:370,406,423`). Probe: `size()==1`, `elem_size()==1`; `deep_memory_bounds.cp:z_alloc_zero_*` and fault-matrix zero-size rows pass. |
| Null pointers | **PASS** | `cplus_free(NULL)` no-op (`alloc.c:392`); `cplus_free_checked(NULL)` returns EINVAL (`alloc.c:360-363`); `cplus_alloc_query_length` zeroes out param and rejects NULL (`alloc.c:503-511`); `memory.hp` NULL guards at `:104-109`, `:88-92`, `:204-208`, `:221`. `deep_memory_bounds.cp:n_*` and `i_off_eq_cap_zero` pass. |
| Interior pointers | **PASS** (fail-closed) | `meta_find` is exact-base (`alloc.c:105-123`), so interior pointers report non-live / 0 size; `cplus_alloc_remaining_capacity` (`alloc.c:402-428`) recognizes interior ranges. Probe: interior free refused. `deep_memory_bounds.cp:i_*` (base+offset) and `u_stack_untracked` pass. |
| Stale metadata | **LIMIT / LATENT** | `meta_insert` has an existing-slot recovery path for host-freed-and-reused addresses (`alloc.c:162-168`). Metadata left behind by a caller's raw `free()` of a tracked pointer (F-02) is a documented contract violation, not reachable through the C ABI alone. |
| Leaks on early return | **PASS** | `cplus_malloc`/`cplus_calloc` free the just-allocated block when `meta_reserve_one` fails (`alloc.c:247-251,283-287`); realloc early returns leave the caller-owned block untouched by design. Probe ends at `live=0 bytes=0` (baseline) after all sequences. |
| Ownership transfer | **PASS** (normal) / **REPRO** (stale alias) | `cplus_transfer_ownership` validates handles and bumps generation (`alloc.c:540-558`); `stdlib_mem_transfer` (`memory.hp:203-215`). Probe: transfer nulls `*src`, bumps generation, old token refused. Stale-alias transfer inherits the §4.1 limitation. |

---

## 3. Change made

### 3.1 `stdlib/memory.hp` — documentation only

The header previously said a stale token "is refused before the guarded
operation touches memory" and listed raw pointers / uninstrumented libraries /
interior pointers as unprotected, but did **not** state the two facts the task
asks to make explicit: (a) detection requires a token captured before the
lifetime boundary, and (b) only the *destination* token is validated.

Added to the top-of-file comment:

* an explicit **"UAF detection limits (IMPORTANT: not arbitrary use-after-free
  detection)"** section stating the three preconditions for detection, that a
  token of 0 opts out, that only `dst_token` is checked, that a recycled source
  address is a logical-aliasing (not an OOB) error, that an untracked freed
  pointer is reported as `STDLIB_CATEGORY_INVALID` (not UAF), and that a freed
  address recycled at the same base is indistinguishable without a stale token;
* a one-line note on `stdlib_mem_copy` restating that only `dst_token` is checked.

No function signature, no code path, and no C ABI changed — `memory.hp` contains
only `static inline` helpers.

---

## 4. Reproduced defects (with before/after evidence)

Both reproduced items are **pre-existing documented limits** (already listed as
F-01 and F-06 in `docs/libc/AUDIT_alloc.md`). They are recorded here with fresh
runtime evidence. Neither was changed: see the "Why not fixed" note on each.

### 4.1 R1 — generation-blind free/realloc/transfer through a stale alias (F-01)

**Probe** (`$TMPBASE/probe_stale.c`, compiled against
`build/libc/libcplus_hardened.a`):

```sh
/usr/bin/clang -O1 -g -Wall -std=c99 -Ilibc/install-include \
  "$TMPBASE/probe_stale.c" build/libc/libcplus_hardened.a \
  -lm -pthread -o "$TMPBASE/probe_stale"
"$TMPBASE/probe_stale"
```

Before/after evidence (addresses/generations are from the actual run):

```
A=0x1004b58d0 gen=1                       # original block, generation 1
after free A: alias=0x1004b58d0 is_live(alias)=0
B=0x1004b58d0 gen=2 same_addr=1           # host recycled the address at gen 2
free via stale alias: rc=0 B still live=0 # <-- DEFECT: B (the NEW owner) was freed
```

`cplus_free_checked` (`alloc.c:369-383`) calls `meta_find(ptr)` and frees on an
address match **without comparing generation**, so a stale alias of A frees B.
The same pattern applies to `cplus_realloc` (`alloc.c:306`) and
`cplus_transfer_ownership` (`alloc.c:547`), and to the SafeCRT layer (not owned).
No memory is corrupted at the host level — the wrong *live block* is released —
but it is a genuine ownership/lifetime defect.

**Why not fixed here:** a correct fix needs a generation-aware entry point (e.g.
`cplus_free_checked_gen(void**, unsigned long long)`) plus a public declaration in
`libc/install-include/cplus_libc.h`, which is **outside this agent's strict file
ownership**. It also cannot be bolted onto the existing one-argument symbol
without breaking the C ABI/caller semantics. The `memory.hp` wrappers already
prevent this at the stdlib layer *when the caller supplies a captured token*;
the header comment now states that this is opt-in and not arbitrary UAF
protection.

### 4.2 R2 — recent-free cache saturation misclassifies a genuine double free (F-06)

**Probe** (`$TMPBASE/probe_recent.c`): allocate and free 1200 distinct 64-byte
blocks (more than `META_RECENT_CAP = 1024`), free a "victim" block before them,
then double-free the victim alias.

```sh
/usr/bin/clang -O1 -g -Wall -std=c99 -Ilibc/install-include \
  "$TMPBASE/probe_recent.c" build/libc/libcplus_hardened.a \
  -lm -pthread -o "$TMPBASE/probe_recent"
"$TMPBASE/probe_recent"
```

```
saturated double-free: rc=22 double_delta=0 invalid_delta=1
```

`recent_add` (`alloc.c:85-101`) overwrites `g_recent[start]` once the table is
full, evicting the victim's classification. `cplus_free_checked` then reports
`invalid_free` instead of `double_free` for a real double free.

**Safety impact: none at the host level.** Both classifications return EINVAL,
neither calls host `free`, and the handle is nulled — the double free is still
*refused*. This is a **diagnostic counter misclassification**, not a
double-free/UAF that reaches the allocator.

**Why not fixed here:** a bounded 1024-entry cache cannot remember unbounded
freed addresses; any replacement policy eventually forgets an old address.
A real fix requires either growing/retagging the cache or changing the counter
contract (and its header/tests), which is not a minimal change and is owned by
a different file. Recorded as a documented limit.

### 4.3 Items investigated but NOT reproduced as defects

* **`cplus_alloc_remaining_capacity` `uintptr_t` wrap (F-19, `alloc.c:420-421`).**
  The expression `b + byte_size` can wrap only if `byte_size` is near
  `SIZE_MAX`, which no legitimate allocation can have (the host allocator
  refuses such requests; `STDLIB_ALLOC_LIMIT` is 1<<40). Because a wrap yields a
  *smaller* value than `p = b + 8`, the interior test `p <= b + byte_size`
  becomes false and the function returns `(size_t)-1` — fail-closed. No false
  capacity and no OOB window was produced. Latent/documented.
* **Generation wrap to 0 (F-18).** Needs 2^64 generation bumps; theoretical.
* **Host-`free` bypass leaves stale metadata (F-02).** Requires the caller to
  violate the documented contract ("never hand a tracked block to raw free").
  Not reachable through the audited C ABI alone.
* **Zero-size `calloc(0, SIZE_MAX)`** returns a 1-byte block rather than
  rejecting the nonsensical request; no overflow occurs (zero branch wins).
  Intentional (F-15).

---

## 5. Post-change verification

After the `memory.hp` documentation edit, all four required tests were rebuilt
(where applicable) and re-run:

```sh
"$TMPBASE/test_alloc_metadata"                       # test_alloc_metadata: ok
"$TMPBASE/test_alloc_fault_matrix" | tail -1         # alloc_fault_matrix: 294/294 checks ok
build/c+ build tests/libc/test_stdlib_realloc.cp --strict --sanitize=none -o "$TMPBASE/realloc2" \
  && "$TMPBASE/realloc2" | tail -1                   # stdlib_realloc: 12/12 checks ok
build/c+ build tests/stdlib/deep_memory_bounds.cp --strict --sanitize=none -o "$TMPBASE/deep2" \
  && "$TMPBASE/deep2" | tail -1                      # MEMORY_BOUNDS 61 0
build/c+ build tests/stdlib/deep_memory_bounds.cp --strict --sanitize=undefined -o "$TMPBASE/deep3" \
  && "$TMPBASE/deep3" | tail -1                      # MEMORY_BOUNDS 61 0
```

**Result: 5/5 invocations pass; no regression.** (`test_alloc_metadata` and
`test_alloc_fault_matrix` do not include `memory.hp`, so their binaries were
unchanged; their pre-change binaries were re-executed.)

---

## 6. Explicit statement on UAF detection limits

* `stdlib/memory.hp` is a **thin, additive, fail-closed bounds checker**, not a
  pointer-provenance or lifetime system. It does not use fat pointers, a
  compiler change, or a new side table.
* It cannot detect arbitrary use-after-free. Detection works only for tracked
  `stdlib_alloc`/`stdlib_calloc` blocks **when the caller captured a generation
  token before the free/realloc/transfer and passes that token** to the guarded
  operation. `token == 0` disables the check.
* Only the **destination** token is validated in `stdlib_mem_copy`, `mem_move`,
  `mem_set`, and `mem_borrow`. There is no source-token parameter; a recycled
  source address is read from the new block (logical aliasing, not OOB).
* A freed pointer whose address is no longer tracked is refused as
  `STDLIB_CATEGORY_INVALID` ("not a tracked stdlib block"), **not** reported as
  `CPLUS_EXIT_USE_AFTER_FREE`; only a matching-but-stale token yields the
  lifecycle/UAF code.
* At the hardened-libc layer, `cplus_free_checked` / `cplus_realloc` /
  `cplus_transfer_ownership` are generation-blind and **do** free/move/transfer
  a new block through a stale alias when the host allocator recycles the address
  (R1). Callers must not retain aliases across a lifetime boundary.
* No claim in this document constitutes a proof of pointer lifetime,
  provenance, or absence of host-level undefined behaviour.

---

## 7. Summary

* Four required tests pass in strict/no-sanitizer builds; three UBSan variants
  are clean and `alloc.c` compiles warning-free under `-Wall -Wextra -Wpedantic`.
* All twelve requested concerns were investigated with source and runtime
  evidence (§2). Normal-path behavior is correct.
* Two pre-existing documented-limit defects were reproduced (R1 stale alias,
  R2 recent-cache saturation). Neither is fixable with a minimal, C-ABI-preserving
  change confined to the owned files; both are recorded with exact commands and
  before/after output and referred to the owning component.
* The single change made is a documentation clarification in `stdlib/memory.hp`
  that removes any implication of arbitrary UAF detection and states the exact
  token/source limitations. It is behavior- and ABI-neutral.

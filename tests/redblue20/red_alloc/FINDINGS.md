# RED-BLUE20 agent 1/20 — ATTACKER findings: modern allocators

Targets: `stdlib/alloc_iface.hp`, `stdlib/arena.hp`, `stdlib/mempool.hp`,
`stdlib/datastack.hp`. Branch `agent/modern-stdlib`.

Only files under `tests/redblue20/red_alloc/` were created. No source/header
edits, no `git add` / `git commit`. No fabricated findings: every item below
has a runnable reproducer with the exact observed return code.

## Campaign

```
build:  ./build/c+ build tests/redblue20/red_alloc/harness.cp \
          --default --sanitize=none -I. \
          -o tests/redblue20/red_alloc/out/harness \
          -- build/libc/libcplus_hardened.a
run:    bash tests/redblue20/red_alloc/run.sh
seed:   0xa11c0de5eed5a11c (splitmix64, fixed)
cases:  3200 deterministic cases, 20 rotating families, one forked child/case,
        canaries + PROT_NONE guard page, pipe result + parent signal classifier
selfcheck: RED_ALLOC_SELFCHECK=1 <bin>  =>  SELFCHECK 5 5
result: RED20 alloc 3200 634 593 0 1973
```

Classifier: SIGSEGV/SIGBUS/fatal exit/short pipe write => `CRASH`;
UNEXPECTED_OK => target accepted hostile input it promises to refuse;
CORRUPT => canary/alignment guarantee violated; `REFUSED` => hostile input
refused or canary intact. Full per-case log: `out/scenarios.log`.

### Per-family verdicts (160 cases each)

| Family | REFUSED | CRASH | CORRUPT | Unexpected OK |
|---|---:|---:|---:|---:|
| 0 alloc invalid size/align | 160 | 0 | 0 | 0 |
| 1 alloc over-alignment | 0 | 0 | 160 | 0 |
| 2 arena invalid args | 160 | 0 | 0 | 0 |
| 3 arena stale handles | 160 | 0 | 0 | 0 |
| 4 arena caller buffer + guard page | 160 | 0 | 0 | 0 |
| 5 arena top realloc (valid) | 160 | 0 | 0 | 0 |
| 6 arena realloc, forged old_size | 98 | 46 | 16 | 0 |
| 7 pool foreign-pointer free | 113 | 47 | 0 | 0 |
| 8 pool double/misaligned free | 160 | 0 | 0 | 0 |
| 9 pool slab raw-size overflow | 0 | 160 | 0 | 0 |
| 10 slot_pool init product overflow | 0 | 160 | 0 | 0 |
| 11 slot_ref stale/double free | 160 | 0 | 0 | 0 |
| 12 slot_pool growth canary | 160 | 0 | 0 | 0 |
| 13 datastack invalid args | 160 | 0 | 0 | 0 |
| 14 datastack VM/reserve exhaustion | 99 | 61 | 0 | 0 |
| 15 datastack realloc forged old_size | 0 | 160 | 0 | 0 |
| 16 scratch scopes out of order | 63 | 0 | 97 | 0 |
| 17 datastack release forged size | 0 | 0 | 160 | 0 |
| 18 stale scratch mark after destroy | 160 | 0 | 0 | 0 |
| 19 default realloc over-alignment | 0 | 0 | 160 | 0 |

---

## Genuine findings

### F-ALIGN-01 — default checked-heap allocator silently ignores `align`
**Severity: genuine contract violation (alignment).** `alloc_iface.hp:40-63`
and `:65-86` define `align` as part of the interface; the NULL/`HeapAllocator`
dispatch path calls `stdlib_alloc(size)` / `stdlib_realloc(ptr,new_size)`,
which call `cplus_alloc` / `cplus_realloc` with **no alignment argument** and
return `max_align_t` (16-byte) pointers. An over-aligned request is accepted
and returns a misaligned pointer. Root cause: `align` is validated but never
used on the default path (`alloc_iface.hp:45,50-57,71,76-80`).

```
./tests/redblue20/red_alloc/out/repro 1   # 32/32 misaligned with align=4096
./tests/redblue20/red_alloc/out/repro 2   # 32/32 misaligned on realloc align=4096
```
Observed: `F-ALIGN-01 alloc misaligned=32/32`, rc=0;
`F-ALIGN-01b realloc misaligned=32/32`, rc=0. (Earlier runs: 29/32 and 31/32 —
probability of accidental 4096 alignment is ~1/256 per allocation.)

### F-ARENA-02a — forged `old_size` on non-top arena realloc causes unbounded `memcpy` → SIGSEGV
**Severity: genuine crash; precondition: caller-supplied `old_size` is forged.**
`arena.hp:226-232`: `top = a->cursor - old_size`; with a 64-byte live block and
`old_size = 2^50` the underflowed `top` does not equal `ptr`, so the non-top
path executes `memcpy(fresh, ptr, old_size)` with ~1 TiB. No validation that
`old_size` is plausible relative to the live allocation.

```
./tests/redblue20/red_alloc/out/repro 3
```
Observed: `F-ARENA-02a before realloc p=0x... old_size=2^50`, **rc=139 (SIGSEGV)**.

### F-ARENA-02b — forged `old_size` aliases a non-top block with the arena top; next allocation overwrites a live neighbour
**Severity: genuine canary corruption; precondition: forged `old_size`.**
`arena.hp:226-247`: with blocks `p` (64B) then `q` (64B), passing
`old_size = (q-p)+64 = 128` makes `cursor - old_size == p` even though `p` is
not the last allocation. The "top of stack" branch shrinks `cursor` to `p+16`;
the caller still considers `q` live. A following 64-byte allocation lands at
`p+16` and its zeroing destroys the first 16 bytes of `q`.

```
./tests/redblue20/red_alloc/out/repro 4
```
Observed: `F-ARENA-02b q_canary_destroyed=16/16 fresh=0x...`, rc=0 (silent).

### F-POOL-01 — `stdlib_mem_pool_add_slab` raw-size overflow wraps its own bounds check → SIGSEGV
**Severity: genuine integer-overflow crash, no forged metadata needed.**
`mempool.hp:91-93` only checks `per_slab > SIZE_MAX/stride`; `mempool.hp:95`
then computes `raw = sizeof(*s) + stride*per_slab + per_slab + ALIGN`, which
overflows. `mempool.hp:109-112`'s span check also wraps (pointer arithmetic),
so it passes. `mempool.hp:115` executes `memset(s->bitmap, 0, per_slab)` with
`per_slab = 542551296285575052` (~5.4e17) bytes.

```
./tests/redblue20/red_alloc/out/repro 5   # init(slot_size=1, per_slab=0x78787878787878c)
```
Observed: `F-POOL-01 init ok slots_per_slab=542551296285575052; now alloc`,
**rc=139 (SIGSEGV)** on the first `stdlib_mem_pool_alloc`.

### F-POOL-02 — `stdlib_slot_pool_init` missing `slot_size * capacity` overflow check → SIGBUS/SIGSEGV
**Severity: genuine integer-overflow crash, no forged metadata needed.**
`mempool.hp:284-286` allocates `slot_size * cap` with no overflow check (contrast
`mempool.hp:305` in `grow`, which does check). `slot_size = 2^61+1, initial=0`
makes the product wrap to 8 bytes; the three backing allocations succeed and
`mempool.hp:343` later zeroes `slot_size` (~2.3e18) bytes at the slot.

```
./tests/redblue20/red_alloc/out/repro 6
```
Observed: `F-POOL-02 init rc=0; now alloc_ref`, **rc=138 (SIGBUS)**.

### F-POOL-03 — wild foreign-pointer free dereferences the slot header before refusing
**Severity: genuine crash of a promised "foreign pointer refused" path.**
`mempool.hp:166` forms `h = ptr - STDLIB_POOL_HEADER_SIZE` and `mempool.hp:171`
reads `h->slab` inside the owner-slab loop. With a pool that owns at least one
slab, releasing `(void*)0x1000` reads unmapped memory before the ownership
comparison can refuse it. (With an empty pool the loop body never runs and the
call is safely refused — the precondition matters.)

```
./tests/redblue20/red_alloc/out/repro 7
```
Observed: `F-POOL-03 release wild=(void*)0x1000`, **rc=139 (SIGSEGV)**.
The same harness family confirms valid foreign pointers (other pool, stack,
managed heap) ARE refused (113 REFUSED cases) — the defect is the wild address.

### F-DS-01 — forged `old_size` on non-top datastack realloc → SIGSEGV
**Severity: genuine crash; precondition: forged `old_size`.**
`datastack.hp:245-250` computes `top = ds->top - old_size` and, on the non-top
path, calls `memcpy(fresh, ptr, old_size)` with no plausibility check. A 64-byte
block with `old_size = 2^40` copies 1 TiB.

```
./tests/redblue20/red_alloc/out/repro 8
```
Observed: `F-DS-01 before realloc p=0x... old_size=2^40`, **rc=139 (SIGSEGV)**.

### F-DS-02 — huge heap capacity in `stdlib_datastack_init` aborts the process instead of returning `STDLIB_FAIL`
**Severity: genuine violation of the "refuse via error*, never abort" contract.**
`datastack.hp:146` calls `cplus_alloc(capacity)` directly with an unvalidated
capacity. `capacity = SIZE_MAX` makes the C+ runtime terminate with
`out of memory: managed allocation failed` (exit 107) rather than letting
`stdlib_datastack_init` return `STDLIB_FAIL`. The VM path with the same huge
reserve (`datastack.hp:79-111`, mmap) correctly refuses.

```
./tests/redblue20/red_alloc/out/repro 9
```
Observed:
```
F-DS-02 init(capacity=SIZE_MAX)
C+ runtime error: out of memory: managed allocation failed
```
**rc=107 (fatal exit).** A standalone 60x60 count shows this is deterministic
for the heap path: `DS14 crashes by v: v0=0 v1=60 v2=0 of 60 each`.

### F-DS-03 — out-of-order scratch scope `leave` rewinds `top` over still-live inner allocations
**Severity: genuine canary corruption, but precondition is caller misuse
(out-of-order leave), so treat as conditional hardening gap.**
`datastack.hp:194-207` restores `saved_top` on `leave` without checking stack
discipline; `stdlib_scratch_scope::drop` (`datastack.hp:317-321`) has the same
property. Leaving the inner scope before the outer one (or leaving only the
outer scope) rewinds `ds->top` below a still-live inner block; the next bump
allocation zeroes and reuses its bytes. In-order LIFO leaves are clean (63
REFUSED cases in family 16, variant 0).

```
./tests/redblue20/red_alloc/out/repro 10
```
Observed: `F-DS-03 p1_canary_destroyed=16/16 fresh=0x...`, rc=0 (silent).

### F-DS-04 — forged `size` in `stdlib_datastack_release` rewinds over a live block
**Severity: genuine canary corruption; precondition: forged `size` (caller
passes 128 for a 64-byte block).**
`datastack.hp:267-273` trusts `size` in `ptr == ds->top - size` and then
subtracts it from `top`. With a forged 128, `top` lands at the start of `p1`;
the next allocation overwrites p1's canary.

```
./tests/redblue20/red_alloc/out/repro 11
```
Observed: `F-DS-04 p1_canary_destroyed=16/16 fresh=0x...`, rc=0 (silent).

---

## Probes that were correctly REFUSED (no finding)

These were attacked hard and consistently refused; recorded so the parent can
see they were actually exercised, not skipped:

* invalid alignment (0 coerced to 16, 3, 5, 6, 24) and zero/`SIZE_MAX` sizes at
  `stdlib_allocate` / `stdlib_reallocate` (family 0, 160/160 REFUSED);
* `stdlib_arena_init(SIZE_MAX)` overflow refusal, NULL arena, zero-size alloc,
  non-power-of-two alignment, new-block-too-small (family 2, 160/160);
* stale arena handles after `do_reset`, `destroy`, and re-init; allocation via a
  destroyed arena returns NULL (family 3, 160/160) — **no generation bypass**;
* caller buffer with an adjacent `PROT_NONE` guard page: no write past the end
  and no guard-page fault (family 4, 160/160);
* valid top-of-stack arena realloc grow/shrink preserves neighbour canary
  (family 5, 160/160);
* pool double-free, misaligned free, and re-release of a recycled slot are
  refused (family 8, 160/160) — **no double-free acceptance**;
* stale `stdlib_slot_ref` get/free after `free_ref` and after pool destroy,
  including a copied handle, are refused (family 11, 160/160) —
  **no stale-handle acceptance / generation bypass**;
* legitimate slot-pool growth preserves handle and canary (family 12);
* datastack invalid args, huge VM reserve, and `commit(capacity+1)` on a
  VM-backed stack all refuse cleanly (families 13, 14 v=0/v=2);
* stale scratch mark after `stdlib_datastack_destroy` is ignored via the
  generation check (family 18, 160/160).

## Reproducer index

| # | Finding | Expected outcome |
|---|---|---|
| 1 | F-ALIGN-01 (alloc align) | diag: misaligned=32/32 |
| 2 | F-ALIGN-01 (realloc align) | diag: misaligned=32/32 |
| 3 | F-ARENA-02a | rc=139 |
| 4 | F-ARENA-02b | diag: 16/16 corrupted |
| 5 | F-POOL-01 | rc=139 |
| 6 | F-POOL-02 | rc=138 |
| 7 | F-POOL-03 | rc=139 |
| 8 | F-DS-01 | rc=139 |
| 9 | F-DS-02 | rc=107 + fatal diagnostic |
| 10 | F-DS-03 | diag: 16/16 corrupted |
| 11 | F-DS-04 | diag: 16/16 corrupted |

All reproducers are built with:
```
./build/c+ build tests/redblue20/red_alloc/repro.cp --default --sanitize=none \
  -I. -o tests/redblue20/red_alloc/out/repro -- build/libc/libcplus_hardened.a
```

RED20 alloc 3200 634 593 0 1973

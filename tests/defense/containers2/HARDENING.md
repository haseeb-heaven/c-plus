# Containers / vector / string / memory — blue-team hardening review (Agent E)

Owned files touched: `stdlib/memory.hp` only.  New files: this directory only.
`stdlib/vector.hp`, `stdlib/string.hp` and `stdlib/collections.hp` were
**reviewed and left unchanged** (no genuine gap found — see below).
`stdlib/safe/*` (Agent D) and all compiler/runtime files were not touched.

## Summary

| Area | Genuine gap? | Action |
|---|---|---|
| `vector` growth overflow / reserve / shrink / insert / copy_from commit-last / `at()` / double drop / self-append | No | 300 forked cases + failure injection; all refusals and state snapshots correct. |
| `string` capacity / self-append / alias-after-clear / null termination / size overflow | No | 300 forked cases + self-alias variants + failure injection; all correct. |
| `collections` hash collision / removal / invalidation / alloc failure / destruction / ring wraparound | No | 450 forked cases with reference models (intmap, strmap owned keys, intset, deque/queue/stack/list); all correct. |
| `memory.hp` bounded queries — **element count of large tracked blocks** | **YES** | Fixed with an exact, additive `static inline` divider in `memory.hp`. Regression fails before, passes after. |
| `memory.hp` stale-generation tokens / transfer / checked release | No | Covered in family 7; all correct. |

## The one genuine gap: `stdlib_mem_count()` silently truncates large counts

`stdlib_mem_count()` used to delegate to `stdlib_alloc_query_length()`, which
computes the element count through `stdlib_safe_div()` in `error.hp`.  That
helper subtracts `b << sh` at most once for each of 16 bit positions.  It is an
exact quotient only while the result fits in 16 bits; from 2^16 upward it
**saturates at 65535** without reporting an error.

Concrete probe (unmodified headers):

```
p=0x75e5400000
alloc_size=100000
query_length(elem=1)=0 count=65535
mem_count(elem=1)=0 count=65535
safe_div(100000,1)=65535
```

This is not a cosmetic issue: `stdlib_mem_count()` is the public bounded-query
helper that `memory.hp` advertises for sizing bulk copies.  A 100000-byte block
(25k `int`s) is an ordinary container size, yet callers were told it held 65535
elements.  Any code that derives a loop bound or a byte count from this query
would under-iterate or under-copy.

### Fix (minimal, ABI-neutral, `stdlib/memory.hp` only)

* Added `stdlib_mem_div_exact(size_t a, size_t b)` — a `static inline`
  shift-and-subtract long division that carries the remainder across **all**
  `size_t` bits (no variable-division operator, so `--strict` still accepts it).
  For `elem_size > SIZE_MAX/2` the quotient can only be 0 or 1, handled before
  the loop so `r << 1` cannot overflow.
* Rewrote `stdlib_mem_count()` to resolve the byte capacity and effective
  element size through the existing `stdlib_alloc_size()` /
  `stdlib_alloc_elem_size()` helpers and then call the exact divider.  Its
  signature, return codes, NULL/untracked handling and `*out_count = 0`
  failure behaviour are unchanged.
* No signature, struct layout, enum, exported symbol or macro changed.  The
  new helper is `static inline`, so no ABI-visible symbol is added.

### Residual limitation (honest, not in an owned file)

`stdlib_alloc_query_length()` and `stdlib_alloc_length()` in `error.hp` still
use the 16-bit `stdlib_safe_div()` and therefore still saturate for large
blocks.  `error.hp` is outside this agent's ownership, so this was **not**
changed here.  The probe above shows `query_length` still returns 65535 after
the fix; only `stdlib_mem_count()` (the `memory.hp` bounded-query API) is fixed.
`stdlib_mem_capacity()` and `stdlib_mem_bounds()` were already exact because
they report bytes, not element counts.

## Harness design

`test_containers2.cp`: 1200 scenarios, fixed SplitMix64/xorshift64* seed
`0xC0FFEE0DF02A5EED`, family = `id % 8` (150 per family).  One `fork()`ed child
per scenario with `alarm(10)`; a 64-byte global sentinel plus two 64-byte guard
allocations are armed and re-checked after each workload.  Exit status encodes
verdict + canary bits, so a crash, hang, wrong result or canary corruption is a
scenario failure.  The parent only reads deterministic ids/results — no address
ever enters the log.

* Family 0 — vector model: push/insert/erase/pop/reserve/shrink/clear, full
  content comparison, slice-generation staleness, idempotent drop.
* Family 1 — vector failure atomicity: inject failures at allocation points 1–3
  of reserve/insert/copy_from; on refusal require byte-identical state.
* Family 2 — string model: plain and self-append (in-place and reallocating),
  clear, reserve, copy_from, `substr`, `field`, `count_fields`, NUL termination
  and capacity-versus-allocation-size agreement.
* Family 3 — string failure atomicity, alias-after-reserve, repeated growth.
* Family 4 — intmap model with tombstones, overwrite, removal and clear.
* Family 5 — intset + strmap with owned-key rebuild, removal and reinsertion.
* Family 6 — deque/queue/stack/list with multiple ring wraparounds and strict
  LIFO/FIFO order checks.
* Family 7 — memory bounded queries, stale-generation tokens, ownership
  transfer, stale-alias checked release, and the large-count regression.

## Exact before / after output

Commands (from repository root; `./build/c+` is the deterministic driver):

```
# BEFORE: temporarily restore the original stdlib/memory.hp, then
./build/c+ build tests/defense/containers2/repro_large_count.cp \
  --strict --sanitize=none -I. -o out/repro_before
timeout 10 out/repro_before

./build/c+ build tests/defense/containers2/test_containers2.cp \
  --strict --sanitize=none -I. -o out/before
timeout 60 out/before
```

Before — minimal reproducer (exit 1) and harness (`out/before.log`):

```
stdlib_mem_count(100000-byte block, elem_size=1) -> rc=0 count=65535

FAMILY 0 150 150 0
FAMILY 1 150 150 0
FAMILY 2 150 150 0
FAMILY 3 150 150 0
FAMILY 4 150 150 0
FAMILY 5 150 150 0
FAMILY 6 150 150 0
FAMILY 7 150 0 150          <-- every large-count case fails
CONTAINERS2 1200 1050 150
```
(`RUN_RC=1`, harness exit 1.)

After — `sh tests/defense/containers2/run.sh` (`out/after.log`):

```
stdlib_mem_count(100000-byte block, elem_size=1) -> rc=0 count=100000
PASS repro_large_count

FAMILY 0 150 150 0
FAMILY 1 150 150 0
FAMILY 2 150 150 0
FAMILY 3 150 150 0
FAMILY 4 150 150 0
FAMILY 5 150 150 0
FAMILY 6 150 150 0
FAMILY 7 150 150 0
CONTAINERS2 1200 1200 0
PASS test_containers2
```
(`RUN_RC=0`.)

"No gap found" for vector/string/collections is backed by families 0–6 passing
identically before and after — the fix only changes family 7.

## ABI fingerprint

Compiled one probe against the original `memory.hp` tree and one against the
fixed tree with the same strict flags, then compared public type sizes and the
exported global symbol set:

```
BEFORE: sizeof: vector=32 string=24 slice=32 string_view=16 intmap=48 strmap=48 intset=40 deque=32 list=24 stack=24 queue=32 stdlib_state=168 error=268
AFTER:  sizeof: vector=32 string=24 slice=32 string_view=16 intmap=48 strmap=48 intset=40 deque=32 list=24 stack=24 queue=32 stdlib_state=168 error=268
exported symbol diff: (empty) -> SYMBOLS_IDENTICAL
```

ABI fingerprint: **unchanged** (additive-only, as required).

## Project regression suite

`sh tests/stdlib/method_tests/run_all.sh` after the fix:

```
| group | total methods | passed | failed | skipped |
| alloc_mem | 87 | 87 | 0 | 0 |
| collections | 63 | 63 | 0 | 0 |
| file_io | 28 | 28 | 0 | 0 |
| json_log_time | 56 | 56 | 0 | 0 |
| multimedia | 127 | 127 | 0 | 0 |
| net_http | 73 | 73 | 0 | 0 |
| str_vec | 47 | 47 | 0 | 0 |
| thread_async | 37 | 37 | 0 | 0 |
| **TOTAL (8 groups)** | **518** | **518** | **0** | **0** |
aggregate: ok (518 methods passed, 0 skipped)
```

Honest note: the **first** invocation of that aggregate reported a transient
`json_log_time` build failure from `stdlib/json.hp` ("use of closed FILE* 'fp'")
— a file outside this agent's ownership and unrelated to the `memory.hp` edit.
Rebuilding that exact source directly succeeded, and a clean second aggregate
run produced the 518/518 result above.  No test was weakened or skipped.

## Files delivered

```
tests/defense/containers2/HARDENING.md          (this file)
tests/defense/containers2/run.sh
tests/defense/containers2/test_containers2.cp   (1200-case harness)
tests/defense/containers2/repro_large_count.cp  (minimal reproducer)
tests/defense/containers2/out/before.log        (before evidence)
tests/defense/containers2/out/after.log         (after evidence)
stdlib/memory.hp                                (only source file changed)
```

This is evidence from fixed deterministic inputs, not a proof of absence of
all defects.

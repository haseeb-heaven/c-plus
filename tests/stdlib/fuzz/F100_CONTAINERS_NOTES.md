# F-061 .. F-075 — deterministic fuzz configurations for stdlib `.hp` containers

## 1. Scope and strict ownership

Fifteen standalone `.cp` programs (each with its own `main`) plus one shared
engine header. No header under `stdlib/`, no compiler code, no build script and
no other test file was modified. Build artefacts were kept outside the
repository (`/tmp/f100verify3`) because other agents concurrently clean
`build/`.

| ID | File (all under `tests/stdlib/fuzz/`) | Seed | Ops | Focus |
| --- | --- | --- | --- | --- |
| F-061 | `f100_cont_f061_collide_int.cp` | `0xF0615EED` | 2200 | `intmap`/`intset`, collision-heavy integer keys |
| F-062 | `f100_cont_f062_tomb_intmap.cp` | `0xF0625EED` | 2400 | `intmap` tombstone reuse (scripted cycles) |
| F-063 | `f100_cont_f063_tomb_intset.cp` | `0xF0635EED` | 2300 | `intset` tombstone reuse (scripted cycles) |
| F-064 | `f100_cont_f064_collide_strmap.cp` | `0xF0645EED` | 2200 | `strmap`, collision-heavy owned string keys |
| F-065 | `f100_cont_f065_tomb_strmap.cp` | `0xF0655EED` | 2400 | `strmap` tombstone reuse + owned-key release |
| F-066 | `f100_cont_f066_deque_ring.cp` | `0xF0665EED` | 2600 | `deque` ring wraparound, reserve, overflow refusal |
| F-067 | `f100_cont_f067_list_churn.cp` | `0xF0675EED` | 2400 | `list` front/back churn, node alloc/free |
| F-068 | `f100_cont_f068_stack_growth.cp` | `0xF0685EED` | 2300 | `stack` doubling growth, top/clear |
| F-069 | `f100_cont_f069_queue_ring.cp` | `0xF0695EED` | 2600 | `queue` FIFO ring wraparound |
| F-070 | `f100_vec_f070_capacity.cp` | `0xF0705EED` | 2500 | `vector` capacity growth, reserve/shrink/overflow |
| F-071 | `f100_vec_f071_erase.cp` | `0xF0715EED` | 2500 | `vector` insert/erase/pop/clear positional model |
| F-072 | `f100_vec_f072_stale.cp` | `0xF0725EED` | 2400 | `vector` slice/allocator generation staleness |
| F-073 | `f100_vec_f073_faults.cp` | `0xF0735EED` | 2400 | `vector` allocation-fault injection |
| F-074 | `f100_cont_f074_faults.cp` | `0xF0745EED` | 2400 | allocation-fault injection across all containers |
| F-075 | `f100_cont_f075_balance.cp` | `0xF0755EED` | 2600 | cleanup / telemetry balance across all containers |

Shared engine: `tests/stdlib/fuzz/f100_cont_engine.hp`. Each config is only a
macro block (`F100_ID`, `F100_SEED`, `F100_OPS`, `F100_MODE`) followed by
`#include "f100_cont_engine.hp"`.

## 2. Determinism, validity and self-checking

- **Fixed-seed LCG.** `state = state * 6364136223846793005ULL +
  1442695040888963407ULL`, high 32 bits (`>> 33`), seeded per table row. No
  clock, `rand()`, environment input or ephemeral value is folded in.
- **Operation budget.** Every config sets `F100_OPS >= 2200`. Tombstone modes
  add a deterministic 32-step script every 32 iterations, so their executed
  operation count is higher (7200 / 6908 / 7200). Fault modes add 65 injected
  operations. The program fails if fewer than 2000 operations executed.
- **Collision-heavy keys.** For the map/set modes the key table is built with
  the public inline hash helpers from `collections.hp`
  (`stdlib_map_hash_int`, `stdlib_map_hash_text`): 64 keys with hash bucket 0
  and 64 with bucket 1 modulo 256. Since every table capacity reached here is
  a power of two <= 256, those keys stay clustered at every capacity; a
  per-bucket cursor guarantees all 128 keys are distinct.
- **Shadow model after every operation.** Independent fixed-size arrays track
  presence/value for all 128 key slots and exact contents/capacity for deque,
  list, stack, queue and vector. Maps are checked for `valid()`, `size()`,
  `contains()` and `get()` across the entire key domain after each op;
  sequences are checked element-by-element with `at_checked()`. Vector
  capacity is tracked exactly across reserve/shrink/clear/drop.
- **Generation staleness without stale dereferences (F-072).** Slices are read
  through `slice::at_checked()` only while `slice::stale() == 0`; after a shape
  change the harness asserts the slice reports stale and refuses reads. A
  separate allocator-metadata probe records a generation for a `stdlib_alloc`
  block, reallocates it and verifies the old generation is rejected and the
  new one accepted using `stdlib_alloc_check_generation()` — never by
  dereferencing the retired address.
- **Deterministic fault injection (F-073/F-074).** Every 37th scheduled
  iteration arms `stdlib_alloc_fail_at(1)` immediately before an operation
  that is guaranteed to allocate (vector reserve/shrink, map grow, list node
  allocation, full queue push). The operation must return `-1`; a snapshot
  taken immediately before arming is compared against the vector's size,
  capacity and contents, and the full shadow model is rechecked. Injection is
  always reset with `stdlib_alloc_fail_reset()`.
- **Capacity overflow probes (F-066/F-070).** `reserve(SIZE_MAX)` /
  `grow(SIZE_MAX)` must return the documented recoverable `-1` without
  mutating state. These are omitted from F-075 because they increment bounds
  telemetry by design.
- **Cleanup / telemetry balance (F-075).** Telemetry is enabled and reset
  *before* the first allocation. After all eight containers are dropped once,
  `alloc_count == free_count` (364/364 in this build) and `alloc_failures`,
  `invalid_ops`, `bounds_violations`, `ownership_violations` and
  `lifecycle_failures` are all zero; 126 container growths were counted.
- **Digest.** A 64-bit FNV-1a accumulator folds in the operation index, kind,
  key slot, value, raw schedule word, result code, explicit feature markers
  and post-operation extents. Each program prints `f0NN_digest <16 hex>` and
  `F-0NN ok <N> ops`; a mismatch prints `F-0NN INVARIANT <label>` and exits 1.
- **No UB inputs.** No NULL arguments where ownership is required, no
  out-of-range indices, no oversized allocations other than the documented
  overflow-refusal probes, no stale pointer dereferences, no raw offsets.

## 3. Exact verification commands and results

Run from the repository root on `agent/modern-stdlib` with the project-built
`./build/c+`. The strict/sanitizers-off pass was run first for every config,
then default and UBSan. Each build and run used `timeout 30`.

```sh
TMP=/tmp/f100verify3
mkdir -p "$TMP"

# 1) strict, sanitizers off (run FIRST for every config)
./build/c+ build tests/stdlib/fuzz/<file>.cp --strict --sanitize=none \
  -o "$TMP/<file>_strict_none"
timeout 30 "$TMP/<file>_strict_none"

# 2) default
./build/c+ build tests/stdlib/fuzz/<file>.cp \
  -o "$TMP/<file>_default"
timeout 30 "$TMP/<file>_default"

# 3) strict + UBSan
./build/c+ build tests/stdlib/fuzz/<file>.cp --strict --sanitize=undefined \
  -o "$TMP/<file>_ubsan"
timeout 30 "$TMP/<file>_ubsan"

# extra checks (all 15 configs)
./build/c+ check tests/stdlib/fuzz/<file>.cp
./build/c+ build tests/stdlib/fuzz/<file>.cp --strict --sanitize=none \
  -Werror -o "$TMP/<file>_werror"
```

All 45 build+run invocations returned rc=0 with 0 bytes of stderr; no
`INVARIANT` line and no timeout (124) occurred. Digests were identical across
the three modes for every config:

| ID | Digest (all three modes) | Final line | Extra deterministic line |
| --- | --- | --- | --- |
| F-061 | `66c579bd06a75951` | `F-061 ok 2200 ops` | — |
| F-062 | `18ff2416c2cc2d69` | `F-062 ok 7200 ops` | — |
| F-063 | `59e8f63b0cc98e79` | `F-063 ok 6908 ops` | — |
| F-064 | `5224aa732de2fc04` | `F-064 ok 2200 ops` | — |
| F-065 | `eca40d87e3ab302d` | `F-065 ok 7200 ops` | — |
| F-066 | `2cf8744079b52b4b` | `F-066 ok 2600 ops` | `F-066 overflow.probes 13` |
| F-067 | `200d32e0eef8f1f3` | `F-067 ok 2400 ops` | — |
| F-068 | `ad4330dab55aacb2` | `F-068 ok 2300 ops` | — |
| F-069 | `c4dab531ea63774c` | `F-069 ok 2600 ops` | — |
| F-070 | `0cfbfacab50c0a12` | `F-070 ok 2500 ops` | `F-070 overflow.probes 12` |
| F-071 | `67f6cdf44ffbcd7a` | `F-071 ok 2500 ops` | — |
| F-072 | `48c2b79270f00633` | `F-072 ok 2400 ops` | `F-072 stale.hits 1268` |
| F-073 | `90eca56c75396c47` | `F-073 ok 2465 ops` | `F-073 faults 65` |
| F-074 | `bb5a83a412b6e965` | `F-074 ok 2465 ops` | `F-074 faults 65` |
| F-075 | `f23c695fc7d44c74` | `F-075 ok 2600 ops` | `F-075 telemetry 364 364 126` |

Example exact stdout (F-075, strict/none):

```
F-075 telemetry 364 364 126
f075_digest f23c695fc7d44c74
F-075 ok 2600 ops
```

Additional checks: `c+ check` succeeded for all 15 files; strict `-Werror`
builds succeeded for all 15 files; three extra repeat runs of each
strict/none binary produced the same digest and empty stderr (45/45 PASS).

## 4. Findings

**No library defect found.** No crash, hang, sanitizer report, invariant
mismatch or telemetry imbalance appeared in any of the 45 build+run
verifications or the repeat-run checks.

**Harness defects found and fixed during development** (not library bugs;
recorded for transparency and regression value):

1. **Duplicate collision keys.** The first key setup restarted each of the 64
   per-bucket searches at `found * 8`, yielding only 5 distinct integers out of
   128; `F-061` reported `INVARIANT intmap.contains`. Repro before the fix:
   build F-061 strict/none and run it. Fixed with a per-bucket
   `next_candidate` cursor; all 128 keys are now distinct.
2. **Uninitialized string keys in mixed modes.** F-074/F-075 drove strmap ops
   but only populated the integer key table, so every strmap key was the empty
   string (`INVARIANT strmap.contains`). Repro before the fix: build either
   config strict/none and run it. Fixed by always populating both key tables.
3. **Capacity lost across `clear()` in the shadow.** `clear()` on deque/stack/
   queue/vector preserves capacity; the shadow reset it to zero, corrupting
   later fault-injection preconditions (`INVARIANT vector.size.capacity`,
   `INVARIANT fault.vector.not.atomic` in F-073). Fixed by keeping shadow
   capacity across `clear()`.
4. **Injection armed on a possibly non-allocating path.** F-073's original
   `insert` case only filled up to the bounded extent (128), which can be
   below the real capacity (256), so `insert` succeeded and the `-1` assertion
   failed. Fixed by using operations that always call `stdlib_alloc`
   (`reserve(cap + n)`, or `shrink_to_fit` when `0 < used < cap`).
5. **Telemetry reset after the first allocations.** F-075 initially reset
   telemetry after the three init `grow()` calls, producing 356 allocs vs 364
   frees (exactly the 8 init buffers). Fixed by enabling/resetting telemetry
   before the first allocation; now 364/364.
6. **Invalid post-`drop()` invariant check.** The engine initially called
   `check_shadows()` after `drop()`; released maps are deliberately invalid,
   producing `INVARIANT intmap.size`. Removed that call.

## 5. Limitations

- AddressSanitizer was not used: the selected C++ backend rejects
  `-fsanitize=address` in this environment (see `docs/stdlib/HP_FUZZ_NOTES.md`
  §7). The three required modes (strict/none first, then default and UBSan)
  all pass.
- These are grammar-driven deterministic programs, not libFuzzer targets;
  they generate only documented-valid operations (plus the explicit
  recoverable overflow/fault cases described above), so a failure is
  attributable to the library rather than to a contract-violating caller.
- No intentionally fatal paths are induced in-process; fatal safety checks
  remain the domain of the forked-child tests described in
  `docs/stdlib/HP_FUZZ_NOTES.md` §6.

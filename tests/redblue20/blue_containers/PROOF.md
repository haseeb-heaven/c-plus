# BLUE20 containers safety proof — 2400 fixed-seed, fork-per-case cases

**Exact result: `BLUE20 containers 2400 2400 0` — 2400 cases, 2400 passed, 0 failed.**

`run.sh` exits 0 and prints `PROOF OK`.

## Scope

The public container surface in this repository is exercised:

- `stdlib/string.hp` — self-append / aliased suffix append (in-place and
  reallocating paths), capacity overflow refusal, `at()`/`substr()`/`field()`
  bounds, `reserve`/`append`/`copy_from` failure atomicity, `drop`.
- `stdlib/vector.hp` — `insert`/`erase`/`pop_back` boundaries, `at()` /
  `at_checked()` / borrowed `slice` bounds, `push_back`/`reserve`/`insert`/
  `copy_from`/`shrink_to_fit` failure atomicity (including generation/stale
  state of an outstanding borrowed slice), `drop`.
- `stdlib/collections.hp` — `intmap` open addressing with forced slot
  collisions, tombstone probing, overwrite/re-put, `clear`, `grow`; plus
  idempotent `drop` for `intmap`, `strmap`, `intset`, `deque`, `list`, `stack`,
  `queue` (alongside `string` and `vector`).
- `stdlib/error.hp` — `STDLIB_ALLOC_LIMIT` overflow refusal and the
  `stdlib_alloc_fail_at(n)` injection points (three buffers of an `intmap`
  rebuild each failed in turn, with full rollback).

## Method

1. **Fixed seed.** A xorshift64* PRNG is reseeded from the case id in every
   child: `state = 0x9E3779B97F4A7C15 ^ (id+1)*0xBF58476D1CE4E5B9`. Every case
   is reproducible in isolation; no address or wall-clock value enters a check.
2. **Fork-per-case, bounded timeout 60.** `run.sh` launches each case as
   `timeout 60 ./proof <id>`. A runtime fatal (double free, bounds abort), a
   hang, a signal, a crash, or a missing `PASS` line is counted as a failure.
   The C+ program additionally contains an equivalent in-process `fork()` +
   60 s `waitpid` deadline harness (no-argument mode) used as an independent
   cross-check. Both harnesses report the identical summary.
3. **Build flags (exact):** `./build/c+ build tests/redblue20/blue_containers/proof.cp
   --default --sanitize=none -I. -o .../proof`. Build exits 0.
4. **Small allocator ceiling.** `STDLIB_ALLOC_LIMIT` is pinned to `4096` in the
   proof translation unit before including `stdlib/error.hp` (the documented
   per-TU override), so overflow branches are reached with small, fast inputs.
5. **Canaries.** A 96-byte seed-derived global sentinel is filled before each
   case and verified after. The bounds family also uses a guarded caller
   buffer (32-byte `pre` + `char buf[160]` + 32-byte `post`) around every
   `substr`/`field` destination. All 2400 cases report `canary_ok = 1`.
6. **Success criterion.** A case passes only if every assertion in its family
   holds **and** the canaries are intact. Families set a sub-case label before
   each group, so any failure names the exact sub-case that failed.

## Coverage (8 families × 300 cases each)

| id % 8 | Family | Sub-scenarios exercised per case |
|---|---|---|
| 0 | `self_append` | full self-append across reallocation; aliased suffix append (`s.append(s.c_str()+k)`) across reallocation; in-place self-append under spare capacity |
| 1 | `overflow_refusal` | `vector_allocation_bytes` ceiling, `vector_calculate_growth` saturation, `reserve(SIZE_MAX)` and `reserve(max+1)` for vector/string, `stdlib_string_calculate_growth` overflow, `intmap.grow(SIZE_MAX)` |
| 2 | `at_bounds` | `vector::at`/`at_checked` (0, last, size, size+1, `SIZE_MAX`, NULL out), `slice::at_checked` and `slice::sub` (in-range, empty, stale-free), `string::at`/`substr`/`field` with guarded destination |
| 3 | `insert_erase` | random insert at 0..size+1 with a parallel reference model, insert at 0/size and two out-of-range indices, random erase with out value, erase at 0/last and out-of-range, drain via `pop_back` then empty refusal |
| 4 | `double_drop` | `drop()` twice on each of `string`, `vector`, `intmap`, `strmap`, `intset`, `deque`, `list`, `stack`, `queue`, and zero-initialised containers |
| 5 | `intmap_collision` | keys forced into one hash slot of a 64-slot table, get/contains, tombstone removal with survivor lookup, re-put into tombstone, overwrite of a live key, `clear` |
| 6 | `alloc_atomic` | injected failure of the one growth allocation for string `append`/`reserve`/`copy_from` and vector `push_back`/`insert`/`copy_from`/`shrink_to_fit`; each of the three `intmap` rebuild buffers failed in turn (both `grow` and `put`-triggered rebuild) with full state rollback |
| 7 | `mixed_stress` | 40 randomized push/erase/put/remove/get/append steps with an independent map counter; invariants (`size <= capacity`, `at(size)=='\0'`, `at_checked(size)==-1`, `size == strlen(c_str())`) after every step |

That is **~14,100 sub-scenarios and tens of thousands of individual assertions**
across the 2400 fixed-seed cases; the reference-model families compare full
container contents after every mutation, not just return codes.

## Exact results

```
$ bash tests/redblue20/blue_containers/run.sh
+ ./build/c+ build .../proof.cp --default --sanitize=none -I. -o .../proof
+ fork-per-case: 2400 children, timeout 60 each
BLUE20 containers 2400 2400 0
case_lines=2400
pass_lines=2400
fail_lines=0
inprogram_crosscheck=BLUE20 containers 2400 2400 0
PROOF OK
```

- `BLUE20 containers 2400 2400 0` — the required summary line.
- 2400 `BLK` child lines, 2400 `PASS`, 0 `FAIL`; no child emitted a crash,
  fatal, timeout, or non-`BLK` line.
- `canary_ok = 1` on all 2400 lines; captured child stderr is **0 bytes**.
- Family distribution: 300 cases for each of the eight families.
- `sha256(cases.log) = cb5c75624df981c5f9ca84a3075d185bef4d282de3401b7c939d589f9b41fbf8`
  (byte-for-byte reproducible across two independent runs).

The build emits only two benign `--default`-mode analyzer warnings: the
documented NULL constructor argument (`string((const char *)0)` → empty) and
the unavoidable `argv` FFI-extent warning for `main`'s command line.

## Reproduction

```sh
cd <repo root>
bash tests/redblue20/blue_containers/run.sh
```

Or drive a single case directly:
```sh
./build/c+ build tests/redblue20/blue_containers/proof.cp \
  --default --sanitize=none -I. \
  -o tests/redblue20/blue_containers/proof
timeout 60 ./tests/redblue20/blue_containers/proof 1234
```

## Honest scope

This is strong evidence, not an exhaustive mathematical proof: a fixed set of
2400 deterministic cases exercises the defined public entry points and the
documented failure paths. It cannot cover every possible program state, and a
live-game result is not claimed from an emulator or build result. All reported
outcomes above are the actual observed output of the two harnesses.

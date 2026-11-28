# ADV1000 containers — deterministic adversarial evidence

Scope: `stdlib/vector.hp`, `stdlib/string.hp`, `stdlib/collections.hp`,
`stdlib/memory.hp` checked operations only:

- vector: `at`, `at_checked`, `push_back`, `insert`, `erase`, `reserve`,
  `shrink_to_fit`, `pop_back`, `view`/slice staleness, `vector_allocation_bytes`,
  `vector_calculate_growth`
- string: `append` (plain, NULL, **self-append**), `reserve`, `substr`, `at`,
  `stdlib_string_calculate_growth`
- collections: `deque`/`list`/`stack`/`queue` checked accessors, `intmap`/
  `strmap`/`intset` `get`/`contains`/`remove`/`grow`, `stdlib_map_next_pow2`
- memory: `stdlib_mem_span_ok`, `stdlib_mem_span_contains`, `stdlib_mem_bounds`,
  `stdlib_mem_key_current`, `stdlib_mem_query`, `stdlib_mem_check`,
  `stdlib_mem_set`/`copy`/`move`, `stdlib_mem_borrow`, `stdlib_mem_check_token`

## Method

Exactly 1000 scenarios, id `0..999`. A fixed-seed xorshift64* PRNG is reseeded
from the id (`0x9E3779B97F4A7C15 ^ id*0xBF58476D1CE4E5B9`), so each scenario is
independently reproducible and no runtime address enters the log. Family is
`id % 7` (families 0..5: 143 scenarios each, family 6: 142). Sizes are
`0..1024`; indices are drawn from the explicit spectrum
`{-1/SIZE_MAX, 0, size-1, size, size+1, small, half, random}`; lengths include
`0, 1, SIZE_MAX, cap, random`. Growth refusal is exercised both at
`STDLIB_ALLOC_LIMIT` / `SIZE_MAX` and through the allocator's logical failure
counter. Stale generation tokens and interior pointers are exercised directly.

Each scenario verifies a bounds refusal or a correct result, then a canary:
a 64-byte global sentinel checked after every scenario, plus (memory-write
family) a separate tracked guard block and a byte-for-byte check that every
buffer byte outside the intended write range is unchanged.

## Exact commands and results

Run from the repository root (`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`):

```sh
./build/c+ build tests/adversarial_1000/containers/adv1000_containers.cp --strict --sanitize=none -o tests/adversarial_1000/containers/adv1000_containers
```

- Result: no compiler output, exit 0. Strict mode initially rejected one
  variable-modulo expression (`i % size`); it was replaced with an equivalent
  subtraction, so the final source builds cleanly under `--strict`.

```sh
timeout 120 ./tests/adversarial_1000/containers/adv1000_containers > tests/adversarial_1000/containers/results/raw.log 2> tests/adversarial_1000/containers/results/raw.err
```

- Result: exit **0**; `raw.err` is 0 bytes; `raw.log` contains 1000 `S ` lines and
  exactly one summary line.
- Summary line: `ADV1000 containers 1000 614 1000 0`
  (1000 scenarios, **614 refusals**, **1000 canary_ok**, **0 failures**).
- Repeating the run produced a byte-identical stdout (`cmp` clean): deterministic.

```sh
grep '^S ' tests/adversarial_1000/containers/results/raw.log > tests/adversarial_1000/containers/results/scenarios.log
grep '^ADV1000' tests/adversarial_1000/containers/results/raw.log > tests/adversarial_1000/containers/results/summary.txt
shasum -a 256 tests/adversarial_1000/containers/results/scenarios.log > tests/adversarial_1000/containers/results/digest.txt
```

- `scenarios.log`: exactly 1000 lines.
- SHA-256 digest: `4683e88875c126e44e5abe6a28973c751990d995f7e8aefbbf9557bd747dfab3`.
- `./run.sh` reproduces all of the above from a clean checkout.

## BREAKING probes (family 6, 142 cases)

All four deliberate attempts refused with the sentinel intact:

1. `oob_at` — `at_checked(SIZE_MAX)`, `at_checked(9)` on an 8-element vector and
   `at(SIZE_MAX)`, `at(9)` all refuse; all 8 elements remain intact.
2. `one_over_limit` — `vector_allocation_bytes(max_count+1, &bytes)` returns -1
   with `bytes` untouched; `vector_calculate_growth(max_count, max_count+1)`
   saturates; `reserve(max_count+1)` on vector and
   `reserve(STDLIB_ALLOC_LIMIT+1)` / `stdlib_string_calculate_growth(...)` on
   string refuse.
3. `stale_borrow` — a token captured before `stdlib_realloc` is refused by
   `stdlib_mem_borrow` on the new block (LIFECYCLE category); a freed pointer is
   refused as untracked.
4. `overflow_growth` — `reserve(SIZE_MAX)` on vector, deque and string,
   `stdlib_ring_copy(..., SIZE_MAX)`, `intmap::grow(SIZE_MAX)` and
   `vector_allocation_bytes(SIZE_MAX)` all refuse.

No library defect was observed. During development four initial failures were
traced to the harness itself (inverted `contains` expectation, an over-strict
post-self-append size bound, a `substr` canary byte that the API legitimately
writes, and an in-block canary that a legitimate in-range write may overwrite);
these were corrected in the harness. The library behaviour was unchanged.

## Status: evidence, not a proof

These are 1000 deterministic, reproducible observations through defined public
entry points. They demonstrate that the checked operations refused the specific
adversarial inputs and that the checked canaries were intact in these runs.
They do **not** prove absence of defects for all program states, all allocator
histories, or concurrent use, and no live-game or injected-DLL claim is made.

## Files (only under `tests/adversarial_1000/containers/`)

- `adv1000_containers.cp` — the 1000-scenario harness
- `run.sh` — reproducible build/run/log/digest driver
- `.gitignore` — ignores generated `results/` and the local binary
- `NOTES.md` — this file
- `results/raw.log`, `results/raw.err`, `results/scenarios.log`,
  `results/summary.txt`, `results/digest.txt` — generated evidence (gitignored)

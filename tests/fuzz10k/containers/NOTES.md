# FUZZ10K containers — deterministic fuzz + forked crash containment

## Scope

Checked operations from `stdlib/vector.hp`, `stdlib/string.hp`,
`stdlib/collections.hp` and `stdlib/memory.hp`:

- vector: `at`, `at_checked`, `push_back`, `insert`, `erase`, `pop_back`,
  `reserve`, `shrink_to_fit`, `view` staleness, `vector_allocation_bytes`,
  `vector_calculate_growth`
- string: `append` (plain, `NULL`, **self-append / alias**), `reserve`, `substr`,
  `at`, `stdlib_string_calculate_growth`
- collections: `deque`/`list`/`stack`/`queue` checked accessors and `reserve`,
  `intmap`/`strmap`/`intset` `get`/`contains`/`remove`/`grow`,
  `stdlib_map_next_pow2`
- memory: `stdlib_mem_span_ok`, `stdlib_mem_span_contains`, `stdlib_mem_bounds`,
  `stdlib_mem_key_current`, `stdlib_mem_query`, `stdlib_mem_check`,
  `stdlib_mem_set`/`copy`/`move`, `stdlib_mem_borrow`, `stdlib_mem_token`,
  `stdlib_mem_check_token`

## Method

- **10,010 distinct deterministic scenarios**, id `0..10009` (`TOTAL 10010`).
- **Master seed `0x1B56C0DE5EED1234ULL`.** Per scenario the PRNG is reseeded as
  `MASTER_SEED ^ (id * 0x9E3779B97F4A7C15ULL)` and advanced with xorshift64*
  (multiplier `2685821657736338717`). No runtime address or time enters the
  log, so each id is independently reproducible.
- Family is `id % 8` (families 0 and 1 get 1252 scenarios, families 2–7 get
  1251): `0` vector `at` bounds, `1` vector single mutation, `2` **random
  operation sequences** (8–16 ops per scenario against an independent `int`
  model), `3` string, `4` memory queries, `5` guarded memory writes,
  `6` collections, `7` deliberate BREAKING probes.
- Sizes span `0..2048`. Indices are drawn from the explicit spectrum
  `{-1/SIZE_MAX, 0, size-1, size, size+1, small, half, random, one}`; lengths
  include `0, 1, SIZE_MAX, cap, random`.
- Growth refusal is exercised at `STDLIB_ALLOC_LIMIT`, `STDLIB_ALLOC_LIMIT+1`
  and `SIZE_MAX` (vector/string/deque/intmap/map helpers). Self-append,
  interior pointers, stale generation tokens and a `NULL` block are exercised
  directly.
- **Verification per scenario:** a bounds refusal or a correct result; a
  64-byte per-id global sentinel checked after every scenario; for the write
  family, a separate tracked guard block plus a byte-for-byte check that every
  buffer byte outside the intended write range is unchanged; and, after
  *every* operation in the sequence family, `size <= capacity` plus an
  element-by-element match against the independent model. Any violation is a
  `FAIL` / corruption and makes the process exit nonzero.
- **Forked subprocess per batch:** `BATCH = 50`, so `NBATCH = 201` children.
  Each child `freopen`s `results/batch_<b>.log`, flushes one `S` line per
  completed scenario, and writes a `BATCH` summary before `_exit`. The parent
  polls with `waitpid(WNOHANG)` on a monotonic clock (`CLOCK_MONOTONIC`); a
  child that dies by signal is a **crash** (`WIFSIGNALED`/`WTERMSIG`), a child
  that exceeds the **12 s per-batch** deadline is `SIGKILL`ed and counted as a
  **hang**. This isolates a fault to one batch instead of losing the run.

## Exact commands and results

Run from the repository root
(`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`).

Build (exact required form, no output, exit 0):

```sh
./build/c+ build tests/fuzz10k/containers/fuzz10k_containers.cp --strict --sanitize=none -I. -o tests/fuzz10k/containers/fuzz10k_containers
```

Run (exit **0**, wall time ~1.5 s):

```sh
timeout 600 ./tests/fuzz10k/containers/fuzz10k_containers > tests/fuzz10k/containers/results/raw.log 2> tests/fuzz10k/containers/results/raw.err
```

Observed stdout (the required summary line):

```
FUZZ10K containers 10010 0 0 0 6682
```

Observed stderr diagnostic:

```
DETAIL dispatched=10010 s_lines=10010 crashes=0 hangs=0 corruptions=0 refusals=6682 canary_ok=10010 missing=0 spawn_err=0 wait_err=0 child_fail_exit=0
```

So: **10,010 scenarios, 0 crashes, 0 hangs, 0 corruptions, 6,682 correct
refusals**; all 10,010 scenarios emitted a completed `S` line and all 10,010
canary checks passed. `raw.err` contains only the single `DETAIL` line (no
crash/hang messages).

Evidence extraction:

```sh
grep '^FUZZ10K' raw.log > summary.txt
for b in $(seq 0 200); do grep '^S ' batch_${b}.log >> scenarios.log; done
shasum -a 256 scenarios.log | awk '{print $1}' > digest.txt
```

- `results/scenarios.log`: exactly **10,010** `S` lines, ids in ascending order
  (`S 0 …` through `S 10009 …`), no `FAIL`, no `canary_ok=0`.
- SHA-256 of `scenarios.log`:
  `ba0f1af383b93e42c5af6aabb3c196a8b758b87d90a0c52a34a1ad182cab1ded`.
- `./run.sh` reproduces build → run → logs → digest from a clean checkout
  (verified: identical digest, exit 0).
- Determinism: two consecutive clean runs produced byte-identical `raw.log`
  and byte-identical `scenarios.log` (`cmp` clean, same digest).

## Crash/hang containment self-test (proves the mechanism is live)

The harness has a dormant fault injector gated by `FUZZ10K_FAULT` (unset for
all evidence above). It kills or spins child process for batch 2 at id 137.

```sh
FUZZ10K_FAULT=crash timeout 600 ./tests/fuzz10k/containers/fuzz10k_containers
# stdout: FUZZ10K containers 10010 1 0 0 6649
# stderr: CRASH batch=2 signal=11 ... s_lines=9997 ...
# exit 1; batch_2.log stops at "S 136 ..."
FUZZ10K_FAULT=hang  timeout 600 ./tests/fuzz10k/containers/fuzz10k_containers
# stdout: FUZZ10K containers 10010 0 1 0 6649
# stderr: HANG batch=2 (killed) ... s_lines=9997 ...
# exit 1; killed at the 12 s deadline; batch_2.log stops at "S 136 ..."
```

These injected runs demonstrate that signal deaths and timeouts are detected,
attributed to a batch, and turned into a nonzero exit — the `0 0` in the real
run is an observation, not an untested code path. Fault-injection artifacts are
kept in `results/fault_*.log` / `results/fault_*.err` (gitignored).

## Status: evidence, not a proof

These are 10,010 deterministic, reproducible observations through defined
public entry points. They show that the checked operations refused the specific
adversarial inputs and that the checked canaries/invariants were intact in
these runs. They do **not** prove absence of defects for all program states,
all allocator histories, or concurrent use, and no live-game or injected-DLL
claim is made.

## Files (only under `tests/fuzz10k/containers/`)

- `fuzz10k_containers.cp` — the 10,010-scenario forked harness
- `run.sh` — reproducible build/run/log/digest driver
- `.gitignore` — ignores generated `results/` and the local binary
- `NOTES.md` — this file
- `results/` (gitignored): `batch_*.log` (per-batch scenario lines),
  `raw.log`, `raw.err`, `scenarios.log`, `summary.txt`, `digest.txt`,
  `fault_crash.log`/`.err`, `fault_hang.log`/`.err`

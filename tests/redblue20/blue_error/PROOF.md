# BLUE20 — Error handling and runtime liveness safety proof (agent 17/20)

Branch: `agent/modern-stdlib`. Scope: `tests/redblue20/blue_error/` only.
No source/header edits, no `git add`/`git commit`, no game assets used.

## Deliverables

| File | Purpose |
| --- | --- |
| `proof.cp` | C+ fork-per-case proof harness (2400 fixed-seed cases) |
| `run.sh` | Builds `proof.cp` and runs it under a hard 60 s wall-clock bound |
| `PROOF.md` | This evidence record |

## What is proved

`stdlib/error.hp` is exercised through its public API only. Six property
groups, 400 cases each (2400 total), dispatched by `case_index % 6`:

| Group | Cases | Invariant asserted |
| --- | --- | --- |
| 0 stale generation refusal | 400 | `stdlib_alloc_check_generation(p, g)` is true only for the live token; after `stdlib_realloc` the old generation is refused on both the new and the retired pointer. Wrong tokens, NULL and gen 0 are refused. |
| 1 recycled-address detection | 400 | A 128-block pool is allocated, freed and re-allocated so addresses are recycled. For every recycled address the new generation differs from the stale one and every stale pool generation is refused. At least one recycling must be observed in every case. |
| 2 metadata cache behavior | 400 | 96 mixed `stdlib_alloc`/`stdlib_calloc` blocks (table grows/rehashes past 48 entries and tombs accumulate). Checks `stdlib_alloc_size`, `stdlib_alloc_elem_size`, `stdlib_alloc_length`, `stdlib_alloc_query_length`, generation, then forget-half and re-verify live/forgotten state. |
| 3 error-callback thread safety | 400 | Per-thread re-entrancy guard drops a nested report from inside the callback (no recursion/stack overflow), and four managed threads calling `stdlib_note` each invoke the callback exactly once (atomic counter = 4). |
| 4 ownership transfer validation | 400 | Valid `stdlib_transfer_ownership` nulls the source, bumps the generation and invalidates the old token. Invalid handles (NULL dst, NULL src, NULL `*src`, non-NULL `*dst`, `dst == src`, freed source) are all rejected with no state change. |
| 5 telemetry consistency | 400 | With telemetry enabled: exact counters (`alloc_count=3`, `free_count=1`, `bytes_allocated=s1+s2+s3`, `alloc_failures=2`, `invalid_ops=1`, `ownership_violations=1`, `api_failures=4`, `container_growths=1`, zero bounds/lifecycle). With telemetry disabled/reset all counters stay zero. |

Determinism: every case is seeded with
`BASE_SEED ^ (case_index * 0x9e3779b9u)`, `BASE_SEED = 0x5EED1234u`, using
xorshift32 and bit-mask ranges only (no wall-clock, randomness, network or
timing dependence).

## Harness design

- `fork()` per case; the child runs exactly one case and reports PASS/FAIL via
  exit code (`_Exit`). No case forks after threads exist — threads are created
  only by the managed `thread` class inside group 3's own child.
- Parent installs a SIGALRM/`waitpid` watchdog (10 s per case, kills and reaps a
  hung child) and counts only `WIFEXITED && exit_code == 0 && !timed_out`.
- `run.sh` adds the hard outer bound `timeout 60` (with `gtimeout`/`perl`
  fallbacks). Real wall time measured: **5.7 s**, well inside the bound.
- Failure paths print `CASEFAIL <index> <code>` and are flushed before `_Exit`;
  a crash/hang is caught by the watchdog as `CASE_REAP_FAIL`.

## Exact results

Build (warnings only — the NULL/`dst==src` calls are intentional negative-input
tests and the `argv` extent notes are benign; no errors):

```
$ bash tests/redblue20/blue_error/run.sh
BLUE20 error 2400 2400 0
```

Exit status: **0**. No `CASEFAIL` / `CASE_REAP_FAIL` / `SPAWN_FAIL` lines were
emitted. Repeated three times with byte-identical output:

```
BLUE20 error 2400 2400 0
BLUE20 error 2400 2400 0
BLUE20 error 2400 2400 0
```

Final line: **`BLUE20 error 2400 2400 0`** (cases 2400, passed 2400, failed 0).

## Detector sensitivity (harness is not vacuously passing)

One assertion per property group was flipped in a temporary copy of
`proof.cp` (outside the owned tree), compiled and run. Each injected defect is
detected by exactly its own 400-case group, proving every group is really
exercised:

```
injected group   result
---------------- ------------------------
stale            BLUE20 error 2400 2000 400
recycle          BLUE20 error 2400 2000 400
meta             BLUE20 error 2400 2000 400
callback         BLUE20 error 2400 2000 400
owner            BLUE20 error 2400 2000 400
telem            BLUE20 error 2400 2000 400
```

The temporary mutated copies were deleted; only the three deliverables remain.

## Bottom line

All 2400 fixed-seed fork-per-case tests PASS under a bounded 60 s timeout, with
demonstrated per-property detection sensitivity. Error handling and runtime
liveness behavior of `stdlib/error.hp` is proven for the stated invariants.

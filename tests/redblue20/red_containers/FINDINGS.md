# RED-TEAM agent 2/20 — containers (string / vector / collections)

Branch: `agent/modern-stdlib`
Scope: **attack only**. No source or header was edited. All deliverables live in
`tests/redblue20/red_containers/`.

## Result

```
RED20 containers 6000 0 0 0 6000
SELFCHECK 5 5
SCENARIOS_SHA256 cc0a6c139973cb4342d859739bf89dc675f495246ad31f5b2586e4e0cab26c6b
```

| Class | Count |
| --- | --- |
| REFUSED (contract held — refusal or safe boundary behaviour observed) | 6000 |
| CRASH (signal / fatal exit / short verdict from forked child) | 0 |
| CORRUPT (canary / container invariant violated) | 0 |
| UNEXPECTED_OK (hostile input accepted where the contract promises refusal) | 0 |

**Genuine findings: NONE.** No crash, memory corruption, growth-overflow,
iterator-invalidation, double-drop or hash-collision failure was reproduced
across 6000 deterministic adversarial cases. The classifier itself was
non-vacuity-checked: `SELFCHECK 5 5` confirms an accepted outcome, a non-zero
child exit, a fatal signal, a canary violation and an out-of-range classifier
code each map to their intended bucket. Two consecutive campaigns produced
byte-identical `scenarios.log` files. Wall time ≈ 15 s (limit 60 s).

## Reproduce

From the repo root:

```sh
sh tests/redblue20/red_containers/run.sh
```

This builds with the repository C+ driver (`--default --sanitize=none -I.`),
runs the control, runs the campaign under `timeout 60`, prints the `RED20 ...`
summary, and writes the SHA-256 of the per-case log. The script always exits 0
on a completed campaign: findings are data, not exit codes.

Manual equivalent:

```sh
./build/c+ build tests/redblue20/red_containers/harness.cp \
  --default --sanitize=none -I. \
  -o tests/redblue20/red_containers/out/harness
REDTEAM_SELFCHECK=1 tests/redblue20/red_containers/out/harness  # SELFCHECK 5 5
tests/redblue20/red_containers/out/harness                      # RED20 ...
shasum -a 256 tests/redblue20/red_containers/scenarios.log
```

`out/`, `scenarios.log` and `scenarios.log.sha256` are gitignored by
`tests/redblue20/red_containers/.gitignore`.

## Harness design

* **Reachable boundaries.** `STDLIB_ALLOC_LIMIT` is pinned to 4096 *inside the
  harness* before any header include, so overflow, 70 %-load-factor rebuilds and
  oversized-key refusals are exercised without any 1 TiB allocation attempt.
  `SIZE_MAX` requests are still issued and still refused.
* **Determinism.** Fixed splitmix64 seed `0xC0FFEE123456789`; three consecutive
  draws per case. Family = `case_index % 19`, so every family gets 315–316 cases
  (6000 total, well above the ≥3000 requirement).
* **Isolation.** One `fork()` per case with a `pipe()` carrying a 12-byte verdict.
  The parent uses `waitpid()` and classifies signals, fatal exits, short reads
  and malformed verdict codes as `CRASH`; a crash in one child cannot hide the
  rest of the campaign.
* **Detection.** A per-child global canary (`0xA5`) is checked after every case,
  and — more importantly — each attack re-reads the container's public state
  (size, capacity, byte-exact contents, terminator, generation/staleness,
  out-parameter sentinels) after the hostile operation. Any deviation is
  `CORRUPT`; an operation that succeeded where the contract promises refusal is
  `UNEXPECTED_OK`.
* **Compiler workaround.** C+ method dispatch keys on the *variable name*
  across the translation unit: reusing `m` for both `intmap` and `strmap`
  misresolves `m.put(int)` to `strmap::put(const char *, int)`. Every container
  variable therefore has a class-unique name. This is a harness-side naming
  constraint, not a library defect.

## Coverage (19 families, 315–316 cases each)

| # | Family | Attack shape |
| --- | --- | --- |
| 0 | `STR_SELF_APPEND` | `append(c_str())` / suffix on both the reallocating and in-place overlap paths; byte-exact prefix/suffix/terminator verification |
| 1 | `STR_ALIAS_CLEAR` | capture interior pointer, `clear()`, then `append(p)` — the historically flagged overlapping-copy path |
| 2 | `STR_HUGE_RESERVE` | `reserve(SIZE_MAX)`, `reserve(LIMIT+1)` refusal; `reserve(LIMIT)` success with content preserved |
| 3 | `STR_OOR_ACCESS` | `substr` at `size+1`, `SIZE_MAX`, `out_size==0`, `NULL` out; out-of-range `field`; out-buffer sentinel |
| 4 | `STR_DOUBLE_DROP` | drop twice, then every read accessor, then re-initialising `append` |
| 5 | `STR_UAF` | post-drop `equals`/`compare`/`find`/`copy_from` with NULL and live sources |
| 6 | `VEC_INSERT_OOR` | `insert` at `size+1`, `size+2`, `SIZE_MAX`; size/capacity/order unchanged |
| 7 | `VEC_ERASE_EMPTY` | `erase`/`pop_back`/`at_checked`/NULL-out on empty vector; out sentinel intact |
| 8 | `VEC_HUGE_RESERVE` | `reserve(SIZE_MAX)`, `reserve(LIMIT/sizeof(int)+1)` refusal, exact-limit success |
| 9 | `VEC_ITER_INVALID` | slice before `push_back` (no realloc → fresh) and before `pop_back`/`clear`/`insert`/`erase`/`reserve`/`shrink_to_fit`/`copy_from`/`drop` (→ stale); stale `at_checked` refusal; `sub()` behaviour |
| 10 | `VEC_DOUBLE_DROP` | double drop, then all reads refuse and a subsequent `push_back` reuses storage |
| 11 | `INTMAP_COLLIDE` | 16–39 keys precomputed to collide under mask 1023 through every growth step; remove/reinsert/tombstone chains; double drop |
| 12 | `INTMAP_GROW` | 20–79 keys through repeated rebuilds; `grow(SIZE_MAX)` refusal without state change |
| 13 | `INTMAP_LIFECYCLE` | `clear` → refill → `drop` twice → post-drop `get`/`contains`/`remove` → reuse |
| 14 | `STRMAP_OVERSIZE` | keys of length `LIMIT..LIMIT+15` refused by `put`; existing entries intact; oversized `get` refuses |
| 15 | `STRMAP_COLLIDE` | 12–31 FNV-colliding owned keys; remove/reinsert/clear/refill; double drop |
| 16 | `DEQUE_WRAP` | repeated `push_front` wrapping the ring, paired `pop_front`/`pop_back`, cross-check against a closed-form model, then `reserve` linearisation and re-verification |
| 17 | `STACK_QUEUE_LIST` | LIFO/FIFO/linked-list push/pop boundaries, OOR reads, double drop, reuse |
| 18 | `FAIL_ATOMIC` | injected allocation failures at each allocation point of string append, vector growth/copy, intmap 3-buffer rebuild, strmap rebuild + key copy, deque ring copy, checking `rc == -1` and byte-exact state preservation (with a vacuity guard: a case whose injection never fired is not counted as success) |

## Notes / residual risks (not counted as findings)

1. During development an initial harness revision produced 288 `CORRUPT`
   verdicts in family 9. Root cause was a **harness** expectation error:
   `slice::sub()` on a stale slice is specified to return an *empty, non-stale*
   slice (`generation_ref == NULL`), not a stale one — this is recorded as
   `[VERIFIED]/[OBSERVED]` in `tests/stdlib/agent2_audit/module_audits/vector.hp.md`.
   The expectation was corrected; those verdicts were false positives, not
   library defects.
2. Code reading of `strmap::put` shows the table rebuild (three allocations)
   happens *before* the final `stdlib_copy_key` allocation. If a failure is
   injected exactly at the key-copy step, `put` returns -1 with logical contents
   unchanged but the internal capacity already grown. Capacity has no public
   accessor, so this is not observable through the API and was not counted as a
   finding; it is noted here as a latent documentation nuance.
3. ASan is unavailable in this toolchain, so classic use-after-free / heap
   overwrite classes are ruled out only indirectly (post-drop accessors return
   benign sentinels and per-container invariants hold). A borrowed
   `string_view` or `slice` outliving its owner remains a documented caller
   lifetime error the library cannot detect.

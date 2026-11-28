# RED-TEAM agent 3 — bounds / lifetime metadata safety

Branch: `agent/modern-stdlib`
Scope: **attack only**. No source or header was edited. All deliverables live in
`tests/redteam/bounds/`.

## Result

```
REDTEAM bounds 6000 6000 0 0 0
SELFCHECK 5 5
SCENARIOS_SHA256 a8d8f636e0764f27c53c3bb5f2354427bf7e4d3eede103638678f2f586d21a0e
```

| Class | Count |
| --- | --- |
| REFUSED (contract-honouring refusal observed) | 6000 |
| CRASH (signal / fatal exit / short result from forked child) | 0 |
| CORRUPT (canary changed or documented zeroing violated) | 0 |
| UNEXPECTED_OK (hostile input accepted where contract promises refusal) | 0 |

**Genuine findings: NONE.** No crash, corruption, or unexpected success was
observed across 6000 deterministic adversarial cases. The classifier itself was
non-vacuity-checked: `SELFCHECK 5 5` confirms that an accepted outcome, a
non-zero child exit, a fatal signal, a canary violation, and an out-of-range
classifier code are each mapped to their intended bucket.

## Reproduce

From the repo root:

```sh
sh tests/redteam/bounds/run.sh
```

This builds the harness with the repository C+ driver in strict mode
(`--strict --sanitize=none -I.`), runs the control, runs the campaign, prints
the `REDTEAM ...` summary line, and writes the SHA-256 of the per-case log.
The script always exits 0 on a completed campaign: findings are data, not exit
codes.

Manual equivalent:

```sh
./build/c+ build tests/redteam/bounds/bounds_redteam.cp \
  --strict --sanitize=none -I. \
  -o tests/redteam/bounds/out/bounds_redteam
REDTEAM_SELFCHECK=1 tests/redteam/bounds/out/bounds_redteam   # SELFCHECK 5 5
tests/redteam/bounds/out/bounds_redteam                       # REDTEAM ...
shasum -a 256 tests/redteam/bounds/scenarios.log
```

`scenarios.log` and `out/` are gitignored by `tests/redteam/bounds/.gitignore`.

## Harness design

* **Determinism.** Fixed splitmix64 seed `0x9E3779B97F4A7C15`; case parameters
  are three consecutive draws per case. Family = `case_index % 20`, so every
  family gets exactly 300 cases (6000 total, ≥5000 requirement met).
* **Isolation.** One `fork()` per case with a `pipe()` for the child's 12-byte
  verdict. The parent uses `waitpid()` and classifies signals, fatal exits,
  short reads, and malformed verdicts as `CRASH`. A crash in any child cannot
  hide the rest of the campaign.
* **No stale dereference.** A stale pointer is retained only as an integer
  address and passed to metadata-only queries (`*_size`, `*_generation`,
  `*_check_generation`, `span_ok`, `span_contains`, `bounds`, `key_current`,
  `query`, capability/identity views, `lifetime_check`, `lifetime_borrow_is_live`).
  Guarded copy/set/read/write helpers are called only with live destinations
  and hostile offsets/tokens, so refusal is the only legitimate outcome.
  Canaries (`0xA5` destination, `0x5A` source, `0xC3` read buffer) detect any
  illicit write. Forged "wild" addresses are constructed from a real static
  anchor (never an int-to-pointer cast) and are still never dereferenced.
* **Shared classifier.** Campaign and control use the same `classify_child()`
  function, so the control actually exercises the production classification
  path.

## Coverage (all 20 families, 300 cases each)

| # | Family | Attack shape exercised | Contract promise checked |
| --- | --- | --- | --- |
| 0 | `stdlib_mem_span_ok` | `off > size`, `off == size` + `n > 0`, `off = size-1` + oversized `n`, `off ≈ SIZE_MAX`, `n = SIZE_MAX` | `1` iff `[off, off+n)` fits; subtraction must not wrap |
| 1 | `stdlib_mem_span_contains` | interior pointer, one-past + `n>0`, last-byte + `n>0`, unrelated live block, below-base, wild address | `0` for out-of-range pointers; integer-only comparison |
| 2 | `stdlib_mem_bounds` | released base, stale interior, NULL, wild, stack address | `-1` and both out-parameters zeroed |
| 3 | `stdlib_mem_key_current` | stale token, token+1, `UINT64_MAX`, another block's generation, stale block with its own old key | `0` for a non-current key |
| 4 | `stdlib_mem_query` | off > cap, tail overrun, stale key, `SIZE_MAX` off/n, interior-as-base, released base | `STDLIB_FAIL` before any access |
| 5 | `stdlib_mem_capacity` / `stdlib_mem_count` | stale interior/base, NULL, wild, stack, elem sizes 0..4 | capacity `0`; count `-1` with `*out = 0` |
| 6 | `stdlib_alloc_identity` / `id_matches` / `id_equal` | stale interior/base, NULL, NULL out, forged generation, zero id | `-1`, zeroed output, `0` on generation mismatch |
| 7 | `stdlib_alloc_view_*` | interior, stale, `SIZE_MAX` offset, forged generation, interior/stale/aliased transfer source | `0` / refusal; no handle mutation on refused transfer |
| 8 | `stdlib_alloc_cap_*` | stale, interior, `SIZE_MAX` offsets, stale generation, NULL | `NONE` / `0` for non-live or out-of-range |
| 9 | `lifetime_check` / `lifetime_borrow_is_live` | stale borrow, stale token on live owner, zero token, NULL, post-transfer borrow | `STDLIB_FAIL` / `0` |
| 10 | `lifetime_owner_release` | double release, alias of released owner, NULL, forged token | `STDLIB_FAIL` + `CPLUS_EXIT_DOUBLE_FREE`, zero extra allocator frees |
| 11 | `lifetime_transfer` | self-transfer, non-empty dst, moved-from src, stale src, valid transfer then second transfer | handles unchanged on refusal; pre-transfer borrow stale |
| 12 | `lifetime_read` / guarded access on stale borrow | NULL descriptor, stale live-check, NULL destination, `SIZE_MAX` offset | `STDLIB_FAIL`, destination canary intact |
| 13 | `lifetime_read` / `lifetime_write` with live borrow | off = 65+, tail overrun, `SIZE_MAX` off, NULL dst/src | `STDLIB_FAIL`, both buffers unchanged |
| 14 | `stdlib_mem_set` / `copy` / `move` | off > cap, tail overrun, stale dst token, stale/NULL source, `SIZE_MAX` off, forged token | `STDLIB_FAIL`, destination canary intact |
| 15 | `stdlib_mem_transfer` | non-empty dst, NULL handle, aliased handles, stale src | `STDLIB_FAIL`, both handles unchanged, live block not freed |
| 16 | `stdlib_mem_release` | repeated cleanup, NULL handle | documented safe no-op; handle NULLed, no crash |
| 17 | recycled address | free then reallocate up to 32 times; old id/token/key/borrow vs new block | old generation/id must not match the recycled block |
| 18 | NULL arguments | all metadata helpers and lifetime entry points with NULL | documented refusal, outputs zeroed, no crash |
| 19 | huge values | `SIZE_MAX` offsets/sizes/lengths across span/query/views/memset/count | refusal, no wrap-around acceptance, canary intact |

## Findings that were NOT reported

Things that look like "accepted hostile input" but are explicitly documented
contract behaviour and therefore are **not** genuine findings:

1. `stdlib_mem_span_ok(base, size, size, 0) == 1` (one-past with a zero-length
   access) — documented in `memory.hp` as upstream-equivalent. The campaign
   deliberately uses `n > 0` for one-past probes.
2. `stdlib_mem_key_current(ptr, 0) == 1` and `token == 0` opting out — documented
   generation opt-out. The campaign never feeds `0` as an expected refusal.
3. `stdlib_mem_release(&p)` twice on the same (already NULLed) handle — documented
   idempotent cleanup. The double-release refusal contract belongs to
   `lifetime_owner_release`, which is attacked separately (family 10).
4. A stale *source* pointer whose address was recycled into a new tracked block —
   `memory.hp` documents that the API has no source-token parameter and cannot
   detect this logical aliasing. The harness never dereferences a stale source.
5. An interior pointer passed as `base` returning `0` for all metadata views —
   documented exact-base keying, and verified as refusal here.

## Development note (honesty about harness bugs)

During harness development two apparent positives were traced to mistakes in
the harness itself and fixed before the final campaign:

* A `lifetime_transfer` "unexpected OK" was caused by treating the *old* owner
  pointer value as a handle that must change; the contract only requires that
  the source be emptied and pre-transfer borrows go stale.
* 92 "CORRUPT" results came from a `mem_transfer` check that compared the
  untouched `dst` local against the saved value of `src` in the aliased-handles
  variant. The check was corrected to compare each handle to its own saved
  value; all 92 disappeared.

Neither was a target defect. No finding in this report came from a hidden or
invented reproducer.

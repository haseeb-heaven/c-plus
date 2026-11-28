# RED-TEAM: checked allocation APIs — FINDINGS

**Verdict: NO GENUINE FINDINGS.** Across 6,000 deterministic adversarial cases the
checked allocators refused every operation that the contract says must be
refused, and no crash, heap/metadata corruption, or unexpected success was
observed. Final tally:

```
REDTEAM alloc 6000 6000 0 0 0
```

## Targets (verified with `nm -gU` on both archives)

Preflight output is saved in `results/nm_safecrt.txt` and `results/nm_hardened.txt`.
Every required symbol is present as a global `T` in **both**
`build/safe/libcplus_safecrt.a` and `build/libc/libcplus_hardened.a`:

- safecrt: `malloc_s`, `calloc_s`, `realloc_s`, `free_s`, `free_checked_s`,
  `aligned_alloc_s`, `cplus_safe_alloc_get_meta`, `cplus_safe_ptr_is_live`,
  `cplus_safe_ptr_was_freed`, `cplus_safe_ptr_size`, `cplus_safe_ptr_elem_size`,
  `cplus_safe_ptr_count`, `cplus_safe_query_length`,
  `cplus_safe_ptr_check_generation`, `cplus_safe_remaining_capacity`,
  `cplus_safe_alloc_live_count`, `cplus_safe_alloc_live_bytes`,
  `cplus_safe_transfer_ownership`, `cplus_safe_alloc_fail_after`,
  `cplus_safe_alloc_fail_reset`, `cplus_safe_alloc_seq`.
- hardened: `cplus_malloc`, `cplus_calloc`, `cplus_realloc`, `cplus_free`,
  `cplus_free_checked`, `cplus_alloc_get_meta`, `cplus_alloc_is_live`,
  `cplus_alloc_size`, `cplus_alloc_live_count`,
  `cplus_alloc_double_free_count`, `cplus_alloc_invalid_free_count`,
  `cplus_alloc_fail_after`, `cplus_alloc_fail_reset`.

## Method

- `gen_cases.py` emits `results/cases.manifest` from a **fixed** seed
  (`0x9E3779B97F4A7C15`, xorshift64\* per case). 6,000 cases, 20 families × 300.
- `redteam_alloc.c` reads the manifest and executes **each case in a `fork()`ed
  child**, so a crash/corruption cannot contaminate later cases. The parent
  classifies by wait status: signal ⇒ `CRASH`; exit 0 ⇒ `REFUSED`; exit 10 ⇒
  `UNEXPECTED_OK`; exit 11 ⇒ `CORRUPT`; exit 20 ⇒ `SELFERR` (setup failure —
  must be 0; it is 0). Each child also writes a one-line detail string into a
  pipe, recorded in `results/scenarios.log`.
- Oracles are *contract-based* (from `stdlib/safe/safe_alloc.h`), not just
  "didn't crash": e.g. after a refused realloc the original pointer must still
  be live, have the original `byte_size`, and still contain the original byte
  pattern; a guard block must be unmodified; double-free must return
  `CPLUS_SAFE_EINVAL`; `calloc_s` overflow must set the block to NULL; etc.
- Attack values are drawn from adversarial pools including `SIZE_MAX`,
  `SIZE_MAX-1`, `RSIZE_MAX_MEM+1`, `(1<<63)+1`, `count*size` products engineered
  to overflow, 0/1 for alignment, interior/one-past/before/stack/static/TOMB/
  `0xDEADBEEF` forged pointers, and failure injection at `k = 0..5,8`.

## Attack families (all 20 × 300 = 6,000 cases REFUSED)

| # | Family | Attack | Oracle / result |
|---|--------|--------|-----------------|
| 0 | `OVR_MALLOC` | `malloc_s` with size > `RSIZE_MAX_MEM` | NULL returned every time |
| 1 | `OVR_CALLOC` | `count*size` overflow and > `RSIZE_MAX_MEM` products | NULL returned; zero args still yield a valid 1-byte block |
| 2 | `OVR_REALLOC` | huge size on a live block | NULL, original preserved (size + byte pattern), no leak of ownership |
| 3 | `ZERO_SIZE` | `malloc_s(0)`, `calloc_s(0,0)`, `calloc_s(0,n)`, `calloc_s(n,0)`, `aligned_alloc_s(align,0)` | defined zeroed 1-byte tracked block |
| 4 | `ALIGN_BAD` | alignment 0, odd, non-power-of-two, huge | NULL, `EINVAL` |
| 5 | `ALIGN_OK` | powers of two 8..4096 | genuinely aligned, bytes writable, tracked |
| 6 | `DOUBLE_FREE` | immediate double-free via both APIs; realloc of freed ptr | refused; block flagged freed |
| 7 | `INVALID_PTR` | TOMB, stack, static, interior, one-past, huge, `0xDEADBEEF` | refused; real block undisturbed |
| 8 | `REALLOC_ATOMIC` | injection at first realloc, injection after one success, invalid size, invalid ptr | original bytes and guard block intact; no unexpected success |
| 9 | `META_CONSISTENCY` | size/elem_size/count/generation across malloc/calloc/aligned/realloc | all query APIs agree; generation strictly increases |
| 10 | `QUERY_NULL` | NULL out-params and NULL pointers on metadata queries | `EINVAL`, no crash |
| 11 | `CYCLES` | 200–799 mixed alloc/free cycles | live count/bytes return to 0; every double-free refused |
| 12 | `STALE_GEN` | free then reallocate stale address | stale generation rejected; reused address gets new generation |
| 13 | `INJECT_ALL` | `fail_after(0..5,8)` across malloc/calloc/aligned/realloc(NULL); `fail_reset` | exactly k successes then failure, then recovery |
| 14 | `INTERIOR_CAP` | `remaining_capacity` base/interior/one-past/before/beyond | exact capacities and `(size_t)-1` sentinel |
| 15 | `FORGED_BATCH` | 20–119 fabricated pointers in one child, free + realloc each | all refused; real block intact |
| 16 | `MIXED` | 100–399 randomized alloc/free/verify ops over 48 slots, then reconcile with allocator counters | invariants hold; `live_count` matches |
| 17 | `REALLOC_ELEM` | realloc a `calloc_s` block with elem_size 1..16 | elem_size preserved, count = bytes/elem_size |
| 18 | `CACHE_EXHAUST` | 1,100–1,599 distinct blocks live at once, then free all and double-free each | no stale-cache collision ever accepted a double free; live count returns to 0 |
| 19 | `HARDENED` | direct attacks on `cplus_malloc`/`cplus_realloc`/`cplus_free_checked` | injection, forged pointer, double-free, zero-size, atomicity all correct |

## Notes on `cplus_realloc(p, RSIZE_MAX_MEM+1)` (family 19, subcase 0)

The hardened-archive allocator has no `RSIZE_MAX_MEM` cap; a huge realloc
therefore returns NULL via host OOM, which is still the refused outcome. The
harness accepts only NULL and additionally verifies the original block's bytes
and metadata are unchanged. No unexpected success occurred.

## Tooling caveat

An attempt was made to cross-check with an AddressSanitizer/UBSan build against
`build/asan/`. ASan is broken in this environment: a **trivial** standalone
fork+`malloc` program (no project code) fails at startup with
`AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189`. Therefore ASan
results are inconclusive and were not used as evidence. The harness's own
pattern/metadata/guard-block oracles provide the memory-integrity checks.

## Reproducibility / evidence

```
cd tests/redteam/alloc
./run.sh                          # regenerates manifest, log and digest
cat results/digest.sha256
```

`results/` is gitignored; `scenarios.log` and `cases.manifest` are entirely
regenerable from `gen_cases.py` + `redteam_alloc.c`. Two consecutive full runs
produced **byte-identical** `scenarios.log` (pointer values normalized to
`<ptr>` so ASLR cannot perturb the digest). Final digests:

```
e76b20ddd216248556df2499f7c0f8fea2e3860408244c8ddaba956a9a66042c  results/scenarios.log
26a319bd416d891a667e64fb74ef37eb9513b1fb0804b26326681c14da0edd66  results/cases.manifest
```

No findings to conceal; all 6,000 verdicts are `REFUSED` and are recorded in
`results/scenarios.log`.

REDTEAM alloc 6000 6000 0 0 0

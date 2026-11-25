# EDGE100 alloc — results

## Summary

```
EDGE100 alloc 117 1053 1053 0
```

- APIs covered: **117** (all public APIs in the seven target headers)
- Cases run: **1053** (9 edge categories per API)
- Passed: **1053**
- Failed: **0**

## Method

- Deterministic: fixed inputs, no PRNG, no time dependence.
- One forked child per `(api, category)` case; a fatal runtime error or
  signal is isolated to that child and classified as a failure.
- Each child sets a 32-byte global canary and an 8-byte stack canary
  around the operation; a modified canary fails the case.
- Build: `./build/c+ build tests/edge100/alloc/harness.cp --default
  --sanitize=none -I. -o <bin> -- build/libc/libcplus_hardened.a`.
- Run: `timeout 60 <bin>` (bounded wall clock).
- Child verdicts: PASS (0) / FAIL -- reason codes in `out/results.tsv`:
  1=pipe error, 2=fork error, 3=CRASH/signal/short write, 4=assert/canary.

## Per-header coverage

| Header | APIs | Cases | Passed | Failed |
|---|---:|---:|---:|---:|
| alloc_iface.hp | 5 | 45 | 45 | 0 |
| alloc_policy.hp | 4 | 36 | 36 | 0 |
| allocator.hp | 21 | 189 | 189 | 0 |
| arena.hp | 17 | 153 | 153 | 0 |
| datastack.hp | 19 | 171 | 171 | 0 |
| error.hp | 34 | 306 | 306 | 0 |
| mempool.hp | 17 | 153 | 153 | 0 |

## Findings (failed cases)

None.

## Artifacts

- `out/results.tsv` — one line per case (api id, name, category, verdict, reason).
- `out/manifest.tsv` — one line per API (id, header, name, cases, passed, failed).
- `out/stdout.log`, `out/stderr.log`, `out/build.log` — raw run/build logs.

EDGE100 alloc 117 1053 1053 0

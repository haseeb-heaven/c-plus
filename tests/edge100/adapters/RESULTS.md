# EDGE100 adapters -- results

```
EDGE100 adapters 47 233 233 0
```

47 public APIs, 233 fork-per-case edge cases, **233 passed, 0 failed**.

## Scope

Every public (`static inline`) API in:

- `stdlib/gsl_adapt.hp` — 29 APIs (3 refusals, 11 narrow, 5 not_null, 10 span)
- `stdlib/lifetime_adapt.hp` — 9 APIs
- `stdlib/alloc_iface.hp` + `stdlib/alloc_policy.hp` — the new MODERN-LAYER
  allocator helpers — 9 APIs

See `API_MANIFEST.md` for the full per-API enumeration and edge dimensions.

## Method

- One `fork()`ed child per case; each arms a 64-byte stack sentinel plus two
  64-byte tracked guard allocations and verifies all three afterwards.
- Child watchdog: `alarm(8)`. Runner bound: `timeout 60` on both build and run.
- Exit bits: `1` = expected verdict observed, `2` = canaries intact,
  `4` = the case genuinely reused a recycled address. Parent accepts only
  `(code & 3) == 3`; crashes, hangs and canary corruption fail the case.
- Fixed, deterministic inputs — no PRNG, no host-specific paths or keys.
- Build flags exactly as required:
  `--default --sanitize=none -I. -- build/libc/libcplus_hardened.a`.

## Commands

```sh
sh tests/edge100/adapters/run.sh
# equivalent manual form:
./build/c+ build tests/edge100/adapters/harness.cp \
    --default --sanitize=none -I. \
    -o tests/edge100/adapters/out/adapters_harness \
    -- build/libc/libcplus_hardened.a
./tests/edge100/adapters/out/adapters_harness
```

Artifacts: `out/build.log`, `out/results.log` (per-case / per-API lines),
`out/results.err`.

## Per-API results

All 47 API lines in `out/results.log` report `<total> <passed> <failed>` with
`failed = 0`. Totals: refusals 2/2 each; narrow 5–7 per width pair; not_null
3–4; span 2–7; lifetime 5–8; allocator helpers 2–6.

`REUSE 8` — all 8 dedicated recycled-address cases (span `at`, lifetime
`check`, `owner_release`, `transfer`, `borrow_is_live`, `owner_is_live`,
`read`, `write`) actually obtained the same address after release, so their
generation/token checks were exercised for real rather than falling back to
the untracked-address path.

## Observations / findings (not case failures)

1. **NULL borrow leaves `err` unset.** `lifetime_read` and `lifetime_write`
   (lifetime_adapt.hp:249, :271) return `STDLIB_FAIL` for a NULL `b` but do not
   call `error_fail_cat` on that branch, so a caller inspecting `err` sees no
   diagnostic. The operations are still fail-closed (no dereference, no copy)
   and the canaries confirm no corruption. The two NULL-borrow cases assert
   fail-closed behaviour rather than an error category; this is worth a future
   consistency fix in the header (out of this agent's file ownership).

2. **Span `at()` has no generation token.** On a recycled address with capacity
   >= the requested range, `gsl_span_i32_at` reads the *new* block
   (`stdlib_mem_check_range` validates capacity only). The header documents
   that a span does not extend the owner's lifetime, so case `d_span_at_6`
   asserts the observed no-crash/valid-range behaviour and this is recorded as
   a documented limit, not a defect. Lifetime descriptors, by contrast, use
   generation stamps and correctly refuse in all 7 recycled-address cases.

3. **Intentional edge inputs produce compiler warnings** (43 total: 37
   `CPLUS-LIFETIME`, 4 `CPLUS-BORROW`, 1 `CPLUS-REALLOC`, 1 clang linker-input
   notice). These are expected: the harness deliberately passes NULL, aliases
   `&src` to two parameters, and probes zero-size reallocation. No errors.

## Determinism and limits

Three consecutive runs produced `EDGE100 adapters 47 233 233 0` each time.
This is host-side evidence for the adapter/allocator-helper API surface only;
it makes no claim about live-game or injected-DLL behaviour.

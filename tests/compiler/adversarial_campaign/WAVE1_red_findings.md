# Wave 1 — red-team findings (verified against `build/c+`, `--strict --sanitize=none`)

All entries were produced by red agents in research-only mode (`/tmp` reproducers,
no repo edits). A finding is listed only if the agent recorded `check: ok rc=0`
AND a one-level control that IS rejected — i.e. a genuine summary blind spot,
not a checker no-op. Blue fix assignments are recorded inline.

## S1 — CRITICAL: CLI `-D`/`-I` never forwarded to in-memory AST analysis
- **Command:** `c+ check --strict --sanitize=none -DDANGER g03.c` → `check: ok rc=0`
  while the same macro baked into source → `[CPLUS-OWNER] double free` rc=1.
- **Cause:** `checkAstSafety()` receives `base` (`Driver.cpp:905`) = `backendArgs(opts)`,
  which excludes `opts.extraArgs`. Real compile (`:969`) includes `extraArgs`.
  An undeclared `-D` type makes the in-memory parse fail and `checkAstSafety`
  returns silently (`AstSafetyCheck.cpp:4619`) → every check skipped, rc=0.
- **Blue assignment:** `ses_df86e8e1affdX6SF0kaGGAjMpf` (Driver.cpp owner).

## S2 — HIGH: free-effects not propagated through helper chains / indirect calls
- `userFrees_` only for direct `free(parm)` (`:3565-3570`). 2-level relay and
  function-pointer/struct-callback frees accepted (s01, s02, s15, s21).
- **Blue assignment:** AstSafetyCheck owner (fixpoint over call graph).

## S3 — HIGH: returned-alias summary only direct `return p;`
- `scanRet` (`:2007-2103`) ignores `return helper(p);`; out-params unhandled.
  UAF + heap OOB accepted (s03, s10, s17). **Blue:** AstSafetyCheck owner.

## S4 — HIGH: nullability summary misses callee-local aliases / wrappers / `p+0`
- `DerefFinder` (`:2194-2228`) only direct deref of the param. s04, s05, s16, s20.
  **Blue:** AstSafetyCheck owner.

## S5 — HIGH: function-pointer/callback calls get no interprocedural treatment
- `getDirectCallee()` null for indirect calls; effect summaries keyed by empty name.
  **Blue:** AstSafetyCheck owner.

## S6 — MEDIUM: global-pointer effects collected but never used for alias/release
- `:2338-2341` used only for race heuristic. s06, s14. **Blue:** AstSafetyCheck owner.

## S7 — MEDIUM: multi-TU analysis per-file; extern effects invisible
- `mt_main.c`+`mt_helper.c` both `check: ok`. Known gap, still STRICT-accepted.
  **Blue:** Driver owner (at minimum STRICT disclosure).

## S8 — MEDIUM: dynamic-index OOB via self/mutual recursion accepted
- `s08`, `s09`: `f(10)` reads `a[10]` on `int a[4]`. `check` skips runtime
  instrumentation (`Driver.cpp:911`). **Blue:** StaticBoundsCheck owner.

## S9 — LOW/MED: out-param allocation and helper-computed sizes not summarized
- s07, s23. `evalByteCount` (`:509`) purely syntactic. **Blue:** AstSafetyCheck owner.

## S10 — LOW: arithmetic-effect summary one level deep
- s22. **Blue:** AstSafetyCheck owner.

## S11 — LOW: v2 contract fields parsed but not enforced; `CPLUS_CONTRACT_PATH` unvalidated
- Only `nullableArg` used (`:3662`). **Blue:** AstSafetyCheck owner.

## S12 — INFO: `--legacy-c` overrides `--strict`
- All checks gated on `Mode::Safe` (`Driver.cpp:903-906`). Documented; flag-risk only.

## P1 — HIGH: provenance/alias laundering (string-keyed state)
- Integer round-trip `u=(uintptr_t)p; q=(int*)u; free(p); *q` passes (alias group
  {q,u}, u not released). memcpy of pointer bytes, union member punning
  (`u.p` freed, `*u.i` read), struct-field store (`s.p=q; free(q); *s.p`),
  and cast-hidden alias (`(int*)(void*)p`, varNameOf at :2886) all pass.
- Chained one-past `a+1+4` then `*p`; tagged pointer `u|2` (a `free(p)` removes
  the only leak warning that previously made it rc=1).
- Recorded STRICT holes: cross-allocation sub/compare wrapped in `+1`
  (`objectDeclOf` :2760), funcptr type confusion, restrict violation with
  distinct alias names, const-cast write through helper/copy.
- **Highest-leverage fix:** allocation-identity + provenance tokens instead of
  pure name keys; narrow STRICT "unknown provenance on deref ⇒ error".
  **Blue assignment:** AstSafetyCheck owner (provenance) + runtime owner
  (generation/epoch tagging for stale-alias-after-reuse).

## T1 — HIGH: temporal / indirect-call / runtime wrong-free
- Indirect `free` via fnptr `void(*f)(void*)=free; f(p); free(p);` passes
  STRICT: first call stays raw `free`, second is tracked → genuine libc
  double-free (F1). Same for indirect realloc (F4), struct/global/callback
  fnptr frees. Fix: treat indirect calls receiving pointers as unverified
  releases in STRICT; resolve fnptr targets or fail closed.
- RUNTIME ACTIVELY FREES WRONG ALLOCATION (F2, worst finding): stale alias at a
  reused address makes `cplus_free_registered` → `cplus_find_live` match the
  NEW live allocation and free it. Recent cache is never consulted on a live
  hit. Fix: address-only live hit is not proof of identity; generation/epoch
  token, check recent-free cache before live lookup.
- `uintptr_t` round-trip stale read prints deterministic wrong value (F3);
  memcpy'd pointer alias UAF (F5) and global `void*` handle free (F6).
- **Blue assignment:** runtime owner (F2), AST owner (F1/F3/F5/F6).

## A1 — HIGH: arithmetic / conversion guards bypassed
- Stale non-zero proof: `if(d){d=zero();return 100/d;}` clean but divides by
  zero (`provablyNonZero` ignores `assignedVars_`). Same for loop lower bound.
- Compound assignments `+= -= *= /= %= <<= >>=` have NO guard/diagnostic
  (distinct opcodes, missed by site recorder + static checks).
- `++`/`--`, unary minus UB unchecked; implicit narrowing and float→int
  conversions unchecked; runtime malloc-size overflow loses heap extent.
- **Blue assignment:** AST owner (detection) + CPlusRewriter owner (emission,
  preserving lvalue semantics for compound ops).

## MEASURED baseline (`verify_bypasses.py`, current `build/c+`)

Parent ran all embedded reproducers against the real binary with
`c+ check --strict --sanitize=none`. Raw results:
`tests/compiler/adversarial_campaign/bypass_results.BEFORE.{json,txt}`.

- **16 / 21 reported bypasses are PRESENT** (accepted, rc=0). Confirmed:
  S1/S2/S3/S4/S6/S8, P1 int-launder + union-pun + tagged, T1 fnptr-free +
  indirect-realloc, A1 stale-nonzero + compound + inc/dec + implicit-narrow +
  float→int.
- **2 predicted bypasses were NOT present** (already rejected): `memcpy` pointer
  alias (`[CPLUS-FFI] sizeof(p) is pointer size`) and cast-hidden alias
  (`[CPLUS-SAFE-LIFETIME] use-after-free`). The ledger above cites the agents'
  predictions; the measured reality is that these two are already handled.
- **3 / 3 controls correctly rejected** → harness is live, not a no-op.

This baseline is the "before" snapshot. After blue fixes land and `c+` is
rebuilt, re-run `verify_bypasses.py` to measure which bypasses are closed.

## Controls correctly rejected (prove the harness is live)
`c01_direct_df`, `c02_direct_null`, `c03_direct_alias`, `c04_direct_oob`,
`s11_extent_single`, `s18_launder_cast_df`, `s19_global_df`, `s22b_arith_direct`,
`s24_write_helper`, `s24b_write_helper2`, `g03b`.
`s11_extent_conflict` / `s12_fnptr_bounds` fail closed with
`[CPLUS-FFI] ... unverifiable extent` (correct behaviour).

# Adversarial campaign — wave 2 (measured)

Wave 2 ran five red agents and five blue agents on `agent/core-safety`, after
wave 1 was pushed as `3ebd44a`. Every number below is measured, not predicted.

## Headline result

`tests/compiler/adversarial_campaign/verify_bypasses.py` on the same host and
binary path, `c+ check --strict --sanitize=none`:

```
wave-1 baseline: 16/21 reproducers accepted (bypasses present)
wave-2 result :  3/21 reproducers accepted
closed by wave 2: 13 (cumulative from the original baseline)
controls: 3/3 rejected in both waves
```

Wave 2 newly closed: `S2_fnptr_free`, `S6_global_alias_uaf`,
`T1_fnptr_double_free`, `A1_compound_assign`, `A1_incdec`, plus every
wave-1 case that had remained open at the start of the wave except the three
listed below. Raw data:
`tests/compiler/adversarial_campaign/bypass_results.BEFORE_WAVE2.json` and
`bypass_results.json`.

## The three still accepted by `check` — with runtime proof

| case | `check` | `build --strict --sanitize=none` then run |
| --- | --- | --- |
| `A1_compound_assign` | accepted | **rc 110**, `C+ runtime error: signed integer overflow: addition result does not fit in a 32-bit signed integer` |
| `A1_implicit_narrow` | accepted | **rc 111**, `C+ runtime error: implicit integer conversion: ... changed it to a 8-bit signed value` |
| `A1_float_to_int` | accepted | **rc 111**, `C+ runtime error: floating-point conversion: floating-point value does not fit in a 32-bit signed integer` |

So these three are **static-analysis gaps, not unguarded runtime code** for the
measured reproducers. Do not read "accepted by check" as "safe": `check` is
static-only by design and emits no instrumentation.

## Critical defects fixed during the wave

1. **Checker crash on valid C.** `void(*get(void))(void*){return free;}`
   followed by `get()(p)` aborted with `dyn_cast on a non-existent value`
   (`Casting.h:656`) inside `scanRet`, because `return free;` yields a
   `DeclRefExpr` naming a `FunctionDecl`, not a `VarDecl`. Guarded with
   `vd ? dyn_cast<ParmVarDecl>(vd) : nullptr`. Verified: no assertion, no stack
   dump; now rejected cleanly.
2. **`__int128` abort.** `(__int128)1<<100; a+a` reached an unchecked
   `getZExtValue()`. Added `fits64`/width guards at all narrowers. Verified:
   rc 0, no abort (128-bit arithmetic remains unguarded — documented gap).
3. **Malformed C for nested arithmetic.** `a+b+c` emitted unbalanced
   parentheses and failed to compile. Fixed insertion ordering and
   full-tuple dedup. Verified: builds and runs, correct result (6).
4. **Late float→int guard.** `(int)d` was checked *after* conversion
   (`(double)((int)d)`), silently accepting `1e30`. Root causes: missing `int`
   word in `parseTokenType`, and `EndOfFile` rejected by `isTypeSpelling`.
   Verified emitted form is now `((int)__cplus_inline_f2i_i64((double)(d),32,1))`
   and out-of-range input traps rc 111.
5. **Configuration gates.** `-include`/`-imacros` AST divergence,
   `-fsanitize-recover=` and `-Wno-error=` downgrades, evadable multi-TU
   disclosure, and `CPLUS_ROOT` runtime substitution are now gated in STRICT.
6. **RAII control flow.** Conditional explicit drops, longjmp/exit/abort with
   live owners, copy-by-return double-free, alias drops, managed global escape,
   switch-continue cleanup, and double explicit drops are diagnosed or fixed.

## Regression repairs (all verified before this report)

- Rejecting `cplus_realloc` results as "opaque pointers" broke the legitimate
  `realloc_chain` fixture; runtime/allocator helpers are now exempt while a
  genuine `extern void *get_handle(void)` still fails closed.
- Macro fail-closed was too broad (`INT_MAX - b`); named limit macros are now
  accepted, runtime macro arithmetic still fails closed.
- AllocExtent token recovery misread `malloc(sizeof *p)`; factor detection now
  rejects `sizeof`/dereference operands.
- Guards in enum constant expressions broke `enum { X = -1 }`; enum ranges and
  literal unary minus are skipped.
- The new explicit-drop guard emitted `cplus_zero` without a declaration; the
  rewriter prelude now declares it (Safe mode only).
- The new bodiless-extern hard error rejected a valid multi-TU positive
  fixture; definitions in other command-line inputs or sibling TUs with a
  header prototype now suppress it, while a source-local `extern void
  evil_free(void*)` stays rejected.

## Full verification

```
repo tests      109 passed, 0 failed
core safety     152 passed, 0 failed
agent1          159 total, 156 PASS, 0 FAIL, 3 SKIP
CTest           2/2
metrics         90/90 safe corpus accepted, 0 false positives,
                STRICT bug detection 97/107
UAF evidence    12/12
runtime memory  28/28, 0 failure entries
ABI safety      33/33
differential    125 comparisons, 0 mismatches
compatibility   21/21
optimizer       11/11
upstream ports  20/20
memory compare  14/14
fuzz            0 mismatches
source preserve 4/4; real-world C 6/6
C standards     56 cases, 48 pass, 0 fail, 8 unsupported (c23)
ABI real-world  36/36
benchmarks      35 measurements, 0 failures
wave-2 regress  19 cases, 0 anomalies, 0 failures, 3 KNOWN_OPEN
```

`alloc_churn` c+ safe release is **1.28 s** (pre-campaign 0.98 s, 1.31×),
with identical stdout; the rejected 31.6× prototype was removed in wave 1.

## Non-claims

- This is not Rust-level safety and not a proof of UB freedom.
- No general UAF, race, provenance or cross-TU ownership guarantee.
- `check` accepting a case is not evidence that `build` is unsafe, and vice
  versa; both stages are reported separately.
- F2 (stale free at a reused address) remains open: closing it requires a
  pointer-borne generation token or an ABI change that was explicitly declined.
- 128-bit arithmetic, macro/header arithmetic and several libc contracts
  remain documented gaps.

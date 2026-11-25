# Adversarial campaign — wave 3 (measured)

Third red/blue round on `agent/core-safety`, after wave 2 was pushed as
`deafe46`. Ten agents worked in parallel: five red agents probing check/build
divergence, RAII residuals, runtime temporal gaps, configuration gates and
real-world C regressions; five blue agents hardening disjoint files plus a
compatibility keeper. All numbers below are measured on this host.

## Result

The parent harness (`verify_bypasses.py`, `c+ check --strict --sanitize=none`)
still reports **16/21 accepted at the original baseline → 3/21 now**, with
3/3 controls rejected. Wave 3 did not reopen any of the 13 closed cases; its
value is in new bug classes it found and fixed. The three still-accepted
cases remain the runtime-guarded ones (compound 110, implicit narrowing 111,
float→int 111) — see `docs/compiler/adversarial-wave2.md`.

## Critical defects found and fixed

1. **`@response` files defeated every gate.** Clang expands `@file` after the
   driver's `extraArgs` checks, so `@rsp` containing `-include guard.h` with a
   no-op `__cplus_bounds_check` made `check` rc=0 and built binaries perform a
   silent OOB (rc=0 instead of 134). Now rejected in STRICT.
2. **Double-dash flag aliases** (`--include=`, `--includeevil.h`,
   `--imacros=`) evaded single-dash prefix checks. All gates now normalize one
   leading `-`.
3. **`-D` redefining a helper** (`-D__cplus_bounds_check=cplus_no_check`)
   erased the emitted guard only in the compiled TU. Reserved `-D/-U` names are
   rejected in STRICT.
4. **`CPLUS_RUNTIME_LIB`** could link a no-op runtime. STRICT now uses only the
   compiled-in/installed library.
5. **Guard helpers were interposable:** a user-defined
   `__cplus_checked_add_i64` produced rc 7 with wrapped overflow. Reserved
   helper names are now a hard STRICT error.
6. **Nested conversions emitted invalid C** (`(char)(int)d` gave 5-arg/3-arg
   helper calls); now rejected statically with a precise diagnostic.
7. **Macro conversions silently returned wrong values** (`CI(d)` →
   `r=2147483647`); STRICT now fails closed on instrumentable macro
   conversions, while constant macros stay accepted.
8. **A lying v2 contract** suppressed the bodiless-extern hard error; pointer
   functions claiming no dangerous effect are no longer trusted in STRICT.
9. **Shipped `libc_contracts.json` failed its own validator** because the
   boolean `optional` field was checked as an array. Fixed; contract loading
   now works.

## RAII and runtime

- Stale `while`/`do` loop frames caused `continue`/`break` to skip owner
  cleanup; cast/parenthesized explicit drops bypassed the zeroing guard;
  conditional `defer` suppressed the automatic drop; indirect termination
  aliases and forward `goto` over an owner were undetected. All now handled,
  with the remaining cross-TU termination-helper case documented.
- Re-registering a retired address could re-arm a real double free (SIGTRAP);
  caller-supplied sizes could authorize a 1 MiB overread from a 4-byte block;
  the retired ledger silently degraded at its cap. Now: retired-address
  rejection, `malloc_size`/`malloc_usable_size` clamping, round-robin eviction
  with honest introspection, and `free()` under the side-table lock. F2
  (stale free at a reused live address) still requires an ABI change and was
  not faked.

## Real-world C

`tests/compiler/real_world_c/wave3/run_suite.py`: 12 pinned projects, 6 repos,
180 cells (3 modes × 5 standards). Final: **175 PASS, 5 MODE_REJECTED,
0 HARD_FAIL**; every built cell's stdout and exit match clang. Two wave-2
regressions were found and fixed: injected C99 `inline` broke `-std=c89`
(tinyexpr, JSMN), and a file ending `}\n\n\n` lost two trailing newlines.
Both are now correct; 60/60 legacy cells preserve source bytes exactly.
Full detail: `docs/compiler/real-world-wave3.md`.

## Full verification

```
repo tests      109 passed, 0 failed
core safety     152 passed, 0 failed
agent1          159 total, 156 PASS, 0 FAIL, 3 SKIP
CTest           2/2
metrics         90/90 safe corpus, 0 FP, STRICT detection 97/107
UAF evidence    12/12
runtime memory  28/28
ABI safety      33/33
differential    125 comparisons, 0 mismatches
compatibility   21/21
optimizer       11/11
upstream ports  20/20
memory compare  14/14
fuzz            0 mismatches
source preserve 4/4; real-world C 6/6; real-world wave3 175/180, 0 hard fail
C standards     56 cases, 48 pass, 0 fail, 8 unsupported (c23)
ABI real-world  36/36
benchmarks      35 measurements, 0 failures
wave-2 regress  19 cases, 0 anomalies, 0 failures
```

`alloc_churn` c+ safe release is **1.40 s** (1.43× pre-campaign 0.98 s),
identical stdout. The rejected 31.6× prototype is still gone.

## Non-claims

Three `check`-mode gaps remain (compound/narrowing/f2i) but their `build`
counterparts trap (110/111/111). F2 needs an ABI change. No general UAF, race,
provenance, C23 or UB-free guarantee. STRICT mode is stricter than DEFAULT by
design; compatibility results are single-host and snapshot-specific.

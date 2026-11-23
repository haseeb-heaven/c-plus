# FINAL AUDIT — Agent 1 compiler safety (2026-11-23, integrated run)

## Scope

Twenty parallel agents were run in two batches of ten: (1) implementation and
defect work, (2) mandatory upstream-reuse study. File ownership was disjoint;
only the parent integrated, rebuilt, and verified. No agent committed or pushed.

## Components inspected

27 compiler source files (Driver, Main, Lexer, Rewriter, StaticBoundsCheck,
AstSafetyCheck, RaiiLowering, OwnershipAnalysis, TypeChecker, Diagnostics),
`runtime/cplus_runtime.c`, CMakeLists, 14 test directories, 10 upstream
projects at pinned commits. Architecture and inventories are in `baseline.md`,
`compiler_architecture.md`, `safety_inventory.md`, `diagnostic_inventory.md`,
`test_inventory.md`, `known_gaps.md`, `SAFETY_MATRIX.md`.

## Defects fixed in this integration

1. **Runtime-variable signed overflow (P1, now closed).** Native checked
   add/sub/mul with exact source-width checks (`CPLUS_EXIT_ARITHMETIC=110`)
   are inserted for non-constant signed expressions under
   `--strict --sanitize=none`. Unsigned wrapping, pointer subtraction and
   foldable constants are untouched. Reproducer changed from printing
   `-2147483648` with rc 0 to a fatal diagnostic with rc 110.
2. **Performance regression from fix 1 (found by the perf agent, closed).**
   The first implementation called an out-of-line runtime helper on every
   operation: nested-loop benchmark 0.17s → 11.16s (58x). The generated C now
   emits inline wrappers using `__builtin_{add,sub,mul}_overflow`, calling the
   runtime helper only on the trap path. Re-measured `nested` safe_release /
   legacy_c_release = **0.941x** (no >2x regression remains); outputs match
   across debug/release/legacy.

## Final verification results (integrated tree)

```
Agent 1 full suite      : TOTAL 159  PASS 156  FAIL 0  SKIP 3
Dormant-fixture suite   : TOTAL 111  PASS  40  FAIL 0  BLOCKED 71
Raw-UAF evidence suite  : 12/12 PASS (6 static rejects, 4 ASan-only escapes, 2 safe controls)
Repo tests              : 109 passed, 0 failed
Core safety suite       : 152 passed, 0 failed
CTest                   : 2/2 passed
Metrics                 : safe 90/90 DEFAULT and STRICT, FP 0/0,
                          detection 80/107 DEFAULT, 97/107 STRICT,
                          clang -Werror clean 51/51
Performance             : no config regressed >2x; release/debug/legacy stdout equal
```

The 3 SKIPs are documented contract exclusions (helper TUs linked from their
main TU; `uaf_conditional.c` is a possible-UAF fixture). The 71 BLOCKED entries
in the dormant suite are fixtures with no pinned expectation; they are reported
with explicit reasons and never counted as PASS.

## Upstream reuse outcome

`docs/compiler/upstream-reuse.md` records 17 pinned upstreams, their licences,
the actual implementation/test files inspected, and a feature-by-feature
decision. No upstream source was copied. Clang/LLVM APIs are linked; algorithms
from Checked C, Apple Bounds Safety, SoftBoundCETS, Clang Static Analyzer, GCC
analyzer, Cake (docs only), Cyclone and CHERI were studied and only adapted as
compiler-side facts. GPL/GPLv3 and no-licence projects were research-only; Cake
also has an AI-training-use addendum.

## Remaining gaps (not hidden)

1. Runtime-variable overflow is now trapped, but the checks are inserted only
   for expressions the AST checker can classify as signed integer arithmetic;
   some unmodelled forms remain. The inline wrapper falls back to the runtime
   helper for widths other than 32/64 bits.
2. General data races, cross-TU ownership ABI, raw `malloc/free` UAF without
   ASan (4 of 12 evidence cases escape STRICT), field/container element
   lifetimes with runtime indices, and full pointer provenance remain
   unsupported (`known_gaps.md`).
3. Verified false positive: `tests/core/positive/thread_guard_ok.cp` emits
   `[CPLUS-FFI] multiple threads mutate shared global 'shared' without
   synchronization` despite a RAII guard. The AST layer only recognises
   `mutex_lock`/`mutex_unlock` callee names and does not inline the RAII helper.
   It is a warning, not a build failure; fixing it requires recognizing
   same-TU lock helpers (P0 of the race audit).
4. Diagnostic tag shapes remain inconsistent; `.err` fixtures pin message
   substrings only, so normalization is documentation-bound, not
   fixture-bound (`docs/compiler/safety-diagnostics.md`).
5. 71 dormant fixtures remain BLOCKED pending expectations.
6. Pre-existing robustness defect: in Safe mode, `c+ check` hangs on a source
   with an unbalanced brace (reproduced with the older baseline compiler too;
   legacy mode rejects it normally).
7. macOS CI now targets `macos-15` with a concurrency group, but no green run
   on the new label has been observed. SonarCloud scope files are corrected but
   `.sonarcloud.properties` takes effect only from the default branch.

## Second hardening pass (same day)

Ten additional parallel agents studied actual upstream implementations of
division/shift, conversion, pointer-arithmetic and interprocedural bounds
propagation (Checked C, Apple Bounds Safety, LLVM/Clang, Clang SA, GCC, GSL,
C2Rust, SoftBoundCETS, CHERI). Findings and pins are in
`docs/compiler/upstream-reuse.md`.

Further confirmed defects and fixes:

1. **Array-decay diagnostic accuracy**: a stack array crossing a function
   boundary was labelled "heap allocation 'p'". Extents now carry an origin
   (`Heap`/`Stack`/`Global`/`Unknown`) and the message reads
   `array index 4 exceeds stack array 'p' of 16 bytes`. Bounds were never lost:
   the case is rejected in DEFAULT and STRICT with `--sanitize=none`; generated
   C is unchanged ordinary C.
2. **`--sanitize=integer` silently disabled native arithmetic guards**: the
   substring test for `signed-integer-overflow` also matched
   `unsigned-integer-overflow`. Whole tokens are now compared; verified
   `--sanitize=integer` traps natively (110), `--sanitize=undefined` uses UBSan
   (134), `--sanitize=none` traps natively (110).
3. **Generated-C portability**: div/rem wrappers no longer inject `__int128`
   into user-visible C (`INT32_MIN`/`INT64_MIN` from `<stdint.h>` instead).
4. **Address-taken locals folded as constants**: `scanf("%d", &x)` left `x`
   equal to its initializer, suppressing every native guard. Address-taken
   locals are now excluded from constant folding.
5. **Native guards extended** beyond `+ - *` to `/ % << >>`, value-changing
   narrowing conversions (exit 111), and out-of-bounds pointer formation with
   one-past formation allowed (exit 101). Predicates are adapted from Clang's
   `EmitUndefinedBehaviorIntegerDivAndRemCheck`/`EmitShl`,
   `EmitIntegerTruncationCheckHelper`, GSL `narrow`, and LLVM
   `BoundsChecking.cpp`; no UBSan runtime is used.

Verification after this pass: native matrix **24/24 PASS** (6 cases × 4
configurations), repo 109/0, core 152/0, CTest 2/2, Agent 1 suite 156 PASS /
0 FAIL / 3 SKIP, metrics 90/90 + 0 FP + 51/51 clang-clean.

## P0 enforcement pass (temporal safety)

Ten parallel agents mapped the five P0 requirements onto the exact code
surface (UAF/double-free, CFG ownership, pointer/bounds, alias, cross-function
lifetime) and inspected upstream implementations at pinned commits.

**Confirmed defect:** all four previously escaping UAF shapes passed
`check --strict --sanitize=none`: stale pointer passed to a callee, `void*`
cast hiding an alias, alias returned by a helper, and runtime-indexed array
element after `free(slots[0])`.

**Fixed (single-TU, direct-callee scope):**
- cast-transparent alias resolution (`castedVarName`), plus read-side transfer
  through `lvalueKey` in `isReleased`;
- `returnedParam_` now seeds alias groups for `q = identity(p)`;
- released pointer passed to any non-allocator/non-liveness call is an error
  in STRICT and a warning in DEFAULT (`cplus_check_live`/`cplus_realloc`
  exempted after a measured false positive on `quarantine_probe.c`);
- conservative runtime-index element rule when a constant-index element of
  the same base was released.

Verification: UAF evidence suite **12/12** — 10 unsafe cases statically
rejected (was 6) and 2 safe controls accepted/runtime-clean; no sanitizer
needed for the new rejections. Full suites remain green: repo 109/0, core
152/0, CTest 2/2, Agent 1 156 PASS / 0 FAIL / 3 SKIP, native matrix 24/24,
metrics 90/90 with 0 false positives in both modes.

**Still not claimed:** complete temporal safety across indirect calls,
integer round-trips, heap address reuse, threads, cross-TU ownership,
containers/fields, or arbitrary pointer provenance. CFG-based ownership and
symbol/region-keyed state remain the next, larger migration; the Clang
`runUninitializedVariablesAnalysis` replacement for definite-init is P1 work.

## Final verdict

PARTIALLY COMPLETE. The audit, all inventories, the native runner, upstream
reuse study, defect fixes, generated-C evidence, mode matrix and regression
evidence are complete and verified. Meaningful safety work remains (items
1–7). No complete-memory-safety claim is made.

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

## Final verdict

PARTIALLY COMPLETE. The audit, all inventories, the native runner, upstream
reuse study, defect fixes, generated-C evidence, mode matrix and regression
evidence are complete and verified. Meaningful safety work remains (items
1–7). No complete-memory-safety claim is made.

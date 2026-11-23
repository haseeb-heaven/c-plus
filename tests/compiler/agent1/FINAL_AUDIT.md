# FINAL AUDIT — Agent 1 compiler safety (2026-11-23)

## Components inspected
27 compiler source files (Driver, Main, Lexer, Rewriter, StaticBoundsCheck,
AstSafetyCheck, RaiiLowering, OwnershipAnalysis, TypeChecker, Diagnostics);
runtime/cplus_runtime.c; CMakeLists; 14 test directories/scripts. Architecture
and inventories are in baseline.md, compiler_architecture.md,
safety_inventory.md, diagnostic_inventory.md, test_inventory.md, known_gaps.md.

## Defects reproduced and fixed during prior rounds
- array-to-pointer decay lost extents across direct and nested calls (fixed:
  stack-array extents, call-site propagation, return summaries);
- arithmetic overflow/divide/shift accepted under STRICT;
- RAII goto/longjmp cleanup bypass, field alias UAF, double fclose,
  integer-to-pointer provenance, format string CWE-134.

## Tests executed (final full run)
```
TOTAL 159  PASS 156  FAIL 0  SKIP 3
```
Categories: compile_fail (58), runtime_fail (5, executed with expected exit
codes and stderr), positive compat DEFAULT (45), STRICT positives (~37),
native harnesses (44 cases + 40-test suite), stress (2 packs), core runner,
legacy (2), sanitizer matrix (4). Three SKIPs are documented contract
exclusions (helper TUs; `uaf_conditional` possible-UAF).

Separate baseline: repo 109/0; core 152/0; CTest 2/2; measure 90/90 safe C,
0 FP, 97/107 STRICT bug detection.

## Modes
- DEFAULT: compat fixtures build+run; proven errors rejected; unverified ops warn.
- STRICT: safe-subset positives build+run; proven/unverified unsafe ops rejected;
  sanitizers not required.
- LEGACY: acceptance confirmed; explicitly not memory safe.

## Generated C
Captured per-test under `results/generated/`. No safety metadata is emitted
into C for proven violations (they are rejected before code generation);
dynamic index checks are injected for fixed arrays. Release build verified
(`--release` build rc=0); perf_debug.txt records debug/release timing.

## Remaining defects / limitations
1. Runtime-variable signed overflow is accepted with `--sanitize=none` (no
   native check inserted). Reproducer: `volatile int a=INT_MAX; return a+1;`
   with runtime-dependent value. Component: AstSafetyCheck arithmetic.
   Severity P1. Next action: insert native checked-arithmetic calls.
2. General data races, cross-TU ownership ABI, raw malloc/free UAF in default
   builds, field/container element lifetimes: unsupported (known_gaps.md).
3. 11 top-level safety fixtures + 100 subdirectory sources have no active
   assertion runner (test_inventory.md). Severity P2.
4. macOS CI job uses retired `macos-14` label; SonarCloud gate still fails on
   intentional unsafe test fixtures. External/process blockers.

## Final verdict
PARTIALLY COMPLETE. All completion criteria for audit, inventories, runner,
test execution, generated-C inspection, mode matrix and regression evidence
are met; meaningful safety work remains (items 1-3 above). No complete-memory-
safety claim is made.

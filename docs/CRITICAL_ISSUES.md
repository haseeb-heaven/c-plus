# Critical issues — honest status and evidence

This document answers the six concerns without claiming more than the evidence
supports.

## 1. Safety coverage is not complete

**True.** No suite proves that all unsafe cases are detected. New evidence:
`python3 tests/metrics/mutation_evidence.py` applies truth-labelled mutations
to 91 safe programs (48 mutants). Detection rate is reported per family
(zero-size allocation, shrunk array, missing free, removed null guard) in
`tests/out/metrics/mutation/report.txt`. Families with low detection are
explicit gaps, not hidden. The 28 gaps in `bugs51_97` are also listed by name.

## 2. Library coverage is incomplete

**True.** `python3 tests/metrics/libc_coverage.py` inventories libc functions
used in the corpus against the 29 entries in `libc_contracts.json` and writes
`docs/LIBC_API_GAP.md`. 27 frequently used functions lack a contract entry;
some have dedicated AST checks instead, but the contract surface is incomplete.
Extending it requires agreed semantics with Agent 2 and new tests.

## 3. Integration is not fully proven

Partially addressed: `tests/core/integration_suite.py` builds a **two-TU**
program end-to-end (C+ lowering, runtime `cplus_alloc`/`cplus_release`,
contract-aware checks, sanitizer flags, link, execution) in four sanitizer
configurations and asserts exact output plus a clean run. This proves those
specific paths work together; it is not a whole-program proof. Cross-TU
ownership ABI remains unsupported.

## 4. Test coverage is not safety

**True.** Passing suites prove only the tested cases. Mutation evidence,
documented gaps and the honest safe-subset spec are the closest available
evidence. No claim of absence of untested bugs is made.

## 5. CI unresolved failures

`safe_lib_errno.h` is **not referenced anywhere in the compiler repository**;
it belongs to Agent 2's `libc/**`/safeclib integration and their `libc-crt`
workflow, which I must not modify. The compiler workflow
`.github/workflows/compiler.yml` was rerun repeatedly: **Linux x86-64 now
passes** (build, CTest, core suite). macOS arm64 jobs are cancelled by the
runner with zero steps executed, so macOS CI green is not claimed; local macOS
verification is green. SonarCloud remains the blocking remote check.

## 6. Code quality

SonarCloud reports 1,283 PR issues; the largest contributors are intentional
unsafe test fixtures (`tests/safety`, `tests/metrics`) and
`compiler/Sema`/`compiler/Driver` style/security rules. A `sonar-project.properties`
file now limits the quality gate to production sources (`compiler`, `runtime`,
`tools`) and excludes test corpora from duplication analysis, because those
fixtures deliberately contain duplicated, unsafe code. This is a measurement
scope correction, **not** a claim that compiler-source issues are fixed; the
remaining source-level findings still need work and the gate must be rechecked.

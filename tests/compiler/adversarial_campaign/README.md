# Adversarial compiler-safety campaign

Continuous red-team / blue-team campaign on branch `agent/core-safety`.

- Red team: 5 agents hunt for concrete bypasses that pass
  `c+ check --strict --sanitize=none` and produce provable unsafe behaviour.
- Blue team: 5 agents harden disjoint compiler/runtime areas in parallel.
- Integration: parent builds, runs full suites, adds permanent regressions and
  commits each verified wave. No "secure" claim without evidence.

Wave 1 launched 2026-11-23: 5 red (spatial, temporal, alias/provenance,
interprocedural, arithmetic/instrumentation) and 5 blue (AstSafetyCheck,
CPlusRewriter/SemaCPlus, runtime, StaticBoundsCheck, OwnershipAnalysis/Driver).

## Parallel workstream (real-world C / C standards)

While wave 1 runs, a separate 10-agent mission audits real downloaded C
projects and C89→C23 support with disjoint ownership:
`tests/compiler/c_standards/`, `tests/compiler/real_world_c/`,
`tests/compiler/abi_real_world/`, and `docs/compiler/real-world-compatibility.md`.
Integration remains parent-owned; no claim of universal C-standard support is
made before execution evidence.

## Verified parallel-workstream results (parent re-ran each)

- `tests/compiler/real_world_c/run_suite.py`: 6 pinned projects (TheAlgorithms/C
  @ `e5dad3fa`), **6/6 passed**, 6/6 source byte-identical after `#line`,
  6/6 c+ vs clang stdout+exit match. Re-ran independently: rc=0.
- `tests/compiler/c_standards/run.py`: **56 cases, 48 passed, 0 failed,
  8 unsupported** (all `c23`, rejected by both c+ default backend and Apple
  clang 16; clang-23/GCC-14 can select c23). Source preservation 56/56.
- `tests/compiler/abi_real_world/run_suite.py`: **36/36 passed** (12 cases ×
  3 modes), oracle 24/24, exact stdout + `nm` symbol sets. Re-ran: rc=0.
- Live 5-project probe (`/tmp/cresearch`, CC0/Apache/Unlicense/MIT projects):
  stdout+exit match clang for all 14/15 c+ builds; the one failure is a
  default-standard difference (c+ legacy defaults gnu89 vs clang gnu17),
  not a lowering defect — adding `-- -std=c99` fixes it. Honest limitation.
- ABI audit finding: existing suites are same-backend; cross-compiler ABI
  claims need mixed-object linking, which `abi_real_world` now partly covers.

## Wave 1 integration (measured, 2026-11-23)

`verify_bypasses.py` before/after on the same host and binary path:
**16/21 accepted -> 6/21 accepted (10 bypasses closed).**
Closed: S1 flag divergence, S2 relay free, S3 returned-alias chain,
S4 null wrapper, S8 self-recursion OOB, P1 integer laundering, P1 union
punning, T1 indirect realloc, A1 stale non-zero proof, A1 inc/dec.
Still open: S2 fnptr free, S6 global alias UAF, T1 fnptr double-free,
A1 compound assignment, A1 implicit narrowing, A1 float->int.

Regression repairs during integration (all verified before commit):
- AST realloc fixpoint produced 3 false positives on the standard
  `tmp=realloc(p,..); if(tmp)p=tmp;` / `if(!q){free(p);return;}` idioms;
  fixed at root (contract ownership transfer skip + branch-aware state).
- agent1's `sp_quarantine_probe` asserted the OLD buggy stale-alias
  acceptance; now expects the correct 105 runtime trap.
- `runtime_memory_safety/cast_oob` upgraded from asan_only(accept) to
  static_reject; `uaf_evidence/use_after_realloc` wording updated but still
  requires a hard reject. Both are stronger expectations, not weakened.

Full suite state after repairs: repo 109/0, core 152/0, agent1 156/0/3,
CTest 2/2, metrics 90/90 accepted with 0 FP and STRICT detection 97/107
(was 96/107), UAF evidence 12/12, runtime memory 28/28, ABI 33/33,
differential 125/125, optimizer 11/11, fuzz 0 mismatches, memory-safety
compare 14/14, source preservation 4/4, real-world C 6/6, C standards
48 passed/0 failed/8 unsupported, ABI real-world 36/36.

KNOWN ISSUE under repair: runtime never-reuse policy regressed alloc_churn
from 0.98s to 31.02s (31.6x). Runtime owner is revising it. Do not commit
wave 1 until performance is restored and re-measured.

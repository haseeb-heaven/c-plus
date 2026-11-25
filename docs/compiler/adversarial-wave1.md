# Adversarial campaign — wave 1 integration report

Date: 2026-11-23. Branch: `agent/core-safety`. All results below are
re-measured from checked-in result files; nothing is estimated.

Sources of truth:

- `tests/compiler/adversarial_campaign/WAVE1_red_findings.md` (red findings,
  assignments, baseline)
- `.../bypass_results.BEFORE.{json,txt}` (before snapshot)
- `.../bypass_results.{json,txt}` (after snapshot)
- `.../README.md` (integration, repairs, full-suite state)
- `.../wave2/regression_suite.py` + `summary.{txt,json}` (permanent suite, this wave)
- `docs/compiler/benchmarks-rust-cplus.md`,
  `benchmarks/rust_vs_cplus/results/summary.txt` (performance)

## 1. Methodology

1. **Wave 1 launched** with 5 red agents (spatial, temporal, alias/provenance,
   interprocedural, arithmetic/instrumentation) and 5 blue agents (AstSafetyCheck,
   CPlusRewriter/SemaCPlus, runtime, StaticBoundsCheck, OwnershipAnalysis/Driver)
   in disjoint ownership. Red agents worked research-only (`/tmp` reproducers,
   no repo edits).
2. A finding was admitted only if the reproducer returned `check: ok rc=0`
   **and** a one-level control was rejected — i.e. a genuine summary blind spot,
   not a checker no-op.
3. Minimal reproducers were embedded in `verify_bypasses.py`. The parent ran the
   full 21-case matrix against the real `build/c+` with
   `c+ check --strict --sanitize=none` (S1 additionally with `-DDANGER`),
   before blue fixes (`bypass_results.BEFORE.*`) and after the rebuild
   (`bypass_results.*`), same host and binary path. The 21 cases = 16 measured
   open bypasses + 2 already-rejected cases + 3 liveness controls.
4. **Wave 2 blue-defender (regression keeper)** converted the measurement tool
   into an asserting suite at
   `tests/compiler/adversarial_campaign/wave2/regression_suite.py`. Each closed
   case must be rejected **with its expected diagnostic category tag**
   (`[CPLUS-OWNER]`, `[CPLUS-LIFETIME]`, `[CPLUS-BOUNDS]`, `[CPLUS-ARITH]`);
   rc alone is not accepted. Open cases are recorded non-fatally as `KNOWN_OPEN`
   (or `NEWLY_CLOSED` if a later wave fixes them). Controls must be rejected.
   There is no `|| true` and every subprocess has a hard 60 s timeout. The
   process exits 0 only if all 10 closed cases and all 3 controls pass their
   category assertion; open cases never fail the suite.

Reproduce:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/adversarial_campaign/wave2/regression_suite.py
```

## 2. Before / after

| Metric (identical command matrix) | Before | After |
| --- | --- | --- |
| Accepted by STRICT (bypasses present) | **16 / 21** | **6 / 21** |
| Rejected | 5 / 21 (2 already handled + 3 controls) | 15 / 21 (10 newly closed + 2 already handled + 3 controls) |
| Controls rejected | 3 / 3 | 3 / 3 |

Ten bypasses closed, six remain. (The two already-rejected cases before wave 1 —
`P1_memcpy_alias_uaf` and `P1_cast_alias_uaf` — are not counted as wave-1 closes.)

| Case | Before | After | After category / diagnostic |
| --- | --- | --- | --- |
| S1_flag_divergence | accepted | **rejected** | `[CPLUS-LIFETIME]` + `[CPLUS-OWNER]` |
| S2_relay_free | accepted | **rejected** | `[CPLUS-OWNER]` |
| S2_fnptr_free | accepted | accepted | **OPEN** |
| S3_return_chain | accepted | **rejected** | `[CPLUS-LIFETIME]` |
| S4_null_wrapper | accepted | **rejected** | `[CPLUS-LIFETIME]` |
| S6_global_alias_uaf | accepted | accepted | **OPEN** |
| S8_self_recursion_oob | accepted | **rejected** | `[CPLUS-BOUNDS]` |
| P1_int_launder_uaf | accepted | **rejected** | `[CPLUS-LIFETIME]` |
| P1_memcpy_alias_uaf | rejected | rejected | `[CPLUS-FFI]` (already handled) |
| P1_union_pun_uaf | accepted | **rejected** | `[CPLUS-LIFETIME]` |
| P1_cast_alias_uaf | rejected | rejected | `[CPLUS-LIFETIME]` (already handled) |
| T1_fnptr_double_free | accepted | accepted | **OPEN** |
| T1_indirect_realloc | accepted | **rejected** | `[CPLUS-LIFETIME]` |
| A1_stale_nonzero | accepted | **rejected** | `[CPLUS-ARITH]` |
| A1_compound_assign | accepted | accepted | **OPEN** |
| A1_incdec | accepted | **rejected** | `[CPLUS-ARITH]` |
| A1_implicit_narrow | accepted | accepted | **OPEN** |
| A1_float_to_int | accepted | accepted | **OPEN** |
| CTRL_direct_double_free | rejected | rejected | `[CPLUS-OWNER]` |
| CTRL_direct_null | rejected | rejected | `[CPLUS-LIFETIME]` |
| CTRL_direct_overflow | rejected | rejected | `[CPLUS-ARITH]` |

## 3. Closed cases — exact diagnostics

Verbatim error text from `bypass_results.txt` / `.json`:

1. **S1_flag_divergence** — `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: released pointer 'p' passed to 'consume'`; also `[CPLUS-OWNER] double free after call to 'consume' which frees its argument` (`-DDANGER`).
2. **S2_relay_free** — `[CPLUS-OWNER] double free of 'p'`.
3. **S3_return_chain** — `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: dereference of released pointer 'q'`.
4. **S4_null_wrapper** — `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] null pointer argument 0 passed to 'wrap'`.
5. **S8_self_recursion_oob** — `[CPLUS-BOUNDS] array index 10 from recursive call is out of bounds of array 'a' of length 4`.
6. **P1_int_launder_uaf** — `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: dereference of released pointer 'q'`.
7. **P1_union_pun_uaf** — `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: dereference of released pointer 'u.i'`.
8. **T1_indirect_realloc** — `[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: dereference of released pointer 'p'`.
9. **A1_stale_nonzero** — `[CPLUS-ARITH] division by zero cannot be excluded`.
10. **A1_incdec** — `[CPLUS-ARITH] signed integer overflow in unary minus of minimum value`.

## 4. Still-open cases (KNOWN_OPEN)

All six return rc=0 with stdout `check: ... ok` — no diagnostic. They are
recorded, not failed, by the wave-2 suite:

- **S2_fnptr_free** — free via function pointer then direct free (F1 family).
- **S6_global_alias_uaf** — global-pointer alias released via helper, then read.
- **T1_fnptr_double_free** — indirect `free` via `void(*f)(void*)=free`.
- **A1_compound_assign** — overflow in `+=` / `-=` (distinct opcodes).
- **A1_implicit_narrow** — `long` → `char` implicit narrowing.
- **A1_float_to_int** — out-of-range `double` → `int` conversion.

## 5. Regression repairs during integration

From `tests/compiler/adversarial_campaign/README.md`, all verified before commit:

- The AST realloc fixpoint produced **3 false positives** on the standard
  `tmp=realloc(p,..); if(tmp)p=tmp;` / `if(!q){free(p);return;}` idioms; fixed
  at root (contract ownership-transfer skip + branch-aware state).
- agent1's `sp_quarantine_probe` asserted the **old buggy** stale-alias
  acceptance; now expects the correct **105** runtime trap.
- `runtime_memory_safety/cast_oob` upgraded from `asan_only(accept)` to
  `static_reject`; `uaf_evidence/use_after_realloc` wording updated but still
  requires a hard reject. Both are stronger expectations, not weakened.

## 6. Performance regression history (`alloc_churn`)

Measured in `docs/compiler/benchmarks-rust-cplus.md` and
`benchmarks/rust_vs_cplus/results/summary.txt`:

| Stage | `alloc_churn` median |
| --- | --- |
| Pre-campaign baseline | **0.98 s** |
| Prototype "never reuse retired addresses" policy | **31.02 s (31.6×)** |
| After reverting that policy (current) | **1.21 s** (1.23× baseline), identical stdout `alloc_churn 9000021000000` |

Current per-configuration medians: c+ safe release **1.210 s / 1,785,856 B
RSS**, legacy release 1.150 s, debug 1.240 s; rustc -O 0.170 s, opt-level=0
0.330 s. The 31× policy was removed, not accepted: it could not close the
stale-free-after-reuse case without an ABI change and the cost was unjustified.

## 7. Full suite results after repairs (measured, from `README.md`)

repo **109/0**; core **152/0**; agent1 **156 PASS / 0 FAIL / 3 SKIP**; CTest
**2/2**; metrics **90/90** accepted with **0 FP** and STRICT detection **97/107**
(was 96/107); UAF evidence **12/12**; runtime memory **28/28**; ABI **33/33**;
differential **125/125**; optimizer **11/11**; fuzz **0 mismatches**;
memory-safety compare **14/14**; source preservation **4/4**; real-world C
**6/6**; C standards **48 passed / 0 failed / 8 unsupported**; ABI real-world
**36/36**.

Parallel workstream (parent re-ran each): `real_world_c` 6/6 passed, 6/6
source byte-identical after `#line`, 6/6 c+ vs clang stdout+exit match (rc=0);
`c_standards` 56 cases, 48 passed / 0 failed / 8 unsupported, source
preservation 56/56; `abi_real_world` 36/36 (12 cases × 3 modes), oracle 24/24
(rc=0). Live 5-project probe: 14/15 c+ builds matched clang stdout+exit; the
single failure was a default-standard difference (c+ gnu89 vs clang gnu17),
fixed with `-- -std=c99`, not a lowering defect.

Wave-2 regression suite, run with `CPLUS_BIN=$PWD/build/c+`:

```
closed_asserted_rejected=10  closed_failed=0
controls_asserted_rejected=3 controls_failed=0
known_open=6  newly_closed=0  anomalies=0  total=19
SUITE PASSED: True
```

Exact commands and full diagnostics are in
`tests/compiler/adversarial_campaign/wave2/summary.{txt,json}`.

## 8. Explicit non-claims

- **No general Rust parity.** Only seven named benchmark programs were
  measured. On `alloc_churn` c+ safe is 1.21 s vs rustc -O 0.170 s (~7× slower,
  side-table bookkeeping); on `bounds_sum` ~3× slower in the first runner. RSS
  ratio C+/Rust is 0.89–0.92. No cross-compiler ABI parity claim: existing ABI
  suites are same-backend; mixed-object linking is only partly covered by
  `abi_real_world`. No universal C-standard claim: c23 cases are unsupported.
- **No UB-free guarantee.** Six bypasses remain open; STRICT detection is
  **97/107**, not 100%. Checks are conservative and incomplete; `--legacy-c`
  overrides `--strict`; multi-TU analysis is per-file and extern effects are
  invisible (S7 gap). The suite asserts categories for the 10 closed cases and
  liveness of 3 controls only — it does not prove absence of other bugs.
- **F2 needs an ABI change.** The runtime wrong-free / stale-free-after-reuse
  defect cannot be closed without generation/epoch tagging (address-only live
  lookup is not identity). The "never reuse retired addresses" prototype was
  reverted at 31.02 s (31.6×); F2 remains open pending ABI work.

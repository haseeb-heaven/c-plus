# Agent 1 issue status (verified on `agent/core-safety`)

Status is based only on implemented, tested and evidenced requirements. Nothing
here claims total memory safety or Rust parity.

## Closed with evidence

| Issue | Evidence |
|---|---|
| #25 | `tests/compiler/libc_contract_matrix/run_suite.py` 29/0; NULL-path leak assertion in `tests/core/run.sh`; genuine leak, interior free and realloc negatives |
| #26 | `tests/compiler/borrow_escape/` 25/25 |
| #28 | `tests/raii_shadowing/` 11/11 |
| #31 | ABI gate 5/5; `abi_ownership` 42/42 (201 steps) |
| #34 | `tests/compiler/default_policy/` 14/14 |
| #36 | `tests/compiler/return_escape/` 4/4 (`this` escape) |
| #37 | `tests/compiler/return_escape/` 4/4 (struct-field local escape) |
| #38 | `tests/compiler/proof_regressions/` 6/6 (double-pointer + array-element alias UAF) |
| #39 | `tests/compiler/proof_regressions/` 6/6 (struct downcast OOB) |
| #40 | `tests/compiler/proof_regressions/` 6/6 (unproven runtime-offset deref) |
| #46 | `tests/compiler/nested_method/` 2/2 |

## Open with partial implementation

| Issue | Verified part | Remaining |
|---|---|---|
| #47 | Function-pointer calls to throwing helpers rejected (`tests/compiler/throw_alias/` 2/2) | Cross-TU extern throwing functions are still outside the per-TU proof |
| #48 | Callee-side destructor emitted for by-value resource parameters | Ownership transfer from caller must be modelled to avoid double-drop; not complete |

## Process/platform items

| Issue | Status |
|---|---|
| #21, #23, #29, #30, #32, #33 | Compiler-side evidence is extensive (58/58 semantic, 20/20 strict rules, ABI gate, Windows PE compile-only, Linux/macOS CI jobs), but final closure requires green SonarCloud, independent human review and the combined release gate. CI workflow and helper scripts are now pushed; GitGuardian test-credential finding was redacted and the alert resolved. |

## Final local gate (latest verified compiler)

```
repo          149 passed, 0 failed
core          163 passed, 0 failed
ctest          2/2
agent1         198 PASS, 0 FAIL, 3 SKIP (201 cases)
semantic        58/58
data_race        9/9
proof1_uaf       4/4
proof_regressions 6/6
return_escape    4/4
nested_method    2/2
throw_alias      2/2
ABI gate         5/5
abi_ownership   42/42
```

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
| #21 | Strict inference, contracts, 20/20 strict rules, 58/58 semantic, ABI gate; local full gate green |
| #23 | Compiler/SDK evidence: ABI gate 5/5, clean SDK smoke, Linux/macOS compiler CI; combined-tree libc 109/0 and CRT all checks |
| #29 | Declaration-identity/provenance/RAII AST conversions; `ast_identity` 9/9, `pointer_provenance` 14/14; remaining token decisions documented in `ast_identity_audit.md` |
| #30 | `strict_rules` 20/20 rules, 40/40 fixtures, adversarial addition 10/10, semantic 58/58 |
| #32 | Linux x86-64/arm64 and macOS arm64 CI jobs; Windows PE compile-only experimental; `docs/compiler/windows_wine_highball_evidence.md` |
| #33 | CI workflow, toolchain probes, ABI gate and helper scripts pushed; combined merge conflict set resolved; independent verification reports in `/tmp/issue*_final.md` |

## Open with partial implementation

| Issue | Verified part | Remaining |
|---|---|---|
| #47 | Function-pointer calls to throwing helpers rejected (`tests/compiler/throw_alias/` 2/2) | Cross-TU extern throwing functions are still outside the per-TU proof |
| #48 | Callee-side destructor emitted for by-value resource parameters | Ownership transfer from caller must be modelled to avoid double-drop; not complete |

## Process/platform items

All Agent 1 process/platform issues are closed on verified evidence. GitGuardian
check-run gating was skipped by operator instruction; the underlying historical
test credential was redacted and the GitHub secret-scanning alert resolved.

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

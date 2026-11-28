# Follow-up audit: SafeCC dataflow diagnostic expectations (issues #180/#223-226)

Verified on this worktree with `build/c+` (rebuilt 09:58 local time). No files
outside `compiler/Sema/AstSafetyCheck.cpp` and `tests/compiler/agent1/` were
changed for this follow-up. No build, commit, push or issue closure was done.

## 1. Reported DIAGNOSTIC_MISMATCH cases do not reproduce here

Harness command (the `.err` expectation lives at `tests/compile_fail/<name>.err`,
NOT inside `tests/compiler/agent1/`):

```sh
bash tests/compiler/agent1/run_all.sh --category compile_fail --test cf_bugcat46
bash tests/compiler/agent1/run_all.sh --category compile_fail --test cf_classic_bug6_null
bash tests/compiler/agent1/run_all.sh --category compile_fail --test cf_null_deref
```

Result: `TOTAL 1  PASS 1  FAIL 0  SKIP 0` for each of the three cases.

Reason: `StaticBoundsCheck.cpp` emits
`[CPLUS-SAFECC] definite null dereference of '<name>': null pointer dereference on every path to this access`,
which still contains the expected substring `null pointer dereference`. The
wording quoted in the follow-up (`pointer is NULL on every path to this
access`) does not occur in this worktree's sources; the gate that reported it
must have used a different `StaticBoundsCheck.cpp`/binary.

If that other gate's wording is authoritative, the portable expectation for
those three `.err` files is the stable fragment
`[CPLUS-SAFECC] definite null dereference of` (that file is outside the edit
scope granted for this follow-up, so it was not changed here).

## 2. `05_managed_churn` false leak does not reproduce here

```sh
./build/c+ check --strict --sanitize=none --no-werror \
  tests/stress/cplus_breaker_v3/05_managed_churn.cp
```

emits `[CPLUS-FFI] arithmetic in a macro expansion cannot be instrumented`
(not a `definite memory leak`), and the actual stress runner passes:

```sh
bash tests/stress/cplus_breaker_v3/run.sh
# C+ Breaker V3: 25 passed, 0 failed
```

The quoted leak message is emitted by `reportFlowLeak()` in
`compiler/Sema/StaticBoundsCheck.cpp`, which is outside the granted edit set
(`compiler/Sema/AstSafetyCheck.cpp` and `tests/compiler/agent1/` only). A
loop-scoped `managed int *a = cplus_new_array(...)` fix therefore cannot be
made in `AstSafetyCheck.cpp`; it needs either permission to edit
`StaticBoundsCheck.cpp` or a re-run of the gate against this worktree's binary.

## 3. Suite counts obtained in this worktree

- `tests/compiler/agent1/run_suite.py` equivalents above: 3/3 pass.
- `tests/stress/cplus_breaker_v3/run.sh`: 25 passed, 0 failed.
- `tests/compiler/feature_safecc/run_suite.py`: 22 cases, 22 passed, 0 failed.
- `tests/compiler/ast_safety_issues_223_226/run_suite.py`: 14 cases, 14 passed,
  0 failed.
- `tests/compiler/proof_regressions/run_suite.py`: 6 cases, 0 failures.
- `tests/compiler/safety_breakers/run_suite.py`: 16 checks, 0 failures.

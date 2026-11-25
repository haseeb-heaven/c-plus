# Agent 2 audit — CURRENT (post-fix) baseline

## Provenance

- Repository: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
- Branch: `agent/modern-stdlib`
- HEAD SHA: `11831a2740d167b5c14586e490e6348c271f4481`
- Date recorded: `2026-11-22T23:28:12Z` (UTC); local date Mon 2026-11-23.

**This is the CURRENT (post-fix) baseline, not the pre-change baseline.** It is a
snapshot of the tree at the HEAD SHA above, after the Agent 2 fixes landed. It is
not evidence of the state before those fixes. Earlier per-defect before/after
evidence lives in `docs/stdlib/DEEP_*_CONTRACT.md` and `docs/libc/AUDIT_*.md`;
consult those documents for pre-fix reproductions and fix verification.

All commands below were actually executed from the repository root with this
environment:

```
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
CPLUS_CLANG=/usr/bin/clang
```

Raw per-command output was preserved under
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/agent2_baseline/`.

## Command results (exact command, exit code, final output line)

| # | Exact command | Exit code | Final output line |
|---|---|---:|---|
| 1 | `env PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin CPLUS_CLANG=/usr/bin/clang sh tests/run_tests.sh` | 0 | `C+ tests: 150 passed, 0 failed` |
| 2 | `env PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin CPLUS_CLANG=/usr/bin/clang sh tests/libc/run_libc_tests.sh` | 0 | `libc tests: 85 passed, 0 failed` |
| 3 | `env PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin CPLUS_CLANG=/usr/bin/clang sh tests/crt/unit/run_unit_tests.sh` | 0 | `crt unit tests: 80 passed, 0 failed` |
| 4 | `env PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin CPLUS_CLANG=/usr/bin/clang sh tests/crt/run_crt_tests.sh` | 0 | `CRT corpus: 10 passed, 0 failed` |
| 5 | `env PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin CPLUS_CLANG=/usr/bin/clang CPLUS_BIN=$PWD/build/c+ sh tests/stdlib/run_deep_tests.sh` | 0 | `deep stdlib tests: 84 passed, 0 failed, 0 skipped (28 file(s) x 3 modes)` |
| 6 | `env PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin CPLUS_CLANG=/usr/bin/clang CPLUS_BIN=$PWD/build/c+ sh tests/stdlib/compile_time/run_compile_time.sh` | 0 | `note: RUNTIME cases are compiler capability gaps and must be covered by runtime suites separately.` |
| 7 | `env PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin CPLUS_CLANG=/usr/bin/clang sh tests/family_gaps/run_family_tests.sh` | 0 | `family suites: 20 passed, 0 failed` |
| 8 | `env PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin CPLUS_CLANG=/usr/bin/clang sh tests/integration/run_integration.sh` | 0 | `integration suite: 8 passed, 0 failed` |
| 9 | `env PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin CPLUS_CLANG=/usr/bin/clang sh tests/run_all_evidence.sh` | **1** | `evidence aggregate: 21 passed, 2 failed` |

Command 5 per-mode table (from its output): `default 28/0/0`, `strict-none 28/0/0`,
`ubsan 28/0/0` (PASSED/FAILED/SKIPPED), no BUILD-FAIL/RUN-FAIL/OUT-FAIL.

Command 6 summary lines (its last line is the note quoted above, not a pass count):

```
compile-time suite (separate from runtime):
  REJECT: 6/9 as expected
  ACCEPT: 19/19 as expected
  RUNTIME: 24/24 as expected
  total gap cases: 27 (see docs/libc/COMPILE_TIME_GAPS.md)
```

The 27 capability/tagging gaps are reported by the runner but are **not** counted
as passes; `RUNTIME` cases are compiler-capability gaps that must be covered by
runtime suites. Command 6 exited 0 despite these gaps by design.

## Exact failures in command 9 (`tests/run_all_evidence.sh`)

This command was executed. It finished with **exit code 1** and the aggregate line
`evidence aggregate: 21 passed, 2 failed`. These are the two failing aggregate
suites, verbatim from its output:

```
FAIL rust-vs-cplus: note: outcome equality is evidence of observed behaviour only; it does not prove equivalent safety guarantees.
FAIL conversion (Rust compile)
tests/rust_vs_cplus/run_compare.sh: line 36: rustc: command not found
FAIL math (Rust compile)
tests/rust_vs_cplus/run_compare.sh: line 36: rustc: command not found
FAIL concurrency (Rust compile)
tests/rust_vs_cplus/run_compare.sh: line 36: rustc: command not found
rust-vs-cplus: 0 matched, 0 documented differences, 0 both-skip, 6 failures
note: outcome equality is evidence of observed behaviour only; it does not prove equivalent safety guarantees.
FAIL cplus-rust-gates: error: one or more cases failed to compile without a matching expectation
tests/conformance/run_conformance.sh: line 235: 33552 Segmentation fault: 11  timeout 10 "$c_exe" > "$c_stdout" 2> "$c_stderr"
warning: case 12 Rust compile failed; see build/conformance/case_12_rs.build.log
error: case 12 Rust failed to compile (expected 'runtime_reject')
case 12  C=runtime_reject (expected runtime_reject, none)  Rust=compile_reject (expected runtime_reject, language-level bounds/panic / unsafe boundary)  NOMATCH
conformance: 5 matched, 7 mismatched (12 gates)
error: one or more cases failed to compile without a matching expectation
```

- **Failure A — `rust-vs-cplus`:** `rustc: command not found` in
  `tests/rust_vs_cplus/run_compare.sh` (3 groups: conversion, math, concurrency);
  `0 matched, 6 failures`. Under the restricted `PATH` mandated for this baseline,
  `rustc` is not on `PATH` (it is normally under a user-local toolchain directory).
- **Failure B — `cplus-rust-gates`:** `tests/conformance/run_conformance.sh` case 12
  reported a C-side `Segmentation fault: 11`, a Rust compile failure that did not
  match its declared expectation, and case 12 `NOMATCH`; `5 matched, 7 mismatched (12 gates)`.

All other 21 aggregate suites in command 9 passed, including the compiler,
libc, CRT, deep-stdlib, compile-time, family, integration, alloc-fault, ABI and
coverage components (the aggregate coverage line here was
`ok   coverage: libc total line coverage: 92.23%`).

**These failures are recorded, not hidden or re-run.** No attempt was made to
re-execute command 9 with a different `PATH` or environment to make them pass.

### Earlier recorded evidence (context, not a re-run)

An earlier recorded log,
`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/task6_evidence.log`,
ends with `evidence aggregate: 23 passed, 0 failed`, and
`docs/stdlib/TASK6_FINAL_REPORT.md` §5 describes the same runner as "all 23 suites,
exit 0". That earlier run was produced in a different toolchain environment (notably
with `rustc` available); it is preserved as prior recorded evidence. The current
re-execution above is the authoritative post-fix baseline for this audit, and its
two failures are environment-sensitive Rust-gate failures, not stdlib runtime
regressions: the stdlib runtime, deep-stdlib and compile-time suites were green in
both the current run and the recorded log.

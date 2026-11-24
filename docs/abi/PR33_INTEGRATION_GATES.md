# PR #33 — Independent review and reproducible merge gates

Issue: [#33 [P1][Integration] Enforce independent agent review and reproducible merge gates](https://github.com/haseeb-heaven/c-plus/issues/33).

## 0. Scope and honesty statement

**This document and `tests/pr33_integration/` define a gate framework. They are
not evidence that independent review happened, and they do not assert release
readiness.** Running these scripts only checks that reports are well formed,
that two reports do not collide, and that a *locally assembled* combined tree
can run a bounded set of suites. The actual cross-branch CI **must be run by a
human/independent verifier** who reproduces the commands, reviews the
negative-test fixtures, and records the artifacts. No completion percentage,
"safety" claim, or "independently reviewed" status may be entered in a PR or
release checklist unless it links to that verifier's evidence. This document
makes no such claim.

The gate scripts are POSIX `sh`, contain no Python and no `|| true`, and never
write to the current worktree source tree, `develop`, `main`, or `master`.

## 1. Ownership map (derived from repository structure)

The boundaries below follow `docs/STRUCTURE.md` ("compiler sources live under
`compiler/`", "runtime C code and the public ABI header live under `runtime/`",
"all `.hp` standard-library headers live under `stdlib/`"), `docs/ARCHITECTURE.md`,
the directory layout (`libc/`, `tests/abi/`, `tests/crt/`, `tests/libc/`,
`tests/integration/`, `tools/abi_compare/`), and the branch rules in
`CONTRIBUTING.md`. The branch associations are observed from
`git diff --name-only develop...<branch>`: `agent/core-safety` (PR AGENT 1)
changes `compiler/Analysis`, `compiler/Sema`, `compiler/Driver`,
`compiler/Main.cpp`, `runtime/cplus_runtime.c`, `runtime/include/`, plus
`tests/compiler`, `tests/safety`, `tests/core`, `tests/compile_fail`,
`tests/runtime_fail`; `agent/modern-stdlib` (PR AGENT 2) changes
`libc/src/`, `libc/install-include/`, `stdlib/safe/`, `third_party/safeclib`,
plus `tests/libc`, `tests/crt`, `tests/abi`, `examples/` and `benchmarks/`.

| Area | Owned paths | Primary branch / agent | Required evidence |
| --- | --- | --- | --- |
| Compiler analysis and lowering | `compiler/Main.cpp`, `compiler/Driver/`, `compiler/Sema/`, `compiler/Analysis/`, `compiler/Diagnostics.*`, compiler-specific `tests/compiler/`, `tests/compile_fail/`, `tests/runtime_fail/`, `tests/safety/`, `tests/core/` | PR AGENT 1 — `agent/core-safety` | `cmake --build build --target c+`, `tests/run_tests.sh`, compiler negative fixtures (compile-fail/runtime-fail) |
| Runtime and stable C ABI | `runtime/cplus_runtime.c`, `runtime/CPlusRuntime.cpp`, `runtime/include/cplus/`, `tests/runtime/` | PR AGENT 1 — `agent/core-safety` | `tests/runtime/run_runtime_tests.sh`, ABI fingerprint compare (shared) |
| libc+ and hardened ordinary-C layer | `libc/src/`, `libc/install-include/`, `libc/contracts/`, `libc/upstream/`, `stdlib/safe/`, `third_party/safeclib/`, `third_party/intel-safestringlib/` | PR AGENT 2 — `agent/modern-stdlib` | `tests/libc/run_libc_tests.sh`, `tests/crt/run_all_crt.sh`, `tests/libc/coverage_libc.sh`, `tests/crt/coverage_safecrt.sh` |
| `stdlib/` `.hp` headers | `stdlib/*.hp` (string, vector, io, file, thread, net, json, …) | PR AGENT 2 — `agent/modern-stdlib` | `tests/stdlib/run_stdlib_tests.sh`, `tests/stdlib/run_coverage.sh` |
| Compatibility tests and ABI evidence | `tests/abi/`, `tests/compat/`, `tests/crt/`, `tests/libc/`, `tests/integration/`, `tools/abi_compare/`, `docs/abi/` | **Shared — integration review required** regardless of which agent edits them | `tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt`, `tests/integration/run_integration.sh` |
| Integration gates (this work) | `tests/pr33_integration/`, `docs/abi/PR33_INTEGRATION_GATES.md` | Integration agent only; no other PR may edit these files | `sh tests/pr33_integration/run.sh` |

Shared hot spots observed on both candidate branches — `CMakeLists.txt`,
`.gitignore`, `README.md`, `tests/**`, `docs/**`, and CI workflow files — are
**not** owned by either agent. An edit there must be called out in the report's
`CHANGED_PATHS`, is checked by `check_overlap.sh`, and needs explicit
independent review. `tests/abi/fingerprints/baseline.txt` is the authoritative
ABI fingerprint baseline; the `safe_layer_baseline.txt` variant is explicitly
additive evidence only (see `docs/abi/safe_layer_fingerprint.md`).

## 2. Mandatory agent report format

Every parallel-agent PR must link a report file that passes
`tests/pr33_integration/check_report.sh`. The schema is line oriented; sections
are closed with their matching `*_END`, and comments/blank lines are ignored.

| Field / section | Rule |
| --- | --- |
| `SCOPE:` | Named scope (one line). Non-empty, no placeholder. |
| `DEPENDENCIES:` | Dependency list (`none` or named branches/PRs). Non-empty, no placeholder. |
| `COMMIT_SHA:` | Exactly 40 hex characters of the commit the report describes. |
| `CHANGED_PATHS_BEGIN/END` | One repo-relative path per line; at least one. Absolute paths and placeholders are rejected. |
| `EXACT_COMMANDS_BEGIN/END` | The commands actually run, verbatim; at least one, no placeholders. |
| `TEST_OUTPUT_BEGIN/END` **or** `TEST_ARTIFACT:` | The full test output, or a path to an artifact holding it. Exactly one of the two. Placeholders rejected. |
| `KNOWN_FAILURES:` | Explicit `none` or a description. Missing/placeholder is a failure. |
| `INDEPENDENT_REVIEW:` | `PENDING` / `NOT REVIEWED` unless a verifier exists; a claim of "reviewed" requires a linked evidence path or URL. |
| `CLAIMS_BEGIN/END` *(optional)* | Test IDs / API names claimed by this PR (e.g. `test:tests/libc/test_c23_nextup.c`, `api:cplus_nextup`). Compared between reports by `check_overlap.sh`. |

Examples and expected results:

```sh
sh tests/pr33_integration/check_report.sh tests/pr33_integration/example_valid_report.txt       # exit 0
sh tests/pr33_integration/check_report.sh tests/pr33_integration/example_invalid_report.txt     # exit 1 (missing fields + placeholders)
```

`example_valid_report.txt` is the template. `example_invalid_report.txt` is a
negative fixture: it omits `COMMIT_SHA`, `KNOWN_FAILURES` and
`INDEPENDENT_REVIEW` and uses `TODO`/`TBD` placeholders. Exit codes: `0` = valid,
`1` = required field missing/placeholder, `2` = usage/unreadable input.

## 3. Conflict and duplicate-claim detection

```sh
sh tests/pr33_integration/check_overlap.sh <reportA> <reportB>
# exit 0 = disjoint; 1 = overlap detected; 2 = a report is invalid/unreadable
```

The detector requires both reports to pass `check_report.sh`, then reports
`WARNING OVERLAP path:` for identical or directory-prefix-related paths and
`WARNING DUPLICATE CLAIM:` for identical `CLAIMS` entries. Either condition
makes the check fail (exit 1).

```sh
sh tests/pr33_integration/check_overlap.sh \
    tests/pr33_integration/example_valid_report.txt \
    tests/pr33_integration/example_report_peer.txt       # exit 0 (disjoint)
sh tests/pr33_integration/check_overlap.sh \
    tests/pr33_integration/example_valid_report.txt \
    tests/pr33_integration/example_report_conflict.txt   # exit 1 (both warnings)
```

`example_report_conflict.txt` deliberately collides with `example_valid_report.txt`
on `runtime/cplus_runtime.c` and on `api:cplus_runtime_recover`; it is itself a
schema-valid report so the test isolates overlap detection.

## 4. Combined-branch gate

```sh
sh tests/pr33_integration/run_combined_gate.sh [branchA] [branchB]
# defaults: agent/core-safety, agent/modern-stdlib
# PR33_GATE_TIMEOUT=<seconds> (default 180), PR33_GATE_OUT=<log dir>
```

Operation:

1. Resolves each name against `refs/heads/` then `refs/remotes/origin/`.
   Missing/unreachable branches, protected names (`develop`, `main`, `master`,
   `HEAD`), or a branch already contained in the other produce `NOT_TESTED`
   with a reason — never a silent pass and never an exit-0 claim of success.
2. Creates a throwaway branch and a temporary worktree via
   `git worktree add -b pr33-combined-<pid> <mktemp dir> <branchA>`, then merges
   `branchB` with `git merge --no-commit --no-ff`. Nothing is committed; the
   current worktree is untouched and `develop`/`main` are never merge targets.
3. Runs, each under a timeout, from the merged tree: `prepare`
   (`tests/libc/build_libc.sh` + `scripts/build_safecrt.sh`), **ABI fingerprint**
   (`tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt`),
   **libc tests** (`tests/libc/run_libc_tests.sh`), **CRT tests**
   (`tests/crt/run_all_crt.sh`), and **coverage**
   (`tests/libc/coverage_libc.sh` + `tests/crt/coverage_safecrt.sh`).
4. Removes the worktree and throwaway branch via a trap; keeps logs in
   `PR33_GATE_OUT` and prints `GATE_ARTIFACT_DIR`. It prints
   `COMBINED_GATE <checks> <pass> <fail> <not_tested>` and always prints
   `INDEPENDENT_REVIEW: NOT PERFORMED`.

A merge conflict, a build failure, a suite failure, or a timeout is a real
failure (exit 1). Missing toolchain (`clang`, `llvm-cov`/`llvm-profdata`) or a
failed prepare step yields `NOT_TESTED` for the affected suites; `NOT_TESTED`
means the gate is **incomplete**, not green. The verifier must refresh remote
refs with `git fetch` before relying on remote-tracking refs; this script does
no network operations.

Gate-framework smoke observation (2026-11-24, local refs, **not independent
review and not a release verdict**): an end-to-end invocation with
`PR33_GATE_TIMEOUT=30` failed at the merge step for the two default branches.
The conflict set in `PR33_GATE_OUT/merge.log` was concentrated in the shared
hot spots identified in section 1 — `.gitignore`, `compiler/Driver/Driver.cpp`,
`demos/run_demos.sh`, `docs/STATUS.md`, `sonar-project.properties`,
`tests/compile_fail/*.err` and `tests/runtime_fail/bounds.stderr`. The temporary
worktree and throwaway branch were removed by the trap. The verifier must
resolve these shared-file conflicts, then re-run the gate and link the new log
artifacts; this observation alone does not resolve them.

## 5. Bounded runner and negative controls

```sh
sh tests/pr33_integration/run.sh
# optional: PR33_RUN_COMBINED_GATE=1 PR33_BRANCH_A=... PR33_BRANCH_B=... sh tests/pr33_integration/run.sh
```

`run.sh` validates the examples, runs a placeholder-specific negative control,
checks that the overlap detector emits **both** warnings, and checks that the
combined gate reports `NOT_TESTED` for nonexistent branches. It never merges by
default; the real combined gate is opt-in. It continues after failures and
prints exactly one summary line of the form:

```text
PR33_GATE <checks> <pass> <fail> <not_tested>
```

Exit status is nonzero when `fail > 0`. A nonzero `not_tested` count means the
gate framework was exercised but release evidence is still missing.

## 6. Release checklist

Merge order follows `CONTRIBUTING.md`: topic branches → `develop` → (tested,
tagged release) → `main`/`master`. **No auto-merge for safety-critical
changes.** Merge in dependency order; e.g. compiler/runtime contract first
(`agent/core-safety`), then libc+ and stdlib consumers (`agent/modern-stdlib`),
then integration; re-run the combined gate after each dependency lands. Before
each merge record a rollback point (the current `develop` SHA as a tag or
branch, e.g. `git tag pr33-rollback-<date>`) so the merge can be reverted with a
single command.

| Status | Meaning | Required linked evidence |
| --- | --- | --- |
| **implemented** | Code exists on a named branch with a mandatory report | Report with `COMMIT_SHA`, `CHANGED_PATHS`, `SCOPE`, `DEPENDENCIES`; `check_report.sh` exit 0 |
| **tested** | Commands were reproduced on the candidate branch | `EXACT_COMMANDS` plus `TEST_OUTPUT`/`TEST_ARTIFACT`; `run.sh` and, for combined work, `COMBINED_GATE` log directory |
| **independently reviewed** | Someone other than the implementing agent reproduced critical safety claims and reviewed negative fixtures | Reviewer name + PR review / linked artifact; `INDEPENDENT_REVIEW` no longer `PENDING`; `check_overlap.sh` exit 0 on both reports |
| **supported** | Promoted to `develop` and released from `main` in dependency order | Merge commits, rollback tag, release PR and post-merge combined-gate log |

Rules:

1. No completion percentage, coverage percentage, "safe", "100%", or
   "independently reviewed" claim is accepted without a linked artifact that a
   verifier can open. Self-reported success is not evidence.
2. `independently reviewed` cannot be set by the implementing agent and cannot
   be inferred from a green build or an emulator result.
3. `supported` requires the combined compiler + runtime + libc+ suites to pass
   after all dependencies are merged, not merely on isolated branches.
4. If any gate reports `NOT_TESTED`, the checklist item stays at `implemented`
   or `tested` — never `independently reviewed` or `supported`.

## 7. Quick reference

```sh
sh tests/pr33_integration/check_report.sh   tests/pr33_integration/example_valid_report.txt
sh tests/pr33_integration/check_overlap.sh  <reportA> <reportB>
PR33_GATE_TIMEOUT=180 sh tests/pr33_integration/run_combined_gate.sh
sh tests/pr33_integration/run.sh
```

Reminder: these commands produce gate *evidence*, not a claim that independent
review has occurred. The responsibility for independent verification rests with
the human verifier, not with these scripts.

# PR33 combined-branch gate: observed merge conflicts (2026-11-25)

`sh tests/pr33_integration/run_combined_gate.sh` (default candidates
`agent/core-safety` + `agent/modern-stdlib`, `PR33_GATE_TIMEOUT=300`) was run
verbatim from the repo root. It failed at the merge step — the combined tree
was never built or tested. Exact command and logs were preserved in the
runner's artifact directory; outcome: `FAIL combined-gate: merge conflict or
merge error combining agent/core-safety and agent/modern-stdlib`.

## Conflicts (17 paths, verbatim from `merge.log`)

| Path | Category / owner |
|---|---|
| `.gitignore` | repo hygiene — both agents |
| `compiler/Driver/Driver.cpp` | compiler driver — Agent 1 |
| `compiler/Sema/RaiiLowering.cpp` | RAII lowering — Agent 1 |
| `demos/run_demos.sh` | demos — Agent 2 |
| `docs/STATUS.md` | status/process — both |
| `sonar-project.properties` | CI config — both |
| `tests/compile_fail/alloc_return_oob.err`, `alloc_size_oob.err`, `classic_bug1_oob.err`, `constant_oob.err`, `loop_oob.err`, `multidim_bounds.err` | expected compiler diagnostics — Agent 1 |
| `tests/compile_fail/classic_bug2_uaf.err`, `classic_bug5_double_free.err`, `double_release.err`, `freed_len.err` | new expected-diagnostic files (add/add) — Agent 1 |
| `tests/runtime_fail/bounds.stderr` | expected runtime diagnostic — Agent 1/runtime |

## What this means

- This is **not** a libc+/modern-stdlib defect: no C++ source file owned by
  Agent 2 is in conflict. The conflicts are compiler diagnostics/lowering,
  status/CI config and two process files.
- I did **not** resolve any conflict: compiler frontend/semantic files are
  Agent 1-owned, and silently choosing either side would hide a diagnostic
  change (e.g., which bounds/UAF messages are expected).
- Merge-gate resolution requires an explicit joint decision: for each
  `tests/compile_fail/*.err` / `tests/runtime_fail/*.stderr`, rebuild the
  combined compiler and record the actual diagnostic, then update the fixture
  and re-run `ctest` (currently 15 compiler-side diagnostic failures separate
  from these conflicts).

## Non-claims

The combined gate remains NOT_TESTED/failed; this document is a handoff, not a
claim that branches are mergeable or that integration tests pass. No
`develop`/`main` merge was attempted, and PR #17 remains open.

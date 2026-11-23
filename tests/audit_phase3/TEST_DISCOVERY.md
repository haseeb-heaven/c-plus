# PHASE 3 — test discovery and coverage mapping

Agent B (audit_phase3). Discovery was performed with `find` across `tests/`;
runner coverage was established by reading the runner scripts themselves
(not by trusting their documentation or cached result files).

## Discovery commands used

```
find tests -type f -name 'deep_*'                       # deep + result artifacts
find tests -type f -name '*.cp'                         # 455 total .cp sources
find tests -type f -name '*.sh'                         # 139 runner scripts
find tests -type f -name '*.cp' -path '*regression*'
find tests -type f -name '*.cp' \( -path '*breaker*' -o -path '*stress*' \)
find tests -type f -name '*.cp' -path '*adversarial*'
find tests -type f -name '*.cp' \( -path '*allocation_failure*' -o ... \)
```

## Category inventory

| Category / directory | `.cp` count | Local runner(s) | Covered by a regular runner? |
|---|---:|---|---|
| `tests/stdlib/deep_*.cp` | 29 | `run_deep_tests.sh`, `run_adversarial.sh --suite deep`, `agent2/run_all.sh`, `tools/run_final_evidence.sh` | Yes — but absent from `run_all_evidence.sh` and `stdlib/run_all.sh` |
| `tests/stdlib/agent2/*.cp` | 17 (16 tests + `run_all.cp` runner source) | `agent2/run_all.sh` (via prebuilt native binary) | Yes, via `tools/run_final_evidence.sh` |
| `tests/stdlib/regressions/adv_*.cp` | 10 | `run_adversarial.sh --suite regressions` | Yes, but only via the native `run_adversarial` tool |
| `tests/stress/cplus_breaker_v3/*.cp` | 25 | `run.sh` + `cases.list` | Yes, via `stdlib/run_all.sh` |
| `tests/stress/cplus_stress_10/*.cp` | 10 | `run_10.sh` | Yes, via `stdlib/run_all.sh` |
| `tests/breaker/cplus_probe_*.cp` | 24 | `cp_breaker.sh` | Yes, via `stdlib/run_all.sh` |
| `tests/stdlib/agent1_handoff/**` | 52 (10 `manifest.json`) | only `cross_tu/run.sh` | **NO — never run automatically** |
| `tests/adversarial_1000/**` | 8 | 10 `.sh`, aggregate `run_all.sh` | Yes (campaign, 600 s/group bound) |
| `tests/fuzz10k/**` | 8 | 11 `.sh`, aggregate `run_all.sh` | Yes (campaign, 900 s/group bound) |
| `tests/fuzz100/**` | 1 | `run.sh`, `run_native.sh` | Yes, via `tools/run_final_evidence.sh` (optional/BLOCKED) |
| `tests/redteam/**` | 8 | 5 `.sh` | Yes, per-subdirectory runners |
| `tests/defense/**` | 9 | 5 `.sh` | Yes, per-subdirectory runners |
| `tests/security/**` | 15 | 3 `.sh` | Yes, per-subdirectory runners |
| `tests/crt/**` | 0 `.cp` (C fuzz harnesses) | 10 `.sh` | Yes, `run_all_crt.sh` / final evidence |
| `tests/stdlib/compile_time/**` | 52 | `run_compile_time.sh` | Yes, via `run_all_evidence.sh` / final evidence |
| `tests/stdlib/system_safety/**` | 27 | `run.sh` | Yes, local runner only |
| `tests/stdlib/fuzz/**` | 44 | `run_fuzz.sh` + `run_adversarial --suite fuzz` | Yes, via final evidence |
| `tests/stdlib/method_tests/**` | 23 | `run_all.sh` + 3 subrunners | Yes, via `run_all_evidence.sh` |
| `tests/stdlib/allocator/**` | 10 | none in directory | Partial (headers included by other suites) |
| `tests/stdlib/upstream_reuse/**` | 8 | none in directory | No dedicated runner |
| `tests/stdlib/known_gaps/**` | 3 | none in directory | `class_linkage` only, via `run_stdlib_tests.sh` |
| `tests/stdlib/allocation_failure/**` | 4 | `run.sh` | Yes, local runner only |
| `tests/stdlib/container_safety/**` | 4 | `run.sh` | Yes, local runner only |
| `tests/stdlib/memory_ownership/**` | 2 | `run.sh` | Yes, local runner only |
| `tests/expanded_examples_adversarial/**` | 0 `.cp` | 7 `.sh` | Yes, `run_all.sh` (full CLI campaigns) |

## Executed set (114 tests)

All tests in the six manageable categories were executed with the audit
protocol; see `RESULTS.md` for every result and reason.

| Group | Count | Source glob | Expectation source |
|---|---:|---|---|
| deep | 29 | `tests/stdlib/deep_*.cp` | `.expected` / `.stderr.expected` / `.stdin` |
| agent2 | 16 | `tests/stdlib/agent2/*.cp` (minus `run_all.cp`) | `.expected` / `.stderr.expected` |
| regression | 10 | `tests/stdlib/regressions/adv_*.cp` | exit code (no fixtures) |
| breaker_v3 | 25 | `tests/stress/cplus_breaker_v3/*.cp` | `cases.list` (positive / reject / trap) |
| stress10 | 10 | `tests/stress/cplus_stress_10/*.cp` | `run_10.sh` case table |
| breaker_probe | 24 | `tests/breaker/cplus_probe_*.cp` | `cp_breaker.sh` `check()` table |

## Discovered but not executed (classified, never silently skipped)

These are reported here rather than folded into the 114 executed rows because
they are either long-running campaigns with their own authoritative aggregate
runners, or evidence manifests that are not pass/fail suites under the
build-and-run protocol.

| Set | Count | Status for this audit | Exact reason |
|---|---:|---|---|
| `tests/stdlib/agent1_handoff/**/*.cp` | 52 | **BLOCKED / NEVER RUN** | No runner consumes the 10 `manifest.json` files; they record "observed ACCEPT" known gaps and `c+ check --strict` diagnostics, not build-and-run pass/fail. Not executable under the mandated protocol without writing a new manifest runner. |
| `tests/adversarial_1000/**` | 8 `.cp` | Covered, not executed | Aggregate runner `tests/adversarial_1000/run_all.sh` bounds each group at 600 s and validates exact `ADV1000` final lines; re-running the whole campaign here is out of scope and would duplicate its authoritative result. |
| `tests/fuzz10k/**` | 8 `.cp` | Covered, not executed | `tests/fuzz10k/run_all.sh` bounds each group at 900 s with 10 000 scenarios; same reason. |
| `tests/redteam/**`, `tests/defense/**`, `tests/security/**` | 32 `.cp` | Covered, not executed | Each subdirectory has its own runner; these are attack/campaign suites, not deep/regression unit tests. |
| `tests/stdlib/compile_time/**` | 52 `.cp` | Covered, not executed | `run_compile_time.sh`; compile-time checker suite, invoked by both evidence aggregators. |
| `tests/stdlib/system_safety/**` | 27 `.cp` | Covered, not executed | `system_safety/run.sh`; its own suite. |
| `tests/stdlib/fuzz/**` (44), `tests/fuzz100` (1), `tests/crt/fuzz` (C) | 45+ | Covered, not executed | `run_fuzz.sh`, `run_fuzz_libc.sh`, `run_fuzz_smoke.sh`; all in `tools/run_final_evidence.sh`. |
| `tests/stdlib/allocator/**` (10), `upstream_reuse/**` (8), `known_gaps/**` (3) | 21 | Partially covered / no dedicated runner | No directory-local runner; only individual headers or the `class_linkage` multi-TU case are exercised elsewhere. Genuine coverage gap worth a future runner. |

## Key coverage conclusions

1. The 29 deep tests are covered by `run_deep_tests.sh`, but that runner is not
   called by `tests/stdlib/run_all.sh` or `tests/run_all_evidence.sh`; it is
   called only by `tools/run_final_evidence.sh` and one CI workflow.
2. `tests/stdlib/agent1_handoff` (52 sources) is a definite never-run gap.
3. Several suites rely on prebuilt native binaries, so a stale `build/native-tools/`
   silently changes what "the runner" means.
4. Default-mode compilation is currently broken repository-wide (see D2 in
   `RESULTS.md`), so every default-mode runner is effectively failing to build
   its inputs, independent of test quality.

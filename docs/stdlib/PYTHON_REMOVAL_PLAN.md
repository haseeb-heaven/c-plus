# Python Test Tooling — Exact Removal Plan

Audit date: 2026-11-23 · Branch: `agent/modern-stdlib` · Auditor: parallel audit agent 1/10

This document is an **audit and removal plan only**. It catalogues every reference to
the 8 Python test-tooling files under `tests/` and every `python3` invocation found in
shell scripts, docs, CI workflows, and make targets. **Nothing has been deleted and no
source file has been modified.** Removal is **not complete** — every site below still
needs to be updated by the orchestrator (see §7). No fabricated paths are used; all
line numbers were read from the working tree on the date above.

---

## 0. Scope — the 8 Python files and their C+ replacement paths

| # | Python file under `tests/` | C+ replacement path (from sibling agents) | Replacement present on disk at audit time? |
|---|----------------------------|-------------------------------------------|---------------------------------------------|
| 1 | `tests/fuzz100/run.py` | `tests/fuzz100/run.cp` | NO |
| 2 | `tests/libc/test_compiler_contract_alignment.py` | `tests/libc/*.cp` (per-file C+ replacement owned by the libc agent; exact filename not yet fixed) | NO (only `tests/libc/test_stdlib_realloc.cp` exists, unrelated) |
| 3 | `tests/libc/test_contract_consumption.py` | `tests/libc/*.cp` (same) | NO |
| 4 | `tests/libc/test_contract_integrity.py` | `tests/libc/*.cp` (same) | NO |
| 5 | `tests/libc/validate_contracts.py` | `tests/libc/*.cp` (same) | NO |
| 6 | `tests/stdlib/agent2/run_all.py` | `tests/stdlib/agent2/run_all.cp` | NO |
| 7 | `tests/stdlib/coverage.py` | `tests/stdlib/coverage.cp` | NO |
| 8 | `tests/stdlib/run_adversarial.py` | `tests/stdlib/run_adversarial.cp` | NO |

Out of scope but noted: `benchmarks/timeit.py`, `scratch/generate_bugs.py`, and the
`python3 - <<'PY'` **inline heredocs** in several shell scripts (§4) are not among the
8 files and have **no C+ replacement path supplied by sibling agents**. They are
catalogued separately and are an orchestrator decision, not a clean substitution.

---

## 1. Executable invocation sites (shell scripts / aggregators)

These directly invoke one of the 8 Python files. Each maps to a C+ replacement from §0.

| file:line | Exact command / text | Python file | C+ replacement path |
|-----------|----------------------|-------------|---------------------|
| `tests/fuzz100/run.sh:6` | `# All discovery, building, running and reporting lives in run.py. This wrapper` (comment) | `tests/fuzz100/run.py` | `tests/fuzz100/run.cp` |
| `tests/fuzz100/run.sh:12` | `exec python3 "$ROOT/tests/fuzz100/run.py" --root "$ROOT" "$@"` | `tests/fuzz100/run.py` | `tests/fuzz100/run.cp` |
| `tests/stdlib/agent2/run_all.sh:46` | `exec python3 "$ROOT/tests/stdlib/agent2/run_all.py" \` (continued lines 47–48: `"$ROOT" "$RESULTS" "$CPLUS" "$BUILD_TIMEOUT" "$TIMEOUT" \` / `"$TEST" "$MODULE" "$CLEAN" "$KEEP"`) | `tests/stdlib/agent2/run_all.py` | `tests/stdlib/agent2/run_all.cp` |
| `tests/stdlib/run_adversarial.sh:15` | `exec python3 "$ROOT/tests/stdlib/run_adversarial.py" "$ROOT" "$@"` | `tests/stdlib/run_adversarial.py` | `tests/stdlib/run_adversarial.cp` |
| `tests/stdlib/run_stdlib_tests.sh:241` | `if python3 "$ROOT/tests/stdlib/coverage.py" >"$OUT/coverage.txt" 2>&1; then` | `tests/stdlib/coverage.py` | `tests/stdlib/coverage.cp` |
| `tests/libc/install_smoke.sh:17` | `python3 "$ROOT/tests/libc/validate_contracts.py" >/tmp/libc_contracts_check.log 2>&1` | `tests/libc/validate_contracts.py` | `tests/libc/*.cp` |
| `tests/libc/run_attack_tests.sh:44` | `if [ -f tests/libc/test_contract_integrity.py ]; then` (existence guard) | `tests/libc/test_contract_integrity.py` | `tests/libc/*.cp` |
| `tests/libc/run_attack_tests.sh:45` | `if python3 tests/libc/test_contract_integrity.py >"$OUT/contract_integrity.run.log" 2>&1; then` | `tests/libc/test_contract_integrity.py` | `tests/libc/*.cp` |
| `tests/libc/run_attack_tests.sh:46` | `echo "ok   test_contract_integrity.py"` | `tests/libc/test_contract_integrity.py` | `tests/libc/*.cp` |
| `tests/libc/run_attack_tests.sh:49` | `echo "FAIL test_contract_integrity.py"` | `tests/libc/test_contract_integrity.py` | `tests/libc/*.cp` |
| `tests/libc/run_attack_tests.sh:4` | `# ... Also runs the Python contract-integrity` (comment) | `tests/libc/test_contract_integrity.py` | `tests/libc/*.cp` |
| `tests/run_all_evidence.sh:41` | `run contract-validator    python3 tests/libc/validate_contracts.py` | `tests/libc/validate_contracts.py` | `tests/libc/*.cp` |
| `tests/run_all_evidence.sh:42` | `run contract-alignment    python3 tests/libc/test_compiler_contract_alignment.py` | `tests/libc/test_compiler_contract_alignment.py` | `tests/libc/*.cp` |
| `tools/run_final_evidence.sh:241` | `# Python contract tests (each is its own authoritative suite).` (comment) | (4 libc files) | `tests/libc/*.cp` |
| `tools/run_final_evidence.sh:243` | `"tests/libc/validate_contracts.py" \` (evidence label string) | `tests/libc/validate_contracts.py` | `tests/libc/*.cp` |
| `tools/run_final_evidence.sh:244` | `python3 tests/libc/validate_contracts.py` | `tests/libc/validate_contracts.py` | `tests/libc/*.cp` |
| `tools/run_final_evidence.sh:247` | `"tests/libc/test_compiler_contract_alignment.py" \` (label) | `tests/libc/test_compiler_contract_alignment.py` | `tests/libc/*.cp` |
| `tools/run_final_evidence.sh:248` | `python3 tests/libc/test_compiler_contract_alignment.py` | `tests/libc/test_compiler_contract_alignment.py` | `tests/libc/*.cp` |
| `tools/run_final_evidence.sh:251` | `"build/c+,tests/libc/test_contract_consumption.py" \` (label) | `tests/libc/test_contract_consumption.py` | `tests/libc/*.cp` |
| `tools/run_final_evidence.sh:252` | `python3 tests/libc/test_contract_consumption.py` | `tests/libc/test_contract_consumption.py` | `tests/libc/*.cp` |
| `tools/run_final_evidence.sh:255` | `"tests/libc/test_contract_integrity.py" \` (label) | `tests/libc/test_contract_integrity.py` | `tests/libc/*.cp` |
| `tools/run_final_evidence.sh:256` | `python3 tests/libc/test_contract_integrity.py` | `tests/libc/test_contract_integrity.py` | `tests/libc/*.cp` |

Note: `tests/stdlib/run_all.sh` does **not** invoke Python directly; it calls
`tests/stdlib/coverage_exec.sh` (line 25) and the stdlib runner (which reaches
`coverage.py` via `run_stdlib_tests.sh:241`). It is affected transitively.

---

## 2. CI workflow references

File: `.github/workflows/libc-crt.yml` (the only CI workflow in the repo). It contains
**no literal `python3` string**; it is affected **indirectly** because the following
`run:` steps execute shell scripts that invoke Python (per §1 / §4). Every one of these
still needs orchestrator attention:

| file:line | Exact command | Transitive Python dependency | C+ replacement path |
|-----------|---------------|------------------------------|---------------------|
| `.github/workflows/libc-crt.yml:41` | `LLVM_COV="$COV" LLVM_PROFDATA="$PROF" sh tests/libc/coverage_libc.sh` | inline `python3` at `tests/libc/coverage_libc.sh:127` | none supplied (inline heredoc, §4) |
| `.github/workflows/libc-crt.yml:43` | `run: sh tests/libc/run_attack_tests.sh` | `test_contract_integrity.py` (`run_attack_tests.sh:45`) | `tests/libc/*.cp` |
| `.github/workflows/libc-crt.yml:53` | `run: sh tests/conformance/run_conformance.sh` | inline `python3` (`:38`, `:238`) | none supplied (inline heredoc, §4) |
| `.github/workflows/libc-crt.yml:57` | `run: sh tests/libc/install_smoke.sh` | `validate_contracts.py` (`install_smoke.sh:17`) | `tests/libc/*.cp` |
| `.github/workflows/libc-crt.yml:73` | `run: sh tests/stdlib/compile_time/run_compile_time.sh` | inline `python3` (`:13`) | none supplied (inline heredoc, §4) |
| `.github/workflows/libc-crt.yml:133` | `run: sh tests/stdlib/run_deep_tests.sh` | reaches `coverage.py` via `run_stdlib_tests.sh:241` | `tests/stdlib/coverage.cp` |
| `.github/workflows/libc-crt.yml:135` | `run: sh tests/stdlib/compile_time/run_compile_time.sh` | inline `python3` (`:13`) | none supplied (inline heredoc, §4) |

The workflow does not reference `tests/fuzz100/run.sh`, `tests/stdlib/run_adversarial.sh`,
or `tools/run_final_evidence.sh`; those are local/aggregator-only paths (§1, §6).

---

## 3. Inline `python3 - <<'PY'` heredocs (NO C+ replacement supplied)

These are general-purpose inline Python (JSON encoding, report generation, coverage
math, benchmark math). They are **not** among the 8 Python files and have **no C+
replacement path from sibling agents**. Flagged for orchestrator decision; do NOT
assume a clean substitution.

| file:line | Exact command / text | Replacement |
|-----------|----------------------|-------------|
| `tests/conformance/run_conformance.sh:38` | `if ! python3 - "$MANIFEST" >"$OUT/plan.tsv" <<'PY'` | none supplied |
| `tests/conformance/run_conformance.sh:237` | `# --- 4. Machine-readable results from python3 ---` (comment) | none supplied |
| `tests/conformance/run_conformance.sh:238` | `if ! python3 - "$OUT/results.tsv" "$OUT/results.json" <<'PY'` | none supplied |
| `tests/crt/coverage_safecrt.sh:72` | `python3 - "$REPORT" <<'PY'` | none supplied |
| `tests/libc/coverage_libc.sh:127` | `python3 - "$REPORT" <<'PY'` | none supplied |
| `tests/stdlib/compile_time/run_compile_time.sh:13` | `python3 - "$ROOT" "$CPLUS" "$OUT" <<'PY'` | none supplied |
| `tests/stdlib/coverage_exec.sh:55` | `python3 - "$OUT/$name.json" "$name" <<'PY'` | none supplied |
| `tests/stdlib/fuzz/f100_json_run.sh:16` | `python3 - "$ROOT" "$CPLUS" "$MANIFEST" "$OUT" <<'PY'` | none supplied |
| `tests/stdlib/fuzz/f100_thread_run.sh:14` | `python3 - "$ROOT" "$CPLUS" "$MANIFEST" "$OUT" <<'PY'` | none supplied |
| `tools/run_final_evidence.sh:72` | `# from the suite's captured log by Python (safe JSON string encoding).` (comment) | none supplied |
| `tools/run_final_evidence.sh:74` | `python3 - "$RESULTS_JSONL" "$1" "$2" "$3" "$4" "$5" "$6" "$7" <<'PY'` | none supplied |
| `tools/run_final_evidence.sh:261` | `python3 - "$EVIDENCE_DIR" "$RESULTS_JSONL" "$SUMMARY_JSON" "$REPORT_MD" \` (continued line 262) | none supplied |
| `tools/run_final_evidence.sh:367` | `python3 - "$RESULTS_JSONL" <<'PY'` | none supplied |
| `benchmarks/run_bench.sh:94` | `python3 - "$name" "$safe_time" "$legacy_time" "$plain_time" <<'EOF'` | none supplied |
| `benchmarks/safety_compare/run_bench.sh:27` | `python3 - "$w" "$OUT" <<'PY'` | none supplied |

Also Python source files outside `tests/`: `benchmarks/timeit.py:1` and
`scratch/generate_bugs.py:1` have `#!/usr/bin/env python3` shebangs. Not in scope; no
replacement supplied.

---

## 4. Docs and evidence artifacts referencing the 8 files / `python3`

These are documentation and captured evidence. They will be **stale** once the Python
files are removed and must be updated or regenerated by the orchestrator. Grouped by
file; each line is a reference site.

### 4a. Top-level docs
- `docs/FINAL_EVIDENCE.md:34,35,36,37,315,334,353,376` — `python3 tests/libc/{validate_contracts,test_compiler_contract_alignment,test_contract_consumption,test_contract_integrity}.py` table rows and command blocks.
- `docs/MODERN_STDLIB.md:184,193,217` — `python3 tests/stdlib/coverage.py` plus prose describing `coverage.py`.

### 4b. `docs/libc/`
- `docs/libc/ADVERSARIAL_FINDINGS.md:23` — `python3 -O` / `test_contract_integrity.py` prose.
- `docs/libc/API_GAP_CLOSURE_PLAN.md:82` — `python3 tools/format_cpp.py --check` (tooling, not one of the 8; note as unrelated).
- `docs/libc/API_STATUS_MATRIX.md:853,862,864` — `validate_contracts.py` prose.
- `docs/libc/C23_MATH_DEPENDENCY_CLOSURE.md:209` — `python3 tools/format_cpp.py --check` (unrelated tooling).
- `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md:36,104,111,138,143,148,157,159,163` — `test_compiler_contract_alignment.py` / `validate_contracts.py` / `python3 -m json.tool` commands and prose (note line 159 contains a machine-specific absolute path).
- `docs/libc/COMPLETION_LEDGER.md:180,234` — `tests/fuzz100/run.py` listed.
- `docs/libc/CONTRACT_CONSUMPTION.md:6,10,154,155,165` — `test_contract_consumption.py` commands + `python3 -O` prose.
- `docs/libc/CONTRACT_STATUS.md:25,100,101` — `validate_contracts.py` / `python3 -m json.tool`.
- `docs/libc/FUZZ_100_CATALOG.md:979,1025,1055,1065` — `tests/fuzz100/run.py` / `run.sh`.
- `docs/libc/FUZZ_COVERAGE_TRUTH.md:23,44,60,77,109,230,233,236,239,242,246` — `coverage.py` parser prose; `python3 -c ... json` one-liners; `python3 tests/stdlib/coverage.py` (242).
- `docs/libc/GAP_ANALYSIS.md:53` — `validate_contracts.py` prose.
- `docs/libc/IMPLEMENTATION_PLAN.md:282,330` — `python3 tests/libc/test_compiler_contract_alignment.py` / `validate_contracts.py`.
- `docs/libc/INTEL_SAFESTRINGLIB_INVENTORY.md:278` — `python3 -m json.tool` (unrelated tooling).
- `docs/libc/RELEASE_READINESS.md:26,83,85,237,251` — `run_adversarial.{py,sh}`, `tests/fuzz100/run.py`.
- `docs/libc/REPORT_QUALITY.md:6,21` — `python3 -m json.tool` (unrelated tooling).

### 4c. `docs/stdlib/`
- `docs/stdlib/COVERAGE_TOOL_LIMITATIONS.md:3,10,180,195,203,213,227,242` — `tests/stdlib/coverage.py` / `coverage_exec.sh`.
- `docs/stdlib/REGRESSION_INVENTORY.md:11,13,221,222,223,224,227,230` — `coverage.py` parser/method prose.
- `docs/stdlib/TASK6_FINAL_REPORT.md:56,65` — `coverage.py` parser/method prose.
- `docs/stdlib/adversarial-testing.md:27,168,207` — `tests/stdlib/run_adversarial.py` / `coverage.py` / `coverage_exec.sh`.
- `docs/stdlib/abi-preserving-runtime-safety.md:642` — `python3 - <<'PY'` inline.
- `docs/stdlib/upstream_study/trapc.md:389` — `python3` + `pypdf` prose (unrelated tooling).

### 4d. Docs under `tests/` and other JSON/evidence
- `tests/adversarial/contract_attack_NOTES.md:3,9,15` — `python3 tests/libc/test_contract_integrity.py`, `validate_contracts.py`, `python3 -O`.
- `tests/fuzz100/README.md:25,28,29,163` — `run.py` usage incl. `python3 tests/fuzz100/run.py --config F001`.
- `tests/fuzz100/RESULTS.md:13,147,153` — `run.py` prose (line 147 has a machine-specific path).
- `tests/stdlib/reports/README.md:4,15,16,173` — `run_adversarial.py` / `.sh`.
- `tests/stdlib/agent2/README.md:109` — `coverage.py` / `coverage_exec.sh`.
- `tests/stdlib/agent2/API_COVERAGE_REPORT.md:5,59,60,181,226,229` — `coverage.py` provenance/method.
- `tests/stdlib/agent2/extra_time_boundaries_NOTES.md:157` — `tests/stdlib/agent2/run_all.py`.
- `tests/stdlib/agent2_audit/known_gaps.md:107` — `python3 tests/libc/test_compiler_contract_alignment.py`.
- `tests/stdlib/agent2_audit/public_api_inventory.md:11,12,406,547,549,551` — `coverage.py` method.
- `tests/stdlib/agent2_audit/test_inventory.md:37` — `coverage.py` / `run_tests.sh`.
- `libc/contracts/compiler_libc_contracts.json:4` and `build/compat/prefix/share/cplus-libc/contracts/compiler_libc_contracts.json:4` — JSON `note` strings citing `tests/libc/test_compiler_contract_alignment.py`.
- `docs/libc/api_status_matrix.json:14368,14377,14378` and `docs/libc/completion_ledger.json:24,76,191,325,355` — generated JSON records citing `validate_contracts.py` / `tests/fuzz100/run.py`.
- `build/final-evidence/results.jsonl:13,14,15,16`, `build/final-evidence/summary.json:214,227,240,257`, `build/fuzz100/summary.json:4` — captured evidence with `"command": "python3 tests/libc/...py"` / `"runner": "tests/fuzz100/run.py"`; regenerate after port.
- `third_party/ctl/README.md:269` — generic `python3`/Plotly mention; **out of scope** (do not touch per project rules).

---

## 5. Make targets

**None found.** Searches of `Makefile`, `*.mk`, `CMakeLists.txt`, and `GNUmakefile`
returned no `python`/`python3` references. The only Makefiles present are generated
build outputs (`build/Makefile`, `build/compat/cmake/Makefile`,
`build/libc-cmake/Makefile`). No action.

---

## 6. Summary of reference counts

| Category | Distinct file:line reference sites (approx.) |
|----------|----------------------------------------------|
| §1 Shell/aggregator invocations of the 8 files | 22 |
| §2 CI workflow steps (indirect) | 7 |
| §3 Inline `python3` heredocs (no replacement) | 15 |
| §4 Docs & evidence artifacts | ~95 (44 Markdown files + JSON/evidence records) |
| §5 Make targets | 0 |
| **Total catalogued reference sites** | **~139** |

---

## 7. References that STILL NEED UPDATING BY THE ORCHESTRATOR

All of the following remain outstanding. **Removal is not complete.** Nothing in this
audit deleted or edited any file.

**Blockers (must be resolved first):**
1. **C+ replacements do not exist on disk.** None of `tests/fuzz100/run.cp`,
   `tests/stdlib/coverage.cp`, `tests/stdlib/run_adversarial.cp`,
   `tests/stdlib/agent2/run_all.cp`, or the four `tests/libc/*.cp` replacements were
   present at audit time. Until sibling agents land them, no invocation can be
   repointed.
2. **`tests/libc/*.cp` exact filenames are unspecified.** The libc agent must publish
   the four concrete `.cp` filenames so `tools/run_final_evidence.sh`,
   `tests/run_all_evidence.sh`, `tests/libc/install_smoke.sh`, and
   `tests/libc/run_attack_tests.sh` can be updated unambiguously.
3. **Inline `python3` heredocs have no replacements (§3).** 15 sites (including 3 in
   `tools/run_final_evidence.sh` and 2 in CI-reached `run_conformance.sh`) need an
   explicit decision: port to a C+ helper, or retain Python as a non-test dependency.
   This is the largest open design question and is not covered by sibling agents.
4. **`run_adversarial.py` / `run_all.py` / `run.py` wrappers have callers.** Before
   deleting the `.py` files, the `.sh` wrappers (and `tools/run_final_evidence.sh`
   `fuzz100` suite) must be repointed; the wrappers currently `exec python3 <file>.py`.

**Ordered update list (once blockers clear):**
- `tests/fuzz100/run.sh:12` → `tests/fuzz100/run.cp`; update comment at `:6`.
- `tests/stdlib/agent2/run_all.sh:46` → `tests/stdlib/agent2/run_all.cp`.
- `tests/stdlib/run_adversarial.sh:15` → `tests/stdlib/run_adversarial.cp`.
- `tests/stdlib/run_stdlib_tests.sh:241` → `tests/stdlib/coverage.cp`.
- `tests/libc/install_smoke.sh:17` → `tests/libc/<validate_contracts>.cp`.
- `tests/libc/run_attack_tests.sh:44-49` → `tests/libc/<test_contract_integrity>.cp` (guard, invoke, echoes).
- `tests/run_all_evidence.sh:41,42` → libc `.cp` replacements.
- `tools/run_final_evidence.sh:243-256` → all four libc `.cp` replacements (labels + commands); regenerate evidence JSONL/summary.
- `.github/workflows/libc-crt.yml:41,43,53,57,73,133,135` → re-verify each step after the underlying scripts are ported; update any step names/comments that say "Python".
- All §4 docs/evidence sites → update prose/commands or regenerate captured evidence (`build/final-evidence/*`, `build/fuzz100/summary.json`).
- Decide the fate of §3 inline heredocs (orchestrator choice).

### Non-blockers / explicitly out of scope
- `third_party/ctl/README.md:269` — do not modify (project rule: no `third_party/` edits).
- `benchmarks/timeit.py`, `scratch/generate_bugs.py`, `tools/format_cpp.py`, `python3 -m json.tool` mentions — unrelated to the 8 test files; no replacement supplied.
- `__pycache__/*.pyc` binaries under `tests/fuzz100`, `tests/libc`, `tests/stdlib` — generated artifacts, removal tracked by whoever deletes the `.py` sources.

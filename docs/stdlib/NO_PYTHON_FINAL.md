# NO_PYTHON_FINAL — is Python fully out of the correctness path?

Auditor: parallel agent 10/10 · Date: 2026-11-23
Repository: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` · Branch: `agent/modern-stdlib`
Observed HEAD: `2294cb1` ("Finalize C+/Rust benchmark proof and untrack generated binaries")

Scope of this report: the checks named in the assignment only —
(1) `find tests tools benchmarks -name '*.py'` excluding `__pycache__`,
(2) `grep python3` in shell runners under `tests/`, `benchmarks/`, `tools/`,
(3) `sh tests/native/build_native_tools.sh`.

No source, test, shell, header, or build file was edited. No `git add`/`git commit` was
performed. The only file authored is this document. Every claim below is taken from the
command output quoted here; nothing is inferred from a stale document.

## Verdict

**The "remove Python testing" requirement is NOT met.**

Python is still executed on the correctness path in 10 shell-runner sites, including
two full test drivers written as inline Python, two coverage-threshold gates, one
manifest parser that drives which conformance cases run, a legacy-lab runner that
executes `*.py` labs, and two `python3` calls to `.py` files that no longer exist on
disk (so those sites are currently broken). Two `.py` files also remain on disk.

Native C+ tools do build cleanly (8 built, 0 failed), but that does not remove the
Python sites that are still wired into the runners.

---

## 1. Check 1 — remaining `.py` files

Command:

```sh
find tests tools benchmarks -name '*.py' -not -path '*/__pycache__/*' | sort
```

Exact output (2 files):

```
benchmarks/timeit.py
tests/security/hostile_inputs/gen_fixtures.py
```

Narrower command `find tests tools -name '*.py'` (same exclusion) yields only:

```
tests/security/hostile_inputs/gen_fixtures.py
```

Classification:

| File | Class | Evidence / rationale |
|------|-------|----------------------|
| `benchmarks/timeit.py` | NON-TEST utility | `#!/usr/bin/env python3`; docstring: "Time a program and report its median wall-clock time." Times a binary with `subprocess` + `time.perf_counter`, prints median/fastest. Invoked only by `benchmarks/run_bench.sh:78,83,88` as a timing helper; no pass/fail verdict. Benchmark utility, outside `tests/`. |
| `tests/security/hostile_inputs/gen_fixtures.py` | NON-TEST utility (generator) | `#!/usr/bin/env python3`; docstring: "Deterministic generator for the hostile-input fixtures … No random sources, no wall-clock values, no host-specific paths." Writes fixed byte literals to `fixtures/`. It generates test input; it does not run or grade a test. Note: it *is* under `tests/`, so any claim that "no `.py` files remain under `tests/`" is false for the current tree. |

`__pycache__` directories were excluded per the task scope; stale `.pyc` files are
not counted as Python source or as executable correctness-path calls.

## 2. Check 2 — every `python3` occurrence in shell runners

Command:

```sh
grep -rn 'python3' --include='*.sh' tests benchmarks tools | sort
```

Result: **15 lines** — 14 executable invocations plus 1 comment. Each occurrence is
listed below with its exact text and classification.

### 2a. CORRECTNESS_PATH violations (10)

| # | file:line | Exact text | Why it is correctness-path |
|---|-----------|------------|----------------------------|
| 1 | `tests/abi/run_legacy_labs.sh:222` | `script_py) add_lab ... "python3 $(basename "$path")" ;;` | `script_kind()` maps any `*.py` runner to `script_py`, and `add_script_lab` records `python3 <name>` as the command used to run that lab. This is the legacy-lab test runner's Python execution branch. |
| 2 | `tests/abi/run_legacy_labs.sh:567` | `( cd "$cwd" && "$TIMEOUT_BIN" "$RUN_TIMEOUT" python3 "$target" ) ...` followed by `rc=$?; classify_rc "$rc"` | Actually executes a Python lab under a timeout; the exit status is fed to `classify_rc`, i.e. Python determines the lab's pass/fail classification. |
| 3 | `tests/conformance/run_conformance.sh:38` | `if ! python3 - "$MANIFEST" >"$OUT/plan.tsv" <<'PY'` | Inline Python parses the JSON manifest into `plan.tsv`. The `while … done <"$OUT/plan.tsv"` loop that follows is the set of conformance cases actually run; failure of this Python aborts the script. Python chooses what gets tested. |
| 4 | `tests/crt/coverage_safecrt.sh:72` | `python3 - "$REPORT" <<'PY'` | Inline Python reads the `llvm-cov` `TOTAL` line and calls `sys.exit("safecrt line coverage … < 80.0%")`. Script has `set -eu` (line 2), so this Python call is the 80% coverage gate that decides the script's exit status. |
| 5 | `tests/libc/coverage_libc.sh:127` | `python3 - "$REPORT" <<'PY'` | Same pattern: parses `llvm-cov` `TOTAL` and calls `sys.exit` below 80%; script has `set -eu` (line 2). Python is the pass/fail gate. |
| 6 | `tests/libc/install_smoke.sh:17` | `python3 "$ROOT/tests/libc/validate_contracts.py" >/tmp/libc_contracts_check.log 2>&1` | A Python contract-validation test invoked from an install smoke test. Script has `set -eu` (line 2), so failure aborts the smoke test. **The referenced file does not exist on disk** (checked: `MISSING tests/libc/validate_contracts.py`), so this site currently fails if reached. A native `validate_contracts` tool builds successfully (§3) but is not called here. |
| 7 | `tests/stdlib/compile_time/run_compile_time.sh:13` | `python3 - "$ROOT" "$CPLUS" "$OUT" <<'PY'` | The entire compile-time safety suite is inline Python: it globs manifests, runs `c+ check --strict` and `c+ build --strict --sanitize=none` via `subprocess`, classifies `REJECT`/`ACCEPT`/`GAP-UNTAGGED`, and accumulates `stats`. All test execution and classification is Python. (`set -u` only; the heredoc prints results and has no trailing `sys.exit`, so the verdict is printed rather than propagated as an exit code — but the correctness logic itself is Python.) |
| 8 | `tests/stdlib/fuzz/f100_json_run.sh:16` | `python3 - "$ROOT" "$CPLUS" "$MANIFEST" "$OUT" <<'PY'` | Inline Python drives the whole fuzz gate: builds each config, runs it under `timeout 30`, extracts `_DIGEST`, checks expected final line and digest, compares digests across modes, then `sys.exit(1 if fail else 0)`; shell ends with `exit $?`. Python decides pass/fail. |
| 9 | `tests/stdlib/fuzz/f100_thread_run.sh:14` | `python3 - "$ROOT" "$CPLUS" "$MANIFEST" "$OUT" <<'PY'` | Same thread-config fuzz driver: `sys.exit(1 if fail else 0)` with `exit $?`. Python decides pass/fail. |
| 10 | `tests/stdlib/run_stdlib_tests.sh:241` | `if python3 "$ROOT/tests/stdlib/coverage.py" >"$OUT/coverage.txt" 2>&1; then` | Python call gates the `pass "coverage …"` / `fail "coverage …"` branch of the stdlib suite. **The referenced file does not exist on disk** (checked: `MISSING tests/stdlib/coverage.py`), so this check currently takes the `fail` branch. A native `coverage` tool builds successfully (§3) but is not called here. |

### 2b. NON-TEST utility occurrences (4 executable + 1 comment)

| # | file:line | Exact text | Classification and rationale |
|---|-----------|------------|------------------------------|
| 11 | `tests/conformance/run_conformance.sh:237` | `# --- 4. Machine-readable results from python3 ---` | Comment only — not an invocation. NON-TEST (documentation). |
| 12 | `tests/conformance/run_conformance.sh:238` | `if ! python3 - "$OUT/results.tsv" "$OUT/results.json" <<'PY'` | NON-TEST utility: serializes the shell-produced `results.tsv` into `results.json`. The per-case verdict is computed in the shell; Python only formats/writes the report (it also derives the JSON `summary.matched` count). Caveat: the `if ! … then exit 1` guard means a Python failure aborts the script, so it can indirectly affect the run even though it does not grade cases. |
| 13 | `tests/stdlib/coverage_exec.sh:55` | `python3 - "$OUT/$name.json" "$name" <<'PY'` | NON-TEST utility: parses `llvm-cov export` JSON and prints a per-header coverage percentage (`print(...)` only; no threshold, no `sys.exit`). Script has `set -u` (line 9). Reporting/formatting, not a gate. |
| 14 | `benchmarks/run_bench.sh:94` | `python3 - "$name" "$safe_time" "$legacy_time" "$plain_time" <<'EOF'` | NON-TEST utility: computes ratios and prints a formatted timing table. No assertions; benchmark output formatting. |
| 15 | `benchmarks/safety_compare/run_bench.sh:27` | `python3 - "$w" "$OUT" <<'PY'` | NON-TEST utility: benchmark timing harness. Runs the compiled variants, takes medians, prints times and checksums. No pass/fail assertion — performance measurement only. |

### 2c. Additional observed Python references (outside the strict `python3`-in-`.sh` grep)

Recorded for completeness; none of these is an additional executable correctness-path call:

* `benchmarks/run_bench.sh:78,83,88` invoke `"$ROOT/benchmarks/timeit.py"` directly by path (the `.py` file above); benchmark timing, no verdict.
* `tests/fuzz100/run.sh:6` is a stale comment ("All discovery, building, running and reporting lives in run.py"), but lines 12–13 call `sh tests/native/build_native_tools.sh` and `exec "$ROOT/build/native-tools/fuzz100_run"`. **No Python is executed** by that runner in the current tree.
* `tests/libc/run_attack_tests.sh:4` is a stale comment mentioning the Python contract-integrity test; line 45 now runs `build/native-tools/test_contract_integrity`. No Python execution.
* `tools/run_final_evidence.sh` and `tests/run_all_evidence.sh` no longer contain `python3` invocations (only comments/labels where noted); e.g. `tests/run_all_evidence.sh:42` now runs `build/native-tools/test_compiler_contract_alignment`.
* Other `python3` strings under `tests/` and `benchmarks/` are in Markdown notes/docs (e.g. `tests/abi/LEGACY_LABS_NOTES.md`, `tests/fuzz100/README.md`, `benchmarks/safety_compare/METHODOLOGY.md`), not shell runners. They were not counted in the 15 lines above and are not execution sites.

## 3. Check 3 — native C+ tools build

Command:

```sh
sh tests/native/build_native_tools.sh
```

Exit code: **0**. Final line of exact output: `native tools: 8 built, 0 failed, 0 missing`.
Every tool built with `ok`:

```
== validate_contracts ==            ok   validate_contracts
== test_compiler_contract_alignment == ok   test_compiler_contract_alignment
== test_contract_consumption ==     ok   test_contract_consumption
== test_contract_integrity ==       ok   test_contract_integrity
== coverage ==                      ok   coverage
== run_adversarial ==               ok   run_adversarial
== run_all ==                       ok   run_all
== fuzz100_run ==                   ok   fuzz100_run
```

Driver: `./build/c+`; output directory: `build/native-tools`. All eight were compiled
with `--strict --sanitize=none` (two with `-- build/libc/libcplus_hardened.a`).
This confirms only that the native replacements **build**; it does not by itself
remove any Python call site listed in §2. In particular, `validate_contracts` and
`coverage` exist natively yet `install_smoke.sh:17` and `run_stdlib_tests.sh:241`
still call the deleted `.py` files.

## 4. Relation to the earlier verification document

`docs/stdlib/NO_PYTHON_TESTING.md` (same branch) records HEAD `487ab89` and reports
"0 `.py` files under `tests`/`tools`" and "native tools: 4 built, 4 failed".
The current tree is at HEAD `2294cb1`, and the checks run for this report observed:

* one `.py` file under `tests/` (`tests/security/hostile_inputs/gen_fixtures.py`) and one under `benchmarks/` (`benchmarks/timeit.py`);
* `native tools: 8 built, 0 failed, 0 missing`;
* 15 remaining `python3` lines in shell runners (§2).

Those are the current observed values; the earlier document's figures do not match
this working tree and should not be treated as current.

## 5. Answer to the requirement

* "No Python in the correctness path": **false** — 10 executable shell sites run Python as part of test execution, gating, or test-case selection (§2a); two of them call `.py` files that are absent on disk.
* "Remove Python testing": **not met** — inline Python still implements entire suites (`run_compile_time.sh`, `f100_json_run.sh`, `f100_thread_run.sh`) and grading gates (`coverage_safecrt.sh`, `coverage_libc.sh`, `install_smoke.sh`, `run_stdlib_tests.sh`, `run_legacy_labs.sh`).
* Non-test Python utilities that remain and are arguably acceptable under a narrow reading: `benchmarks/timeit.py`, `tests/security/hostile_inputs/gen_fixtures.py`, and the four NON-TEST utility sites in §2b.
* Positive result: all native C+ tools build successfully, so the infrastructure needed to finish the removal is present.

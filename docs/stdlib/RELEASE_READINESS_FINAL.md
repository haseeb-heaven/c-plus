# Final release-readiness audit — `agent/modern-stdlib`

**Audit file:** `docs/stdlib/RELEASE_READINESS_FINAL.md`
**Branch:** `agent/modern-stdlib`
**HEAD at snapshot:** `2294cb1e7d880d55a8403b7c7e0e46a6ce85a41f` — "Finalize C+/Rust benchmark proof and untrack generated binaries"
**Snapshot time:** 2026-11-23 10:03:54 IST (local), repository root `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
**Role of this document:** point-in-time audit only. It edits no source, stages nothing, and performs no commit, push, or PR action.

---

## 0. Read this first — concurrency and evidence limits

1. This audit ran alongside 9 other subagents in the same working tree. The
   working tree changed **while this audit was running**: at 10:03:35 IST the
   non-scratch uncommitted set was 9 files; at 10:03:54 IST it was the same 9
   files plus a new untracked `docs/FINAL_DELIVERY_REPORT.md`, and the line
   counts of `benchmarks/safety_compare/METHODOLOGY.md` and `PROOF.md` had
   grown (e.g. METHODOLOGY.md from `+64/-?` to `+138/-?`). **All file lists and
   diff stats below are the 10:03:54 IST snapshot and will drift.** Re-run the
   verification commands in §8 immediately before any integration.
2. No `git fetch` was performed. "In sync" below means local HEAD equals the
   locally cached `origin/agent/modern-stdlib` tracking ref; it is **not** a
   claim about the current state of the remote server.
3. No build, test, benchmark, or live-game run was executed as part of this
   audit. Commands in §8 marked "recommended" are provided, not claimed as
   passing. Where this document says a check was performed, it was a read-only
   git/filesystem inspection and the exact command is shown.
4. Ownership of the uncommitted files listed in §3 belongs to the other
   parallel agents. This document proposes groupings only; it does not claim
   authority to commit them.

---

## 1. Headline risks

| # | Risk | Evidence (snapshot) | Severity |
|---|------|---------------------|----------|
| H1 | **Nine generated Mach-O arm64 executables remain tracked in the git index** under `benchmarks/` and `tests/`, despite the HEAD commit message "untrack generated binaries". HEAD removed 14 such binaries from `benchmarks/safety_compare/results/bin/`, but the nine in §4 are still tracked. | §4 | **High** |
| H2 | **Three of those tracked binaries are currently modified in the working tree** (`c_to_cp_caller`, `caller_direct`, `cp_to_c_driver`) alongside their `results.log` files. This is active regeneration of checked-in artifacts; committing them would bake architecture-specific, host-dependent binaries into history. | §3, §4 | **High** |
| H3 | **`libc/src/string/upstream_safecrt.c` is wired into `tests/libc/build_libc.sh` but NOT into the canonical `libc/CMakeLists.txt` `LIBC_SOURCES` list.** The CMake static/shared library build silently omits the newly integrated upstream safecrt implementation. | §6 | **High** |
| H4 | **Stale shell references to deleted Python files remain**: `tests/libc/install_smoke.sh:17` calls `tests/libc/validate_contracts.py` and `tests/stdlib/run_stdlib_tests.sh:241` calls `tests/stdlib/coverage.py`; neither file exists on disk. These code paths are dangling. | §5 | **Medium** |
| H5 | **`libc/src/c23/remainders.c` and `benchmarks/stdlib/allocator/tracking_toggle.c` are added but not wired into any automated build/run script.** `remainders.c` is documented as a deliberately BLOCKED probe (no emitted symbol); `tracking_toggle.c` is documented with manual `clang` commands only. Both need an explicit keep-or-wire decision. | §6 | **Medium** |
| H6 | **Working tree is not clean and is actively being written by other agents.** Any release tag, commit, or PR cut from this snapshot would capture partially completed edits (notably the benchmark proof rewrite and binary regeneration). | §0, §3 | **Medium** |
| H7 | Prior audit docs are partly stale relative to this snapshot: `docs/libc/RELEASE_READINESS.md` says 3 of 5 new C sources are unwired; at this snapshot only `remainders.c` (and the newly added `upstream_safecrt.c`) are absent from CMake. Reconcile before quoting them. | §7 | **Low** |

---

## 2. Branch sync and index state

Commands run (read-only):

```sh
git rev-parse HEAD
git rev-parse origin/agent/modern-stdlib
git status -sb | head -1
git branch -vv
git diff --cached --stat
```

Observed output:

```text
2294cb1e7d880d55a8403b7c7e0e46a6ce85a41f   # HEAD
2294cb1e7d880d55a8403b7c7e0e46a6ce85a41f   # origin/agent/modern-stdlib (cached tracking ref)
## agent/modern-stdlib...origin/agent/modern-stdlib
```

- Local branch and the cached remote-tracking ref are **identical** (no ahead/behind markers).
- Upstream is configured as `origin/agent/modern-stdlib`.
- **Staging area is empty** (`git diff --cached --stat` produced no output). Nothing is staged.
- Conclusion: **branch is in sync with its last-known origin ref; remote freshness unverified** (no fetch, per §0.2).

Recent history (head of `git log --oneline -15`):

```text
2294cb1 Finalize C+/Rust benchmark proof and untrack generated binaries
ce018ca Integrate proven upstream implementations into C+ stdlib
1112745 Untrack regenerable resource-exhaustion runner artifacts
a85f7f5 Add resource exhaustion evidence notes
4011336 Add resource exhaustion suite runner and restore probe
ec60ef5 Finalize allocation overflow evidence and clean generated security artifacts
c5d2582 Complete Task 6 security hardening and fix reproduced defects
ec8f2e2 Finalize system, allocator and security deliverables
664af8b Add Task 5 allocator architecture and Task 6 security audit deliverables
960c997 Add Task 4 ABI lab, Task 3 system-safety suites, and Task 5 allocator work
cbcea57 Merge remote-tracking branch 'origin/agent/modern-stdlib' into agent/modern-stdlib
5bb3b3a Complete native C+ test conversion, strict-header adaptation and Task 2 safety suites
487ab89 Convert Python test tooling to native C+ and add 100 fuzz configs
9a061ae Wire new C23/atomic sources into builds and fix aligned allocation tracking
ed2ac0f Add expectation fixtures for the new memory bounds deep test
```

---

## 3. Uncommitted changes (10:03:54 IST snapshot)

`git status --porcelain`, excluding `scratch/`:

```text
 M benchmarks/safety_compare/METHODOLOGY.md
 M benchmarks/safety_compare/PROOF.md
 M benchmarks/safety_compare/validate_results.cp
 M docs/stdlib/upstream-reuse.md
 M tests/abi/c_to_cp/artifacts/c_to_cp_caller
 M tests/abi/c_to_cp/artifacts/caller_direct
 M tests/abi/c_to_cp/artifacts/results.log
 M tests/abi/cp_to_c/artifacts/cp_to_c_driver
 M tests/abi/cp_to_c/artifacts/results.log
?? docs/FINAL_DELIVERY_REPORT.md
```

`git diff --stat`:

```text
 benchmarks/safety_compare/METHODOLOGY.md      | 138 ++++++++++++++++++++++++--
 benchmarks/safety_compare/PROOF.md            |  74 ++++++++++++++++++--
 benchmarks/safety_compare/validate_results.cp |  45 +++++++++++++++++-
 docs/stdlib/upstream-reuse.md                 |   8 +-
 tests/abi/c_to_cp/artifacts/c_to_cp_caller    | Bin 52032 -> 52032 bytes
 tests/abi/c_to_cp/artifacts/caller_direct     | Bin 52032 -> 52032 bytes
 tests/abi/c_to_cp/artifacts/results.log       |   2 +-
 tests/abi/cp_to_c/artifacts/cp_to_c_driver    | Bin 71824 -> 71824 bytes
 tests/abi/cp_to_c/artifacts/results.log       |   4 +-
 9 files changed, 245 insertions(+), 26 deletions(-)
```

Notes:

- `docs/stdlib/upstream-reuse.md` — the change visible at the start of this
  audit (before other agents' edits) was a 2-line correction to integration
  records **I8/I9**, removing the malformed line-count fragments
  `+120/−lines` and `+118/−lines` from the `stdlib/string.hp` and
  `stdlib/vector.hp` cells. Current diff is 8 lines; re-read before grouping.
- `scratch/bugs51.cp` … `scratch/bugs97.cp` (47 untracked files) are excluded
  from this audit per instruction ("excluding scratch/bugs"). They are
  covered by the existing `.gitignore` negation `!scratch/bugs*.cp`.
- `docs/FINAL_DELIVERY_REPORT.md` appeared during the audit; it is untracked
  and owned by another agent. It is not assessed here.
- `docs/stdlib/RELEASE_READINESS_FINAL.md` (this file) is a new untracked file
  authored by this audit; it will appear in a fresh `git status`.

---

## 4. Generated binaries tracked in the git index under `benchmarks/` and `tests/`

Command used:

```sh
git ls-files benchmarks/ tests/ | while read f; do
  ft=$(file -b "$f"); case "$ft" in *Mach-O*|*ELF*|*archive*|*shared\ object*)
    echo "$f :: $ft";; esac; done
```

Nine genuine Mach-O 64-bit arm64 executables are tracked (script files with
the executable bit set were excluded from this list; they are not binaries):

| Path | Size (bytes) |
|------|--------------|
| `benchmarks/stdlib/allocator/allocator_bench` | 53376 |
| `benchmarks/stdlib/allocator/tracking_off` | 33872 |
| `benchmarks/stdlib/allocator/tracking_on` | 52552 |
| `tests/abi/c_to_cp/artifacts/c_to_cp_caller` | 52032 |
| `tests/abi/c_to_cp/artifacts/caller_direct` | 52032 |
| `tests/abi/cp_to_c/artifacts/cp_to_c_driver` | 71824 |
| `tests/stdlib/agent1_handoff/cross_tu/results/case_a` | 92184 |
| `tests/stdlib/agent1_handoff/cross_tu/results/case_b` | 74024 |
| `tests/stress/cplus_breaker_v3/25_freed_len_trap` | 53264 |

Cross-check of HEAD commit:

```sh
git show --stat --format= HEAD | grep -cE 'Bin [0-9]+ -> 0 bytes'
# 14
```

So HEAD removed 14 generated binaries from
`benchmarks/safety_compare/results/bin/` (e.g. `clang_O2_legacy_speed`,
`cplus_legacy_*`, `rustc_O_*`) and added
`benchmarks/safety_compare/results/.gitignore`, but the nine artifacts above
were out of scope for that cleanup and remain tracked. **Cleanup is
incomplete.** Three of them are being regenerated right now (§3, H2).

Recommendation (not executed): untrack these nine paths in a dedicated commit
and add explicit `.gitignore` rules, but only after confirming each
corresponding runner rebuilds its artifact from source first — e.g.
`benchmarks/stdlib/allocator/run.sh` already rebuilds `allocator_bench` from
`allocator_bench.c` with `clang`, while the ABI artifact runners were **not**
verified in this audit. Do not assume they are reproducible.

---

## 5. Python files remaining

Commands run:

```sh
find . -path ./.git -prune -o -type f -name '*.py' -print | sort
git ls-files '*.py'
find . -path ./.git -prune -o -type f \( -name 'test_*.py' -o -name '*_test.py' -o -name '*test*.py' \) -print
```

Observed:

- **No `test_*.py` / `*_test.py` / `*test*.py` files exist anywhere** in the
  working tree (empty output).
- Tracked `.py` files in the index (2):

  | File | Nature | Current use |
  |------|--------|-------------|
  | `benchmarks/timeit.py` | benchmark timing helper | invoked by `benchmarks/run_bench.sh:78,83,88`; documented in `benchmarks/safety_compare/METHODOLOGY.md:195` |
  | `tests/security/hostile_inputs/gen_fixtures.py` | deterministic fixture generator | documented in `tests/security/hostile_inputs/NOTES.md:27,32,190` |

- Untracked/ignored: `scratch/generate_bugs.py` (not in the index; ignored by
  `scratch/*` since only `bugs*.cp` and `demo.c` are negated).
- No `tools/*.py` files exist in this repository.
- Stale bytecode (`__pycache__/*.pyc`) may remain under `tests/`; generated and
  ignored via `.gitignore`.

Additional findings beyond file presence:

- Dangling references to Python files that **no longer exist** (verified with
  `grep -rn` and `[ -e … ]`):

  ```text
  tests/libc/install_smoke.sh:17:python3 "$ROOT/tests/libc/validate_contracts.py" ...   # MISSING
  tests/stdlib/run_stdlib_tests.sh:241: if python3 "$ROOT/tests/stdlib/coverage.py" ...  # MISSING
  tests/fuzz100/run.sh:6: comment "…lives in run.py"                                   # MISSING run.py
  ```

  `tests/stdlib/agent2/run_all.py` is also missing. These are H4.
- Inline `python3 - <<'PY'` heredocs remain in several shell scripts
  (catalogued in `docs/stdlib/PYTHON_REMOVAL_PLAN.md`), e.g.
  `benchmarks/run_bench.sh:94`, `benchmarks/safety_compare/run_bench.sh:27`,
  `tests/conformance/run_conformance.sh:38,238`,
  `tools/run_final_evidence.sh:74,261,367`. The removal plan explicitly marks
  these as **orchestrator decision**, with no replacement supplied.

Conclusion: the Python *test-runner* conversion is complete; Python *helpers*
and inline heredocs remain by design and are not a release blocker on their
own, but the dangling references (H4) are.

---

## 6. Source files added but not wired into build scripts

Method: list source files added in recent commits (`git show --diff-filter=A
--name-only` for commits `2294cb1 … 9a061ae`), then grep for each basename in
`CMakeLists.txt`, `libc/CMakeLists.txt`,
`tests/sdk/consumer/CMakeLists.txt`, and in shell runners under `tests/`,
`benchmarks/`, `tools/`, `scripts/`.

### 6.1 Confirmed wiring gaps

| Source | Added in | CMake (`libc/CMakeLists.txt`) | Shell runner | Assessment |
|--------|----------|-------------------------------|--------------|------------|
| `libc/src/string/upstream_safecrt.c` | `ce018ca` | **ABSENT** from `LIBC_SOURCES` | **present** in `tests/libc/build_libc.sh` (long source list) | **H3** — two build paths disagree; the CMake library omits the new implementation. |
| `libc/src/c23/remainders.c` | `5bb3b3a` | **ABSENT** | **ABSENT** from `tests/libc/build_libc.sh` | **H5** — documented as intentional: source header says "all BLOCKED, no body"; `docs/libc/C23_REMAINDERS_CLOSURE.md` calls it a probe record with no symbol. Needs keep-or-remove decision, not silent omission. |
| `benchmarks/stdlib/allocator/tracking_toggle.c` | `ec8f2e2` | n/a | **ABSENT from all `*.sh` and CMake files** | **H5** — only documented with manual `clang` commands in `benchmarks/stdlib/allocator/NOTES.md:144` and `results/tracking_toggle.txt:2-3`. `run.sh` builds `allocator_bench.c`, not this file. |

### 6.2 Confirmed wired

- `libc/src/math/math_c23_farith.c`, `math_c23_hypot3_f16.c`,
  `math_c23_next.c`, `libc/src/stdlib/atomic_wrappers.c` — all present in
  both `libc/CMakeLists.txt` and `tests/libc/build_libc.sh`.
- `libc/src/c23/remainders.c` is the only `libc/src/**/*.c` file absent from
  `libc/CMakeLists.txt` besides `upstream_safecrt.c` (checked with a loop over
  `find libc/src -name '*.c'`).
- `benchmarks/safety_compare/{alloc_bench.rs,bench_modern.rs,safety_matrix.rs,legacy_speed.c}` —
  not named individually in `run_all.sh`, but that runner **auto-discovers**
  sources via globs (`for _f in "$HERE"/*.c "$HERE"/*.rs "$HERE"/*.cp;` at
  line 363) and dispatches by extension (lines 188–199). They are wired.
- `benchmarks/stdlib/allocator/allocator_bench.c` — built by `run.sh`.
- `tests/abi/legacy_c/lc*.c`, `tests/abi/c_to_cp/*.c`,
  `tests/abi/cp_to_c/*.c`, `tools/abi_compare/*.c` — driven by their
  respective ABI shell runners (not fully re-verified in this audit beyond
  presence of `run*.sh` scripts in those directories).

---

## 7. Prior audit documents to reconcile

| Document | Status relative to this snapshot |
|----------|-----------------------------------|
| `docs/libc/RELEASE_READINESS.md:22-23,227` | Reports 3 of 5 new C sources unwired and a failing final-evidence report. At this snapshot `math_c23_farith.c`, `math_c23_hypot3_f16.c`, `math_c23_next.c` and `atomic_wrappers.c` are wired; stale, needs refresh. |
| `docs/stdlib/NO_PYTHON_TESTING.md` | V1 (no `.py` under `tests/`/`tools/`) still holds. Its V4 note about dangling `validate_contracts.py` / `coverage.py` references is **confirmed current** (§5). |
| `docs/stdlib/PYTHON_REMOVAL_PLAN.md` | Lists the same 8 converted files and marks inline heredocs + `timeit.py` as out of scope / orchestrator decision. Still consistent. |
| `docs/libc/C23_REMAINDERS_CLOSURE.md` | Still consistent: `remainders.c` intentionally emits no symbol. |
| `docs/stdlib/upstream-reuse.md` | Currently modified by another agent (I8/I9 line-count cleanup); do not treat as settled until that change lands. |

---

## 8. Exact commit grouping plan

This is a **proposed grouping only** — file ownership and commit authorship
belong to the orchestrator/other agents (§0.4). Nothing here was staged or
committed by this audit.

**Group C1 — ABI generated-artifact cleanup (do NOT commit binaries as-is)**
- Paths: `tests/abi/c_to_cp/artifacts/c_to_cp_caller`,
  `tests/abi/c_to_cp/artifacts/caller_direct`,
  `tests/abi/c_to_cp/artifacts/results.log`,
  `tests/abi/cp_to_c/artifacts/cp_to_c_driver`,
  `tests/abi/cp_to_c/artifacts/results.log`.
- Decision needed: either (a) revert these five working-tree edits and keep
  the committed binaries, or (b) untrack the binaries, add `.gitignore`, and
  commit only the runner/script changes that make them reproducible. Committing
  the regenerated Mach-O blobs is the option to avoid (H1/H2).
- Suggested message: `chore(abi): stop regenerating checked-in artifacts; ignore build outputs`.

**Group C2 — Benchmark proof/evidence correction** (owned by other agents)
- Paths: `benchmarks/safety_compare/METHODOLOGY.md`,
  `benchmarks/safety_compare/PROOF.md`,
  `benchmarks/safety_compare/validate_results.cp`.
- Suggested message: `docs(bench): correct safety-compare proof and methodology evidence`.
- Gate: re-run §9 command B4 and confirm numbers in PROOF.md match the
  regenerated evidence before committing.

**Group C3 — Upstream integration record cleanup**
- Paths: `docs/stdlib/upstream-reuse.md`.
- Scope: remove the malformed `+NNN/−lines` fragments from I8/I9 (and any
  further edits the owning agent made).
- Suggested message: `docs(upstream): fix malformed line-count records for I8/I9`.

**Group C4 — Build wiring fix (H3)**
- Paths: `libc/CMakeLists.txt` (add `src/string/upstream_safecrt.c` to
  `LIBC_SOURCES`). No source edits were made by this audit; this is a planned
  change for the owning agent.
- Suggested message: `build(libc): wire upstream_safecrt.c into CMake library build`.
- Gate: rebuild both static and shared targets, run `tests/libc/run_libc_tests.sh`.

**Group C5 — Python dangling-reference fix (H4)**
- Paths: `tests/libc/install_smoke.sh` (line 17),
  `tests/stdlib/run_stdlib_tests.sh` (line 241), and the stale comment in
  `tests/fuzz100/run.sh` (line 6).
- Suggested message: `fix(tests): remove dangling calls to deleted Python runners`.

**Group C6 — Untrack remaining generated binaries (H1)**
- Paths: the nine files in §4, plus `.gitignore` additions.
- Suggested message: `chore: untrack regenerable benchmark and ABI binaries`.
- Gate: verify every affected runner rebuilds its artifact (§4).

**Group C7 — New reports (ownership TBD)**
- Paths: `docs/FINAL_DELIVERY_REPORT.md` (untracked, other agent),
  `docs/stdlib/RELEASE_READINESS_FINAL.md` (this audit).
- Suggested message: `docs: add final delivery and release-readiness audit reports`.

Recommended order: C4 → C5 → C2/C3 → C6/C1 → C7. Keep C1/C6 (binary
removal) separate from feature/doc commits so a revert does not discard
evidence work.

---

## 9. Verification commands

### 9.1 Snapshot re-checks performed in this audit (read-only)

```sh
# A1 — working tree, excluding scratch
git status --porcelain | grep -v '^?? scratch/'
git diff --stat
git diff --cached --stat

# A2 — branch sync against cached origin ref (no fetch)
git rev-parse HEAD origin/agent/modern-stdlib
git status -sb | head -1

# A3 — generated binaries still tracked
git ls-files benchmarks/ tests/ | while read f; do
  ft=$(file -b "$f" 2>/dev/null)
  case "$ft" in *Mach-O*|*ELF*|*archive*|*shared\ object*) echo "$f :: $ft";; esac
done

# A4 — Python files and dangling references
find . -path ./.git -prune -o -type f -name '*.py' -print | sort
git ls-files '*.py'
grep -rn --include='*.sh' -E 'validate_contracts\.py|coverage\.py|run\.py' tests/

# A5 — build wiring gaps
grep -nF 'upstream_safecrt' libc/CMakeLists.txt
grep -nF 'remainders.c' libc/CMakeLists.txt tests/libc/build_libc.sh
grep -rn --include='*.sh' --include='CMakeLists.txt' -F 'tracking_toggle.c' .
```

### 9.2 Recommended pre-merge verification (NOT run in this audit)

Use a fresh, gitignored build directory so other agents' `build/` work is
untouched. Configure requires LLVM/Clang dev packages per root `CMakeLists.txt`.

```sh
# B1 — clean CMake build + CTest (fresh dir, gitignored via build-*/)
cmake -S . -B build-audit-verify -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-audit-verify --parallel
ctest --test-dir build-audit-verify --output-on-failure

# B2 — libc shell build and libc suite (covers the H3 wiring fix)
sh tests/libc/build_libc.sh
sh tests/libc/run_libc_tests.sh

# B3 — native test tools + main suites
sh tests/native/build_native_tools.sh
CPLUS_BIN=./build-audit-verify/c+ sh tests/run_tests.sh

# B4 — benchmark evidence regeneration (for C2 gate)
sh benchmarks/safety_compare/run_all.sh
sh benchmarks/stdlib/allocator/run.sh

# B5 — confirm the nine binaries are rebuildable BEFORE untracking (C6 gate)
#       Run each owning runner in a clean checkout copy and confirm it recreates
#       the artifact; do not delete from the index until this is proven.

# B6 — final clean-tree gate (expect: only intended new docs after C1–C6 land)
git status --porcelain | grep -v '^?? scratch/'
git diff --cached --stat
git rev-parse HEAD origin/agent/modern-stdlib
```

Known caveat from prior evidence (not re-verified here):
`docs/stdlib/NO_PYTHON_TESTING.md` V2 recorded `sh tests/native/build_native_tools.sh`
building only 4/8 native tools. Treat B3 as a gate, not a formality, and
report the observed result rather than assuming success.

---

## 10. Decision authority

**PR creation and any branch push are the orchestrator's decision, not this
audit's.** This subagent has no authority to and did not run `git add`,
`git commit`, `git push`, or any PR/GitHub command. No source file was edited;
the only file authored is `docs/stdlib/RELEASE_READINESS_FINAL.md`.

No claim in this document is derived from a build, test, benchmark, emulator,
or live-game run performed by this audit. All statements are either direct
read-only observations of git/filesystem state at the stated snapshot, or
explicitly labelled as proposals, recommendations, or gates.

# Final integration audit — ABI-preserving checked layer + benchmark/upstream work

- **Audit date:** 2026-11-23.
- **Branch / HEAD at audit time:** `agent/modern-stdlib`, initially HEAD
  `2294cb1e7d880d55a8403b7c7e0e46a6ce85a41f`, tracking `origin/agent/modern-stdlib`
  ("up to date"). **The branch advanced to `511afc9` ("Reconcile final
  benchmark validator and upstream reuse evidence") during this audit** — a
  sibling agent committed the three tracked edits that were uncommitted at the
  first read (§6). Earlier evidence docs were captured at HEAD
  `cbcea57174e5c6b26d07bcc4e1b5c8f7501b340a`. These are snapshot boundaries,
  not contradictions — results below are quoted from the named artifact and its
  own recorded HEAD, never re-synced silently.
- **Volatile working tree:** 10 sibling agents are working in parallel in this
  checkout. `git status` changed materially between the first and last reads of
  this audit (new commits, regenerated/deleted `tests/stdlib/agent2/results/*`,
  an untracked native-runner manifest set). §6 reports both the initial state
  and the latest observed state; any path claim in this document is "as
  observed at the stated read," not a guarantee of the final tree.
- **Role of this document:** evidence-only final integration audit. It reads
  existing docs/artifacts and reports an inventory. **This task changed no
  source, test, build file, config, or git index entry.** The only file created
  is this document.
- **Evidence labels:** **[ON DISK]** = artifact present in this checkout and
  inspected for this audit; **[QUOTED]** = count/result quoted from a named
  sibling doc or artifact, not re-executed here; **[PENDING]** / **[UNTESTED]** =
  no recorded result; **[BLOCKED]** = external cause with no pass/fail outcome.

---

## 1. Headline findings

1. **No ABI-visible API change is claimed.** The checked layer is implemented as
   additive, header-only `static inline` helpers and new value descriptors over
   the existing `stdlib/error.hp` side table; public `T *` stays one word,
   existing signatures/layouts/error codes are unchanged, and the only new C
   symbols are two prefixed safeclib entries (`cplus_sc_upstream_bsearch_s`,
   `cplus_sc_upstream_memrchr_s`). The measured evidence is unchanged layouts
   (70/70 × 2 toolchains) and a clean symbol/export audit (69/69); no ABI was
   modified or proven stable beyond the tested subset.
2. **9/9 upstream integrations have a changed source file and an executed
   regression test** with recorded rc 0, independently re-run by the
   `upstream-reuse.md` author on 2026-11-23 — not asserted from builds or studies.
3. **Benchmark proof is recorded, not fabricated**, but every C+ *safe* timing
   row in the final driver is **blocked** by the external
   `CPLUS_EXIT_ARITHMETIC` skew; the driver result is `ok=15 blocked=5 fail=0`
   with `BENCH_VALID 192 0` (post-fix; `BENCH_BLOCKED 5` reported separately).
4. **Suite aggregate is BLOCKED / NOT GREEN:** 5 of 6 serialized suites exit
   non-zero; only `tests/abi/run_abi_compat.sh` is clean (exit 0).
5. **Three external blockers remain and are not fixable in this repository:**
   `CPLUS_EXIT_ARITHMETIC` (external c-plus-core runtime/header skew),
   ASan/Leak/Thread/Memory/CFI unsupported by the backend probe, and
   Windows/Linux/x86-64 untested. A host-SDK issue and a native-tool source
   mismatch are recorded separately.
6. **Working tree is volatile and dirty:** the 3 tracked edits seen at first
   read were committed by a sibling (`511afc9`); the latest read shows a
   different dirty set (SAFETY_MATRIX_NOTES.md modified, regenerated/deleted
   `agent2/results/*`, 47 untracked scratch files, new native-runner artifacts).
   None were created by this audit; all are owned by other agents/tasks.

---

## 2. Scope of "this phase"

The branch diverges from `origin/main` at `0b1c1516bb…` and carries ~3,002
changed files in total — far broader than this audit. "This phase" is scoped to
the ABI-preserving checked layer, upstream reuse, and benchmark proof delivered
on `agent/modern-stdlib` in the recent local commits and working-tree edits:

| Commit | Subject | Relevance |
|---|---|---|
| `2294cb1` | Finalize C+/Rust benchmark proof and untrack generated binaries | benchmark proof docs/artifacts |
| `ce018ca` | Integrate proven upstream implementations into C+ stdlib | 9 upstream integrations + benchmark suite |
| `1112745` | Untrack regenerable resource-exhaustion runner artifacts | evidence hygiene |
| `a85f7f5` | Add resource exhaustion evidence notes | safety evidence |
| `4011336` | Add resource exhaustion suite runner and restore probe | safety evidence |
| `ec60ef5` / `c5d2582` / `ec8f2e2` / `664af8b` / `960c997` | allocator/security/ABI-lab/allocator-architecture work | checked-layer foundation |

---

## 3. Files changed by this phase (inventory)

### 3.1 Checked layer / safe wrappers — source (from `ce018ca`)

- `stdlib/alloc_policy.hp` — **new**, mimalloc overflow/size-class predicates.
- `stdlib/gsl_adapt.hp` — **new**, GSL narrow/not_null/span re-expression.
- `stdlib/lifetime_adapt.hp` — **new**, SaferCPlusPlus lifetime tokens.
- `stdlib/memory.hp` — **new / modified**, additive bounds-aware wrappers
  (`stdlib_mem_capacity/count/token/check/copy/move/set/borrow/transfer/release`);
  also extended with SoftBoundCETS check predicates (+130 lines).
- `stdlib/allocator.hp` — modified (+108), Fil-C liveness/bounds queries.
- `stdlib/json.hp` — modified (+122), yyjson depth/length budgets.
- `stdlib/string.hp` — modified (+120), MS STL growth/overlap-safe append.
- `stdlib/vector.hp` — modified (+118), MS STL growth + commit-last guards.
- `libc/src/string/upstream_safecrt.c` — **new**, ported `bsearch_s`/`memrchr_s`.
- `libc/install-include/cplus_safecrt_upstream.h` — **new**, public declarations.
- `tests/libc/build_libc.sh` — wiring for the new safecrt source.

### 3.2 Integration tests and sibling notes (from `ce018ca`)

- `tests/libc/test_safecrt_upstream.c`
- `tests/stdlib/upstream_reuse/{alloc_policy,filc_diag,gsl_adapt,json_yyjson,lifetime_adapt,softbound,string_stl,vector_stl}.cp`
- `tests/stdlib/upstream_reuse/{alloc_policy,filc_diag,gsl_adapt,json_yyjson,lifetime_adapt,softbound,string_stl,vector_stl}_NOTES.md`
- `docs/stdlib/upstream_reuse/SAFECRT_INTEGRATION.md`
- Earlier phase: `tests/stdlib/deep_memory_bounds.cp` + `docs/stdlib/MEMORY_BOUNDS_CONTRACT.md`
  (61 checks, 0 failures in three modes — see §4 verification table).

### 3.3 ABI evidence layer

- Committed sources: `tests/abi/abi_compat_{pointers,layouts,header_neutrality,c_driver,c_part,cplus_caller,cplus_lib}.c/.cp`,
  `tests/abi/abi_compat_shared.h`, `tests/abi/abi_compat_expected_{libc,safecrt}_symbols.txt`,
  `tests/abi/run_abi_compat.sh`; `tests/abi/fingerprints/baseline.{json,txt}`
  touched by `ce018ca`.
- Committed docs: `docs/abi/compatibility-matrix.md`,
  `docs/abi/source_audit.md`, `docs/abi/external_blocker_evidence.md`;
  `docs/libc/ABI_EVIDENCE_PLAN.md`.
- Probe sources present but **with no recorded result** **[UNTESTED]**:
  `tests/stdlib/agent2/abi_safety_probe.cp`,
  `tests/stdlib/agent2/abi_safety_alloc_lifecycle.cp`.

### 3.4 Upstream / safe-layer documentation

- `docs/stdlib/upstream-reuse.md` — 9-source study + §10 integration results
  (I1–I9), ABI ledger, and explicit non-claims.
- `docs/stdlib/abi-preserving-runtime-safety.md` — runtime metadata architecture,
  ABI guarantees, foreign-C limits, upstream table, VERIFIED/UNTESTED summary.
- `docs/stdlib/MEMORY_BOUNDS_CONTRACT.md` — additive thin-pointer wrapper contract.
- `docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md` — external skew record.
- `docs/stdlib/upstream_study/*.md` — 9 studies (original 7 + `trapc.md` +
  `softboundcets.md`); TrapC has **no source / license / commit** (UNDETERMINED),
  SoftBoundCETS is BSD-3-Clause tree / per-file MIT-style, **no upstream source
  copied** from either.
- `docs/stdlib/TASK6_FINAL_REPORT.md`, `docs/security/verification-status.md`,
  `docs/security/upstream-techniques.md`.

### 3.5 Benchmark proof (from `ce018ca` / `2294cb1`)

- Docs: `benchmarks/safety_compare/{PROOF,RESULTS,MEMORY_USAGE,METHODOLOGY,SAFETY_MATRIX_NOTES,VALIDATOR_NOTES,bench_rust_NOTES,bench_modern_NOTES,legacy_speed_NOTES,ALLOC_COMPARE_NOTES}.md`.
- Sources: `benchmarks/safety_compare/{modern,loop,nested}.{cp,c,rs,goose}`,
  `bench_modern.{cp,rs}`, `alloc_bench.{cp,rs}`, `legacy_speed.c`,
  `safety_matrix.{cp,rs}`, `validate_results.cp`, `run_bench.sh`, `run_all.sh`.
- Artifacts: `benchmarks/safety_compare/results/{table.txt,summary.json,evidence.csv,logs/,runs/}`
  (generated binaries were untracked by `2294cb1`).

---

## 4. Verification commands and results available

All rows below are **[QUOTED]** from the named artifact. At the first read this
audit independently confirmed **[ON DISK]** presence of
`build/abi/compat/results.log`, `build/abi/{host,cplus}.out`,
`tests/stdlib/agent2/results/summary.txt`, `tests/stdlib/agent2/SANITIZER_MATRIX.md`,
`benchmarks/safety_compare/results/table.txt`, `results/summary.json`,
`RESULTS.md`, `docs/stdlib/TASK6_FINAL_REPORT.md`,
`tests/security/resource_exhaustion/NOTES.md`, and `tests/fuzz100/RESULTS.md`.
**Note on volatility:** by the final read, a sibling agent had deleted
`tests/stdlib/agent2/results/summary.txt` / `summary.json` and most per-test
result files (apparently regenerating them via a native runner). The quoted
Agent 2 counts therefore remain *recorded evidence*, not a claim that the
artifact is present in the final tree; regenerate with
`sh tests/stdlib/agent2/run_all.sh` to reproduce. No suite was re-executed by
this audit.

| Area | Exact command | Recorded result | Exit | Artifact |
|---|---|---|---|---|
| ABI baseline | `sh tests/abi/run_abi_compat.sh` | `abi_compat: 69 passed, 0 failed` | 0 | `build/abi/compat/results.log` (tail verified: `RESULT: abi_compat: 69 passed, 0 failed`) |
| ABI layouts | `sh tests/abi/run_abi_layouts.sh` | `70/70 layouts match` on host clang **and** C+ backend; `diff -u` byte-identical; 0 mismatches | 0 | `build/abi/{host,cplus}.out` |
| Agent 2 aggregate | `sh tests/stdlib/agent2/run_all.sh` | 50 PASS, 0 FAIL, 0 BUILD_FAILURE, 0 TIMEOUT, 0 SKIPPED, 0 BLOCKED | 0 | `tests/stdlib/agent2/results/summary.txt` (header: `Agent 2 aggregate regression summary (487ab89)`, 90s/30s timeouts) — **artifact deleted by a sibling agent during this audit; re-run to regenerate** |
| Three-mode deep (earlier state) | deep runner, 28 tests × 3 modes | 84 invocations, 0 failures | 0 | `docs/stdlib/upstream-reuse.md` §7 / `docs/stdlib/TASK6_FINAL_REPORT.md` |
| Aggregate evidence | `sh tests/run_all_evidence.sh` | 23 aggregate suites, 0 failures | 0 | `docs/stdlib/upstream-reuse.md` §7 |
| Memory-bounds wrapper | `timeout 30 build/c+ build tests/stdlib/deep_memory_bounds.cp --strict --sanitize=none` + run; also default + `--sanitize=undefined` | `MEMORY_BOUNDS 61 0` in all three modes, empty stderr, stdout byte-identical; harness `3 passed, 0 failed` | 0 | `docs/stdlib/MEMORY_BOUNDS_CONTRACT.md` §5 |
| Sanitizer matrix | `timeout 30 build/c+ build tests/stdlib/deep_collections_alloc.cp --strict --sanitize=none …` | 72 attempts, 27 executed, **23 PASS / 4 FAIL** (`--sanitize=integer` unsigned overflow in `cplus_runtime.c:174:5`); ASan/Leak/Thread/Memory/CFI **UNSUPPORTED** | 1 (matrix) | `tests/stdlib/agent2/SANITIZER_MATRIX.md` (captured at earlier HEAD, **not re-executed** in current collection) |
| Resource exhaustion | suite runner, 8 harnesses × 2 modes | 16 passed, 0 failed; 78 checks × 2 modes all 0 failures; 1 in-test SKIP (`RLIMIT_NPROC`); UBSan symbol count 10 | 0 | `tests/security/resource_exhaustion/NOTES.md` |
| CVE-like gate | `sh tests/scenarios/cve_like/run_cve_like.sh` | `cve-like safe variants: 10 passed, 0 failed` | 0 | `tests/scenarios/cve_like/NOTES_{1..5}.md` |
| Fuzz100 | runner | `pass=56, fail=9, build_failure=1, timeout=0, skipped=6, blocked=0` (72 discovered, 66 executed) | 1 | `tests/fuzz100/RESULTS.md` |
| Final benchmark driver | `sh benchmarks/safety_compare/run_all.sh` | `ok=15 blocked=5 fail=0 required_failures=0`; `BENCH_VALID 192 0`, `BENCH_BLOCKED 5` | 0 | `benchmarks/safety_compare/results/table.txt`, `PROOF.md` §"Final reproducible-driver run" |
| Nine integrations | per-test builds (e.g. `build/c+ build tests/stdlib/upstream_reuse/gsl_adapt.cp --strict --sanitize=none`) | `test_safecrt_upstream` all checks passed; `ALLOC_POLICY 42/0`; `ALLOC_DIAG 26 0`; `GSL_ADAPT 80 0`; `JSON_YYJSON 35 0`; `LIFETIME_ADAPT 25 0`; `SOFTBOUND 51 0`; `28/0`; `VECTOR_STL 14 0` — **9/9 rc 0** | 0 each | `docs/stdlib/upstream-reuse.md` §10.5 |
| Serialized suite matrix | 6 commands (see §5) | 5 of 6 non-zero; only ABI clean | 1/1/1/1/0/1 | `docs/security/verification-status.md` §2/§6 |

### 4.1 Final benchmark driver details

- Matched checksums: `loop=19999999900000000`, `nested=16000000`,
  `modern=2200000200000`, `bench_modern=5595356797869224`,
  `alloc_bench=4750706934088118880`.
- Selected medians (`results/table.txt`): `alloc_bench` Rust `-O` 0.010404 s vs
  C+ legacy 0.039720 s; `bench_modern` Rust 0.023018 s vs C+ legacy 0.020533 s;
  `modern` Rust 0.009449 s vs C+ legacy 0.008306 s.
- **All C+ safe rows are `blocked`** by the external runtime/header mismatch;
  no safe-mode timing is claimed from this driver.
- Earlier direct `--strict --sanitize=none` measurements in per-tool NOTES are
  **not** sanitizer results and must not be presented as such.
- The allocator cross-check compares C+ at **-O0** against Rust at **-O**; its
  6.25× ratio is explicitly **not** attributable to allocator design alone.

---

## 5. Remaining blockers

### B1 — External `CPLUS_EXIT_ARITHMETIC` runtime/header skew **[BLOCKED]**

The instrumented path compiles the **external**
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c`
(line 129 uses `CPLUS_EXIT_ARITHMETIC`) against the **local**
`runtime/include/cplus/cplus.h`, whose enum ends at `CPLUS_EXIT_IO = 109`.
Every instrumented runtime build fails before any test code compiles:

```
cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
error: failed to compile instrumented C+ runtime
```

- Attributable failures: 51 (`tests/run_tests.sh`) + 58 (`run_deep_tests.sh`
  build failures) = **109 invocations**; also blocks C+ safe benchmark rows.
- Environment-only bridges used elsewhere change **no source and relax no
  check**: `CPLUS_INCLUDE=.../c-plus-core/runtime/include`, or a build-only
  `-DCPLUS_EXIT_ARITHMETIC=110` shim for the resource-exhaustion UBSan evidence.
- Fix belongs to the owner of external `c-plus-core` / Agent 1 (define the
  constant in the consumed header, or align include paths deterministically).
  **Not fixed by this phase.** Re-verification commands are in
  `docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md` §"Re-verification commands".

### B2 — ASan and four other sanitizers unsupported **[BLOCKED / no pass]**

Backend probe (`/usr/bin/clang`, Apple clang 16) rejects **ASan/`address`,
`leak`, `thread`, `memory`, `cfi`** with
`error: sanitizer '…' is not supported by the selected backend`. Host Apple
clang 16 ASan additionally aborts at startup even for `int main(){return 0;}`.
CVE-like *baseline* ASan/TSan evidence therefore used Homebrew clang 23.1.6.
No pass is claimed for any of the five unsupported configurations; a blocked or
unsupported sanitizer is never counted as a green result.

### B3 — Windows / Linux / x86-64 untested **[UNTESTED]**

All completed evidence is **macOS arm64 / Apple clang 16.0.0 / C+ 0.1.0**
(LP64). Untested: macOS x86_64, Linux (glibc/musl, any arch), Windows
(MSVC/UCRT x86 or x64). Not verified: calling-convention details (varargs,
aggregate returns, stack alignment, `__cdecl`/`__stdcall`), symbol
versioning/ordinals, C++ mangling, or an EXE-over-DLL round-trip. The concrete
plan (clang-cl/MSVC golden table, `#pragma pack` assertions, `dumpbin
/exports`, calling-convention probe) is **planned, not executed**
(`docs/libc/ABI_EVIDENCE_PLAN.md`).

### B4 — Host SDK / default-clang issue **[BLOCKED]**

Default `clang` on PATH is Homebrew 23.1.6 targeting `arm64-apple-macosx27.0.0`,
but `MacOSX27.sdk` is absent. This aborts the prerequisite builds of
`tests/libc/run_libc_tests.sh` (`errno.h` not found) and
`tests/crt/unit/run_unit_tests.sh` (`stdio.h` not found). The ABI suite avoids
it by pinning `PATH=/usr/bin:/bin` and `CPLUS_CLANG=/usr/bin/clang` (Apple
clang 16), which is why it alone is clean.

### B5 — Native-tool source mismatch **[BLOCKED]**

All 7 failed native-tool builds fail at `./stdlib/error.hp:223:30: error: use
of undeclared identifier 'reporting'` (follow-ons at 224/226). Distinct from
B1 (`0 of 7` native build logs contain `CPLUS_EXIT_ARITHMETIC`). Recorded as
observed; **no source was changed by this phase**.

---

## 6. Uncommitted changes / branch movement during the audit

Because sibling agents are active, this section reports two observations.

### 6.1 First read (HEAD `2294cb1`)

**Tracked modifications (3, none by this audit):**

| Path | Nature of edit (as read) |
|---|---|
| `benchmarks/safety_compare/METHODOLOGY.md` | Adds `bench_modern`/`alloc_bench` checksums; tightens fairness rule — C+ `-O0` vs Rust `-O` is not apples-to-apples; only fair speed config is `--release --strict --sanitize=none`; `--legacy-c` is explicitly not memory safe and not a fair C+ baseline. |
| `benchmarks/safety_compare/validate_results.cp` | Adds `status` field parsing; `blocked`/`skipped` rows are exempt from `runs>=7` and checksum rules, never contribute failures, and are counted on a `BENCH_BLOCKED <count>` line; strict validation for `ok`/no-status/unknown. |
| `docs/stdlib/upstream-reuse.md` | Clarifies that safeclib I1 also ports upstream bodies verbatim under MIT (correcting "only verbatim code"); removes stale `+120/−lines` / `+118/−lines` annotations from I8/I9. |

**Untracked (47):** `scratch/bugs51.cp` … `scratch/bugs97.cp` (tracked scratch
covers `bugs1`–`bugs50`; these are new, multi-include C/C+ repro-style files).
No changes were staged; this audit added/committed nothing.

### 6.2 Latest observed read (HEAD `511afc9`)

A sibling agent committed the three edits above in
`511afc9 Reconcile final benchmark validator and upstream reuse evidence`; they
are **no longer uncommitted**. The working tree is now dirty with a *different*
set of changes owned by other agents:

- **Modified:** `benchmarks/safety_compare/SAFETY_MATRIX_NOTES.md`.
- **Deleted:** most of `tests/stdlib/agent2/results/*` — the per-test
  `.build.log`/`.commands`/`.stderr`/`.stdout` files, `summary.txt`,
  `summary.json` — consistent with a native runner regenerating that directory.
- **Modified in place:** `deep_collections_alloc.{stderr,stdout}`,
  `deep_fs_task5.commands`.
- **New untracked:** `tests/stdlib/agent2/results/.native_agent.manifest`,
  `.native_build_err`, `.native_build_out`, `.native_deep.manifest`,
  `.native_stdin`, `deep_collections_alloc.diff`, plus the 47 scratch files.

### 6.3 Implications for handoff

- **Re-run the benchmark driver with the now-committed validator.** The recorded
  `BENCH_VALID 192 0` / `BENCH_BLOCKED 5` and `ok=15 blocked=5 fail=0` postdate the validator change
  that is now in `511afc9`; run `sh benchmarks/safety_compare/run_all.sh` and
  confirm the counts with the current binary before treating them as current.
- **Re-run `sh tests/stdlib/agent2/run_all.sh`** to regenerate the deleted
  aggregate result artifacts if the 50/0 result is needed as live evidence.
- `validate_results.cp` is C+ source under the project formatter rule; confirm
  the committed version passes the formatter check.
- The 47 scratch files and the native-runner artifacts are unrelated generated
  files; confirm with their owning agents before including or discarding them.
- **This audit did not stage, add, commit, or push anything, and still has not
  modified any file other than this document.**

---

## 7. Explicit non-claims

This audit and the underlying phase **do not** claim:

1. **No ABI change made or proven.** Probes and docs only for the documentation
   tasks; integrations are additive/`static inline`/new prefixed symbols. Passing
   checks mean the checked subsets matched captured expectations — not that an
   ABI was modified, or that future edits cannot break it.
2. **No memory-safety proof.** C+ checked APIs are runtime/API-boundary checks
   plus compile-time diagnostics, categorically different from Rust's
   compile-time ownership/borrow checking. Cases 06–08, 13 and 15 in the
   CVE-like matrix are explicitly *not fixable by libc* — raw aliases, stale
   pointers, dispatch indices and thread races were repaired caller-side.
3. **No Rust-equivalence / no "language X is faster/safer" proof.** Matching
   checksums verify one deterministic integer result. Rust `Vec`/`String` and
   Goose arrays are different stdlib implementations. Residual machine-code
   bounds/overflow checks were not disassembled/counted.
4. **No cross-platform proof.** Only macOS arm64 / Apple clang 16 / C+ 0.1.0
   was exercised; Linux, Windows and x86/x86-64 results are not predicted.
5. **No performance guarantee.** One machine, synthetic workloads, material
   run-to-run/pass-to-pass variance; non-instrumented loop timings are dominated
   by constant folding. Peak RSS is an observation, not a correctness/safety
   metric; `modern` C+ safe RSS is explicitly approximate.
6. **No live-game / Wine / DLL-injection / Windows x86 success** from any build
   or emulator result.
7. **No blanket "all green" claim.** The serialized aggregate is **BLOCKED /
   NOT GREEN** (5/6 suites non-zero). An earlier clean deep run
   (`84 passed, 0 failed`) is recorded separately and is **not** substituted for
   the current blocked run.
8. **No PENDING/UNTESTED result is a result.** PENDING cells are absence of
   evidence, not passes, failures, or timeouts. This includes: native
   `safety_matrix.cp` results/notes, `validate_results.cp` run output at
   synthesis time, p95 for the 5-variant `loop`/`nested` and 4-variant `modern`
   speed rows, Goose/plain-C/Fil-C peak RSS, and all Fil-C speed/safety results
   (no toolchain installed).
9. **No claim that blocked/skipped benchmark rows validate evidence.** They are
   exempted from checksum/run-count rules and counted separately, never as
   passes.
10. **No sanitizer pass claimed where unsupported or blocked.** ASan/Leak/Thread/
    Memory/CFI have no pass; canonical `--sanitize=undefined` results quoted
    from sibling notes are attributed to those notes, not re-asserted where the
    current run is blocked.
11. **No compiler/runtime fix claimed.** No `compiler/` or `runtime/` file was
    edited; B1/B5 remain for their owners. Underlying design limitations
    (exact-base-address lookup only, opt-in generation checks, unguarded
    generation wrap, address reuse, non-local exits, foreign-C boundaries) are
    documented findings, **not** fixed by this work.

---

## 8. Evidence index (key documents)

| Document | Role |
|---|---|
| `docs/stdlib/upstream-reuse.md` | 9-source upstream study, I1–I9 integration ledger, 9/9 independent re-runs, ABI/API change ledger, non-claims |
| `docs/stdlib/abi-preserving-runtime-safety.md` | Runtime metadata architecture, ABI guarantees, foreign-C limits, upstream table, VERIFIED/UNTESTED summary |
| `docs/stdlib/MEMORY_BOUNDS_CONTRACT.md` | Additive thin-pointer wrapper contract + 61-check, three-mode verification |
| `docs/abi/compatibility-matrix.md` | Snapshot ABI matrix (69/69 baseline; other directories PENDING/PARTIAL), platform scope, non-claims |
| `docs/security/verification-status.md` | Serialized 6-suite matrix, exact diagnostics, sanitizer status, BLOCKED aggregate |
| `docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md` | B1 root cause, exact failing commands, Agent 1 coordination boundary, re-verification criteria |
| `benchmarks/safety_compare/PROOF.md` | Speed/RSS/safety synthesis, final driver run (`ok=15 blocked=5 fail=0`, `BENCH_VALID 192 0`, `BENCH_BLOCKED 5`), non-claims |
| `benchmarks/safety_compare/METHODOLOGY.md`, `VALIDATOR_NOTES.md` | Protocol, fairness rules, validator semantics (see §6 for validator reconciliation) |

**Handoff condition:** re-run the benchmark driver with the now-committed
validator and regenerate the deleted Agent 2 result artifacts, reconcile the
untracked scratch/native-runner files with their owning agents, and obtain an
Agent 1 fix/decision for B1 (and B3 platform coverage) before any "all green" or
cross-platform ABI claim is made.

# Final delivery report — benchmark and upstream-integration work

**Branch:** `agent/modern-stdlib`. **Report date:** 2026-11-23.
**Current HEAD at synthesis time:** `2294cb1e7d880d55a8403b7c7e0e46a6ce85a41f`.

This document is an **evidence synthesis**, not a new experiment. Nothing was
re-run, rebuilt, staged or committed to produce it, and no source, header, test
or build file was modified. Every number below is copied from the named sibling
document or from an authoritative runner artifact on disk. Where a sibling
document is stale relative to a runner artifact, the runner artifact is cited
and the discrepancy is called out in §11.

**Headline counts (all qualified by §10–§11):**

| Area | Headline |
|---|---|
| Upstream integrations | **9/9** changed-source integrations independently built and executed, **0 failures** (`upstream-reuse.md` §10.5) |
| Agent-2 regression runner (authoritative artifact) | **56 PASS / 0 FAIL / 0 BUILD_FAILURE / 0 TIMEOUT / 0 SKIPPED / 0 BLOCKED**, TOTAL 56 (`tests/stdlib/agent2/results/summary.txt`) |
| Benchmark driver (authoritative artifacts) | **ok=15, blocked=5, fail=0**, driver exit 0 (`results/summary.json`, `results/table.txt`) |
| Suite aggregate at `cbcea571` | **BLOCKED / NOT GREEN** — 1 of 6 suites clean (ABI 69/0); 5 non-zero (`verification-status.md` §6) |
| ABI baseline | **69 passed / 0 failed** runner steps; **70/70** layouts; self-reported 80/80 layouts, 39/39 symbols (`compatibility-matrix.md`, `FINAL_EVIDENCE.md`) |
| Safety-outcome gate | CVE-like **10 passed / 0 failed**; resource exhaustion **16 passed / 0 failed**; safety matrix 8 recoverable scenarios both languages + 1 fatal ABORT |
| Sanitizer matrix (earlier HEAD) | 27 runs, **23 PASS / 4 FAIL** under `--sanitize=integer`; 5 sanitizers unsupported |
| Fuzz100 (current `RESULTS.md`) | **pass=56, fail=9, build_failure=1, skipped=6**, exit 1 |
| Native test tools (no-Python snapshot, `487ab89`) | **4 built / 4 failed** compile (build script exit 1) |

---

## 1. Implementation — modules and files

### 1.1 Integrated upstream components (9)

The integration commit `ce018ca3…` ("Integrate proven upstream implementations
into C+ stdlib") landed nine components. Source files exist on disk at the
stated paths; test sources live under `tests/stdlib/upstream_reuse/` (the study
document often writes them with the shortened `tests/stdlib/` prefix — see
§11).

| # | Component | Changed C+ source file(s) | Test on disk | Result |
|---|---|---|---|---|
| I1 | safeclib `bsearch_s` / `memrchr_s` | `libc/src/string/upstream_safecrt.c` (new), `libc/install-include/cplus_safecrt_upstream.h` (new) | `tests/libc/test_safecrt_upstream.c` | `all checks passed`, rc 0 |
| I2 | mimalloc size-overflow / size-class primitives | `stdlib/alloc_policy.hp` (new, 177 lines) | `tests/stdlib/upstream_reuse/alloc_policy.cp` | `ALLOC_POLICY 42 / FAILURES 0`, rc 0 |
| I3 | Fil-C liveness / bounds queries | `stdlib/allocator.hp` (modified, +108 lines) | `tests/stdlib/upstream_reuse/filc_diag.cp` | `ALLOC_DIAG 26 0`, rc 0 |
| I4 | Microsoft GSL `narrow` / `not_null` / `span` | `stdlib/gsl_adapt.hp` (new, 434 lines) | `tests/stdlib/upstream_reuse/gsl_adapt.cp` | `GSL_ADAPT 80 0`, rc 0 |
| I5 | yyjson depth / length budgets | `stdlib/json.hp` (modified, +122 lines) | `tests/stdlib/upstream_reuse/json_yyjson.cp` | `JSON_YYJSON 35 0`, rc 0 |
| I6 | SaferCPlusPlus lifetime tokens | `stdlib/lifetime_adapt.hp` (new, 268 lines) | `tests/stdlib/upstream_reuse/lifetime_adapt.cp` | `LIFETIME_ADAPT 25 0`, rc 0 |
| I7 | SoftBoundCETS check predicates | `stdlib/memory.hp` (modified, +130 lines) | `tests/stdlib/upstream_reuse/softbound.cp` | `SOFTBOUND 51 0`, rc 0 |
| I8 | Microsoft STL string growth / overlap-safe append | `stdlib/string.hp` (modified) | `tests/stdlib/upstream_reuse/string_stl.cp` | `28 / 0`, rc 0 |
| I9 | Microsoft STL vector growth / commit-last guards | `stdlib/vector.hp` (modified) | `tests/stdlib/upstream_reuse/vector_stl.cp` | `VECTOR_STL 14 0`, rc 0 |

Additional files landed for this work: `tests/stdlib/deep_memory_bounds.cp`
(capacity/count wrappers), four `tests/stdlib/agent2/abi_safety_*.cp` probes,
and `tests/abi/abi_compat_*.c/.cp` ABI probes. The only verbatim upstream code
in scope is the pre-existing `memset_explicit` copy from LLVM libc; all other
upstream work is adapted design, not copied code.

### 1.2 Benchmark / safety-compare harness

`benchmarks/safety_compare/` contains `run_bench.sh`, `run_all.sh`,
`run_one.pl`, `validate_results.cp`, the `{loop,nested,modern,bench_modern,
alloc_bench,safety_matrix}` sources for C+/C/Rust/Goose, `METHODOLOGY.md`,
`RESULTS.md`, `MEMORY_USAGE.md`, `PROOF.md`, `SAFETY_MATRIX_NOTES.md`, and the
authoritative `results/{summary.json,table.txt,evidence.csv}` plus per-run logs.
The allocator benchmark lives in `benchmarks/stdlib/allocator/` with its
`NOTES.md` and `results/allocator_bench.csv`.

### 1.3 Test-tool migration

`docs/stdlib/NO_PYTHON_TESTING.md` records the conversion of six tracked Python
test tools to native C+ binaries under `build/native-tools/`, with native shell
wrappers (`run_all.sh`, `run.sh`, `run_validate_contracts.sh`, …). No `.py`
source remains under `tests/` or `tools/`.

---

## 2. Testing — separated into compile-time and runtime

### 2.1 Compile-time results

- **No Python test sources remain:** `find tests tools -name '*.py'` is empty
  (exit 0); the four named wrappers and `tools/run_final_evidence.sh` contain no
  `python3` invocation. Six predecessor `.py` files are deleted in the working
  tree. (`NO_PYTHON_TESTING.md` V1/V3.)
- **All 8 native `.cp` tool sources exist.** At HEAD `487ab89`, **4 built**
  (`validate_contracts`, `test_compiler_contract_alignment`,
  `test_contract_consumption`, `test_contract_integrity`) and **4 failed to
  compile** under `--strict`: `coverage`, `run_adversarial`, `run_all`,
  `fuzz100_run`. Diagnostics are `[CPLUS-FFI] pointer parameter … has
  unverifiable extent` and one `[CPLUS-LIFETIME] variable 'total' is
  uninitialized`. Script exit 1; pre-existing binaries are stale and were not
  used as results. (V2 / §2 of that note.)
- **Compile-time gap cases:** an earlier evidence run recorded **27** cases
  classified `RUNTIME` — compiler capability gaps (no C+ ownership/lifetime
  diagnostic) that must be covered by runtime suites; these are **not passes**
  of a compile-time check. (`FINAL_EVIDENCE.md` compile-time suite.)
- **Strict analyser-driven source changes:** three `size_t bytes = 0;`
  initialisers were added to `stdlib/vector.hp` because the analyser does not
  infer writes through out-pointers. (`upstream-reuse.md` I9.)
- The C+ driver's `--emit-c` diff on the integer workloads showed no hot-loop
  checks are inserted in safe mode with sanitizers off. (`PROOF.md` §3.5.)

### 2.2 Runtime results

| Runner / suite (authoritative artifact) | Result | Exit | Source |
|---|---|---|---|
| `tests/stdlib/agent2/run_all.sh` | **56 PASS / 0 FAIL / 0 BUILD_FAILURE / 0 TIMEOUT / 0 SKIPPED / 0 BLOCKED** (29 deep + 16 agent2 + 11 existing) | 0 | `tests/stdlib/agent2/results/summary.txt` |
| Nine integration tests (independent rebuild+run) | **9/9 built, 0 failures**, each rc 0 with the self-check in §1.1 | 0 | `upstream-reuse.md` §10.5 |
| `tests/abi/run_abi_compat.sh` | `abi_compat: 69 passed, 0 failed` | 0 | `build/abi/compat/results.log` |
| `tests/scenarios/cve_like/run_cve_like.sh` | `cve-like safe variants: 10 passed, 0 failed` | 0 | `NOTES_5.md`, `PROOF.md` §5.1 |
| `tests/security/resource_exhaustion` | `16 passed, 0 failed`, 1 documented in-test SKIP; 78 checks × 2 modes, 0 failures | 0 | `NOTES.md`, `PROOF.md` §5.2 |
| `benchmarks/safety_compare/safety_matrix.cp/.rs` | 8 recoverable scenarios, both programs `MATRIX_DONE 8`, exit 0; 1 compile-only Rust `E0382` rejection; 1 C+ fatal ABORT exit 70 | 0 | `SAFETY_MATRIX_NOTES.md` |
| `tests/fuzz100/run.sh` (current) | `pass=56, fail=9, build_failure=1, timeout=0, skipped=6, blocked=0` | 1 | `tests/fuzz100/RESULTS.md` |
| `tests/run_tests.sh` (at `cbcea571`) | `C+ tests: 99 passed, 51 failed` (all 51 = `compilation failed` from the external blocker) | 1 | `verification-status.md` §2 |
| `tests/stdlib/run_deep_tests.sh` (at `cbcea571`) | `28 passed, 58 failed, 1 skipped` (all 58 = BUILD-FAIL from the external blocker) | 1 | `verification-status.md` §2 |
| `tests/native/build_native_tools.sh` (at `cbcea571`) | `1 built, 7 failed, 0 missing` (`reporting` source mismatch) | 1 | `verification-status.md` §2 |
| libc / CRT prerequisite builds (at `cbcea571`) | aborted — `errno.h` / `stdio.h` not found, no `MacOSX27.sdk` | 1 each | `verification-status.md` §2 |

**Suite aggregate at `cbcea571`: BLOCKED / NOT GREEN — 5 of 6 suites exited
non-zero; only the ABI suite was clean.** This is the status to quote for the
suite-level picture, not any single green suite. Blocked compilation is not a
failed fixture and is not a pass (§9).

---

## 3. Safety — allocation, ownership, cleanup, invalid input

C+ checked APIs are **runtime/API-boundary checks plus compile-time
diagnostics**. They are categorically different from Rust's compile-time
ownership/borrow checking. No Rust-equivalence and no 100% memory-safety claim
is made or implied anywhere in this report.

- **Allocation.** `calloc_s(SIZE_MAX,2)` is refused with `EOVERFLOW`; negative
  lengths and short lengths are refused; element-count product overflow is
  rejected at the library boundary; `realloc_s` failure preserves the original
  pointer and generation (case 05); an arena `offset+bytes` wrap is refused by
  an overflow-safe guard. Growth primitives are size-overflow-checked
  (mimalloc-derived predicates) and use commit-last mutation with scoped
  rollback (STL-derived; C+ keeps 2× growth, not STL's 1.5×). JSON parsing has
  explicit depth (16384) and input-length (64 MiB) budgets.
- **Ownership.** `stdlib/error.hp` keeps an open-addressed side table
  (`ptr`, `byte_size`, `elem_size`, `generation`) and exposes liveness/length/
  generation queries; `stdlib/memory.hp` adds thin bounds-aware wrappers;
  `stdlib/lifetime_adapt.hp` adds runtime generation tokens; `stdlib/gsl_adapt.hp`
  adds concrete `not_null`/`span` value descriptors. Generation checks refuse
  stale aliases and reissued blocks. Interior access requires callers to pass
  `(base, offset)` — exact-base-address lookup only; no fat/capability pointers.
- **Cleanup.** Double release is refused (`CPLUS_SAFE_EINVAL`, rc 22) via a
  bounded free table; use-after-free is detected via a bounded live-allocation
  table plus generation identity (stderr diagnostic, defined refusal); the
  default fail-fast violation trap aborts with exit 70 in fatal mode
  (subprocess only). Zero-length allocation is defined but differs from Rust:
  `malloc_s(0)` reports a tracked 1-byte block; `Vec::new()` has length 0.
- **Invalid input.** CVE-like cases 01–05, 10–14 show defined refusals or
  bounded outcomes; cases 06–08 (aliasing/UAF/write-after-free) and 13/15
  (dispatch index, thread race) are **explicitly NOT fixable by libc** — their
  repairs are caller-side (single ownership, save-next/unlink, bound check,
  mutex + join). The library gives checked allocation plus defence-in-depth
  liveness queries, not a "safe pointer".
- **Native outcome matrix.** 8 scenarios match observed tokens across both
  languages; the decisive difference is that C+ *runs and refuses* a freed
  pointer operation using runtime metadata, while safe Rust never produces a
  program for the equivalent use-after-move (`E0382`). Shared `OK`/`REFUSED`
  tokens are observed behaviour, **not** equivalent guarantees.

---

## 4. Sanitizers — supported, executed, unsupported

Backend capability is from the recorded driver probe
(`tests/stdlib/agent2/SANITIZER_MATRIX.md`) using `/usr/bin/clang` (Apple clang
16):

| Status | Sanitizers |
|---|---|
| **Supported** by probe | `none` (`strict-none`), `undefined` (UBSan), `integer` |
| **Unsupported** by probe | `address` (ASan), `leak`, `thread` (TSan), `memory`, `cfi` — build exit 1, no binary, **no pass claimed** |

**Executed evidence (earlier HEAD, not re-executed in the current collection):**

- `SANITIZER_MATRIX.md`: 9 tests × 3 accepted configurations = **27 runs,
  23 PASS and 4 FAIL**. The 4 failures are genuine `--sanitize=integer`
  unsigned-overflow findings at `cplus_runtime.c:174:5` in
  `deep_collections_alloc`, `deep_collections_boundary`, `deep_string_task4`,
  `deep_vector_task4` (run exit 134, so FAIL, not UNSUPPORTED).
- Resource-exhaustion UBSan: with an environment-only
  `-DCPLUS_EXIT_ARITHMETIC=110` bridge, **8/8 builds and 8/8 runs**, empty
  stderr, zero UBSan diagnostics; instrumentation verified by symbol count
  (10 `__ubsan` symbols vs 0 for strict-none). 78 checks × 2 modes, 0 failures.
- CVE-like baselines: original C files produce genuine ASan/TSan crashes using
  Homebrew clang 23.1.6 because Apple clang 16's sanitizer runtimes abort at
  startup on this host; safe variants re-checked under ASan/UBSan produced zero
  diagnostics. The Apple clang 16 runtime failure is a host issue, not a result
  about any program.

**Current-run caveat:** at HEAD `cbcea571` the deep harness's `ubsan` mode could
not build any of 29 selected tests because of the `CPLUS_EXIT_ARITHMETIC`
external blocker, so **no fresh UBSan or integer execution occurred**. The
benchmark driver likewise reports all C+ safe rows as `blocked` (§7). A
blocked or unsupported configuration is never counted as a pass. Sanitizer
results are evidence about specific executions, not a safety certificate.

---

## 5. Upstream reuse — exact components integrated

All nine rows satisfy the evidence standard: a changed source file **and** an
executed, self-checking regression test (§1.1). Pins, licences and rationale:

| ID | Component | Upstream pin | Licence | Integration |
|---|---|---|---|---|
| I1 | safeclib `bsearch_s`/`memrchr_s` | `rurban/safeclib` @ `90b076e9…` | MIT | Ported bodies; two new prefixed C symbols only |
| I2 | mimalloc predicates | `microsoft/mimalloc` @ `c683b7c6…` | MIT | 4 scalar helpers in new `alloc_policy.hp` |
| I3 | Fil-C liveness queries | `pizlonator/fil-c` `deluge` @ `a80e3280…` | BSD-2-Clause-style | Read-only queries in `allocator.hp` |
| I4 | GSL `narrow`/`not_null`/`span` | `microsoft/GSL` `main`, per-file touches | MIT | Concrete value descriptors in new `gsl_adapt.hp` |
| I5 | yyjson budgets | `ibireme/yyjson` @ `be9bd74b…` (v0.13.0) | MIT | Budget helpers in `json.hp`; parser vendored as-is |
| I6 | SaferCPlusPlus tokens | `duneroadrunner/SaferCPlusPlus` @ `5edaf67a…` | BSL-1.0 | Runtime lifetime tokens in new `lifetime_adapt.hp` |
| I7 | SoftBoundCETS predicates | `santoshn/softboundcets-34` @ `9a9c09f0…` | BSD-3-Clause tree / MIT-style files | Subtract-form checks in `memory.hp`; no shadow metadata |
| I8 | MS STL string growth | `microsoft/STL` @ `0cabaf3c…` | Apache-2.0 WITH LLVM-exception | Growth helper + overlap-safe append in `string.hp` |
| I9 | MS STL vector growth | `microsoft/STL` @ `0cabaf3c…` | Apache-2.0 WITH LLVM-exception | Byte/growth helpers + commit-last guards in `vector.hp` |

**No ABI-visible API change** was made by any integration: all are new
`static inline` helpers, additive value descriptors, or additive changes that
leave every existing signature, struct layout, error code and telemetry point
intact. I1 adds two new prefixed symbols; I2–I4 and I6–I7 add none.

**Researched but explicitly NOT integrated:** Cake (licence rejected —
GPL-3.0-or-later + AI-training addendum; no source copied); TrapC (no
repository, SPDX or LICENSE —UNDETERMINED; documentation only; managed-pointer
RTTI/no-op `free` would change the ABI); Fil-C pointer representation, GC and
custom ABI; SoftBoundCETS trie, shadow stack, key/lock words, free map,
`-fsoftboundcets`, `main` wrapping and `abort()` policy; MS STL 1.5× growth and
iterator/exception machinery; GSL generic templates; SaferCPlusPlus static
scope enforcement; mimalloc allocator itself; yyjson upstream tests. Only
boundary-preserving ideas were adapted. (§§1–9, §10.4 of `upstream-reuse.md`.)

---

## 6. Benchmark results — speed, RSS, checksums

### 6.1 Authoritative final driver run

Generated `2026-11-23T04:27:57Z` by `benchmarks/safety_compare/run_all.sh`;
`results/summary.json`: **counts ok=15, blocked=5, fail=0**, exit 0. Protocol:
1 warmup + 7 interleaved runs per binary, 30 s run timeout, linear-interpolated
p95, pinned `PATH` and `/usr/bin/clang`. All C+ safe rows are `blocked` by the
external `CPLUS_EXIT_ARITHMETIC` runtime/header skew, so **no safe-mode timing
is claimed from this driver**.

| Workload | Variant | median (s) | p95 (s) | status |
|---|---|---:|---:|---|
| alloc_bench | Rust -O | 0.010404 | 0.013873 | ok |
| alloc_bench | C+ legacy | 0.039720 | 0.049571 | ok |
| bench_modern | Rust -O | 0.023018 | 0.026586 | ok |
| bench_modern | C+ legacy | 0.020533 | 0.024590 | ok |
| modern | Rust -O | 0.009449 | 0.011401 | ok |
| modern | C+ legacy | 0.008306 | 0.010272 | ok |

**Checksums matched** across compared variants: `loop=19999999900000000`,
`nested=16000000`, `modern=2200000200000`, `bench_modern=5595356797869224`,
`alloc_bench=4750706934088118880`. Validator (post-fix): `BENCH_VALID 192 0` with `BENCH_BLOCKED 5` reported separately; the older `BENCH_VALID 157 0` was from an earlier evidence file.

### 6.2 Earlier direct measurements (supplementary, different build configurations)

- `RESULTS.md` 5-run medians for `loop`/`nested`: plain C 0.0036/0.0023 s,
  C+ safe (UBSan) 0.1075/0.0090 s, C+ legacy 0.0038/0.0023 s, Rust 0.0042/
  0.0026 s, Goose 0.0043/0.0024 s. Safe/legacy ratios ~30× / ~3.9× are
  **UBSan instrumentation cost**, not a safe-lowering cost; all non-instrumented
  compilers folded the loops into a closed form.
- `modern` 7-run interleaved medians: C+ safe 0.0140 s, C+ legacy 0.0061 s,
  Rust 0.0060 s, Goose 0.0044 s (safe/legacy ~2.3×, with ~1.8×–3.1× pass
  spread). All printed `2200000200000`.
- `PROOF.md` §3.5 (sanitizers off, n=31): safe C+ vs legacy C median ratios
  **1.020× (loop), 1.039× (nested)** — no measurable safe-mode overhead; p95
  also recorded.
- Allocator tracking toggle: tracking ON ≈ **3.07×** the OFF median
  (0.012287 s vs 0.004004 s). C+ `stdlib_alloc` vs Rust `Vec` at **-O0 vs -O**
  is 6.25× and explicitly **not** attributable to allocator design alone.
- p95 for the earlier 5-variant `loop`/`nested` and 4-variant `modern` speed
  tables is **PENDING** (medians only; §3.5 p95 is a different configuration).

### 6.3 Peak RSS (observed only, not a safety metric)

`/usr/bin/time -l` `maximum resident set size`, median of 7 interleaved runs
after 1 warmup (`MEMORY_USAGE.md`):

| Workload | C+ safe | C+ legacy | Rust -O |
|---|---:|---:|---:|
| loop | 3,719,168 B (3.55 MiB) | 1,671,168 B (1.59 MiB) | 1,851,392 B (1.77 MiB) |
| nested | 3,719,168 B (3.55 MiB) | 1,671,168 B (1.59 MiB) | 1,851,392 B (1.77 MiB) |
| modern | 21,348,352 B (20.36 MiB) | 18,235,392 B (17.39 MiB) | 14,188,544 B (13.53 MiB) |

`modern` C+ safe is variable (20,267,008–23,396,352 B across passes) and its
~20–21 MiB figure is approximate. `loop`/`nested` RSS is process baseline.
Rust `Vec`/`String` and Goose dynamic arrays are different standard-library
implementations. Plain C, Goose and Fil-C RSS are PENDING; no Fil-C toolchain
is installed. **Comparable measured throughput on matched synthetic workloads
is not Rust-equivalent guarantees and not proof of memory safety.**

---

## 7. ABI preservation

- **Baseline suite:** `abi_compat: 69 passed, 0 failed`, 0 timeouts, exit 0
  (`build/abi/compat/results.log`). Self-reported checks: 20/20 pointer ABI,
  80/80 layouts, 13/13 C→C+, 10/10 C+→C, 39/39 audited symbols with **no**
  ` T __Z` C++-mangled symbols, header-neutrality `diff` empty. Separate
  `abi_layouts: 70/70 layouts match`.
- **Integration ledger:** no public signature, struct layout, error code or
  symbol name changed; raw `T *` stays one word; no pointer widening/tagging
  and no new exported symbol except I1's two prefixed additions
  (`upstream-reuse.md` §10.5).
- **Scope limits:** only macOS arm64 / Apple clang 16 / C+ 0.1.0 was tested.
  Linux, Windows (MSVC/UCRT), x86_64 and x86 are **UNTESTED**; no C++ mangled
  ABI was tested. The 69/69 result says the checked subsets matched captured
  expectations — it does not mean an ABI was modified or proven stable.
- **Snapshot gaps:** `tests/abi/legacy_c/`, `cp_to_c/`, `precompiled/` and
  `fingerprints/` were PENDING at capture; `c_to_cp/` was PARTIAL (build 1/1,
  run 1/1, 4/4 observed values, formal assertion count PENDING).
- Passing ABI checks say nothing about runtime memory safety or the unresolved
  `CPLUS_EXIT_ARITHMETIC` blocker.

---

## 8. Remaining work and blockers

1. **External runtime/header skew — `CPLUS_EXIT_ARITHMETIC`** (primary blocker).
   The instrumented build path compiles external
   `c-plus-core/runtime/cplus_runtime.c` against this branch's local
   `runtime/include/cplus/cplus.h`, whose enum ends at `CPLUS_EXIT_IO = 109`.
   Attributable: 51 + 58 = **109 invocations**; it blocks all safe-mode/instrumented
   builds and 5 benchmark rows. Fix must come from the `c-plus-core` owner or
   an explicitly authorized branch change; environment-only bridges were used
   for evidence without editing checked-in files or suppressing checks.
2. **Host SDK / default clang** — Homebrew clang 23.1.6 targets missing
   `MacOSX27.sdk`, aborting libc/CRT prerequisite builds (`errno.h`, `stdio.h`).
   Pin `/usr/bin/clang` (Apple clang 16) as the ABI suite does.
3. **`stdlib/error.hp:223` `reporting` undeclared** — 7 native-tool build
   failures at `cbcea571`; source-level, distinct from blockers 1–2.
4. **Native tools** — 4/8 compile at `487ab89`; the 4 failures are in-flight
   `[CPLUS-FFI]`/`[CPLUS-LIFETIME]` diagnostics. Stale binaries must not be
   reported as results.
5. **Fuzz100 not green** — current `RESULTS.md`: 9 fails + 1 build failure
   (exit 1); earlier snapshot recorded 100/0.
6. **Four `--sanitize=integer` unsigned-overflow findings** in runtime hashing
   (`cplus_runtime.c:174:5`); outside the Agent-2 integration scope but real.
7. **Coverage gaps** — p95 for some speed tables; Goose/plain-C/Fil-C RSS;
   Fil-C speed and safety (no toolchain); 27 compile-time capability-gap cases;
   `PROOF.md` marks native safety-matrix results / `validate_results.cp` output
   PENDING even though `SAFETY_MATRIX_NOTES.md` holds detailed results (§11).
8. **Path-level discrepancies** in `upstream-reuse.md` (§11) need a documentation
   correction pass.
9. **No live-game, emulated or production validation** was performed; these are
   host-side synthetic binaries.

Re-verification criteria: `tests/run_tests.sh` with no `compilation failed`
lines and exit 0; `run_deep_tests.sh` with `BUILD-FAIL 0` in all three modes
and no `CPLUS_EXIT_ARITHMETIC`; libc/CRT prerequisites built with the pinned
Apple clang 16; native tools 8 built / 0 failed; and the C+ safe benchmark rows
measured rather than `blocked`. Until then no "all green" claim is valid.

---

## 9. Final verdict

**QUALIFIED / PARTIAL DELIVERY — not "all green".**

- **Delivered:** nine upstream integrations, each with a changed source file and
  a passing executed regression test (9/9, 0 failures), with no ABI-visible API
  change. Agent-2 runner 56/0. ABI baseline 69/0 and layouts 70/70. The
  benchmark driver completed with 15 ok / 5 blocked / 0 fail and matched
  checksums; safety-outcome gate 10/0, resource exhaustion 16/16, safety matrix
  executed in both languages.
- **Not delivered / blocked:** the six-suite aggregate at `cbcea571` is
  BLOCKED / NOT GREEN (5/6 non-zero); instrumented C+ builds and all C+ safe
  benchmark rows are blocked by the external `CPLUS_EXIT_ARITHMETIC` skew; 5
  sanitizers are unsupported and integer sanitizer has 4 genuine findings;
  fuzz100 and 4 native tools are not green; ABI scope is one macOS arm64
  toolchain.
- **Interpretation:** on these synthetic workloads the compared binaries did
  the same integer work (identical checksums) and their throughput was
  measured; with sanitizers off, safe C+ showed no measurable overhead on the
  integer loops. These are observations on one host at one point in time.
  **There is no Rust-equivalence proof and no claim of 100% safety.** Matching
  checksums and similar medians verify one deterministic result, not equivalent
  type systems or guarantees. C+ refusals are runtime-enforced; Rust rejects
  several of the same patterns at compile time. RSS is an observation, not a
  correctness or safety property.

---

## 10. Source-document index

`benchmarks/safety_compare/{PROOF,RESULTS,METHODOLOGY,MEMORY_USAGE,SAFETY_MATRIX_NOTES}.md`;
`docs/stdlib/upstream-reuse.md`; `docs/security/verification-status.md`;
`docs/abi/compatibility-matrix.md`; `docs/stdlib/NO_PYTHON_TESTING.md`;
`tests/stdlib/agent2/results/summary.txt`.
Authoritative runner artifacts additionally cited:
`benchmarks/safety_compare/results/{summary.json,table.txt}`;
`build/abi/compat/results.log`; `tests/fuzz100/RESULTS.md`;
`tests/scenarios/cve_like/NOTES_5.md`;
`tests/security/resource_exhaustion/NOTES.md`; `docs/FINAL_EVIDENCE.md`.

## 11. Stale-document discrepancies (runner artifact is authoritative)

These snapshots were captured at different HEADs (`487ab89`, `cbcea571`,
earlier `11831a2`). They are recorded here, not silently reconciled:

1. **`docs/FINAL_EVIDENCE.md` is stale.** It reports an earlier run with
   overall **FAIL**, `PASS=15 FAIL=1`, `run_tests: 150 passed, 0 failed`, deep
   `87 passed, 0 failed`, `fuzz100: pass=100, fail=0, skipped=10`, libc
   `87 passed, 3 failed`, and Python contract tools. The authoritative current
   snapshots contradict it: `verification-status.md` reports
   `run_tests: 99 passed, 51 failed`, deep `28 passed, 58 failed, 1 skipped`,
   `fuzz100/RESULTS.md` reports `pass=56, fail=9, build_failure=1, skipped=6`,
   and `NO_PYTHON_TESTING.md` reports native tools (4/8 built). Quote the
   runner artifacts, not `FINAL_EVIDENCE.md`, for current status.
2. **`upstream-reuse.md` §7 vs `tests/stdlib/agent2/results/summary.txt`.**
   §7 says the aggregate runner executed 51 tests with **50 PASS**; the
   authoritative summary at `487ab89` is **56 PASS / 0 FAIL / TOTAL 56**
   (29 deep + 16 agent2 + 11 existing). Use 56.
3. **Native tool counts differ by HEAD.** `NO_PYTHON_TESTING.md` (`487ab89`):
   **4 built / 4 failed**. `verification-status.md` (`cbcea571`): **1 built /
   7 failed**. Both are stale at the current HEAD `2294cb1e`; neither is a pass.
4. **`upstream-reuse.md` §3 vs §10 on `stdlib/memory.hp`.** §3 calls it "file
   added by this task"; §10.2 calls it "modified, +130 lines". The file exists;
   the ledger row (I7, additive changes only) governs.
5. **`PROOF.md` §7 marks the native safety-matrix results and
   `validate_results.cp` output PENDING**, but `SAFETY_MATRIX_NOTES.md` is
   present with full observed tokens (8 scenarios, E0382 compile rejection,
   fatal ABORT exit 70). Cite `SAFETY_MATRIX_NOTES.md` for the matrix; the
   `validate_results.cp` standalone run output remains PENDING.
6. **Host CPU label.** `RESULTS.md` and `SAFETY_MATRIX_NOTES.md` say "Apple M3
   Pro"; `METHODOLOGY.md` §6's authoritative `sysctl` record says **Apple M2**
   (`Mac14,7`, T8112). Use M2.
7. **Test paths.** `upstream-reuse.md` writes integration tests as
   `tests/stdlib/<name>.cp`; on disk they are under
   `tests/stdlib/upstream_reuse/`. It also cites
   `tests/stdlib/agent2/abi_safety_probe.cp`, which is absent — four
   `tests/stdlib/agent2/abi_safety_*.cp` probes and an `abi_safety_probe.expected`
   fixture are present instead. Documentation correction needed; no result is
   claimed from the missing path.

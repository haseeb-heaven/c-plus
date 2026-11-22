# Response to six critical reviewer issues

Branch: `agent/modern-stdlib`. This document is an **evidence record, not a proof**.
Every number below is taken from a named file in this repository or recomputed from
its machine-readable data; nothing is inferred from a neighbouring test or from a
successful build. Where the local evidence cannot establish a claim, the claim is
explicitly withheld.

Evidence discipline used throughout:

- `docs/libc/api_status_matrix.json` and `docs/libc/API_STATUS_MATRIX.md` state their
  own rule: **TESTED means a dedicated test file exists; it does not imply
  Rust-equivalence, and COMPILER-ENFORCED and RUNTIME-ENFORCED are distinct**.
- `docs/libc/SAFETY_GATES.json` opens with: "C+ is NOT Rust-equivalent; statuses
  describe evidence, not guarantees."
- `docs/libc/api_status_matrix.json` pins the audited snapshot to HEAD `9e014d0`
  with header fingerprints (`cplus_libc.h` sha256[:16] `03475613fe7e3813`) and notes
  that the working tree was edited by parallel agents. Re-verify after other
  agents' changes land.
- Sanitizer output (UBSan/ASan/TSan) is treated as verification evidence, never as
  a language guarantee (`docs/libc/CPLUS_RUST_CONFORMANCE.md` §"How to read the
  statuses", `docs/libc/RUSTC_DIRECT_COMPARISON.md`).

---

## Issue 1 — Safety coverage is not complete; no evidence that all unsafe cases are detected or prevented

**Reviewer claim.** The safety work does not cover every unsafe case and there is
no evidence that all such cases are detected or prevented.

**Current evidence (exact file + number).**

- `docs/libc/SAFETY_GATES.json`: 12 gates; **C+ status is `PARTIAL` for 11 and
  `NOT_PROVEN` for 1 (gate 12, FFI/unsafe boundaries); 0 are `PROVEN`**. Same
  distribution in `docs/libc/CPLUS_RUST_CONFORMANCE.md` §"Distribution" (line 49):
  "0 PROVEN, 11 PARTIAL, 1 NOT_PROVEN for C+; 11 PROVEN and 1 PARTIAL for safe Rust."
- Concrete uncovered paths recorded in `SAFETY_GATES.json`: gate 1 only rejects
  constant/direct subscripts (`int *p = malloc(1); for (...) p[i]=...` compiles
  silently); gate 4 "a stale raw alias cannot be revoked"; gate 5 detection is a
  bounded **1024-entry** cache with collisions; gate 6 "**547 of 1200** stack-escape
  scenarios are undiagnosed"; gate 9 signed overflow / divide-by-zero / invalid
  shifts are "NOT FIXABLE BY LIBC"; gate 12 has no language-enforced FFI boundary.
- `docs/libc/api_status_matrix.json` (recomputed): status `TESTED` **538**,
  `RUNTIME-ENFORCED` **73**, `COMPILER-ENFORCED` **10**, `IMPLEMENTED` **12**,
  `UNSUPPORTED` **13**. Evidence columns with `NO EVIDENCE`: `abi_test` **644**,
  `logging_test` **587**, `fuzz_differential` **306**, `contract` **414**,
  `boundary_test` **80**, `unit_test` **25** (of **646** rows).
- `docs/libc/API_STATUS_MATRIX.md` §"Six explicitly requested categories":
  `compile-time-vs-runtime` **83** rows, `contract-gaps` **414**, `test-gaps` **22**,
  `abi-portability` **644**. The document states RUNTIME-ENFORCED rows "are NOT
  static guarantees".

**What is DONE.** A per-gate honest status record exists (`SAFETY_GATES.json`,
`CPLUS_RUST_CONFORMANCE.md`) with named evidence files for each Partial mechanism;
compile-time rejection tests for constant OOB, use-after-move, null deref and
dispatch (`tests/compile_fail/`); checked `_s` entry points with tracked capacity
and strict fail-closed mode; runtime trap for detected violations; machine-readable
contracts with implicit limits recorded rather than hidden (`CONTRACT_STATUS.md`
§3–4).

**What REMAINS.** Language-level bounds/lifetime/FFI enforcement, the 547
undiagnosed stack-escape scenarios, arbitrary-alias and provenance safety, runtime
index checks not supplied by sanitizers, and the ABI/logging/fuzz/contract evidence
gaps above.

**NOT CLAIMED.** We do **not** claim that all unsafe cases are detected or
prevented. We do **not** claim Rust-equivalence (0 of 12 C+ gates are PROVEN), nor
that sanitizer detection constitutes a language guarantee.

---

## Issue 2 — Library coverage is incomplete (substantial stdlib API surface missing)

**Reviewer claim.** A substantial part of the standard-library API surface is
missing.

**Current evidence (exact file + number).** Recomputed from
`docs/libc/iso_c_inventory.json`: **948 APIs across 27 headers** —
`HOST/COMPILER-PROVIDED` **359**, `PLATFORM_BACKED` **476**,
`IMPLEMENTED_BUNDLED` **29**, `IMPLEMENTED_UPSTREAM` **8**, `UNSUPPORTED` **49**,
`MISSING` **27**. That is 920/948 accounted for (97.05%).

- The **27 MISSING** entries are enumerated in the JSON: 23 C23 math APIs
  (`nextup`/`nextdown`/`fadd`/`fsub`/`fmul`/`fdiv` families incl. `*f`/`*l`, 3-arg
  `hypot`, `frexp16`, `fma16`, `logb16`, `exp16`), plus `timespec_getres`,
  `mbrtoc8`, `c8rtomb`, `FE_TIESAWAY`. Blockers are recorded per entry: "no `cplus_`
  wrapper and no host declaration gated for C17 baseline"; host `<uchar.h>` absent
  on this toolchain (inventory note line 16); `FE_TIESAWAY` "requires compiler
  `<fenv.h>` support the host does not provide".
- `docs/libc/COVERAGE_100_AUDIT.md` lines 22–28: vendoring LLVM libc 20.1.1's
  `memset_explicit` "would require copying a new dependency closure and adding a
  C++ build path — this was not done"; closing the rest "would require either a
  newer host toolchain or a new pinned upstream dependency closure".
- The **49 UNSUPPORTED** are deliberate (`stdatomic.h` 34, `stdio.h` 13,
  `stdlib.h` 2) — type-generic C ABI, insecure APIs, platform-specific.
- Caveat: `COVERAGE_100_AUDIT.md` prose says **28** MISSING / **7** UPSTREAM while
  the machine-readable inventory says **27** / **8**. The JSON is authoritative
  here; the prose figure is stale and should be reconciled.

**What is DONE.** A full C17-baseline inventory exists (inventory note line 18:
"the C17 baseline surface is exhaustive"), every accounted API has a
classification/source/declaration/test entry, and every missing or unsupported API
has a recorded reason rather than a silent omission. A separate declared-symbol
manifest (`api_status_matrix.json`, **646** rows) is also complete for what it
covers: `MISSING` **13** there, `UNSUPPORTED` **13**.

**What REMAINS.** The 27 MISSING C23 APIs need a newer host toolchain or a new
pinned upstream dependency closure; the 49 UNSUPPORTED APIs remain by design. The
exhaustive C23 `_FloatN`/`_FloatNx` overload matrix is explicitly not enumerated
(inventory note line 18).

**NOT CLAIMED.** We do **not** claim 100% ISO C completeness: **27 APIs are
MISSING with blockers and 49 are UNSUPPORTED by design**. We do **not** claim the
C23 surface is complete.

---

## Issue 3 — Integration is not fully proven (compiler / runtime / libc / checked together)

**Reviewer claim.** The pieces have been tested in isolation but not proven to
work together as one stack.

**Current evidence (exact file + number).**

- `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md` lines 22–29: Agent 1's
  `compiler/Analysis/libc_contracts.json` "**is not present on this branch**";
  `compiler/Analysis/` has only `BoundsAnalysis.cpp` and `OwnershipAnalysis.cpp`.
  The alignment JSON was built from written requirements and "**must be
  re-validated against the real compiler file when PR #16 merges**". No compiler
  or runtime source was modified.
- `docs/libc/GAP_ANALYSIS.md`: Phase 5 GAP — contracts that need Agent 1 (static
  bounds for runtime indices, ownership/lifetime, FFI) "are documented ... but
  **not enforced by this branch**"; Phase 2 highest-priority gap — "C and C++
  consumer compile matrix for the nine listed headers ... is missing"; Phase 1 —
  "Linux CI green **NOT VERIFIABLE HERE**"; Phase 6 — "NOT VERIFIABLE: Linux repeat
  (workflow job does not yet run this script)".
- `docs/libc/api_status_matrix.json` (recomputed): `abi_test` is `NO EVIDENCE` for
  **644 of 646** rows (only 2 ABI tests exist); category `abi-portability` = **644**.
- `tests/sdk/run_sdk_consumer.sh` is described in `docs/libc/SDK_CONSUMER.md` as an
  "end-to-end runner", but its scope is install + `find_package` + link + run —
  **not** a compiler-diagnostic-to-runtime-to-checked-API pipeline.
- Contract validation (`CONTRACT_STATUS.md` §5,
  `COMPILER_CONTRACT_ALIGNMENT.md` §"Verification") reports "contract validation:
  ok (5 files validated, 202 C17 APIs)" — a consistency check, not integration
  proof.

**What is DONE.** Local builds of `libcplus_hardened.a` and
`libcplus_safecrt.a`, a clean-export rebuild (`CI_RUN128_FIX_EVIDENCE.md`), SDK
consumer install/link/run, additive compiler-contract JSON plus standalone schema
test, and separate unit/negative/boundary suites each have recorded evidence.

**What REMAINS.** One suite that drives compiler diagnostics **and** runtime traps
**and** bundled libc **and** checked `_s` APIs through the same build and call
path; re-validation against Agent 1's real contract file after PR #16; the C/C++
header consumer matrix; and Linux/Windows ABI evidence. A **separate end-to-end
suite is being added by another agent** and is not part of this document's
evidence.

**NOT CLAIMED.** We do **not** claim integration is proven from isolated tests.
No single-suite, cross-component result is asserted here.

---

## Issue 4 — Test coverage is not safety; passing tests do not prove absence of bugs

**Reviewer claim.** Passing tests do not establish safety or the absence of bugs.

**Current evidence (exact file + number).**

- `docs/libc/api_status_matrix.json` `claim` field: "Evidence record, not a safety
  proof. TESTED means a dedicated test file exists; it does not imply
  Rust-equivalence. COMPILER-ENFORCED and RUNTIME-ENFORCED are distinct and neither
  implies the other." Recomputed gaps (of 646 rows): `fuzz_differential`
  **306 NO EVIDENCE**, `logging_test` **587**, `abi_test` **644**, `contract`
  **414**, `boundary_test` **80**, `unit_test` **25**.
- `docs/libc/CPLUS_RUST_CONFORMANCE.md` lines 87–92: paired suites "measure
  **observed outcomes only**: exit codes, stderr substrings, and sanitizer reports
  for a fixed scripted input. A pass ... does **not** prove the absence of
  undefined behaviour, a language-level guarantee, or Rust equivalence."
- Locally recomputed `build/rust_vs_cplus/*.csv`: **473 matched, 119 documented
  differences, 8 both-skip, 0 failures** over 600 cases (alloc 96/1/3,
  concurrency 96/0/4, conversion 55/45/0, math 70/30/0, memory 73/26/1,
  stdio 83/17/0). `tests/rust_vs_cplus/run_compare.sh` line 63 states this is
  "evidence of observed behaviour only; it does not prove equivalent safety
  guarantees."
- `docs/libc/COVERAGE_100_AUDIT.md`: "100% of declared tests pass (except one
  documented pre-existing compiler case)" but line coverage (libc) is **91.74%**,
  and the library is "NOT 100% ISO C complete".
- `docs/libc/RUSTC_DIRECT_COMPARISON.md`: UBSan detects only **2 of 7** scenarios;
  UAF, data races, uninitialized reads and wrapping arithmetic were not detected;
  "sanitizer output is not equivalent to Rust's compile-time ... guarantees."

**What is DONE.** A large executed corpus with honest evidence rules: paired
runtime comparisons, per-method suites, ~81,000 deterministic corpora, 150 C+
compiler tests (149 pass, 1 warning-only), 80 libc unit tests, 79 CRT unit tests
(all counts per `COVERAGE_100_AUDIT.md` §"Test coverage"), plus explicit
documentation that TESTED ≠ PROVEN.

**What REMAINS.** The evidence-column gaps above; no test can demonstrate the
absence of bugs — only the presence of behaviour for the cases actually run.

**NOT CLAIMED.** We do **not** claim that passing tests prove safety, the absence
of undefined behaviour, or Rust-equivalence. "100% of declared tests pass" is the
only pass claim made, and it is bounded as above.

---

## Issue 5 — CI failure unresolved: Linux missing `safe_lib_errno.h`; pipeline must be rerun

**Reviewer claim.** The Linux CI failure (`safe_lib_errno.h` missing) is unresolved
and the pipeline must be rerun.

**Current evidence (exact file + number).**

- `docs/libc/CI_RUN128_FIX_EVIDENCE.md`: run **#128** tested an older PR head
  (**88 commits**). A clean export (`git archive HEAD | tar -x -C /tmp/ci128-sim`)
  produced `CLEAN_EXPORT_BUILD_OK`; `third_party/safeclib/include/safe_lib_errno.h`
  is tracked (**3608 bytes**); deleting it and rebuilding generated
  `build/safe/safe_lib_errno.h` (**3628 bytes**) via the fallback in
  `scripts/build_safecrt.sh`. Conclusion (lines 37–41): "The remaining work is to
  push the branch and observe a new GitHub Actions run; no CI result is claimed
  from this local evidence alone."
- `docs/libc/GAP_ANALYSIS.md` Phase 1: "Previous CI failures (`safe_lib_errno.h`)
  **DONE locally**"; "Linux CI green **NOT VERIFIABLE HERE** ... workflow has
  `linux-crt` + `asan-libc-crt` jobs. Must be observed on GitHub after push."

**What is DONE.** Both the checked-in header path and the `.in` generation fallback
were verified from a clean export with no build artifacts and no source-tree luck;
the failure condition was reproduced locally by removing the header.

**What REMAINS.** Push the branch and observe the new GitHub Actions run; only a
remote result can close this.

**NOT CLAIMED.** We do **not** claim CI is green. No remote Linux result is claimed
from local clean-export evidence.

---

## Issue 6 — SonarCloud: high duplication, poor security/reliability; needs recheck

**Reviewer claim.** SonarCloud reports high duplication and poor security and
reliability ratings; the gate needs rechecking.

**Current evidence (exact file + number).**

- `docs/libc/SONAR_TRIAGE.md` §1 (external assessment, treated as authoritative
  input, **not locally scanned — no credentials**): duplication (CPD) **23.4%**
  vs required **<= 3%**; security **D** vs **A**; reliability **E** vs **A**.
- `docs/libc/SONAR_TRIAGE.md` §§4–5 and `sonar-project.properties` lines 35–56:
  only `sonar-project.properties` was changed — six generated fixture families
  were added to `sonar.exclusions` and to `sonar.cpd.exclusions`
  (`tests/crt/adversarial/test_*50*.c`, `tests/libc/test_corpus_*.c`,
  `tests/scenarios/scenario_*.c`, `scratch/bugs*.cp`,
  `tests/compile_fail/bugcat*.c`, `tests/compile_fail/bugcat*.err`). Production
  paths (`libc/src/**`, `libc/log/**`, `stdlib/safe/src/**`, `stdlib/safe/*.h`)
  are explicitly kept in scope; **no threshold lowered, no rule disabled, no
  production source excluded**.
- `docs/libc/SONAR_TRIAGE.md` §6: "Local verification of the SonarCloud gate is
  impossible without credentials ... The remote gate **must be re-run after
  pushing this branch** ... We do not claim the gate now passes" — the new
  duplication number is unmeasured and the security/reliability ratings are
  untriaged. §3 labels the security/reliability root causes a **hypothesis**.
- `docs/libc/GAP_ANALYSIS.md` Phase 1: "SonarCloud D/E + 23.4% duplication —
  PARTIAL ... Remote gate unmeasured."

**What is DONE.** Configuration change made and reviewed for scope; root-cause
hypothesis documented; verified by reading the properties file that production
paths and thresholds are untouched; no fabricated security/reliability fix.

**What REMAINS.** Fetch the issue list with a token, triage each issue into
generated-fixture noise / real production issue / documented false positive,
then re-run the remote scan after push and record the actual duplication
percentage and ratings in `SONAR_TRIAGE.md`.

**NOT CLAIMED.** We do **not** claim SonarCloud passes. No new duplication number
is asserted, and no production security or reliability finding is claimed fixed.

---

## Priority list — what would actually close each issue

1. **Safety:** promote gates to `PROVEN` with language-level tests for runtime
   bounds, interprocedural lifetime (incl. the 547 undiagnosed stack escapes) and
   a safe FFI boundary; stop relying on sanitizers as evidence of a guarantee;
   fill ABI/fuzz/logging/contract evidence gaps.
2. **Coverage:** close the 27 MISSING C23 APIs via a newer toolchain or a pinned
   upstream dependency closure, and reconcile the stale `COVERAGE_100_AUDIT.md`
   prose (28 MISSING / 7 UPSTREAM) with `iso_c_inventory.json` (27 / 8).
3. **Integration:** land and run the separate end-to-end suite being added by the
   other agent; re-validate `compiler_libc_contracts.json` against Agent 1's file
   when PR #16 merges; add the C/C++ header consumer matrix and Linux/Windows ABI
   evidence.
4. **Tests vs safety:** keep the TESTED/PROVEN distinction and the
   evidence-column gaps visible; do not re-label observed-outcome passes as
   safety guarantees.
5. **CI:** push and observe the new GitHub Actions Linux run (`linux-crt`,
   `asan-libc-crt`) and record the result — this is the only thing that closes
   issue 5.
6. **SonarCloud:** obtain a token, triage the returned issues, re-run the remote
   gate after push, and record the measured duplication/security/reliability
   values — this is the only thing that closes issue 6.
## Per-family gap table (computed from iso_c_inventory.json)

| Family | APIs | Bundled/upstream | Platform-backed | Toolchain-provided | Missing | Unsupported |
|---|---:|---:|---:|---:|---:|---:|
| stdlib.h | 49 | 4 | 34 | 9 | 0 | 2 |
| string.h | 28 | 23 | 5 | 0 | 0 | 0 |
| stdio.h | 75 | 0 | 44 | 18 | 0 | 13 |
| ctype.h | 14 | 0 | 14 | 0 | 0 | 0 |
| errno.h | 4 | 0 | 1 | 3 | 0 | 0 |
| time.h | 17 | 0 | 10 | 6 | 1 | 0 |
| math.h | 223 | 3 | 174 | 23 | 23 | 0 |
| wchar.h | 59 | 0 | 54 | 5 | 0 | 0 |
| wctype.h | 21 | 0 | 18 | 3 | 0 | 0 |
| locale.h | 9 | 0 | 2 | 7 | 0 | 0 |
| complex.h | 69 | 0 | 66 | 3 | 0 | 0 |
| fenv.h | 25 | 0 | 11 | 13 | 1 | 0 |
| signal.h | 12 | 0 | 2 | 10 | 0 | 0 |
| threads.h | 41 | 0 | 41 | 0 | 0 | 0 |
| stdatomic.h | 49 | 0 | 0 | 15 | 0 | 34 |

The reviewer's 🔴/🟡 labels are broadly consistent with this table. The largest real gaps are
`stdatomic.h` (34 type-generic APIs cannot be single C ABI functions), `math.h` (23 C23 APIs with no
dependency-closed upstream source), and `stdio.h` (13 deliberately unsupported Annex K/insecure APIs).
Everything else is platform-backed by a named host backend or provided by the toolchain — which is
real coverage, but is explicitly NOT a bundled implementation. See `docs/libc/ORDINARY_C_SAFETY.md`.


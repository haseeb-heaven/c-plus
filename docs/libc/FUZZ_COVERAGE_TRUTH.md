# Fuzz / coverage truth document for the expanded fuzzing program

**Status: point-in-time evidence snapshot, not a completion certificate.**
Recorded 2026-11-23 during parallel agent work on branch `agent/modern-stdlib`.
The F001..F100 corpus is still being written by sibling agents; every number in
§6 is a snapshot that must be recomputed before it is quoted as final.

**Reading rule.** Every count below names its source, its measurement method and
its denominator. Denominators differ (948 ISO C entries, 381 stdlib entries,
38 compiler contracts, 714 consolidated matrix rows, 56 test programs, 12
discovered fuzz files) and are **not interchangeable**. No percentage is
computed here; the few percentages quoted are recorded in a named source and are
labelled as recorded, not re-derived. A passing test, a static name match, or a
fuzz `PASS` is evidence about a program that ran — never proof of safety.

---

## 1. (a) Public API inventory — what the denominators mean

| Inventory | Exact count | Denominator / method | Source |
|---|---:|---|---|
| ISO C entries | **948** | All `apis` records; `counts.total_apis` | `docs/libc/iso_c_inventory.json` (`counts.total_apis`) |
| stdlib public callables | **381** | `sorted(Path('stdlib').glob('*.hp'))` = **21** headers; `coverage.py`'s `entries()` parser, one `(name, owning_class)` = one entry | `docs/stdlib/REGRESSION_INVENTORY.md` §§2–3 |
| compiler contracts | **38** | `len(d['apis'])` where `d = json.load('libc/contracts/compiler_libc_contracts.json')` | `libc/contracts/compiler_libc_contracts.json`; `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md` |

ISO C 948-entry breakdown (sums to 948; same JSON `counts`):

| Classification | Count |
|---|---:|
| PLATFORM_BACKED | 476 |
| HOST/COMPILER-PROVIDED | 359 |
| IMPLEMENTED_BUNDLED | 29 |
| IMPLEMENTED_UPSTREAM | 8 |
| MISSING | 27 |
| UNSUPPORTED | 49 |

Other views of the *same* 948: by kind — function 569, macro 301, type 78; by
C version — C89 301, C99 409, C11 125, C95 66, C23 47.

stdlib 381-entry denominator detail (from `REGRESSION_INVENTORY.md` §6):
collapsing entries to distinct bare names gives **232** names (135 exercised,
97 not) — 149 entries are overloads or cross-header repeats of the same name.
The 381-entry view is primary because overloads are distinct C++ signatures.
Headers with 0 public entries are skipped by `coverage.py`, so 21 is the number
of headers inspected, not a claim that all 21 contributed entries.

The 38 compiler contracts are the 29 baseline names (`malloc` … `fwrite`) plus
9 extensions (`memcpy_s`, `strcpy_s`, `snprintf_s`, `vsnprintf`, `malloc_s`,
`calloc_s`, `realloc_s`, `free_s`, `aligned_alloc_s`).

---

## 2. (b) APIs with dedicated tests

| Population | Count with a dedicated test | Denominator | Method / source |
|---|---:|---:|---|
| ISO C entries | **516** | 948 | Non-empty `test_file` field on each ISO C inventory record. 432/948 have no `test_file`. By classification: PLATFORM_BACKED 473, IMPLEMENTED_BUNDLED 26, HOST/COMPILER-PROVIDED 9, IMPLEMENTED_UPSTREAM 8; every MISSING (27) and UNSUPPORTED (49) entry has no `test_file`. (`docs/libc/iso_c_inventory.json`) |
| Consolidated status matrix | **606** `TESTED` | 714 | `counts.by_status.TESTED` in `docs/libc/api_status_matrix.json`. Other statuses: IMPLEMENTED 12, COMPILER-ENFORCED 10, RUNTIME-ENFORCED 73, PLANNED 0, UNSUPPORTED 13 (sums to 714). The same file records `unit_test.with_evidence = 699`, `no_evidence = 15` out of the same 714. Its own `claim` field: *"TESTED means a dedicated test file exists; it does not imply Rust-equivalence."* |
| stdlib | **56** test programs | 56 | `tests/stdlib/agent2/results/summary.json`: `total = 56`, `counts = {PASS:56, FAIL:0, BUILD_FAILURE:0, TIMEOUT:0, SKIPPED:0, BLOCKED:0}`. Groups: deep 29, agent2 16, existing 11. This is **program granularity**, not a per-API map; the summary has no per-API field. |
| compiler contracts | **0** with a compiler-consumption test | 38 | `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md` §"Compiler-consumption evidence": all 38 rows have `NONE` for "test proving compiler consumption". All 38 do have a schema/name/aspect alignment check (`tests/libc/test_compiler_contract_alignment.py`) that **parses the JSON only and never opens or exercises the compiler**. |

Two important non-equivalences: 714 ≠ 948 (the status matrix covers a different,
consolidated symbol universe), and "test file exists" ≠ "test executed and
passed" ≠ "API is safe".

---

## 3. (c) APIs with static name references only

"Static name reference" means a lexical token match in source text. It does not
mean the code compiled, ran, or reached the intended branch.

| Population | Count with a static reference | Denominator | Method / source |
|---|---:|---:|---|
| stdlib deep corpus | **255** | 381 | `REGRESSION_INVENTORY.md` §4: 234 literal call matches `[L]` + 21 compiler-inserted `drop()` credits `[D]` + 0 `*_drop` owner credits `[O]`. 126/381 have no reference. Corpus is **only** 28 `deep_*.cp` + 5 `deep_*.stdin`; docs, non-deep tests, examples, `multi_tu/` were excluded as evidence. |
| stdlib, stricter lexical variant | **250** | 381 | Same document §6-D7: stripping comments and string literals yields 250 exercised / 131 not exercised. Documented for transparency; the headline 255/126 is the faithful raw-text result. |
| ISO C, repository-wide name scan | **323** | **871 distinct API names** | I computed this read-only with the `coverage.py` rule `\b<name>\s*\(` over raw text of 494 files under `tests/libc/**`, `tests/crt/**`, `tests/stdlib/**`, `examples/**` (`.c`, `.h`, `.cp`). 548/871 distinct names have no literal call-shaped reference. Denominator is 871 distinct names, **not** 948 entries, because the inventory repeats names across headers/kinds; macros and types are structurally undercounted by a call-shaped regex. This measure was computed for this document and is not previously published in the repo. |
| compiler contracts | **38** | 38 | All 38 names are referenced by the JSON-alignment test as schema strings; 0/38 are referenced by any compiler source file on this branch (`compiler/Analysis/libc_contracts.json` is absent). |

Caveats carried over verbatim from `REGRESSION_INVENTORY.md` §6 that materially
affect category (c): unqualified name matching means one `size(` call credits
every `size` entry (D2); 21 `drop` credits have **no literal call** and rest on
the owning type token appearing in the corpus (D5); three of those owner tokens
(`loop`, `surface`, `window`) occur only in comments/strings (D7). Category (c)
is therefore an *upper bound* on static reachability, not a verified call count.

---

## 4. (d) APIs with runtime executed coverage where measurable

| Population | Measurable result | Denominator / method | Source |
|---|---|---|---|
| stdlib test programs | **56/56** built and executed with exit 0 | 56 programs; `build_exit = 0`, `runtime_exit = 0`, `classification = PASS` for every row | `tests/stdlib/agent2/results/summary.json` (generated 2026-11-23T06:27:48, head `1df837f`) |
| stdlib **per-API** runtime coverage | **not measurable on this host** | `coverage_exec.sh` preprocesses each test into one lowered unit; `#line` mapping to `.hp` headers is lost, so it reports lowered-unit lines, not per-header/per-API execution. Its own output says "per-header attribution unavailable on this host". It also swallows non-zero run exits with `|| true`, has no timeout and no coverage gate. | `tests/stdlib/coverage_exec.sh`; `docs/stdlib/COVERAGE_TOOL_LIMITATIONS.md` §2; `docs/MODERN_STDLIB.md` "What coverage means here" |
| ISO C per-API runtime coverage | **not available in the named sources** | No per-API execution map exists in `iso_c_inventory.json`, `api_status_matrix.json` or the recorded runner summaries. Available evidence is program-level: libc 83 passed / 0 failed, CRT unit 79 / 0, CRT fuzz smoke 11 / 0 (recorded, not re-run here); a 600-case Rust-vs-C+ paired comparison (473 matched, 119 documented differences, 8 both-skip, 0 failures). | `docs/libc/COVERAGE.md`; `docs/libc/COVERAGE_100_AUDIT.md` |
| libc source line/function coverage | recorded **91.74%** lines; function coverage **97.90%** last recorded, **not re-measured here** | Denominator is libc *source lines / functions*, **not** the 948 APIs and not per-API correctness. | `docs/libc/COVERAGE.md`; `docs/libc/COVERAGE_100_AUDIT.md` |
| compiler contracts runtime execution | **0/38** attributable to contract consumption | The alignment test never loads a contract into the compiler; no compiler-side test exists on this branch. | `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md` |

Consequence: the only executed-coverage number with a clean program-level
denominator here is **56/56 stdlib programs passed**. It must not be restated as
"100% of APIs executed" or "100% of APIs safe".

---

## 5. (e) Fuzz coverage by the new F001..F100 configs

### 5.1 Catalog mechanism and its denominator

Catalog: `tests/fuzz100/README.md`, `tests/fuzz100/run.py`, `tests/fuzz100/run.sh`.
The runner discovers `f100_*` **regular files** under `tests/crt/fuzz/` and
`tests/stdlib/fuzz/`, strips the `f100_` prefix, and takes the first `F<3 digits>`
token in the remaining filename as the config. Build is chosen by extension
(`.c` → C, `.cp` → C+, anything else → `SKIPPED` as unsupported). Build and run
are each bounded at 30 s. `PASS` requires build exit 0 **and** run exit 0;
`SKIPPED` is not a pass; missing prerequisites are `BLOCKED`, never a pass.

Therefore the catalog's natural denominator is **discovered harness files**, not
948/381/38 APIs and not a pre-ordained 100. The `F001..F100` namespace is a
naming scheme; a harness counts only once a file with that token exists and
executes. There is no expected-output fixture in this corpus: exit 0 is the
harness's own self-report.

### 5.2 Point-in-time snapshot (must be recomputed)

`build/fuzz100/summary.json`, read during this session while sibling agents were
still adding harnesses:

| Measure | Count | Denominator |
|---|---:|---:|
| discovered harness files | **12** | 12 `f100_*` regular files in the two search dirs |
| executed | **8** | 12 discovered − 4 `SKIPPED` (unsupported extensions `.h`/`.hp`) |
| PASS | **3** | 8 executed (all three were `.cp` harnesses) |
| BUILD_FAILURE | **5** | 8 executed (all five were `.c` harnesses; compiler exited 1) |
| FAIL / TIMEOUT / BLOCKED | **0 / 0 / 0** | 8 executed |
| SKIPPED | **4** | 12 discovered |
| distinct `F001..F100` tokens parsed | **0** | all 12 cases reported `config: null` |

**This snapshot is not a pass.** 5 of 8 executed harnesses failed to build, no
harness has been assigned an `Fnnn` token yet, no configuration is recorded as
executed, and the corpus is incomplete. Do not quote these numbers as the final
result of the 100-configuration program; re-run `sh tests/fuzz100/run.sh` after
the corpus work lands and replace this table with the new `summary.json` counts.

### 5.3 API references present in the in-progress snapshot (lexical, not executed)

The current `f100_*` files reference these exported symbols by name:
`cplus_memcmp`, `cplus_memcpy`, `cplus_memmove`, `cplus_memset`,
`cplus_libc_log_set_enabled`, `cplus_snprintf_s`, `cplus_vsnprintf_s`,
`cplus_vsprintf_s`, `cplus_safe_log_set_level`; plus stdlib-side probes for
containers, filesystem path generation and string-view append. This is a static
reference list only: the five C harnesses that reference the memory/format
symbols did not build in the snapshot, so no executed coverage is attributed to
them here.

### 5.4 Fuzz evidence that predates F001..F100 (separate denominators)

| Evidence | Count | Denominator / method | Source |
|---|---:|---|---|
| `fuzz_differential` evidence field | **308 with**, 406 without | 714 rows of the consolidated matrix | `docs/libc/api_status_matrix.json` `counts.evidence_coverage.fuzz_differential` |
| libc deterministic fuzz harness | **4800 validated operations** over **8 exported APIs** | 300 iterations × 8 API calls × 2 logging modes; sizes bounded to `[0,256]`; fixed seed; digest `2b62150fb0777106`; UBSan clean; **ASan unsupported on this host** | `tests/libc/fuzz_libc.c`; `docs/libc/LIBC_FUZZ_NOTES.md` |
| stdlib HP fuzz harness | **1200 cases**, 18 operations, **5 headers** | Fixed-seed LCG; valid operations only; explicitly **no UB inputs** (no NULL-to-nonnull, no OOB index, no overflow, no UAF/double-free); documented recoverable failures only | `tests/stdlib/fuzz/fuzz_hp.cp`; `docs/stdlib/HP_FUZZ_NOTES.md` |
| Recorded fuzz runner results | libc fuzz 2 passed, stdlib fuzz 3 passed, CRT fuzz smoke 11 passed; combined CRT fuzz 2000+1500 iterations ok | Program/iteration counts, **not** API counts; recorded, not re-run here | `docs/libc/COVERAGE.md` |

Note the deliberate validity policy: the HP harness generates only valid
operations and the libc harness only bounded sizes. They detect invariant
violations in those domains; they are not arbitrary-byte / arbitrary-pointer
fuzzing and cannot be cited as coverage of hostile input classes.

---

## 6. Explicit non-claims — what 100 fuzz configurations and passing tests do NOT prove

**100 fuzz configurations and 100% passing tests are not 100% safety proof.**
They are a finite set of generated cases with bounded sizes, fixed seeds and
self-reported exit codes. In particular, none of the following is guaranteed:

1. **Arbitrary raw pointers are not guaranteed safe.** Managed-allocation
   tracking covers only C+ managed allocations. Doubly/invalid-free detection is
   bounded (1024-entry recent-free caches) and address-reuse-sensitive;
   cross-allocator use, host `malloc`/`free` pointers, stale raw addresses and
   arbitrary pointer arithmetic remain heuristic or UB. `cplus_check_live()` is
   explicit, and RAII cleanup is bypassable by non-local exits (`longjmp`).
   Sources: `docs/libc/AGENT1_PRIORITY_HANDOFF.md` Appendix A;
   `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md` honesty notes.
2. **Uninstrumented C libraries are not covered.** 476 ISO C entries are
   PLATFORM_BACKED adapters delegating to host libc/libm: host errno, locale, FP
   environment, complex ABI and macro-backed functions keep host semantics.
   Code outside the instrumented C+ libraries can violate a contract without any
   C+ check firing. Fuzzers here never feed arbitrary invalid pointers or
   `SIZE_MAX`-style lengths (`LIBC_FUZZ_NOTES.md`, `HP_FUZZ_NOTES.md`).
3. **Compiler gaps are not closed by passing runtime tests.** 0/38 compiler
   contracts are proven consumed; `compiler/Analysis/libc_contracts.json` is
   absent on this branch. Documented gaps include UAF/double-free, CFG ownership,
   pointer-arithmetic/bounds verification, sound alias analysis, cross-function
   and cross-TU lifetime, definite initialization on all paths, strict-safe mode
   and thread-safety analysis. The compile-time suite records 5 tagged rejects,
   7 untagged (PARTIAL) rejects and **40 compiles-clean** out of 52 cases — the
   clean compiles are capability gaps, not passes. UBSan/ASan only observe
   violations that actually execute; ASan is unsupported on this host.
4. **Unsupported and missing APIs are not covered at all.** The ISO C inventory
   has 49 UNSUPPORTED and 27 MISSING entries (e.g. `threads.h` 41 MISSING,
   `uchar.h` 9 MISSING, `stdatomic.h` 34 UNSUPPORTED). The consolidated matrix has
   13 UNSUPPORTED rows (Annex K `_s` gaps). A green suite says nothing about
   these. This document asserts no whole-library completion percentage, and
   `docs/libc/COVERAGE_100_AUDIT.md` explicitly rejects a 100% complete claim
   (27 MISSING, 49 UNSUPPORTED).
5. **Static references are not execution or correctness.** §2–§3 counts record
   file presence and name tokens. They ignore control flow, reachability,
   overload selection, assertions, branch outcomes and expected-output diffs.
   The stricter stdlib variant (250/381) differs from the headline (255/381),
   which is itself evidence that the static measure is approximation.
6. **The F100 snapshot is not a completed corpus.** Five build failures, zero
   assigned `Fnnn` tokens, no expected-output fixtures, and a runner that treats
   exit 0 as the only success signal (§5.2) mean the snapshot cannot be read as a
   finished 100-configuration result.
7. **Portability and ABI are not proven.** ABI evidence is limited to 2 APIs in
   `tests/cases/c_abi.c` and `tests/compat/`; Windows x86 ABI and Linux CI are
   unverified locally (`docs/libc/api_status_matrix.json` category
   `abi-portability`, 712 rows flagged).
8. **Telemetry is diagnosis, not guarantee.** Per-API counters in
   `stdlib/safe/src/safe_stats.c` record calls/ok/fail; they do not prove
   correctness.

---

## 7. How to recompute (no numbers in this document should be updated by hand)

```sh
# (a) ISO C inventory counts
python3 -c "import json; d=json.load(open('docs/libc/iso_c_inventory.json')); print(d['counts'])"

# (b) stdlib executed test programs
python3 -c "import json; d=json.load(open('tests/stdlib/agent2/results/summary.json')); print(d['counts'], d['total'])"

# (b)/(d) consolidated matrix and evidence columns
python3 -c "import json; d=json.load(open('docs/libc/api_status_matrix.json')); print(d['counts'])"

# (a) compiler contracts size
python3 -c "import json; print(len(json.load(open('libc/contracts/compiler_libc_contracts.json'))['apis']))"

# (c) static stdlib call-site inventory: docs/stdlib/REGRESSION_INVENTORY.md §§1–7
python3 tests/stdlib/coverage.py

# (e) fuzz catalog — rerun after sibling agents finish writing the corpus
sh tests/fuzz100/run.sh
python3 -c "import json; d=json.load(open('build/fuzz100/summary.json')); print(d['discovered'], d['executed'], d['counts'])"
```

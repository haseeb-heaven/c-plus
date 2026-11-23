# ISO C and stdlib completion ledger

- **Generated at:** 2026-11-23T01:08:52Z
- **Branch:** `agent/modern-stdlib`
- **Artifact type:** evidence record, **not** a safety proof and **not** a 100% claim.
- **Machine-readable source of truth:** [`completion_ledger.json`](completion_ledger.json) (schema_version `1`).
- **C4 last refreshed:** 2026-11-23 (fuzz corpus refresh only; C1, C2, C3 and C5 were not re-derived here).
- **Verdict:** NOT MET. 100% cannot currently be claimed.

> All figures below are counts with denominators. No completion percentage is computed,
> inferred, or implied. A test-file path proves a dedicated test exists; it does not prove
> exhaustive branch coverage, Rust equivalence, compiler enforcement, or runtime safety.

## 1. What 100% would require

All five criteria must hold at the same time. None currently holds.

| ID | Criterion | Required | Observed | Met | Exact blocker |
|---|---|---|---|---|---|
| C1 | Close all 27 missing ISO C APIs | 27/27 MISSING rows converted to an implemented status with test evidence | 27/948 ISO C entries remain MISSING_BLOCKED | **NO** | All 27 are C23 additions with no cplus wrapper and no C17-gated host declaration; 26 of them additionally lack a dependency-closed pinned upstream source, and FE_TIESAWAY lacks any toolchain value. |
| C2 | Resolve all 49 unsupported ISO C entries | 49/49 UNSUPPORTED rows either shipped with test evidence or converted by a recorded scope decision with a portable alternative | 49/948 ISO C entries remain UNSUPPORTED | **NO** | 34 entries are type-generic C11/C23 atomics with no single C ABI symbol; 13 are Annex K entries the macOS host CRT cannot provide (11 `stdio.h`, 2 `stdlib.h`); 2 are `gets` (removed in C11) and `tmpnam` (TOCTOU), still not converted by a scope decision. |
| C3 | Close all compile-time gaps by Agent 1 | 27/27 declared compile-time capability/tagging gaps closed with stable tagged diagnostics and no false positives on the 19/19 ACCEPT controls | 27 gaps reported out of 8 manifests / 52 cases; 5 tagged REJECT, 7 untagged PARTIAL, 40 compile-clean | **NO** | Agent 1 compiler work is not present in this branch. Agent 2 supplied only reproducers, contracts and handoff; no compiler/** or runtime/** file was edited. |
| C4 | Execute 100/100 fuzz configurations | 100/100 configurations built, run and recorded as PASS with a catalog that enumerates them | named catalog now present and enumerating 100 config sources; parent-supplied current runner result: discovered=101, PASS=95, SKIPPED=6, FAIL=0, BUILD_FAILURE=0, TIMEOUT=0, BLOCKED=0; on-disk snapshot (mtime 2026-11-23T01:25:27Z): discovered=104, executed=98, PASS=98, SKIPPED=6, FAIL=0, BUILD_FAILURE=0 | **NO** | No recorded run has 100/100 config PASS. Both recorded snapshots are below 100 PASS; the 6 SKIPPED are support files, not configs; 2 config sources were added after the last summary.json write; PASS is build exit 0 plus process exit 0 with no expected-output fixture. Some new sources are not yet wired into build scripts (another agent). |
| C5 | Test all platform modules | 152/152 stdlib entries in the 9 platform modules exercised by built-and-run tests, plus Linux/macOS/Windows portability evidence | 152/381 stdlib entries in platform modules; none has a runtime test in tests/stdlib/agent2/results/summary.json; 116/152 lack even a static call-site match | **NO** | SDL3, libcurl, libuv and ffmpeg development files are absent, audio/image have no runtime test in the corpus, and Linux CI plus Windows x86 remain unverified. |

> **C1/C2 unchanged by fuzz work.** The 27 MISSING and 49 UNSUPPORTED ISO C API
> entries remain as recorded below. Several closure-planning documents now exist
> in `docs/libc/`, but a plan, a source path or a fuzz `PASS` is not per-API
> closure evidence. These counts stand unless a closure agent supplies
> re-verified, per-API evidence and this ledger is updated from it.

## 2. Headline counts

| Domain | Total | IMPLEMENTED_TESTED | IMPLEMENTED_UNTESTED | HOST_ONLY | MISSING_BLOCKED | UNSUPPORTED |
|---|---:|---:|---:|---:|---:|---:|
| ISO C APIs | 948 | 507 | 6 | 359 | 27 | 49 |
| stdlib entries | 381 | 219 | 162 | 0 | 0 | 0 |
| Combined | 1329 | 726 | 168 | 359 | 27 | 49 |

Statuses are mutually exclusive and assigned per entry; every one of the 948 ISO C APIs
and 381 stdlib entries has one row in the JSON ledger with an evidence path.

### Status definitions

| Status | Meaning |
|---|---|
| `IMPLEMENTED_TESTED` | Implementation source plus a dedicated test file / runtime suite record exists; does not imply Rust-equivalence or exhaustive branch coverage. |
| `IMPLEMENTED_UNTESTED` | Implementation exists in source or header but no dedicated executed test evidence exists. |
| `HOST_ONLY` | Toolchain-provided macro, type, keyword or freestanding header; no linkable symbol is implemented by this library. |
| `MISSING_BLOCKED` | No implementation and no backend; blocked by an unclosed dependency or toolchain gap. |
| `UNSUPPORTED` | Deliberately not shipped; exact limitation recorded in the evidence. |

## 3. ISO C: 948 APIs

Source: `docs/libc/iso_c_inventory.json` (948 entry records). The ledger maps the
inventory classifications as follows: `IMPLEMENTED_BUNDLED` / `IMPLEMENTED_UPSTREAM` /
`PLATFORM_BACKED` with a `test_file` become `IMPLEMENTED_TESTED`; the same classes without a
`test_file` become `IMPLEMENTED_UNTESTED`; `HOST/COMPILER-PROVIDED` becomes `HOST_ONLY`;
`MISSING` becomes `MISSING_BLOCKED`; `UNSUPPORTED` stays `UNSUPPORTED`.

| Status | Count / denominator |
|---|---:|
| `IMPLEMENTED_TESTED` | 507/948 |
| `IMPLEMENTED_UNTESTED` | 6/948 |
| `HOST_ONLY` | 359/948 |
| `MISSING_BLOCKED` | 27/948 |
| `UNSUPPORTED` | 49/948 |

### 3.1 The 6 implemented-but-untested APIs

| Header | API | Inventory class | Source |
|---|---|---|---|
| `string.h` | `strcoll` | PLATFORM_BACKED | `libc/src/string/string_extra4.c` |
| `string.h` | `strxfrm` | PLATFORM_BACKED | `libc/src/string/string_extra4.c` |
| `string.h` | `strerror` | PLATFORM_BACKED | `libc/src/string/strerror.c` |
| `string.h` | `strdup` | IMPLEMENTED_BUNDLED | `libc/src/string/extra2.c` |
| `string.h` | `strndup` | IMPLEMENTED_BUNDLED | `libc/src/string/strndup.c` |
| `string.h` | `strnlen` | IMPLEMENTED_BUNDLED | `libc/src/string/extra.c` |

### 3.2 The 27 missing APIs (criterion C1)

| Header | Missing / 948 | Reason IDs |
|---|---:|---|
| `fenv.h` | 1 | BR-01, BR-03 |
| `math.h` | 23 | BR-01, BR-02 |
| `time.h` | 1 | BR-01, BR-02 |
| `uchar.h` | 2 | BR-01, BR-02 |

23 of the 27 are `math.h` C23 additions (`nextup*` 6, `fadd*` 3, `fsub*` 3, `fmul*` 3,
`fdiv*` 3, `hypot` 3-argument, `frexp16`, `fma16`, `logb16`, `exp16`). The other four are
`timespec_getres` (time.h), `mbrtoc8` and `c8rtomb` (uchar.h), and `FE_TIESAWAY` (fenv.h).

### 3.3 The 49 unsupported entries (criterion C2)

| Header | Unsupported / 948 | Reason IDs |
|---|---:|---|
| `stdatomic.h` | 34 | BR-04 |
| `stdio.h` | 13 | BR-05, BR-06 |
| `stdlib.h` | 2 | BR-05 |

- 34 type-generic atomics (27 C11 `stdatomic.h` + 7 C23 additions): `BR-04`.
- 13 Annex K entries (11 `stdio.h` + 2 `stdlib.h` sort/search with context): `BR-05`.
- 2 deliberately excluded: `gets` (removed in C11) and `tmpnam` (TOCTOU): `BR-06`.

## 4. stdlib: 381 entries

Source: `docs/stdlib/REGRESSION_INVENTORY.md` (381 public callable entries across 21 headers).
A `§4 exercised` credit in that document is a **static call-site match**, not execution coverage
(inventory §7, caveats D2 and D7). The runtime suite is
`tests/stdlib/agent2/results/summary.json`: 56 PASS / 56 total (0 FAIL, 0 BUILD_FAILURE,
0 TIMEOUT, 0 SKIPPED, 0 BLOCKED). `tests/stdlib/agent2/FINAL_AUDIT.md` records an earlier
50/50 run of the same harness.

Only entries in the 12 runtime-tested modules that also have a static call-site match are
graded `IMPLEMENTED_TESTED`. The 10 runtime-module entries with no match, and **all** entries
in the 9 platform modules, are graded `IMPLEMENTED_UNTESTED`: the platform-module audits are
explicitly static only and FINAL_AUDIT states "Untested: all platform modules' runtime behavior".

| Status | Count / denominator |
|---|---:|
| `IMPLEMENTED_TESTED` | 219/381 |
| `IMPLEMENTED_UNTESTED` | 162/381 |
| `HOST_ONLY` | 0/381 |
| `MISSING_BLOCKED` | 0/381 |
| `UNSUPPORTED` | 0/381 |

### 4.1 Platform modules (criterion C5)

| Module header | Entries / 381 | No static match | Runtime tests in agent2 suite | External dependency blocker |
|---|---:|---:|---|---|
| `async.hp` | 14 | 9 | 0 | libuv |
| `audio.hp` | 29 | 21 | 0 | none (no runtime test in corpus) |
| `graphics.hp` | 16 | 9 | 0 | SDL3 |
| `gui.hp` | 18 | 16 | 0 | SDL3 |
| `http.hp` | 19 | 14 | 0 | libcurl |
| `image.hp` | 10 | 9 | 0 | none (no runtime test in corpus) |
| `input.hp` | 10 | 10 | 0 | SDL3 |
| `video.hp` | 25 | 22 | 0 | ffmpeg |
| `window.hp` | 11 | 6 | 0 | SDL3 |
| **Total** | **152** | **116** | **0** | — |

The runtime corpus deliberately excludes tests needing SDL3, libcurl, libuv and ffmpeg
development files (`tests/stdlib/agent2/existing_tests.txt`); `safety_regressions.cp` needs
`<uv.h>`. `audio.hp` (miniaudio) and `image.hp` (stb) have vendored headers but no runtime test
in the corpus. Linux CI is UNVERIFIED remotely and the Windows x86 path is script-only (P2 in
`docs/libc/AGENT1_PRIORITY_HANDOFF.md`).

## 5. Exact blockers by criterion

### C1 — Close all 27 missing ISO C APIs

- **Required:** 27/27 MISSING rows converted to an implemented status with test evidence
- **Observed:** 27/948 ISO C entries remain MISSING_BLOCKED
- **Exact blocker:** All 27 are C23 additions with no cplus wrapper and no C17-gated host declaration; 26 of them additionally lack a dependency-closed pinned upstream source, and FE_TIESAWAY lacks any toolchain value.
- **Blocker IDs:** BR-01, BR-02, BR-03
- **Evidence:** `docs/libc/iso_c_inventory.json`, `docs/libc/AUDIT_FINDINGS_TRACKER.md`, `docs/libc/C23_MATH_UPSTREAM.md`, `docs/libc/C23_SMALL_UPSTREAM.md`, `docs/libc/UCHARC23_UPSTREAM.md`

### C2 — Resolve all 49 unsupported ISO C entries

- **Required:** 49/49 UNSUPPORTED rows either shipped with test evidence or converted by a recorded scope decision with a portable alternative
- **Observed:** 49/948 ISO C entries remain UNSUPPORTED
- **Exact blocker:** 34 entries are type-generic C11/C23 atomics with no single C ABI symbol; 13 are Annex K entries the macOS host CRT cannot provide (11 `stdio.h`, 2 `stdlib.h`); 2 are `gets` (removed in C11) and `tmpnam` (TOCTOU), still not converted by a scope decision.
- **Blocker IDs:** BR-04, BR-05, BR-06
- **Evidence:** `docs/libc/iso_c_inventory.json`, `docs/libc/AUDIT_FINDINGS_TRACKER.md`, `docs/libc/API_STATUS_MATRIX.md`

### C3 — Close all compile-time gaps by Agent 1

- **Required:** 27/27 declared compile-time capability/tagging gaps closed with stable tagged diagnostics and no false positives on the 19/19 ACCEPT controls
- **Observed:** 27 gaps reported out of 8 manifests / 52 cases; 5 tagged REJECT, 7 untagged PARTIAL, 40 compile-clean
- **Exact blocker:** Agent 1 compiler work is not present in this branch. Agent 2 supplied only reproducers, contracts and handoff; no compiler/** or runtime/** file was edited.
- **Blocker IDs:** BR-07
- **Evidence:** `docs/libc/AGENT1_PRIORITY_HANDOFF.md`, `docs/libc/AGENT1_COMPILER_GAPS.md`, `docs/libc/COMPILE_TIME_GAPS.md`

### C4 — Execute 100/100 fuzz configurations

- **Required:** 100/100 configurations built, run and recorded as PASS with a catalog that enumerates them
- **Observed:** named catalog present (`docs/libc/FUZZ_100_CATALOG.md`, §1.4 as-built registry of 100 config sources with exact paths). Parent-supplied current runner result: discovered=101, PASS=95, SKIPPED=6, FAIL=0, BUILD_FAILURE=0, TIMEOUT=0, BLOCKED=0 (after the aligned_alloc side-table fix). On-disk `build/fuzz100/summary.json` snapshot (mtime 2026-11-23T01:25:27Z): discovered=104, executed=98, PASS=98, SKIPPED=6, FAIL=0, BUILD_FAILURE=0, TIMEOUT=0, BLOCKED=0. A read-only tree inventory finds 110 `f100_*` files: 100 config sources (60 `.c` + 40 `.cp`) and 10 support files.
- **Exact blocker:** no recorded run has 100/100 config PASS. Both recorded snapshots are below 100 PASS; 6 SKIPPED are support files, not configs; 2 config sources (as-built F-092/F-093) were added after the last summary.json write; PASS is build exit 0 plus process exit 0 with no expected-output fixture, so it is not a correctness proof. Some new sources are not yet wired into the wider build scripts (another agent). The catalog's plan-ID statuses reconcile to 90 EXISTING / 10 TO_ADD by API intent, not 100/100.
- **Blocker IDs:** BR-08
- **Evidence:** `docs/libc/FUZZ_100_CATALOG.md`, `tests/fuzz100/README.md`, `tests/fuzz100/run.py`, `build/fuzz100/summary.json`, `tests/crt/fuzz/F100_MEMORY_STRING_NOTES.md`, `tests/crt/fuzz/F100_ALLOC_LIFE_NOTES.md`, `tests/crt/fuzz/F100_FORMAT_IO_NOTES.md`, `tests/stdlib/fuzz/F100_CONTAINERS_NOTES.md`, `tests/stdlib/fuzz/F100_STRING_FILE_NOTES.md`, `tests/stdlib/fuzz/F100_THREAD_JSON_NOTES.md`, `tests/stdlib/fuzz/F100_SYSTEM_NOTES.md`, `docs/libc/LIBC_FUZZ_NOTES.md`

### C5 — Test all platform modules

- **Required:** 152/152 stdlib entries in the 9 platform modules exercised by built-and-run tests, plus Linux/macOS/Windows portability evidence
- **Observed:** 152/381 stdlib entries in platform modules; none has a runtime test in tests/stdlib/agent2/results/summary.json; 116/152 lack even a static call-site match
- **Exact blocker:** SDL3, libcurl, libuv and ffmpeg development files are absent, audio/image have no runtime test in the corpus, and Linux CI plus Windows x86 remain unverified.
- **Blocker IDs:** BR-09, BR-10, BR-11
- **Evidence:** `tests/stdlib/agent2/existing_tests.txt`, `tests/stdlib/agent2/FINAL_AUDIT.md`, `docs/stdlib/REGRESSION_INVENTORY.md`, `docs/libc/AGENT1_PRIORITY_HANDOFF.md`

### Fuzz snapshot detail (C4)

`docs/libc/FUZZ_100_CATALOG.md` is present and refreshes the F001-F100 plan against the as-built corpus (§1.4 lists all 100 config sources by exact path). The `tests/fuzz100` runner discovers `f100_*` files at run time; the last machine-readable snapshot `build/fuzz100/summary.json` (mtime 2026-11-23T01:25:27Z) records discovered=104, executed=98, counts={'PASS': 98, 'FAIL': 0, 'BUILD_FAILURE': 0, 'TIMEOUT': 0, 'SKIPPED': 6, 'BLOCKED': 0}. The parent session reports a later run at discovered=101, PASS=95, SKIPPED=6, FAIL=0, BUILD_FAILURE=0. Both are point-in-time: six thread/JSON files were added after the on-disk snapshot. This agent did not re-run the runner because that would write files outside its strict ownership. A different harness (`tests/libc/fuzz_libc.c`) reports 4800 libc operations, not 100 configs.

## 6. Blocked-reason register

| ID | Blocked reason | Affected | Criteria |
|---|---|---|---|
| BR-01 | C23 API has no cplus wrapper and no C17-gated host declaration | 27/948 ISO C APIs | C1 |
| BR-02 | No dependency-closed pinned upstream source for the C23 gap | 26/948 ISO C APIs (23 C23 math DECL-ONLY + timespec_getres + mbrtoc8 + c8rtomb) | C1 |
| BR-03 | FE_TIESAWAY has no host or upstream value on this toolchain | 1/948 ISO C APIs | C1 |
| BR-04 | Type-generic atomics cannot be represented as one C ABI symbol | 34/948 ISO C APIs (27 C11 stdatomic + 7 C23 atomic additions) | C2 |
| BR-05 | macOS host CRT lacks ISO C11 Annex K semantics | 13/948 ISO C APIs (11 stdio Annex K + 2 stdlib Annex K sort/search with context) | C2 |
| BR-06 | ISO C entry removed or deliberately excluded for insecurity | 2/948 ISO C APIs (gets removed in C11; tmpnam TOCTOU) | C2 |
| BR-07 | Agent 1 compile-time enforcement gaps not landed in this branch | 27/52 compile-time cases are declared capability/tagging gaps | C3 |
| BR-08 | No recorded 100/100 config PASS run; runner evidence is partial and point-in-time | required 100/100 executed PASS; last recorded PASS counts 95 and 98, both < 100, with 6 SKIPPED support files | C4 |
| BR-09 | External development dependencies for platform modules are absent | 7/9 platform modules (SDL3, libcurl, libuv, ffmpeg) | C5 |
| BR-10 | Platform modules have no runtime test in the agent2 corpus | 152/381 stdlib entries across 9 modules; 116 of them have no static call-site match | C5 |
| BR-11 | Portability claims beyond this host are unverified | Linux CI and Windows x86 DLL path | C5 |
| BR-12 | Audit findings remain open (additional risk, not one of the five criteria) | 46 OPEN + 7 GAP-COMPILER + 4 BLOCKED of 127 findings | additional risk |
| BR-13 | Source data-quality discrepancies (do not change headline counts) | 7 stale per-header aggregates; 2 named source documents absent | additional risk |

## 7. Data-quality notes and additional open work

- docs/libc/iso_c_inventory.json counts.by_header disagrees with its entry records for math.h, stdio.h, string.h, threads.h, uchar.h, wchar.h and complex.h; counts.by_classification and the 948 entries agree.
- tests/stdlib/agent2/FINAL_AUDIT.md records an earlier 50/50 run; tests/stdlib/agent2/results/summary.json is the current record with 56/56 PASS.
- build/fuzz100/summary.json is written by a sibling agent; its last observed values (mtime 2026-11-23T01:25:27Z: discovered=104, PASS=98, SKIPPED=6) are a point-in-time snapshot, not a final 100-config result. The parent session reports a later run at discovered=101, PASS=95, SKIPPED=6. Neither is 100/100 PASS.
- STATIC call-site matches in REGRESSION_INVENTORY.md are explicitly not execution coverage; platform-module matches are lexical artifacts (inventory D2/D7).

- **Audit findings:** 17 FIXED, 53 DOCUMENTED-CONTRACT, 7 GAP-COMPILER, 4 BLOCKED, 46 OPEN of 127 (docs/libc/AUDIT_FINDINGS_TRACKER.md).
- **ABI evidence:** ABI test evidence 2/714 rows; Windows x86 ABI and Linux CI unverified locally (docs/libc/api_status_matrix.json).

## 8. Evidence index

### Present sources

- `docs/libc/iso_c_inventory.json`
- `docs/stdlib/REGRESSION_INVENTORY.md`
- `tests/stdlib/agent2/results/summary.json`
- `docs/libc/AUDIT_FINDINGS_TRACKER.md`
- `docs/libc/AGENT1_PRIORITY_HANDOFF.md`
- `tests/stdlib/agent2/FINAL_AUDIT.md`
- `tests/stdlib/agent2/existing_tests.txt`
- `tests/fuzz100/README.md`
- `tests/fuzz100/run.py`
- `build/fuzz100/summary.json`
- `docs/libc/api_status_matrix.json`
- `docs/libc/LIBC_FUZZ_NOTES.md`
- `docs/libc/FUZZ_100_CATALOG.md` (present; §1.4 enumerates the 100 config sources)
- `docs/libc/API_GAP_CLOSURE_PLAN.md` (present, but its own header says it is a planning/evidence deliverable that "closes no API and runs no test runner"; C1/C2 unchanged)
- `tests/crt/fuzz/F100_MEMORY_STRING_NOTES.md`, `tests/crt/fuzz/F100_ALLOC_LIFE_NOTES.md`, `tests/crt/fuzz/F100_FORMAT_IO_NOTES.md`
- `tests/stdlib/fuzz/F100_CONTAINERS_NOTES.md`, `tests/stdlib/fuzz/F100_STRING_FILE_NOTES.md`, `tests/stdlib/fuzz/F100_THREAD_JSON_NOTES.md`, `tests/stdlib/fuzz/F100_SYSTEM_NOTES.md`

### Formerly named-absent sources (present at this refresh)

(None of the originally named absent sources remains absent. Presence of a
planning document is not closure evidence: see the C1/C2 note in §1 and BR-13.)

### Reproduction commands recorded by the source documents

```sh
sh tests/stdlib/agent2/run_all.sh --clean
CPLUS_BIN=$PWD/build/c+ sh tests/stdlib/compile_time/run_compile_time.sh
sh tests/run_all_evidence.sh
sh tests/fuzz100/run.sh
```

A future revision may change this ledger only after those commands are re-run and their
machine-readable outputs are re-read. No status may be upgraded from a source-file path, a
static call-site match, a build artifact, or an emulator result alone.

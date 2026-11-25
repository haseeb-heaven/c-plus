# libc / SafeCRT implementation plan — honest baseline, work order, and blockers

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Role:** planning deliverable only. This document changes no source, test, contract, compiler, or CI file and runs no test runner. It reconciles the named input documents and turns them into an ordered, evidence-linked work order.
**Owner boundary:** Agent 2 owns `libc/**` and `stdlib/safe/**` (plus docs/contracts/tests for those layers). Agent 1 owns `compiler/**` and `runtime/**`. CI/Sonar work needs remote credentials. Nothing below assigns Agent 2 work inside `compiler/**` or `runtime/**`.

## 0. How to read this document

### 0.1 Evidence levels (used literally throughout)

| Level | Meaning |
|---|---|
| **recorded** | A pass/fail figure is stated in a named repository document or runner log; this plan did **not** re-execute it. |
| **derived** | A count was recomputed here from on-disk machine-readable records (JSON `apis`/`headers` arrays, test-file counts). No test was run. |
| **static audit** | A source-review finding with `file:line` evidence in one of the five audit documents. Not a runtime proof. |
| **blocked** | No implementation, no pinned self-contained upstream source, or no local capability. An exact unblock condition is given in §5. |

### 0.2 Dependency scale for the work order

| Value | Meaning |
|---|---|
| `none` | Documentation-only or already fixed; acceptance is verification of an existing artifact. |
| `low` | One owned file, no public API/ABI change. |
| `med` | Two or more owned files, new namespaced API, or shared/cross-layer state; needs a design decision. |
| `high` | New subsystem or platform/toolchain change. |
| `external` | Cannot be closed by Agent 2; see §4 (Agent 1) or §5 (blocked). |

### 0.3 Owner labels

| Label | Scope |
|---|---|
| `A2-runtime` | `libc/src/**`, `libc/log/**`, `libc/install-include/**`, `stdlib/safe/src/**`, `stdlib/safe/*.h` |
| `A2-docs` | `docs/libc/**`, `libc/contracts/**`, test notes and proposed regression tests under `tests/` |
| `A1-compiler` | `compiler/**`, `runtime/**`, `c+` driver, `tests/stdlib/compile_time/*.json`, compile-fail fixtures |
| `CI/SEC` | `.github/workflows/**`, remote SonarCloud scan (`SONAR_TOKEN`) |

### 0.4 Standing honesty rules

No section of this document claims 100% ISO C coverage, complete memory safety, or Rust equivalence. The single most important source statement is `docs/libc/api_status_matrix.json` → `claim`: *"Evidence record, not a safety proof. TESTED means a dedicated test file exists; it does not imply Rust-equivalence. COMPILER-ENFORCED and RUNTIME-ENFORCED are distinct and neither implies the other."* See also `docs/libc/COVERAGE.md:3` ("No full ISO C replacement claim"), `docs/libc/COVERAGE.md:127-128` (sanitizer/fuzz passing is evidence of tested behaviour only), `docs/libc/WRAPPER_SAFETY_AUDIT.md:97` ("no 100% claim"), and `docs/libc/AUDIT_memory_string.md:461-468` (caller responsibilities, default unknown-capacity mode is unchecked).

---

## 1. What is actually implemented and tested now

### 1.1 Public callable surface (derived from the umbrella audit)

`docs/libc/WRAPPER_SAFETY_AUDIT.md:6-21` inventories 11 public headers and **672 deduplicated distinct callables**:

| Header | Distinct callables |
|---|---:|
| `libc/install-include/cplus_libc.h` | 560 |
| `libc/install-include/cplus_libc_safe.h` | 17 |
| `libc/install-include/cplus_uchar.h` | 4 |
| `libc/install-include/cplus_threads.h` | 25 |
| `stdlib/safe/safe_c.h` | 17 |
| `stdlib/safe/safe_alloc.h` | 22 |
| `stdlib/safe/safe_memory.h` | 16 |
| `stdlib/safe/safe_string.h` | 16 |
| `stdlib/safe/safe_io.h` | 4 |
| `stdlib/safe/safe_format.h` | 23 |
| `stdlib/safe/safe_ext_mem.h` | 2 |
| **Total (deduplicated)** | **672** |

Backend provenance in the same source (`WRAPPER_SAFETY_AUDIT.md:23-24`): bundled/musl-derived 29, copied upstream 8, host-delegated 476, toolchain-provided 359, plus 27 MISSING and 49 UNSUPPORTED (sum = 948).

### 1.2 Machine-readable ISO C inventory (derived from `docs/libc/iso_c_inventory.json`)

Recomputed from the per-API records in `.headers[].apis[]` (not from the file's stale `counts.by_header` block — see §7, contradiction C1):

| Classification | Count | Source |
|---|---:|---|
| `HOST/COMPILER-PROVIDED` | 359 | `iso_c_inventory.json:counts.by_classification`, confirmed by per-API derivation |
| `PLATFORM_BACKED` | 476 | same |
| `IMPLEMENTED_BUNDLED` | 29 | same |
| `IMPLEMENTED_UPSTREAM` | 8 | same |
| `UNSUPPORTED` | 49 | same |
| `MISSING` | 27 | same |
| **Total APIs / headers** | **948 / 27** | `iso_c_inventory.json:counts.total_apis`, `.headers` array length |

The 27 MISSING APIs are (all tagged C23): `nextup/f/l`, `nextdown/f/l`, `fadd/f/l`, `fsub/f/l`, `fmul/f/l`, `fdiv/f/l`, `hypot (3-argument C23 overload)`, `frexp16`, `fma16`, `logb16`, `exp16` (23 in `math.h`); `timespec_getres` (`time.h`); `mbrtoc8`, `c8rtomb` (`uchar.h`); `FE_TIESAWAY` (`fenv.h`). Source: `iso_c_inventory.json` per-API `classification=="MISSING"` records; same list in `docs/libc/C23_MATH_UPSTREAM.md:5-9`, `docs/libc/AUDIT_numeric_wide.md:92-100`, `docs/libc/COVERAGE.md:74`.

The 49 UNSUPPORTED APIs are 34 `<stdatomic.h>` type-generic/ABI entries, 13 `<stdio.h>` entries (`gets`, `tmpnam`, and Annex K names `fopen_s`/`fclose_s`/`fread_s`/`fprintf_s`/`printf_s`/`sprintf_s`/`scanf_s`/`fscanf_s`/`sscanf_s`/`gets_s`/`tmpnam_s`), and 2 `<stdlib.h>` entries (`bsearch_s`, `qsort_s`). Source: `iso_c_inventory.json` per-API `classification=="UNSUPPORTED"` records; `docs/libc/COVERAGE.md:75`.

### 1.3 Evidence matrix (derived from `docs/libc/api_status_matrix.json`)

| Field | Value | Source path |
|---|---|---|
| `total_apis` | 714 | `api_status_matrix.json:counts` |
| `public_header_symbols` / `safe_header_symbols` | 590 / 111 | same |
| `c17_gap_closing_apis` | 203 | same |
| By status | TESTED 606, RUNTIME-ENFORCED 73, COMPILER-ENFORCED 10, IMPLEMENTED 12, UNSUPPORTED 13, PLANNED 0 | `counts.by_status` |
| By classification | BUNDLED 356, PLATFORM_BACKED 303, UPSTREAM 29, PARTIAL 13, MISSING 13 | `counts.by_classification` |
| ABI evidence | `abi_test` with evidence 2, **no evidence 712** | `counts.evidence_coverage.abi_test` |
| Contract evidence | with evidence 345, no evidence 369 | `counts.evidence_coverage.contract` |
| Logging evidence | with evidence 69, no evidence 645 | `counts.evidence_coverage.logging_test` |
| Boundary / fuzz evidence | 508 / 308 with evidence | `counts.evidence_coverage.{boundary_test,fuzz_differential}` |
| Snapshot | branch `agent/modern-stdlib`, HEAD `3131acfaa32705e766a33a8e4dfb499d126c27c2`, 17 header sha256 fingerprints | `api_status_matrix.json:snapshot` |

The top 20 gap register (`api_status_matrix.json:top_gaps`) is the authoritative backlog for traceability gaps (ABI matrix, ABI evidence, 8 placeholder `_s` contracts, FFI gate, interior-pointer bounded wrappers, runtime state tests, logging telemetry, Annex K sort/search, C11 `<stdatomic.h>`/`<tgmath.h>`, long-double portability, stale symbol audit, pending method matrix, CI/ASan gates, format split, corpus attribution, thread/signal lifecycle, bounded-cache heuristics, symbol versioning, error-path logging, API-count inflation). This plan references those ranks where relevant; it does not duplicate all 20.

### 1.4 Recorded test results (all **recorded**, not re-run by this plan)

Source for the entire table: `docs/libc/COVERAGE.md:9-43`. Commands are the repository's own runner entry points (all confirmed present on disk).

| Area | Command | Recorded result |
|---|---|---|
| CRT corpus | `sh tests/crt/run_all_crt.sh` (corpus stage) | 10 passed, 0 failed |
| Safe CRT unit | `sh tests/crt/unit/run_unit_tests.sh` | 79 passed, 0 failed (79 `tests/crt/unit/test_*.c`; corrected from stale 76) |
| Safe CRT core / examples / adversarial | `sh tests/crt/run_all_crt.sh` (safecrt / examples / adversarial stages) | 6 / 26 / 71 passed, 0 failed |
| Fuzz smoke | `sh tests/crt/fuzz/run_fuzz_smoke.sh` | 11 passed, 0 failed |
| Safe CRT line coverage | `sh tests/crt/coverage_safecrt.sh` | 85 passed, 0 failed; 92.38% total lines, every file >= 87.44% |
| Combined fuzz | `tests/crt/fuzz/fuzz_all_safe_apis.c`, `fuzz_all_exported.c` | 2000 + 1500 iterations, ok, no crash |
| Libc tests | `sh tests/libc/run_libc_tests.sh` | 83 passed, 0 failed (83 `tests/libc/test_*.c` after exclusions; corrected from stale 62) |
| Libc line coverage | `sh tests/libc/coverage_libc.sh` | 91.74% total lines (`COVERAGE_100_AUDIT.md`); function coverage 97.90% last recorded, not re-measured |
| Libc fuzz / install smoke | `sh tests/libc/run_fuzz.sh` / `sh tests/libc/install_smoke.sh` | 2 passed, 0 failed / ok |
| C17 gap matrix | `docs/libc/API_TEST_MATRIX.md`, `libc/contracts/c17_contracts.json` | 202 APIs mapped to source + test + host backend + contract + limitation; JSON validated |
| CTL / exceptions / runtime | `sh tests/ctl/run_ctl_tests.sh`, `sh tests/exceptions/run_except_tests.sh`, `sh tests/runtime/run_runtime_tests.sh` | 11 / 2 / 4 passed, 0 failed |
| Modern stdlib (`.hp`) | `sh tests/stdlib/run_stdlib_tests.sh` | 38 passed, 0 failed, 0 skipped |
| Stdlib fuzz / safety matrix | `sh tests/stdlib/fuzz/run_fuzz.sh`, `sh tests/safety/run_safety_matrix.sh` | 3 / 3 passed, 0 failed |
| Rust-vs-C paired runtime | `sh tests/rust_vs_cplus/run_compare.sh` | 600 cases (6 families x 100): 473 matched, 119 documented differences, 8 both-skip, 0 failures. **Outcome equality is observed behaviour, not equivalent safety guarantees** (`COVERAGE.md:36`) — this plan makes no Rust-equivalence claim. |
| Per-family gaps | `sh tests/family_gaps/run_family_tests.sh` | 20 variants (10 sources x plain/UBSan); `math_gaps` 1346 checks / 23 explicit SKIPs / 0 failures in both modes (`MATH_GAPS_EVIDENCE.md`) |
| Hundred-scenario per-method | `sh tests/methods/run_hundred.sh` | 30 variants (5 libc-only x 2 + 5 dual-layer x 4); recorded 30/30 pass |
| Compile-time safety | `sh tests/stdlib/compile_time/run_compile_time.sh` | 7 manifests, 46 cases (9 REJECT, 17 ACCEPT, 20 RUNTIME) — see §4 for the honest outcome split |
| Deep stdlib regression | `sh tests/stdlib/run_deep_tests.sh` | 15 `deep_*.cp` sources x 3 modes = 45 variants |
| End-to-end integration | `sh tests/integration/run_integration.sh` | 8 checks, local macOS only |
| C+ compiler tests | `sh tests/run_tests.sh` | 150 tests (32 positive `.c` + 13 positive `.cp` + 97 compile-fail `.c` + 1 compile-fail `.cp` + 6 runtime-fail `.c` + 1 legacy): 149 pass, 1 pre-existing warning-only case |
| Aggregate | `sh tests/run_all_evidence.sh` | Single entry point for all runners; prints one summary line per suite |

`docs/libc/COVERAGE.md:45-50` records per-API telemetry in `stdlib/safe/src/safe_stats.c` (`cplus_safe_stat_record/reset/get/dump`, verified by `tests/crt/unit/test_safe_stats.c`) and explicitly calls it diagnostic telemetry, not a safety guarantee.

### 1.5 Implementation shape actually shipped

`docs/libc/COVERAGE.md:52-68` records: hardened libc slice (errno, alloc, memory, string, ctype, stdlib, stdio — all PLATFORM-BACKED wrappers or platform-backed with checks/logging), checked CRT backed by pinned rurban/safeclib `90b076e94c4b0fb202dddf948d5d620e5395c1b0` (MIT; 8 names listed) via `scripts/build_safecrt.sh`, with `intel-safestringlib` vendored for provenance only (not linked). Tracked-extension allocators and metadata helpers are listed in the same block. Musl-derived copied functions are enumerated at `COVERAGE.md:85-92`: `cplus_memccpy, cplus_stpcpy, cplus_stpncpy, cplus_strtok_r, cplus_strsep, cplus_memmem, cplus_strverscmp, cplus_strcasestr, cplus_strchrnul, cplus_div, cplus_ldiv, cplus_lldiv, cplus_llabs, cplus_atoll` in `libc/src/string/musl_extra.c` and `libc/src/stdlib/musl_stdlib_extra.c`, pinned `7813e91` (`libc/upstream/musl/PROVENANCE.md`). The 202 new C17 APIs are PLATFORM_BACKED namespaced wrappers, deliberately not counted as bundled/upstream (`COVERAGE.md:130-138`).

### 1.6 Sanitizer evidence and its limits

Source: `docs/libc/COVERAGE.md:107-128`, `docs/libc/ASAN_AUDIT.md`.

- **UBSan**: recorded pass for gap-closing libc suites with `-fsanitize=undefined -fno-sanitize-recover=all` on Apple Clang 16 / arm64.
- **ASan**: VERIFIED with Homebrew clang 23 plus explicit `-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`; 12 allocator scenarios classify correctly (`heap-buffer-overflow`, `heap-use-after-free`; valid reuse/realloc-failure/alignment/metadata cases clean). Apple clang 16 still aborts at startup with `sanitizer_malloc_mac.inc:189`. No manual ASan poisoning is used (each block is its own host allocation; `ASAN_AUDIT.md:21-60`). Linux CI remains the authoritative cross-platform path and is unverified locally (`COVERAGE.md:111-120`).
- **TSan**: NOT VERIFIED locally; Apple Clang 16 needs `DYLD_INSERT_LIBRARIES=.../libclang_rt.tsan_osx_dynamic.dylib` and otherwise reports "Interceptors are not working" (`COVERAGE.md:121-123`).
- **libFuzzer**: not linkable locally (`libclang_rt.fuzzer_osx.a` absent); deterministic fuzz harnesses pass (`COVERAGE.md:124-125`).

### 1.7 Wrapper-safety findings baseline

`docs/libc/WRAPPER_SAFETY_AUDIT.md:26-35` aggregates the five audits into **127 findings: 16 High, 68 Medium, 43 Low/Other**:

| Audit | Findings | High | Medium | Low/Other |
|---|---:|---:|---:|---:|
| `docs/libc/AUDIT_alloc.md` | 20 | 3 | 9 | 8 |
| `docs/libc/AUDIT_memory_string.md` | 50 | 6 | 27 | 17 |
| `docs/libc/AUDIT_stdio_format.md` | 15 | 3 | 7 | 5 |
| `docs/libc/AUDIT_numeric_wide.md` | 21 | 1 | 14 | 6 |
| `docs/libc/AUDIT_lifecycle.md` | 21 | 3 | 11 | 7 |
| **Total** | **127** | **16** | **68** | **43** |

Sub-counts are independently stated in each audit: `AUDIT_alloc.md:225-231` (HIGH 3 F-01/F-02/F-03; MEDIUM 9; DOCUMENTED LIMIT 4; LOW 4), `AUDIT_memory_string.md:344-350` (High 6, Medium 27, Low 16, Info 1), `AUDIT_stdio_format.md:313-322` (HIGH 3 F1/F2/F3, MED 7 F4-F10, LOW 5 F11-F15), `AUDIT_numeric_wide.md:61` (HIGH 1, MEDIUM 14, LOW 6), `AUDIT_lifecycle.md:39-47` (High 3 F1/F2/F3, Medium 11 F4-F13+F20, Low 7 F14-F19+F21). These are **documented audit findings, not silently fixed bugs** (`COVERAGE.md:78`).

### 1.8 Explicitly not shipped (baseline incompleteness)

From `docs/libc/COVERAGE.md:70-83`: 27 MISSING APIs; 49 UNSUPPORTED by design; 387 of 437 distinct vendored safeclib public APIs NOT_INTEGRATED (40 INTEGRATED, 10 ADAPTER_ONLY, 387 NOT_INTEGRATED, 0 UNAVAILABLE; only a 19-file subset compiled — 18 upstream sources + 1 local adapter; 188 upstream `.c` not compiled); ABI evidence almost absent; 127 open wrapper-safety findings; Annex K `bsearch_s`/`qsort_s`, `asctime_s`/`ctime_s`, `strerrorlen_s`, `tmpnam`/`gets_s`, standalone `<stdatomic.h>`/`<tgmath.h>` ABI symbols, and a userspace `FILE` buffering engine not shipped; remote CI and SonarCloud unverified; raw C UB/provenance/unknown capacity cannot be made safe by libc alone.

---

## 2. Work order — Phase 1: HIGH findings

All rows are **static audit** findings. `WRAPPER_SAFETY_AUDIT.md:73-88` calls for small commits with regression tests, a unified close registry, bounded capacity clamps, consistent NULL/EINVAL, logger fixes, no fabricated C23 constants, Agent 1 coordination, and `sh tests/run_all_evidence.sh` after each fix group. Status column reflects the input documents as written, not a fresh source check.

| ID | Finding and primary source | Owner scope | Est. dep. | Acceptance command (run after the change) | Status / note |
|---|---|---|---|---|---|
| `AUDIT_alloc.md` F-01 | Generation-blind checked free/transfer frees/transfers a **new** block through a stale alias. `libc/src/stdlib/alloc.c:340-371,524-542`; `stdlib/safe/src/safe_alloc.c:319-350,511-530` | `A2-runtime` | `med` | `sh tests/libc/run_libc_tests.sh` + `sh tests/crt/unit/run_unit_tests.sh`; add the stale-alias scenario to `tests/libc/test_alloc_attack.c` and `tests/crt/unit/test_alloc_metadata_s.c`; ASan variant per `AUDIT_alloc.md:245-266` | Open. `_gen` entry points keep old ABI (`expected_gen==0` = no check). |
| `AUDIT_alloc.md` F-02 | Host `free()` of a tracked pointer leaves false-live metadata and permanent `live_count/live_bytes` inflation. `alloc.c:189-197,544-558`; `tests/libc/test_alloc_attack.c:624-641` | `A2-runtime` | `med` | `sh tests/libc/run_libc_tests.sh`; amend `tests/libc/test_alloc_attack.c:624-641` to call `cplus_alloc_forget(q)` before `free(q)` and assert counters return to baseline (`AUDIT_alloc.md:267-280`) | Open; needs `_forget` API + `host_free_bypass_count` diagnostic. |
| `AUDIT_alloc.md` F-03 | Two aligned allocators with opposite deallocators; `cplus_aligned_alloc` untracked, Windows needs `_aligned_free`, `free` macro hazard. `libc/src/stdlib/stdlib_c17.c:53-71`; `stdlib/safe/src/safe_alloc.c:217-239`; `libc/install-include/cplus_libc.h:69-78` | `A2-runtime` | `med` | `sh tests/crt/asan/run_asan.sh` (dedicated ASan runner; it probes the compiler and builds into `build/asan`), with scenario 11 in `tests/crt/asan/test_alloc_asan.c:320-348` extended to use `cplus_aligned_free`; Windows-only compile check **cannot be verified on this host** — record as such (`AUDIT_alloc.md:281-293`) | Open; add `cplus_aligned_free`. |
| `AUDIT_stdio_format.md` F1 | `cplus_fclose_s` probe-chain deletion without tombstones + `was_live` bypass → second `fclose()` on a freed `FILE*` (**confirmed UAF**). `stdlib/safe/src/safe_io.c:13-54,78-108`; repro §8.2 | `A2-runtime` | `med` | `sh tests/crt/unit/run_unit_tests.sh` (existing `tests/crt/unit/test_fclose_s.c`); port §8.2 collision repro; ASan must report no UAF; 600-stream collision stress (`AUDIT_stdio_format.md:331-341`) | Open. |
| `AUDIT_stdio_format.md` F2 | Plain `cplus_fclose` false rejection/fd leak at recycled addresses + cross-registry bypass. `libc/src/stdio/stdio.c:10-37,62-82`; repro §8.1 | `A2-runtime` | `med` (shared registry with F1/F3) | `sh tests/libc/run_libc_tests.sh` (existing `tests/libc/test_stdio_attack.c`); assert `/dev/fd` count returns to baseline; cross-API close test (`AUDIT_stdio_format.md:343-354`) | Open. |
| `AUDIT_stdio_format.md` F3 | Bounded 256/512 double-close caches wrap and evict; genuine double-close becomes second `fclose`. §7.3 | `A2-runtime` | `med` | Close 257 distinct streams then double-close the first; fill 513 `cplus_fclose_s` slots then double-close an early stream; assert `EOF/EINVAL` and restored `/dev/fd` baseline; run `sh tests/libc/run_attack_tests.sh` (`AUDIT_stdio_format.md:356-363`) | Open. |
| `AUDIT_lifecycle.md` F1 | `cplus_thrd_create` detached-thread context lives on the caller's stack → use-after-scope. `libc/src/thread/threads_c11.c:119-159,217-223`; `tests/libc/test_threads_c11.c:323-325` | `A2-runtime` | `low` | `sh tests/libc/run_libc_tests.sh` (existing `tests/libc/test_threads_c11.c`) + barrier/poisoned-stack ASan probe (`AUDIT_lifecycle.md:461-466`) | Open. Heap-allocate ctx; trampoline free; map failure to `thrd_nomem`. |
| `AUDIT_lifecycle.md` F2 | uchar passes a 4-byte `unsigned` where host `mbstate_t` is 128 bytes. `libc/src/uchar/uchar.c:29-35,57-61,89-94` | `A2-runtime` | `low`–`med` | `sh tests/libc/run_libc_tests.sh`; compile-time `sizeof` assert + canary under stateful locale with ASan + repeated `mbrtoc16`/`c16rtomb` with `ps==NULL` (`AUDIT_lifecycle.md:468-472`) | Open. Real `static mbstate_t` for host state; separate surrogate word. |
| `AUDIT_lifecycle.md` F3 | `longjmp` recovery skips C++/RAII destructors; nested UBSan fault aborts. `stdlib/safe/src/safe_runtime.c:49-61,115-134`; `docs/libc/RUNTIME_RECOVERY.md:43` | `A2-runtime` (mitigation/docs) + `A1-compiler` (full fix, §4) | `external` for full fix | `sh tests/runtime/run_runtime_tests.sh`; guarded-body leak/lock probe and documented nested-UB abort (`AUDIT_lifecycle.md:474-480`) | Split ownership. Runtime can prefer `failstop=1`; do not claim RAII safety. |
| `AUDIT_memory_string.md` §5 #1–#3 (6 High) | `cplus_strcpy`/`strcat` no capacity; `strncpy`/`strncpy_bounded` huge `n`; `memrchr` huge `n` + NULL; `isblank` uncast int. `libc/src/string/{string.c:9-31,extra2.c:6-12,bounded.c:92-97,ctype2.c:4}` | `A2-runtime` | `med` | `sh tests/libc/run_libc_tests.sh` (existing `tests/libc/test_ctype.c`, `test_ctype_full.c`, `test_string_extra2.c`, `test_libc_safe_api.c`) + **new** `tests/libc/test_strncpy_huge_n.c` (proposed at `AUDIT_memory_string.md:390`, not on disk) + ASan/UBSan guard bytes (`AUDIT_memory_string.md:361-406`) | Partially contested: `WRAPPER_SAFETY_AUDIT.md:59-61` says `isblank` is "FIXED in this pass" — re-verify source before editing (§7, C9). |
| `AUDIT_numeric_wide.md` F01 | Wide format strings pass `%n` through; `format(wprintf,…)` attributes ignored by Apple Clang → no static checking. `libc/src/wchar/wchar_format.c:21-40,56-67` | `A2-runtime` (`cplus_*_checked` sibling) + `A1-compiler` for F11 | `med` + `external` | `sh tests/libc/run_libc_tests.sh` (existing `tests/libc/test_wchar_format.c`); add sentinel `L"%n"` probe and narrow `-Wformat=2` compile probe (`AUDIT_numeric_wide.md:180-191`) | Open. Compiler-side wide-format attribute is a separate Agent 1 item. |

**Phase 1 DoD:** see §7.2.

---

## 3. Work order — Phase 2: MEDIUM findings

### 3.1 Allocation (`docs/libc/AUDIT_alloc.md`)

| ID(s) | Finding / source | Owner | Dep. | Acceptance command | Note |
|---|---|---|---|---|---|
| F-05, F-06 | Recent-free cache false positive (fresh host `malloc` at a just-freed address counted as double free); heuristic classification; libc overwrites first slot when full, SafeCRT still direct-mapped. `alloc.c:85-101,357-361`; `safe_alloc.c:30-42,111-117,332-338` | `A2-runtime` | `med` | `sh tests/libc/run_libc_tests.sh` + `sh tests/crt/run_safecrt_tests.sh` (loops `tests/crt/test_safe_*.c`, including `test_safe_alloc.c`; also invoked by `sh tests/crt/run_all_crt.sh`); add recycled-address/collision scenarios (`AUDIT_alloc.md:311-325`) | Port open-addressed tagged cache; add `recent_table_full`; downgrade address-only "double free" claims. |
| F-08, F-09, F-10 | `cplus_calloc` overflow `ENOMEM` vs `calloc_s` `EOVERFLOW`; `fail_after(negative)` disables in libc but fails-all in SafeCRT; injection state process-global with no libc getter, `g_seq` counts decisions not allocations. `alloc.c:246-249,199-214,576-586`; `safe_alloc.c:193-198,124-131,532-551` | `A2-runtime` + `A2-docs` | `low`–`med` | `sh tests/libc/run_libc_tests.sh` + `sh tests/crt/unit/run_unit_tests.sh`; assert chosen `errno` and negative-argument semantics in `tests/libc/test_alloc_attack.c`, `tests/crt/unit/test_alloc_metadata_s.c` | Align or loudly document divergence; add `cplus_alloc_injection_active()`. |
| F-11 | `cplus_free` discards refusal; `cplus_free_checked` nulls `*inout` even on refusal, dropping the handle to a still-live host block. `alloc.c:364,373-379` | `A2-runtime` | `med` | `sh tests/libc/run_libc_tests.sh`; extend `tests/libc/test_alloc_attack.c:226-231`; pair with Phase 1 F-01 `_gen` variant | Document refusal logging; decide handle-nulling policy. |
| F-12 | `cplus_memcpy_bounded`/`memmove_bounded` check destination only; unknown capacity unchecked by default. `libc/src/string/bounded.c:24-52,61-73`; `cplus_libc_safe.h:33,55-62` | `A2-runtime` | `med` | `sh tests/libc/run_libc_tests.sh` + **new** `tests/libc/test_bounded_source_overread.c` (proposed at `AUDIT_memory_string.md:437-439`, not on disk) | Add `known_src`/`_bounded2`; keep old symbol as wrapper. |
| F-14 | `aligned_alloc_s` does not enforce C11 `size % alignment == 0` (unlike `cplus_aligned_alloc`). `stdlib/safe/src/safe_alloc.c:217-226` | `A2-runtime` | `low` | `sh tests/crt/unit/run_unit_tests.sh` (existing `tests/crt/unit/test_alloc_metadata_s.c`) | Add check; document the two-allocator distinction from F-03. |
| F-07 | No `alloc_size` attribute on exported wrappers; `.len` extent lowering incomplete. `cplus_libc.h:24-28`; `safe_alloc.h:51-61` | `A1-compiler` (§4), `A2-runtime` may add guarded attributes | `external` | Agent 1 compile-fail cases mirroring `tests/compile_fail/alloc_size_oob.c` with `cplus_malloc`/`malloc_s` + `.err` files (`AUDIT_alloc.md:295-309`) | Hand-off: `docs/patches/agent1-len-managed-allocators.md`. |

### 3.2 Memory / string / ctype (`docs/libc/AUDIT_memory_string.md`, 27 Medium findings)

| ID(s) / group | Finding / source | Owner | Dep. | Acceptance command | Note |
|---|---|---|---|---|---|
| NULL-guard group | `memchr`, `strchr`/`strrchr`, `strstr`, `strcasecmp`/`strncasecmp` have no NULL guards; ASCII-only case folding. `libc/src/string/extra.c:8-40`, `extra2.c:13-36`. Same class for host extras `strcoll`/`strxfrm`, `strnstr`, `swab`, `strtok`, and all 9 musl extras in `musl_extra.c:26-264` | `A2-runtime` | `low` each, `med` aggregate | `sh tests/libc/run_libc_tests.sh` (existing `test_string_extra2.c`, `test_libc_safe_api.c`) + UBSan table-driven NULL cases + `sh tests/libc/run_attack_tests.sh` | Treat musl verbatim copies as caller-precondition APIs; add guards where ABI-compatible, document the rest. |
| Bounded-tier group | `memcpy_bounded`/`memmove_bounded` source bound absent; `memcmp_bounded` refusal returns 0 (indistinguishable from equality) vs `memcmp_s_auto` -1; `strcpy`/`strcat`/`strncat_bounded` call `strlen` before capacity check. `bounded.c:61-113`; `safe_memory.c:228-245` | `A2-runtime` | `med` | `sh tests/libc/run_libc_tests.sh` + **new** `tests/libc/test_bounded_refusal_sentinel.c` (proposed at `AUDIT_memory_string.md:421-424`, not on disk); `sh tests/crt/unit/run_unit_tests.sh` | Choose `CPLUS_BOUNDS_REFUSED`, set `errno`, document in `cplus_libc.h`. |
| `cplus_memcpy` overlap | Byte-loop `memcpy` has no overlap detection; `_bounded` form has no `restrict` either. `memory.c:6-14`; `bounded.c:61-66` | `A2-runtime` | `med` | `sh tests/libc/run_libc_tests.sh` + guard-byte test modelled on `tests/crt/adversarial/test_mem50_cross_api.c` | ABI/contract decision: detect at runtime or keep `restrict` caller contract. |
| Direct `_s` / timing-safe tier | Caller-supplied `dmax`/`smax` never cross-checked against BOS; `cplus_sc_timingsafe_*` pass `BOS_UNKNOWN` for both operands. `stdlib/safe/src/safe_memory.c:35-81`, `safeclib_ext_wrappers.c:18-24` | `A2-runtime` + `A2-docs` | `low` | `sh tests/crt/unit/run_unit_tests.sh` + `sh tests/crt/fuzz/run_fuzz_smoke.sh`; document in `stdlib/safe/safe_memory.h` | Keep upstream delegation; document no object-size check. |

### 3.3 Stdio / format (`docs/libc/AUDIT_stdio_format.md`, 7 Medium findings)

| ID | Finding / source | Owner | Dep. | Acceptance command | Note |
|---|---|---|---|---|---|
| F4 | `stdio2.c`/`stdio3.c` unvalidated delegates crash on NULL; no format attributes. `libc/src/stdio/stdio2.c:3-19`, `stdio3.c:11-43` | `A2-runtime` | `low`–`med` | `sh tests/libc/run_attack_tests.sh` + `sh tests/libc/run_libc_tests.sh`; table-driven NULL cases | Documented gap (`cplus_libc.h:216-225`); decide guard policy per API. |
| F5 | `fread_s`/`fwrite_s` trust `bmax`, silently clamp, no `errno`, mislabel `size==0`/`count==0`. `stdlib/safe/src/safe_io.c:110-138` | `A2-runtime` | `med` | `sh tests/crt/unit/run_unit_tests.sh` (existing `tests/crt/unit/test_fwrite_s.c`) + canary cases from `AUDIT_stdio_format.md:380-389` | Add `known_bmax` from `CPLUS_SAFE_OBJ_SIZE`; keep plain `EOVERFLOW` test unchanged. |
| F6 | `fflush(stdin)` and update-stream sequencing unenforced. `stdio2.c:5`; `scratch/bugs56.cp` bug 265 | `A2-runtime` + `A2-docs` | `med` | `sh tests/libc/run_attack_tests.sh`; add documented helper test when implemented | Caller/runtime boundary; no silent sequencing fix. |
| F7 | Literal gate only in `_checked` macros; direct `cplus_*_s` and all `v*` bypass; `__builtin_constant_p` false-negatives. `stdlib/safe/src/safe_format.c:158-172`; `safe_format.h:78-104`; §7.4/§8.3 | `A2-runtime` + `A1-compiler` | `med` + `external` | `sh tests/libc/run_libc_tests.sh` (`tests/libc/test_format_attack.c`) + `sh tests/family_gaps/run_family_tests.sh` + compile probes from `scratch/bugs{19,20,42,56}.cp` (`AUDIT_stdio_format.md:365-378`) | Compiler pass is the real CWE-134 control; runtime gate is advisory. |
| F8 | `scanf_s` lacks Annex K `rsize_t`, rejects correct Annex-K calls; `scanf_fmt_check` ignores glibc `m`. `safe_format.c:75-112,233-323` | `A2-runtime` + `A2-docs` | `low` | `sh tests/crt/unit/run_unit_tests.sh`; add width/`%n`/`m`-modifier cases | Document contract divergence. |
| F9 | `freopen`/`fdopen` registry reconciliation; `stream` not NULL-checked in `freopen`; fd ownership unclear. `libc/src/stdio/stdio3.c:17-29` | `A2-runtime` | `med` (needs shared registry from Phase 1 F1/F2/F3) | `sh tests/libc/run_libc_tests.sh`; cross-API close test | Block behind unified registry. |
| F10 | Default `_exit(70)` violation trap kills the process on a literal-gate false positive; `fflush(NULL)` flushes unrelated streams. `stdlib/safe/src/safe_runtime.c:54-60` | `A2-runtime` + `A2-docs` | `low`–`med` | `sh tests/runtime/run_runtime_tests.sh`; controlled diagnostic test from `scratch/bugs19.cp` | Prefer controlled diagnostic over silent `_exit` for false-positive class. |

### 3.4 Math / time / wide / numeric (`docs/libc/AUDIT_numeric_wide.md`, 14 Medium findings)

| ID(s) | Finding / source | Owner | Dep. | Acceptance command | Note |
|---|---|---|---|---|---|
| F02, F03 | Non-reentrant static-buffer time APIs and global `setlocale`/`localeconv` state. `libc/src/time/time.c:8-9`, `time_ascii.c:11-35`, `libc/src/locale/locale.c:5-6` | `A2-runtime` + `A2-docs` | `med` | `sh tests/libc/run_libc_tests.sh`; two-thread `gmtime` vs `gmtime_r` stress; TSan only if available (`AUDIT_numeric_wide.md:193-200`) | Prefer `_r` APIs; header warning mandatory. |
| F04 | `cplus_exp10l` returns literal `0.0L` for non-binary64 `long double`. `libc/src/math/math_c23_musl.c:51-59` | `A2-runtime` | `med` (needs 80-bit target to verify) | Compile for an 80-bit `long double` target and assert `cplus_exp10l(0.5)==powl(10.0L,0.5)`, or `#error` on unsupported ABI (`AUDIT_numeric_wide.md:214-224`) | Cannot be fully verified on macOS arm64 (`long double == double`). |
| F05, F06 | `abs`/`labs`/`llabs`/`imaxabs` negate `MIN` (UB); `atoi`/`atol` truncate overflow without `ERANGE`. `libc/src/stdlib/stdlib_extra.c:9-12`, `musl_stdlib_extra.c:27-45` | `A2-runtime` | `low` | `sh tests/libc/run_libc_tests.sh`; boundary table `INT_MIN/LONG_MIN/LLONG_MIN/INTMAX_MIN`, ±0, MAX (`AUDIT_numeric_wide.md:226-232`) | Unsigned-magnitude trick or documented return. |
| F12, F13 | Narrow `strto*` have no NULL `nptr` guard (wide `wcsto*` does); `clock_gettime`/`timespec_get`/`nanosleep` and `wcsftime`-`tm`/`strftime` unguarded. `stdlib_extra.c:13-14`, `stdlib_extra3.c:10-25`, `time_extra.c:9-20`, `wchar_numeric.c:119-129` | `A2-runtime` | `low` | `sh tests/libc/run_libc_tests.sh` + UBSan NULL table (`AUDIT_numeric_wide.md:214-224`) | Make the guard surface consistent. |
| F14, F15 | Hostile `struct tm` delegated unchecked; unbounded wide-string ops have no capacity parameter. `time.c:5-11`, `libc/src/wchar/wchar.c:7-17`, `wchar_gaps.c:13-78` | `A2-runtime` + `A2-docs` | `low`–`med` | `sh tests/libc/run_libc_tests.sh`; contract docs (`cplus_libc.h`) with capacity requirements | Caller responsibility; document, do not claim safety. |
| F07–F10 | 23 C23 math APIs + `FE_TIESAWAY`/`timespec_getres`/`mbrtoc8`/`c8rtomb`. `AUDIT_numeric_wide.md:92-100` | **blocked** | `external` | `sh tests/libc/run_libc_tests.sh` (23 SKIPs today) + `sh tests/libc/test_math_c23_upstream.c` harness | See §6.1–6.2. Do not fabricate. |
| F11 | `format(wprintf/wscanf,…)` attributes ignored by Clang → diagnostics suppressed. `libc/src/wchar/wchar_format.c:56-67`; `cplus_libc.h:223-224` | `A1-compiler` (§4) | `external` | Agent 1 compile-probe for wide non-literal formats | Not fixable by wrapper code alone. |

### 3.5 Lifecycle / concurrency / logging (`docs/libc/AUDIT_lifecycle.md`, 11 Medium findings)

| ID(s) | Finding / source | Owner | Dep. | Acceptance command | Note |
|---|---|---|---|---|---|
| F4, F5 | Signal wrappers pass NULL `sigset_t*` to host (10 exports); fenv NULL guards inconsistent (`fegetenv`, `fesetenv`, `fesetexceptflag`, `feupdateenv` unguarded). `libc/src/signal/signal2.c:14-53`; `libc/src/fenv/fenv.c:10-11`, `fenv_extra.c:35-53` | `A2-runtime` | `low` | `sh tests/libc/run_libc_tests.sh` (existing `test_signal.c`, `test_extra7_signal.c`, `test_fenv_extra.c`) + table-driven NULL/EINVAL cases under UBSan (`AUDIT_lifecycle.md:482-487`) | 14 pointer-taking exports. |
| F6, F7 | `cplus_libc_log` clobbers `errno`; ignores short writes/EINTR/SIGPIPE. `libc/log/log.c:24,64`; header comment at `:5-13` | `A2-runtime` | `low` | `sh tests/libc/run_libc_tests.sh` (`tests/libc/test_logging_attack.c`) + closed-fd errno probe + nonblocking-pipe 4000-byte case (`AUDIT_lifecycle.md:489-495`) | Same root cause as `AUDIT_numeric_wide.md` F16 — fix once. |
| F8, F9, F10, F11 | Racy runtime config globals (`volatile` ≠ atomic); cleanup TLS not unwound on `thrd_exit`; unbounded report/handler recursion; `safe_log`/`safe_stats` not async-signal-safe. `stdlib/safe/src/safe_runtime.c:34-72,115-149`; `safe_log.c:20`; `safe_stats.c:21,33-45` | `A2-runtime` | `med` | `sh tests/runtime/run_runtime_tests.sh`; TSan two-thread config churn (local TSan unverified, §1.6); handler re-entry and `thrd_exit` cleanup probes (`AUDIT_lifecycle.md:474-495`) | Use `_Atomic`/`atomic_flag`; register `pthread_key` destructor; pre-register stat names. |
| F12 | `mtx_timedlock` trylock+`nanosleep` loop discards the timed flag, tight-spins on invalid `tv_nsec`, no generation token. `libc/src/thread/threads_c11.c:226-276` | `A2-runtime` | `med` | `sh tests/libc/run_libc_tests.sh` (`test_threads_c11.c`); timedlock on a PLAIN mutex must return error; monotonic-skew test (`AUDIT_lifecycle.md:184`) | Track timed bit in opaque `cplus_mtx_t`. |
| F13 | `ONCE_FLAG_INIT` hardcodes Apple private ABI constant `0x30B1BCBA`; `call_once` discards `pthread_once` result. `libc/install-include/cplus_threads.h:60-69`; `threads_c11.c:344-350` | `A2-runtime` + `A1-compiler` (robust SDK-generated init) | `med` + `external` | Compile-time `static_assert` against host `PTHREAD_ONCE_INIT` + multi-thread once smoke test (`AUDIT_lifecycle.md:195`) | Portable per-platform source file preferred. |
| F20 | Thread cancellation unhandled; cancelled thread can leave `cplus_mtx_t` locked. `libc/src/thread/thread.c:9-21` | `A2-docs` primarily | `low` | `sh tests/libc/run_libc_tests.sh`; document contract in `cplus_threads.h` | C11 adapters have no cancellation by design. |

**Phase 2 DoD:** see §7.3.

---

## 4. Work order — Phase 3: LOW findings and documented limits

These are predominantly contract/diagnostic items. No new safety guarantee is asserted by closing them.

| Family / IDs | Action | Owner | Dep. | Acceptance command |
|---|---|---|---|---|
| `AUDIT_alloc.md` F-04, F-13, F-15, F-16 (DOCUMENTED LIMIT), F-17–F-20 (LOW) | Document lock-free host-`realloc` window, zero-size 1-byte rule, exact-base interior-pointer semantics, `size_t` counter wrap, generation wrap, `uintptr_t` wrap / O(n) scan, no `RSIZE_MAX_MEM` on `cplus_aligned_alloc`; add `recent_full` counter where named. `alloc.c:308-337,386-412,511-522,544-574`; `stdlib_c17.c:53-71` | `A2-docs` + `A2-runtime` | `low` | `sh tests/libc/run_libc_tests.sh`; `grep -n 'realloc.*lock\|zero-size\|interior' libc/install-include/cplus_libc.h stdlib/safe/safe_alloc.h` |
| `AUDIT_memory_string.md` 16 Low + 1 Info (§4) | Document provenance/termination caller duties for the correctly-cast ctype family, `strlcpy`/`strlcat`, `strerror`, `memset_explicit`; keep `cplus_libc_set_unknown_capacity_mode` default-off and documented. `ctype.c:5-17`, `c23_small_gaps.c:102-111`, `bounded.c:20-22` | `A2-docs` | `low` | `sh tests/libc/run_libc_tests.sh` + `sh tests/crt/unit/run_unit_tests.sh`; `docs/libc/ORDINARY_C_SAFETY.md` updated |
| `AUDIT_stdio_format.md` F11–F15 | Document raw-format forfeiture, truncation/NUL caller duty, `sprintf_s`/`fopen_s` shape divergence, `%I64n` parser limit, pointer-into-object `fmt_check_dest_bounds`. §9 | `A2-docs` | `low` | `sh tests/crt/unit/run_unit_tests.sh` + `sh tests/libc/run_libc_tests.sh`; header contract check |
| `AUDIT_numeric_wide.md` F16–F21 (except F16 dedup with lifecycle F6) | Document host NaN/FP semantics, latent duplicate `exp10`, huge-width DoS, `div`/`wcstok` UB preconditions, `long`-vs-`time_t` header mismatch. §1 | `A2-docs` (+ `A2-runtime` for F21 header type) | `low` | `sh tests/libc/run_libc_tests.sh`; header signature check in `libc/install-include/cplus_libc.h` |
| `AUDIT_lifecycle.md` F14–F19, F21 | Decide `errno` macro wiring vs removal of the unused `__cplus_errno_location` stub; update stale `tests/libc/test_logging_attack.c` A2 expectation to 4095-byte `"...\n"` tail and rewrite `tests/adversarial/logging_attack_NOTES.md` A4/A5 as fixed with evidence; expose telemetry overflow counter; document TSS/`call_once`/host complex/fenv contracts. `errno.c:1-4`; `log.c:38-65`; `safe_stats.c:18-21,58-65` | `A2-runtime` + `A2-docs` | `low` | `sh tests/libc/run_libc_tests.sh` (updated `test_logging_attack.c`) + `sh tests/crt/unit/run_unit_tests.sh` + exact-boundary 4090/4091/4092 truncation cases (`AUDIT_lifecycle.md:489-495`) |

**Phase 3 DoD:** see §7.4.

---

## 5. Compiler-dependent items — explicitly assigned to Agent 1

**Agent 2 does not edit `compiler/**` or `runtime/**`.** The items below are requests/hand-offs; none of them currently passes compile-time enforcement. Two source documents state this explicitly: `docs/libc/COMPILE_TIME_GAPS.md:1-14` ("report only — no compiler edits"; an `ACCEPT` in *Observed* means the safety property is **not** proven; `RUNTIME` cases are capability gaps) and `docs/libc/KNOWN_COMPILER_GAPS.md:1-4`.

| # | Missing capability | Evidence / finding IDs | Hand-off document (link) | Acceptance command (Agent 1 side) | Status |
|---|---|---|---|---|---|
| 1 | Class-field lifetime tracking for `vector`/`collections`/`net`/`file`/`error`/`string` (use-after-`drop`/`close`, double-`drop`, reset misuse) | `COMPILE_TIME_GAPS.md:216-224`; cases `vector_use_after_drop.cp`, `collections_use_after_drop.cp`, `net_use_after_close.cp`, `file_double_close.cp`, `error_use_after_reset.cp` | `docs/libc/COMPILE_TIME_GAPS.md` §7 item 1 | `sh tests/stdlib/compile_time/run_compile_time.sh` + `sh tests/run_tests.sh` | Not enforced; `drop()` lowers to a plain method call. |
| 2 | Guard ownership state (mutex owner, locked/unlocked, guard→mutex liveness, handle validity) | `COMPILE_TIME_GAPS.md:226-231`; 4 `thread_*` gap cases | `docs/libc/COMPILE_TIME_GAPS.md` §7 item 2 | same | All 4 compile cleanly; runtime-enforced only. |
| 3 | Alias analysis / exclusivity for class owners; slice→source linking | `COMPILE_TIME_GAPS.md:233-241`; `vector_mutable_alias.cp`, `collections_mutable_alias.cp`, `vector_invalid_slice.cp`, `string_dangling_view.cp` | `docs/libc/COMPILE_TIME_GAPS.md` §7 item 3 | same | Not enforced; direct `int[3]` subscript *is* tagged `[CPLUS-BOUNDS]`. |
| 4 | `[CPLUS-` tags on use-after-move / escaped-borrow / copied-owner rejections | `COMPILE_TIME_GAPS.md:243-252`; 6 PARTIAL cases; manifest contradictions in §2 | `docs/libc/COMPILE_TIME_GAPS.md` §7 item 4 | same | 5 of 46 cases have tagged REJECT; 6 are untagged PARTIAL. Converting them removes the `net.json`/`file.json` summary contradictions. |
| 5 | Stdlib class bounds metadata (constant OOB via container methods; stale slices after reallocation) | `COMPILE_TIME_GAPS.md:254-260`; `vector_const_oob.cp`, `vector_invalid_slice.cp` | `docs/libc/COMPILE_TIME_GAPS.md` §7 item 5 | same | Method-level length/capacity metadata missing. |
| 6 | Dynamic function-pointer dispatch bounds (UBSan-only today) | `tests/compile_fail/case13_dispatch_const.c` (tagged static reject), `tests/runtime_fail/case13_dispatch_runtime.c` (Clang UBSan) | `docs/libc/DISPATCH_BOUNDS_HANDOFF.md` (whole file) | `PATH=/usr/bin:/bin:/usr/sbin:/sbin ./build/c+ check tests/compile_fail/case13_dispatch_const.c --default`; `./build/c+ build tests/runtime_fail/case13_dispatch_runtime.c -o /tmp/d && /tmp/d` | Runtime fixture must be changed to require a C+ diagnostic once instrumentation lands. |
| 7 | `.len` lowering for SafeCRT/libc allocators; `alloc_size` attributes; `&var` out-param null false positive | `docs/patches/agent1-len-managed-allocators.md:14-50`; `KNOWN_COMPILER_GAPS.md:58-62`; `AUDIT_alloc.md` F-07 | `docs/patches/agent1-len-managed-allocators.md`; `docs/libc/KNOWN_COMPILER_GAPS.md` §3 | Add compile-fail cases for `cplus_malloc(sizeof(int)*size)` and `malloc_s(sizeof(int)*size)` with `.err` files; `.len` success case from the hand-off | `collectDynamicPointers()` `kAllocators` recognizes only compiler-runtime allocators. |
| 8 | Interprocedural/pointer-extent/heap-capacity bounds; fix 2-arg `calloc` mis-sizing and post-free reassignment false positives; 547 undiagnosed stack-escape cases | `KNOWN_COMPILER_GAPS.md:6-43,64-81`; `api_status_matrix.json:top_gaps` rank 4 (SAFETY_GATES.json gate 12 FFI `NOT_PROVEN`) | `docs/libc/KNOWN_COMPILER_GAPS.md` (whole file) | `sh tests/run_tests.sh`; `c+ check --default tests/scenarios/scenario_06.c`; count `[CPLUS-` diagnostics | 695 diagnostics cover 653 of 1200 cases; 547 undiagnosed. **No Rust-equivalence claim.** |
| 9 | Destructor-aware unwinding / exception-based recovery; `-fsanitize-recover=all` instead of `-fno-sanitize-recover=all` | `AUDIT_lifecycle.md` F3; `docs/libc/RUNTIME_RECOVERY.md:43,45-51` | `docs/libc/RUNTIME_RECOVERY.md`; `docs/libc/AUDIT_lifecycle.md` §3 F3 | compiler-side runtime recovery test from `RUNTIME_RECOVERY.md` | Nested UBSan fault currently aborts; documented as accepted boundary. |
| 10 | Wide-format attribute support (`format(wprintf/wscanf,…)`) and tagged narrow-format literal gate | `AUDIT_numeric_wide.md` F11; `AUDIT_stdio_format.md` §11 "Compiler / toolchain"; `api_status_matrix.json:top_gaps` rank 14 | `docs/libc/AUDIT_stdio_format.md` §11; `docs/libc/COMPILE_TIME_GAPS.md` §6-7 | `-Wformat=2` compile probes from `scratch/bugs{19,20,42,56}.cp` | Apple Clang ignores wide-format attributes. |
| 11 | Once-flag initialization generated from target SDK rather than hard-coded | `AUDIT_lifecycle.md` F13; `cplus_threads.h:60-69` | `docs/libc/AUDIT_lifecycle.md` §6 "Compiler" | compile-time `static_assert` against host `PTHREAD_ONCE_INIT` | Private Apple constant is fragile. |
| 12 | Re-validate the compiler-readable contract schema against Agent 1's real file when PR #16 merges | `COMPILER_CONTRACT_ALIGNMENT.md:22-29,114-126`: `compiler/Analysis/libc_contracts.json` is **absent** on this branch; 32 emitted entries vs 29 requested names | `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md` (whole file); `docs/patches/agent1-hp-class-linkage.patch` | `python3 tests/libc/test_compiler_contract_alignment.py`; `python3 tests/libc/validate_contracts.py`; Agent 1 compiler-side contract-loading test | Schema built from a written request, not from the real JSON. Multi-TU `.hp` linkage is reported as already working at `KNOWN_COMPILER_GAPS.md:62` — do not re-apply the patch blindly. |

**Baseline for Agent 1 work** (`docs/libc/COMPILE_TIME_GAPS.md:119-157`, derived from the 7 manifests): 46 cases → 35 ACCEPT, 6 PARTIAL (untagged), 5 tagged REJECT; expected REJECT 9 → only 5 tagged; 5 of 7 families (`thread`, `collections`, `vector`, `net`, `file`) have zero tagged REJECTs. The compiler was **not** executed by this plan.

---

## 6. Blocked items and their exact unblock conditions

Nothing in this section has a fabricated implementation. A blocked item moves to "in progress" only when the named condition is satisfied by a named artifact.

### 6.1 23 C23 math APIs — DECL-ONLY

State: `libc/src/math/math_c23.c:122-359` and `libc/src/math/math_c23_upstream.c:48-129` emit declarations only; calling any of `nextup/f/l`, `nextdown/f/l`, `fadd/f/l`, `fsub/f/l`, `fmul/f/l`, `fdiv/f/l`, 3-arg `hypot`, `frexp16`, `fma16`, `logb16`, `exp16` is an unresolved external; tests SKIP each (`docs/libc/AUDIT_numeric_wide.md:84,94`; `docs/libc/C23_MATH_UPSTREAM.md:11-32`). `nm -gU build/compat/cmake/libcplus_hardened_libc.a` shows only `cplus_exp10/f/l` (musl copy) and `cplus_roundeven/f/l` (builtin) C23 symbols defined (`AUDIT_numeric_wide.md:25-31`).

**Exact unblock condition (one of):** (a) a toolchain exposing `__builtin_nextup*`, `__builtin_fadd*` etc. that the existing builtin-gated `math_c23.c` skeleton can call; or (b) an approved decision to vendor the LLVM libc `src/__support/FPUtil/{FPBits,NormalFloat,dyadic_float,FMA,Hypot,cast,rounding_mode,BasicOperations,double_double,ExceptValues,PolyEval}.h`, `generic/{add_sub,mul,div}.h`, `expxf16.h`, and the `src/__support/CPP/*`, `hdr/*`, `macros/*` layers; or (c) a different upstream with self-contained C sources. Additionally: the `fadd`/`fsub`/`fmul`/`fdiv` family has a **C23 signature mismatch** (LLVM returns `float` from `double`/`long double` args; C23 requires same-width `double`/`long double`), and `faddf`/`fsubf`/`fmulf`/`fdivf`, `fmaf16`, `hypot3` are HTTP 404 at `llvmorg-20.1.1` — those need a genuinely new source or signature decision (`C23_MATH_UPSTREAM.md:73-108,110-130`). Source path: `docs/libc/C23_MATH_UPSTREAM.md` (exact URLs at `:42-77`, per-API blockers at `:84-108`), `docs/libc/MATH_GAPS_EVIDENCE.md`. **Not part of the library build:** `libc/src/math/math_c23_upstream.c` is intentionally absent from `libc/CMakeLists.txt`; a parent-owner decision is required even for the extern-only file (`C23_MATH_UPSTREAM.md:161-169`).

### 6.2 `timespec_getres`, `FE_TIESAWAY`, `mbrtoc8`, `c8rtomb` — 4 named C23 gaps

State: all BLOCKED, tests SKIP. Exact blockers and unblock conditions:

- `timespec_getres`: no pinned upstream source (musl `v1.2.6`/`master` ship only `timespec_get.c`; LLVM libc `llvmorg-20.1.1`/`main` have no `timespec_getres.{cpp,h}`/CMake target). Deriving it from `clock_getres` is a **new algorithm** and forbidden. Unblock: a pinned source or host/compiler support. Source: `docs/libc/C23_SMALL_UPSTREAM.md:20,49-50,63-68,123-127`; `docs/libc/AUDIT_numeric_wide.md:96`.
- `FE_TIESAWAY`: absent from host `<fenv.h>` and LLVM's pinned `fenv-macros.h`; AArch64 FPCR has no ties-away encoding. Publishing a numeric value is **fabrication**. Unblock: a compiler/ABI that defines it. Source: `C23_SMALL_UPSTREAM.md:17-28,51-53,69-72,128-134`; `AUDIT_numeric_wide.md:95`.
- `mbrtoc8` / `c8rtomb`: no pinned musl revision ships them (404 at pin, `v1.2.6`, `master`); only an unmerged 2023 mailing-list patch exists, rejected for unpinned provenance, dependence on musl-internal `mbstate_t` layout, an `s[0]` read before NULL check, `weak_alias`/`_Alignas`/`INT_MIN` needs, and a known C-locale correctness defect. Unblock: a pinned, conforming upstream source. Source: `docs/libc/UCHARC23_UPSTREAM.md:15-37,90-120`; `libc/upstream/musl/PROVENANCE.md:55-56`; `AUDIT_numeric_wide.md:97`.
- Context (not blocked): `memset_explicit` was closed by an LLVM libc copy but its source `c23_upstream_small.c` is **not in CMake** and has a duplicate-symbol risk against `c23_small_gaps.c` on glibc >= 2.38 (`C23_SMALL_UPSTREAM.md:19,74-115,165-176`). Unblock for build integration: owner decision on CMake inclusion and the `#if` guard.

### 6.3 49 UNSUPPORTED APIs (by design)

State: deliberately not shipped — 34 `<stdatomic.h>` type-generic C ABI entries, 13 `<stdio.h>` (insecure `gets`/`tmpnam`, Annex K macro names, `gets_s`/`tmpnam_s`), 2 `<stdlib.h>` (`bsearch_s`, `qsort_s`). Source: `docs/libc/iso_c_inventory.json` per-API records; `docs/libc/COVERAGE.md:75,79`; `api_status_matrix.json:top_gaps` ranks 8/9.

**Exact unblock condition:** an explicit product/ABI decision per family. For `<stdatomic.h>`/`<tgmath.h>`, do **not** invent `cplus_atomic_*`/`cplus_tgmath` linkable symbols for `_Generic` macros; the supported pattern is calling host headers directly, plus compile-only consumer tests (`api_status_matrix.json:top_gaps` rank 9). For `qsort_s`/`bsearch_s`, a portable context-thunk implementation in `libc/src/stdlib/stdlib_c17.c` is only a **proposal** (`top_gaps` rank 8); it requires an approved signature deviation, a contract entry, and tests before it can be implemented. For the insecure/Annex K macro names, unblock = a decision to ship a namespaced, non-macro ABI with documented divergence. No implementation is proposed here.

### 6.4 ASan host issue

State: Apple clang 16 ASan aborts at startup (`sanitizer_malloc_mac.inc:189` CHECK failure, even with a valid `-isysroot`); Homebrew clang 23 links and runs ASan **only with** `-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`; without it, default sysroot `MacOSX27.sdk` is missing and linking fails. Source: `docs/libc/ASAN_AUDIT.md:90-112,192-221`; `docs/libc/COVERAGE.md:111-120`.

**Exact unblock condition:** use the explicit Homebrew clang 23 + `-isysroot` invocation for local evidence, and **separately** observe the Linux `asan-libc-crt` CI job after push for any cross-platform claim. A local ASan pass is never a substitute for Linux CI. Do not add `asan_interface.h` poisoning (`ASAN_AUDIT.md:37-60`). TSan remains unverified locally (needs `DYLD_INSERT_LIBRARIES=.../libclang_rt.tsan_osx_dynamic.dylib`); libFuzzer is not linkable locally (`COVERAGE.md:121-125`).

### 6.5 Remote CI and SonarCloud — unverified

State: Linux `linux-crt` / `asan-libc-crt` jobs and GitHub Actions results must be observed after push; a clean local export rebuild is not a remote green result (`docs/libc/COVERAGE.md:80`; `api_status_matrix.json:top_gaps` rank 13). SonarCloud: no credentials locally; the external 23.4% duplication / security D / reliability E ratings were not re-scanned and no gate pass is claimed (`COVERAGE.md:81`; `docs/libc/SONAR_TRIAGE.md:1-17,111-119`).

**Exact unblock condition (CI):** push the branch and read the job logs for `linux-crt`, `asan-libc-crt`, and the Linux SDK consumer / win32 cross-build; capture into `docs/libc/CI_EVIDENCE.md` (per `top_gaps` rank 13).
**Exact unblock condition (Sonar):** a `SONAR_TOKEN` with permission to fetch issues and quality-gate status; run the two documented `curl` calls, triage each issue into generated-fixture noise / real production issue / justified suppression, then re-run the remote scan after push and record the actual numbers (`SONAR_TRIAGE.md:129-146`). Until then, no rating improvement is claimed.

### 6.6 Other evidence-gated items (same rule)

- ABI evidence: `api_status_matrix.json` records 712/714 APIs with `abi_test = NO EVIDENCE`; unblock = implement `tests/abi/exported_symbols.c` + `tests/abi/run_abi.sh` and run on Linux/Win32 (`top_gaps` ranks 2/18). See §7 C4 for the conflicting 644/646 figure in `COVERAGE.md:77`.
- Rust-equivalence / FFI: `docs/libc/SAFETY_GATES.json` reports 0 PROVEN, 11 PARTIAL, 1 NOT_PROVEN (gate 12 FFI), and 547 undiagnosed stack-escape scenarios; unblock = Agent 1 compiler work plus a dedicated FFI boundary test (`top_gaps` rank 4). **No equivalence claim is made here.**
- Contract traceability: 369/714 APIs lack a machine-readable contract and 8 `_s` entries still carry placeholder text (`top_gaps` rank 3; `docs/libc/CONTRACT_STATUS.md` §3). Unblock = concrete signatures/error codes and `validate_contracts.py` failing on placeholders.
- Method results: `docs/libc/METHOD_TEST_MATRIX.md` has all 9 groups `_pending`; unblock = run `sh tests/methods/run_methods.sh` and record per-group output (`top_gaps` rank 12).
- Symbol audit snapshot: `docs/libc/SYMBOL_AUDIT.md` was generated for 233 symbols at HEAD `9a0b179`, not the current 714/HEAD `3131acf` (`top_gaps` rank 11). Unblock = regenerate from the matrix generator.

---

## 7. Definition-of-done checklists

### 7.1 Phase 0 — baseline and reconciliation (this plan)

- [x] All eleven named inputs read; counts recomputed from machine-readable per-API records where summaries disagreed (§1.2, §8).
- [x] No test runner executed by the plan author; recorded results explicitly labelled `recorded` (§0.1, §1.4).
- [x] No file other than `docs/libc/IMPLEMENTATION_PLAN.md` created/modified; no `git add`/commit.
- [ ] The parent agent resolves or records each contradiction in §8 before any code change is accepted as "done".

### 7.2 Phase 1 — HIGH findings

- [ ] Each HIGH fix lands as a small, separately reviewable commit with its regression test (per `WRAPPER_SAFETY_AUDIT.md:75`).
- [ ] Every guarded entry point rejects NULL consistently with `EINVAL` where the contract allows it (`WRAPPER_SAFETY_AUDIT.md:80-81`).
- [ ] Close tracking unified with tombstones/generation across both stdio layers; cross-registry handoff tests added (`WRAPPER_SAFETY_AUDIT.md:76-77`).
- [ ] Capacity clamps added to memory wrappers; never rely on caller-declared `dmax` alone where BOS is available (`WRAPPER_SAFETY_AUDIT.md:78-79`).
- [ ] Both DEFAULT and STRICT modes tested with sanitizers disabled; sanitizers are additional evidence only (`WRAPPER_SAFETY_AUDIT.md:92-94`).
- [ ] Fatal cases run in subprocesses with timeouts and exact exit codes recorded; allocation failures use `stdlib_alloc_fail_at/after/reset`, never memory exhaustion (`WRAPPER_SAFETY_AUDIT.md:94-96`).
- [ ] Contracts/limitations docs updated with each fix; `sh tests/run_all_evidence.sh` summary recorded pass/fail/skip; no 100% claim (`WRAPPER_SAFETY_AUDIT.md:88,97`).

### 7.3 Phase 2 — MEDIUM findings

- [ ] Every row in §3 has either a code change + regression test or an explicit "documented caller responsibility" entry in `cplus_libc.h` / `stdlib/safe/safe_*.h`.
- [ ] Cross-layer consistency table refreshed for the four memory tiers (`raw`/`_bounded`/`_s`/`_s_auto`) and the two stdio registries (`AUDIT_memory_string.md:241-259`; `AUDIT_stdio_format.md:185-196`).
- [ ] New test files named as "proposed, not on disk" in §2/§3 are actually created before their row is marked done.
- [ ] ABI-affecting new symbols (`_gen`, `_forget`, `cplus_aligned_free`, `_bounded2`, `known_bmax`) exported in the install header and covered by `sh tests/libc/install_smoke.sh`.
- [ ] Logger `errno` save/restore and short-write retry verified by the closed-fd and nonblocking-pipe probes; stale logging notes updated.
- [ ] `sh tests/run_all_evidence.sh` re-run with pass/fail/skip recorded.

### 7.4 Phase 3 — LOW / documented limits

- [ ] Every DOCUMENTED LIMIT in `AUDIT_alloc.md` F-04/F-13/F-15/F-16 appears in the public header with the residual limitation stated.
- [ ] Stale adversarial logging assertion (`tests/libc/test_logging_attack.c` A2) updated to the 4095-byte bounded output with `"...\n"` tail; A4/A5 notes rewritten as fixed with evidence (`AUDIT_lifecycle.md` F15).
- [ ] No_low finding is "fixed" by silently changing a safety contract; documentation changes cite the source path.
- [ ] `sh tests/libc/run_libc_tests.sh` and `sh tests/crt/unit/run_unit_tests.sh` green; documentation-only grep checks pass.

### 7.5 Phase 4 — Agent 1 compiler hand-offs

- [ ] Each of the 12 items in §5 has an Agent 1 owner and a recorded compiler-side outcome; Agent 2 touched no `compiler/**`/`runtime/**` file.
- [ ] The 46-case compile-time manifest set is re-run and the 6 untagged PARTIAL outcomes are either tagged REJECT or explicitly accepted with rationale; `net.json`/`file.json` summary contradictions resolved (`COMPILE_TIME_GAPS.md:50-63,243-252`).
- [ ] `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md` re-validated against the real `compiler/Analysis/libc_contracts.json` when PR #16 merges (4-point checklist at `:114-126`).
- [ ] `docs/patches/agent1-hp-class-linkage.patch` checked against `KNOWN_COMPILER_GAPS.md:62` before any re-application.
- [ ] `sh tests/run_tests.sh` and `sh tests/stdlib/compile_time/run_compile_time.sh` results recorded with compiler version and host.

### 7.6 Phase 5 — blocked items

- [ ] Each of §6.1–6.6 moves only when its exact unblock condition is met by a named artifact (toolchain version, pinned upstream commit, CI log URL, or token-authenticated report).
- [ ] C23 SKIPs in `tests/libc/test_{math_c23,math_gaps,math_c23_upstream,c23_small_gaps,uchar_c23}.c` are converted to real assertions only when a real backend lands (`AUDIT_numeric_wide.md:210-212`).
- [ ] No fabricated constant, algorithm, upstream file, rating, or ABI symbol is introduced to "close" a blocked row.
- [ ] Local ASan pass and remote CI pass remain separately reported, never conflated.
- [ ] No 100%-coverage, complete-memory-safety, or Rust-equivalence statement appears in any updated doc.

---

## 8. Contradictions found in the inputs (reported, not fixed)

The parent agent should resolve these; this plan only records them.

| # | Contradiction | Sources |
|---|---|---|
| C1 | `iso_c_inventory.json:counts.by_header` reports MISSING counts that do not match the per-API records: math.h **34 vs 23**, threads.h **41 vs 0**, uchar.h **9 vs 2**, wchar.h **11 vs 0**, complex.h **18 vs 0**, stdio.h **1 vs 0**, string.h **1 vs 0**. The per-API records and `counts.by_classification` both give **27** MISSING. This plan uses 27/23 as authoritative. | `docs/libc/iso_c_inventory.json` (`counts.by_header` vs `.headers[].apis[]` and `counts.by_classification`) |
| C2 | `C23_MATH_UPSTREAM.md` says math.h has "exactly these 23" MISSING APIs, agreeing with the per-API records, but contradicting the JSON summary's 34. | `docs/libc/C23_MATH_UPSTREAM.md:5-9`; `iso_c_inventory.json:counts.by_header["math.h"]` |
| C3 | `COVERAGE_100_AUDIT.md` prose says 28 MISSING / 7 UPSTREAM (and internally `:15` vs `:52` say 27 vs 28), while the JSON says 27 / 8. | `docs/libc/COVERAGE.md:74`; `docs/libc/AUDIT_numeric_wide.md:100`; `iso_c_inventory.json` |
| C4 | ABI-evidence counts disagree: `COVERAGE.md:77` says "644 of 646 declared APIs" with NO EVIDENCE; `api_status_matrix.json:counts.evidence_coverage.abi_test` and `top_gaps` rank 2 say **712 of 714**. Likely different snapshots/denominators. | `docs/libc/COVERAGE.md:77`; `docs/libc/api_status_matrix.json` |
| C5 | `api_status_matrix.json:counts.c17_gap_closing_apis` = **203**, while `COVERAGE.md:23` says **202** APIs mapped in the C17 matrix. | `api_status_matrix.json:counts`; `docs/libc/COVERAGE.md:23` |
| C6 | `cplus_isblank` is listed as an open **High** finding (`ctype2.c:4` passes uncast `int`) but `WRAPPER_SAFETY_AUDIT.md:59-61` says it was "FIXED in this pass". Both are dated 2026-11-23. Verify the source before editing. | `docs/libc/AUDIT_memory_string.md:89,196,346,361-377`; `docs/libc/WRAPPER_SAFETY_AUDIT.md:59-61` |
| C7 | Lifecycle F3 (`longjmp` skips RAII) has severity **High** in `AUDIT_lifecycle.md:44,86`, but the umbrella audit groups it under **MED** item 7. | `docs/libc/AUDIT_lifecycle.md:44,86`; `docs/libc/WRAPPER_SAFETY_AUDIT.md:64` |
| C8 | `net.json` records `net_use_after_transfer.cp` as `"observed": "REJECT"` while tagging `false` and `tagged_cplus_diagnostics_observed: 0`; `file.json` records `file_invalid_borrow.cp` as REJECT while its note and summary say PARTIAL. `COMPILE_TIME_GAPS.md` resolves both to PARTIAL (untagged) — the manifests remain internally inconsistent on disk. | `docs/libc/COMPILE_TIME_GAPS.md:50-63,99,114` |
| C9 | `COMPILER_CONTRACT_ALIGNMENT.md` emits 32 contract entries against 29 requested names, and states Agent 1's `compiler/Analysis/libc_contracts.json` is **absent** on this branch, so the schema is unvalidated; meanwhile `KNOWN_COMPILER_GAPS.md:62` says multi-TU `.hp` linkage already compiles, despite an existing Agent 1 linkage patch. | `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md:22-29,35,114-126`; `docs/libc/KNOWN_COMPILER_GAPS.md:62`; `docs/patches/agent1-hp-class-linkage.patch` |
| C10 | The two logger-`errno`-clobber findings are the same defect counted in two audits (`AUDIT_numeric_wide.md` F16 and `AUDIT_lifecycle.md` F6), so the 127 total includes at least one cross-audit duplicate; likewise wide-format `%n` appears in both `AUDIT_numeric_wide.md` F01 and `AUDIT_stdio_format.md` §11. | `docs/libc/AUDIT_numeric_wide.md:55,238`; `docs/libc/AUDIT_lifecycle.md:122-129`; `docs/libc/WRAPPER_SAFETY_AUDIT.md:26-35` |
| C11 | `AUDIT_stdio_format.md:321` says "Total: 15 findings — 3 HIGH, 7 MED, 5 LOW" and then notes F10's temp-file aspect is LOW but the trap raises the overall finding to MED; the per-layer split (runtime 8, compiler 3, caller 4) sums to 15. No discrepancy in totals, but severity labeling of F10 is dual — flagged so it is not double-counted. | `docs/libc/AUDIT_stdio_format.md:313-325` |
| C12 | `COVERAGE.md:20` calls the earlier 91.80% libc line-coverage figure stale in favour of 91.74%, but still prints 91.80% in the same row; function coverage 97.90% is "last recorded, not re-measured". | `docs/libc/COVERAGE.md:20` |

---

## 9. Source index (all assertions above cite one of these)

- Umbrella / counts: `docs/libc/WRAPPER_SAFETY_AUDIT.md`, `docs/libc/COVERAGE.md`, `docs/libc/iso_c_inventory.json`, `docs/libc/api_status_matrix.json`.
- Five audit files: `docs/libc/AUDIT_alloc.md`, `AUDIT_memory_string.md`, `AUDIT_stdio_format.md`, `AUDIT_numeric_wide.md`, `AUDIT_lifecycle.md`.
- Compiler hand-offs: `docs/libc/COMPILE_TIME_GAPS.md`, `docs/libc/DISPATCH_BOUNDS_HANDOFF.md`, `docs/libc/KNOWN_COMPILER_GAPS.md`, `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md`, `docs/libc/RUNTIME_RECOVERY.md`, `docs/patches/agent1-len-managed-allocators.md`, `docs/patches/agent1-hp-class-linkage.patch`.
- Blocked-item evidence: `docs/libc/C23_MATH_UPSTREAM.md`, `C23_SMALL_UPSTREAM.md`, `UCHARC23_UPSTREAM.md`, `MATH_GAPS_EVIDENCE.md`, `docs/libc/ASAN_AUDIT.md`, `docs/libc/SONAR_TRIAGE.md`, `docs/libc/SAFETY_GATES.json`.
- Runner entry points (all confirmed present): `tests/run_all_evidence.sh`, `tests/run_tests.sh`, `tests/crt/run_all_crt.sh`, `tests/crt/unit/run_unit_tests.sh`, `tests/crt/fuzz/run_fuzz_smoke.sh`, `tests/crt/coverage_safecrt.sh`, `tests/libc/run_libc_tests.sh`, `tests/libc/coverage_libc.sh`, `tests/libc/run_fuzz.sh`, `tests/libc/run_attack_tests.sh`, `tests/libc/run_corpus_tests.sh`, `tests/libc/install_smoke.sh`, `tests/stdlib/run_stdlib_tests.sh`, `tests/stdlib/compile_time/run_compile_time.sh`, `tests/stdlib/run_deep_tests.sh`, `tests/stdlib/fuzz/run_fuzz.sh`, `tests/safety/run_safety_matrix.sh`, `tests/rust_vs_cplus/run_compare.sh`, `tests/family_gaps/run_family_tests.sh`, `tests/methods/run_hundred.sh`, `tests/integration/run_integration.sh`, `tests/runtime/run_runtime_tests.sh`.
- Proposed-but-not-on-disk acceptance tests are explicitly marked in §2/§3: `tests/libc/test_strncpy_huge_n.c`, `tests/libc/test_bounded_refusal_sentinel.c`, `tests/libc/test_bounded_source_overread.c`.

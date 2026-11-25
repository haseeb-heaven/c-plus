# C+ hardened libc coverage (reconciled 2026-11-23)

Tracks what is actually shipped/verified on `agent/modern-stdlib` (PR #17). **No full ISO C replacement claim.**

This file was reconciled against the test runners and the machine-readable inventories named below. "Recorded result" means the pass/fail figure is stated in a named repository document or runner log; this reconciliation re-derived the case counts from the on-disk sources but did **not** re-execute every suite. No number here is inferred from a neighbouring test or from a successful build.

## Verified (this host)

| Area | Command | Result |
|---|---|---|
| CRT corpus | `sh tests/crt/run_all_crt.sh` (corpus stage) | 10 passed, 0 failed (recorded) |
| Safe CRT unit | `sh tests/crt/unit/run_unit_tests.sh` | **79 passed, 0 failed** (79 `tests/crt/unit/test_*.c` files; corrected from the stale 76) |
| Safe CRT core | `sh tests/crt/run_all_crt.sh` (safecrt stage) | 6 passed, 0 failed (recorded) |
| Safe CRT examples | `sh tests/crt/run_all_crt.sh` (examples stage) | 26 passed, 0 failed (recorded) |
| Adversarial mitigations | `sh tests/crt/run_all_crt.sh` (adversarial stage) | 71 passed, 0 failed (recorded) |
| Fuzz smoke | `sh tests/crt/fuzz/run_fuzz_smoke.sh` | 11 passed, 0 failed (recorded) |
| Safe CRT line coverage | `sh tests/crt/coverage_safecrt.sh` | 85 passed, 0 failed (**92.38%** total lines; every file >= **87.44%**) (recorded) |
| Combined fuzz | `tests/crt/fuzz/fuzz_all_safe_apis.c`, `fuzz_all_exported.c` | 2000+1500 iterations, ok, no crash (recorded) |
| Libc tests | `sh tests/libc/run_libc_tests.sh` | **83 passed, 0 failed** (83 `tests/libc/test_*.c` after the runner excludes `*_attack.c` and `test_corpus_format.c`; corrected from the stale 62) |
| Libc line coverage | `sh tests/libc/coverage_libc.sh` | **91.74%** total lines per `COVERAGE_100_AUDIT.md` (the earlier 91.80% figure in this file was stale); function coverage 97.90% last recorded, not re-measured here |
| Libc fuzz | `sh tests/libc/run_fuzz.sh` | 2 passed, 0 failed (recorded) |
| Libc install & contract smoke | `sh tests/libc/install_smoke.sh` | ok (static + shared lib + CMake config + `validate_contracts.py` + SDK link) (recorded) |
| C17 gap-closing matrix | `docs/libc/API_TEST_MATRIX.md`, `libc/contracts/c17_contracts.json` | 202 APIs mapped to source + test + host backend + contract + limitation; JSON validated |
| Logging / telemetry evidence | `tests/libc/test_logging_telemetry.c` | default-off, enabled/disabled, failure paths, no sensitive data, concurrent calls, no logging allocations |
| CTL containers | `sh tests/ctl/run_ctl_tests.sh` | 11 passed, 0 failed (recorded) |
| Exceptions (exceptions4c) | `sh tests/exceptions/run_except_tests.sh` | 2 passed, 0 failed (recorded) |
| Runtime recover & cleanup | `sh tests/runtime/run_runtime_tests.sh` | 4 passed, 0 failed (recorded) |
| Modern stdlib (.hp) | `sh tests/stdlib/run_stdlib_tests.sh` | 38 passed, 0 failed, 0 skipped (**100.0%** API coverage across all 21 `.hp` headers) (recorded) |
| Stdlib fuzz | `sh tests/stdlib/fuzz/run_fuzz.sh` | 3 passed, 0 failed (recorded) |
| Safety matrix | `sh tests/safety/run_safety_matrix.sh` | 3 passed, 0 failed (recorded) |

### Larger comparison and suite runners (results recorded, not re-run here)

| Area | Command | Counts / recorded result |
|---|---|---|
| Rust-vs-C+ paired runtime | `sh tests/rust_vs_cplus/run_compare.sh` | **600 cases** (6 families x 100): 473 matched, 119 documented differences, 8 both-skip, 0 failures. Outcome equality is observed behaviour, not equivalent safety guarantees. |
| Per-family checked coverage | `sh tests/family_gaps/run_family_tests.sh` | **20 variants** = 10 `tests/family_gaps/*.c` sources x 2 modes (plain, UBSan). `math_gaps` recorded 1346 checks / 23 explicit SKIPs / 0 failures in both modes; see `MATH_GAPS_EVIDENCE.md`. |
| Hundred-scenario per-method | `sh tests/methods/run_hundred.sh` | **30 variants** = 5 libc-only sources x 2 modes + 5 dual-layer sources x 4 mode/layer combinations; recorded 30/30 pass (`COVERAGE_100_AUDIT.md`). |
| Compile-time safety | `sh tests/stdlib/compile_time/run_compile_time.sh` | **7 manifests** (`tests/stdlib/compile_time/*.json`), **46 cases** (9 REJECT, 17 ACCEPT, 20 RUNTIME). RUNTIME cases are documented compiler capability gaps that still need runtime-suite coverage; results are reported separately from runtime safety. |
| Deep stdlib regression | `sh tests/stdlib/run_deep_tests.sh` | **15** `tests/stdlib/deep_*.cp` sources x 3 modes (default, strict-none, ubsan) = **45 variants**; runner reports pass/fail/skip per mode. |
| End-to-end integration | `sh tests/integration/run_integration.sh` | **8 checks** (both library builds, driver lower+link, exit 0, `integration: ok` stdout, logging/refusal diagnostics, compiler rejection of `contract_probe.c`, expected diagnostic substring). Local macOS only; see `tests/integration/README.md`. |
| C+ compiler tests | `sh tests/run_tests.sh` | **150 tests** (32 positive `.c` + 13 positive `.cp` + 97 compile-fail `.c` + 1 compile-fail `.cp` + 6 runtime-fail `.c` + 1 legacy): 149 pass, 1 pre-existing warning-only case (`COVERAGE_100_AUDIT.md`). |
| Complete evidence aggregate | `sh tests/run_all_evidence.sh` | Single entry point that runs all runners above and prints one summary line per suite. |

### Telemetry (per-API counters)

`stdlib/safe/src/safe_stats.c` exposes `cplus_safe_stat_record/reset/get/dump`;
every hardened adapter (memory/string/alloc/io/format/error) records calls/ok/fail.
This is diagnostic telemetry, not a safety guarantee. Verified by
`tests/crt/unit/test_safe_stats.c`.

## Shipped libc slice (`libc/`)

| Family | Symbols | Status |
|---|---|---|
| errno | `__cplus_errno_location` | IMPLEMENTED |
| alloc | `cplus_malloc/calloc/realloc/free` | IMPLEMENTED (platform allocator backend + checks/logs) |
| memory | `cplus_memcpy/memmove/memset/memcmp/memchr/memrchr` | IMPLEMENTED |
| string | `cplus_strlen/strnlen/strcpy/strncpy/strcat/strncat/strcmp/strncmp/strchr/strrchr/strstr/strcasecmp/strncasecmp/strdup` | IMPLEMENTED |
| ctype | full basic ctype + toupper/tolower | IMPLEMENTED (ASCII/basic) |
| stdlib | `abs/labs/atoi/atol/strtol/strtoul/qsort/bsearch` | IMPLEMENTED/PLATFORM-BACKED |
| stdio | `fopen/fclose/fread/fwrite/fseek/ftell/fflush/remove/rename/fgets/fputs/getc/putc/feof/ferror/clearerr/snprintf/vsnprintf/printf/fprintf/scanf/fscanf` | PLATFORM-BACKED wrappers, logged |

## Shipped checked CRT (`stdlib/safe/`)

rurban/safeclib-backed (MIT, pinned `90b076e94c4b0fb202dddf948d5d620e5395c1b0`, linked via `scripts/build_safecrt.sh`): `cplus_memcpy_s/memmove_s/memset_s/memcmp_s/memzero_s`, `cplus_strcpy_s/strncpy_s/strcat_s/strncat_s/strcmp_s/strnlen_s/strtok_s/strerror_s`. `intel-safestringlib` remains vendored for provenance/reference only (not linked). Platform-backed adapters: standard-C `cplus_fopen_s/fclose_s` (with double-close stream cache) and bounded `cplus_fread_s/fwrite_s`, `cplus_snprintf_s/vsnprintf_s/sprintf_s/vsprintf_s/printf_s/fprintf_s/scanf_s/fscanf_s`. Standard-C-signature tracked extensions: `malloc_s/calloc_s/realloc_s/free_s/free_checked_s/aligned_alloc_s` plus `cplus_safe_alloc_get_meta`, `cplus_safe_len`, `cplus_safe_query_length`, `cplus_safe_transfer_ownership`.

Note: See `tests/crt/`, `tests/libc/`, `tests/stdlib/`, and `tests/safety/SCENARIO_MATRIX.md` for current test commands and counts.

## Not shipped / blockers (state of incompleteness)

See [ISO_C_AUDIT.md](ISO_C_AUDIT.md) for the complete ISO C99/C11 header-by-header Implemented / Partial / Missing API table and prioritized roadmap. Authoritative machine-readable source: `docs/libc/iso_c_inventory.json` (948 APIs, 27 headers): HOST/COMPILER-PROVIDED 359, PLATFORM_BACKED 476, IMPLEMENTED_BUNDLED 29, IMPLEMENTED_UPSTREAM 8, UNSUPPORTED 49, MISSING 27.

- **27 APIs are MISSING** (no implementation, no host symbol, no locally available pinned upstream source): 23 C23 math APIs (`nextup/nextdown`, `fadd/fsub/fmul/fdiv` families, 3-arg `hypot`, `_Float16` variants), `timespec_getres`, `mbrtoc8`, `c8rtomb`, `FE_TIESAWAY`. Note: `COVERAGE_100_AUDIT.md` prose says 28 MISSING / 7 UPSTREAM; the JSON's 27 / 8 is authoritative, as `CRITICAL_ISSUES_RESPONSE.md` documents.
- **49 APIs are UNSUPPORTED by design** (34 `<stdatomic.h>`, 13 `<stdio.h>`, 2 `<stdlib.h>`): type-generic C ABI, insecure APIs, platform-specific.
- **387 vendored safeclib public APIs are NOT_INTEGRATED** out of 437 distinct names (`SAFECLIB_API_INVENTORY.md`: 40 INTEGRATED, 10 ADAPTER_ONLY, 387 NOT_INTEGRATED, 0 UNAVAILABLE). Only a 19-file subset of safeclib is compiled (18 upstream sources + 1 local adapter); 188 upstream `.c` files are not compiled.
- **ABI evidence is limited**: `API_STATUS_MATRIX.md` records `abi_test` = NO EVIDENCE for **644 of 646** declared APIs; only `tests/cases/c_abi.c` and `tests/compat/` exist. Windows x86 ABI and Linux CI are unverified locally.
- **127 wrapper-safety findings remain open** (`WRAPPER_SAFETY_AUDIT.md`, 2026-11-23): 16 High, 68 Medium, 43 Low/Other across five audit areas. These are documented audit findings, not silently fixed bugs.
- Annex K `bsearch_s`/`qsort_s`, `asctime_s`/`ctime_s`, `strerrorlen_s`, `tmpnam`/`gets_s`, standalone `<stdatomic.h>`/`<tgmath.h>` ABI symbols, and a complete userspace `FILE` buffering engine (intentionally delegated to the host CRT) are also not shipped.
- **Remote CI is UNVERIFIED**: the Linux `linux-crt` / `asan-libc-crt` jobs and any GitHub Actions result must be observed after push; a clean-export local rebuild is not a remote green result.
- **SonarCloud is UNVERIFIED**: no credentials or token locally; the external 23.4% duplication / security D / reliability E ratings were not re-scanned, and no gate pass is claimed (`SONAR_TRIAGE.md`).
- Cross-platform ABI/SDK matrix and Agent 1 contract enforcement interop remain open.
- Raw C UB/provenance/unknown-capacity cannot be made safe by libc alone.

## musl-derived functions (copied upstream, pinned 7813e91, MIT)

`cplus_memccpy, cplus_stpcpy, cplus_stpncpy, cplus_strtok_r, cplus_strsep,`
`cplus_memmem, cplus_strverscmp, cplus_strcasestr, cplus_strchrnul,`
`cplus_div, cplus_ldiv, cplus_lldiv, cplus_llabs, cplus_atoll`
— implemented in `libc/src/string/musl_extra.c` and `libc/src/stdlib/musl_stdlib_extra.c`,
provenance in `libc/upstream/musl/PROVENANCE.md`, tests in
`tests/libc/test_musl_extra.c` and `tests/libc/test_musl_stdlib_extra.c`.

## C runtime bug-class coverage

[`C_BUG_CLASS_MATRIX.md`](C_BUG_CLASS_MATRIX.md) catalogues 35 known C runtime
bug classes with an honest MITIGATED / PARTIAL / NOT FIXABLE BY LIBC status and
the residual limitation for each. Evidence is produced by the two dedicated
adversarial runners (thousands of deterministic generated scenarios per class):

    sh tests/libc/run_attack_tests.sh     # targeted break attempts + contract integrity
    sh tests/libc/run_corpus_tests.sh     # 600-1000+ generated cases per bug class

Every test uses defined inputs; raw-C UB is documented rather than turned into
a crashing test. No claim of complete memory safety is made.

## Sanitizer evidence (this host)

- **UBSan**: recorded pass for the gap-closing libc suites with
  `-fsanitize=undefined -fno-sanitize-recover=all` on Apple Clang 16 / arm64.
- **ASan**: VERIFIED with Homebrew clang 23 plus explicit
  `-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk` (see
  `docs/libc/ASAN_AUDIT.md`). All 12 API-level allocator scenarios classify
  correctly: OOB read/write and UAF read/write report
  `heap-buffer-overflow` / `heap-use-after-free`; valid reuse, realloc-failure,
  alignment and metadata cases are clean. Apple clang 16 on this host still
  aborts at startup with `sanitizer_malloc_mac.inc:189`, so that compiler is
  not usable for ASan here. The allocators delegate each block to its own host
  malloc allocation, so no manual ASan poisoning is used. Linux CI remains the
  authoritative cross-platform ASan path and is unverified locally.
- **TSan**: NOT VERIFIED on this host. Apple Clang 16 requires
  `DYLD_INSERT_LIBRARIES=.../libclang_rt.tsan_osx_dynamic.dylib` and reports
  "Interceptors are not working" without it. Linux CI runs TSan separately.
- **libFuzzer**: not linkable on this host (`libclang_rt.fuzzer_osx.a` absent);
  the project's deterministic fuzz harnesses do pass (see table above).

Sanitizer/fuzz passing is evidence of tested behaviour only, never proof of
memory safety or of raw-pointer validity.

## Gap-closing slice provenance

The 202 new APIs are **PLATFORM_BACKED** — namespaced `cplus_*` wrappers that
call a named host C runtime function. They are deliberately not counted as
bundled or upstream implementations. Their source/test/contract mapping is
machine-readable in `libc/contracts/c17_contracts.json` and tabulated in
[`API_TEST_MATRIX.md`](API_TEST_MATRIX.md). The host backends were probed on
Apple Clang 16 / macOS arm64; platforms lacking a given symbol must gate it out
rather than receive a from-scratch replacement.

Raw C pointer validity, NUL-termination, destination capacity and format-string
correctness remain caller responsibilities. A `_s` suffix or a namespaced
wrapper never makes an arbitrary raw pointer safe.

## Ordinary C APIs

[`ORDINARY_C_SAFETY.md`](ORDINARY_C_SAFETY.md) documents the opt-in layer that
makes unchanged `malloc`/`memset`/`memcpy`/`strcpy`/`fread`/`printf` calls use
tracked capacity and compiler format attributes. Regression:
`tests/crt/unit/test_safe_c_api.c` (included in `run_unit_tests.sh`).

## Per-method scenarios

[`SCENARIO_RESULTS.md`](SCENARIO_RESULTS.md) records 384 distinct runtime
scenarios and ~81,000 generated cases across both libraries, with per-method
case counts, differential/fuzz evidence and the defects the scenarios found.

# C+ hardened libc coverage (honest, #22/#23)

Tracks what is actually shipped/verified on `agent/modern-stdlib` (PR #17). **No full ISO C replacement claim.**

## Verified (this host)

| Area | Command | Result |
|---|---|---|
| CRT corpus | `sh tests/crt/run_all_crt.sh` (corpus stage) | 10 passed, 0 failed |
| Safe CRT unit | `sh tests/crt/unit/run_unit_tests.sh` | 76 passed, 0 failed |
| Safe CRT core | `sh tests/crt/run_all_crt.sh` (safecrt stage) | 6 passed, 0 failed |
| Safe CRT examples | `sh tests/crt/run_all_crt.sh` (examples stage) | 26 passed, 0 failed |
| Adversarial mitigations | `sh tests/crt/run_all_crt.sh` (adversarial stage) | 71 passed, 0 failed |
| Fuzz smoke | `sh tests/crt/fuzz/run_fuzz_smoke.sh` | 11 passed, 0 failed |
| Safe CRT line coverage | `sh tests/crt/coverage_safecrt.sh` | 85 passed, 0 failed (**92.38%** total lines; every file >= **87.44%**) |
| Combined fuzz | `tests/crt/fuzz/fuzz_all_safe_apis.c`, `fuzz_all_exported.c` | 2000+1500 iterations, ok, no crash |
| Libc tests | `sh tests/libc/run_libc_tests.sh` | 62 passed, 0 failed (50 baseline + 12 new gap-closing suites) |
| Libc line coverage | `sh tests/libc/coverage_libc.sh` | 62 passed, 0 failed (**91.80%** total lines; **97.90%** functions) |
| Libc fuzz | `sh tests/libc/run_fuzz.sh` | 2 passed, 0 failed |
| Libc install & contract smoke | `sh tests/libc/install_smoke.sh` | ok (static + shared lib + CMake config + `validate_contracts.py` + SDK link) |
| C17 gap-closing matrix | `docs/libc/API_TEST_MATRIX.md`, `libc/contracts/c17_contracts.json` | 202 APIs mapped to source + test + host backend + contract + limitation; JSON validated |
| Logging / telemetry evidence | `tests/libc/test_logging_telemetry.c` | default-off, enabled/disabled, failure paths, no sensitive data, concurrent calls, no logging allocations |
| CTL containers | `sh tests/ctl/run_ctl_tests.sh` | 11 passed, 0 failed |
| Exceptions (exceptions4c) | `sh tests/exceptions/run_except_tests.sh` | 2 passed, 0 failed |
| Runtime recover & cleanup | `sh tests/runtime/run_runtime_tests.sh` | 4 passed, 0 failed |
| Modern stdlib (.hp) | `sh tests/stdlib/run_stdlib_tests.sh` | 38 passed, 0 failed, 0 skipped (**100.0%** API coverage across all 21 `.hp` headers) |
| Stdlib fuzz | `sh tests/stdlib/fuzz/run_fuzz.sh` | 3 passed, 0 failed |
| Safety matrix | `sh tests/safety/run_safety_matrix.sh` | 3 passed, 0 failed |

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

## Not shipped / blockers

See [ISO_C_AUDIT.md](ISO_C_AUDIT.md) for the complete ISO C99/C11 header-by-header Implemented / Partial / Missing API table and prioritized roadmap.

- Full ISO C: this branch closes the enumerated `UNSUPPORTED` gaps in `<fenv.h>`, `<time.h>`, `<wchar.h>`, `<wctype.h>`, `<complex.h>`, math `*f`/`*l`, `<stdlib.h>` exits and `<inttypes.h>` wide conversions (all PLATFORM_BACKED). Remaining: Annex K `bsearch_s`/`qsort_s`, `asctime_s`/`ctime_s`, `strerrorlen_s`, `tmpnam`/`gets_s`, standalone `<stdatomic.h>`/`<tgmath.h>` ABI symbols, and a complete userspace `FILE` buffering engine (intentionally delegated to the host CRT).
- Real libFuzzer/ASan/UBSan CI on Linux runners (workflow added; remote result unverified here).
- Cross-platform ABI/SDK matrix and Agent 1 contract enforcement interop.
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

- **UBSan**: all 12 new gap-closing test suites pass with
  `-fsanitize=undefined -fno-sanitize-recover=all` on Apple Clang 16 / arm64.
- **ASan**: NOT VERIFIED on this host. Apple Clang 16 aborts at process start
  with `AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189`
  (`asan_init_is_running`) for every instrumented binary. This is a toolchain
  defect, not a test failure; Linux CI in `.github/workflows/libc-crt.yml`
  runs ASan+UBSan and must be consulted for that platform.
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

# Per-method scenario + fuzz evidence (both libraries)

Runner: `sh tests/scenarios/run_scenarios.sh` — **52/52 suites pass** (plain + UBSan where applicable). Every case uses a fixed seed, defined inputs, canaries, and reports a `FINDING:` with the exact tuple on any crash or corruption. Raw-C UB is documented, never executed by the test itself.

Two libraries are exercised:
- **plain libc** — `libc/install-include/cplus_libc_safe.h` + `libcplus_hardened.a`
- **C+ SafeCRT** — `stdlib/safe/safe_c.h` + `libcplus_safecrt.a` + plain libc

## High-volume generated scenarios (1000+ cases each)

| File | Family | Cases | FINDINGs |
|---|---|---:|---:|
| scenario_01 | integer truncation → undersized allocation | 1,332 | 0 |
| scenario_02 | stale alias after realloc | 1,024 | 0 |
| scenario_03 | double free through aliases | 1,200 | 0 |
| scenario_04 | flexible-array size mismatch | 1,200 | 0 |
| scenario_05 | overlapping memcpy / memmove | 69,632 | 0 |
| scenario_06 | stack pointer escapes (compile-only) | 1,200 | 547 not diagnosed¹ |
| scenario_07 | input-controlled strcpy overflow | 1,440 | 0 |
| scenario_08 | missing NUL / strlen overread | 1,024 | 0 |
| scenario_09 | uninitialized fields | 1,280 + 500 compile | 0² |
| scenario_10 | allocation size overflow | 1,200 | 0 |
| scenario_11 | use-after-free / dangling alias | 1,200 | 0 |
| scenario_12 | format misuse / `%n` | 1,095 | 0 |

¹ Genuine compiler gap handed to Agent 1 — see `KNOWN_COMPILER_GAPS.md`.
² All 500 compile probes emit a diagnostic, but none diagnoses the heap field read itself (compiler gap).

## Distinct runtime scenarios per method (10–30 each)

| File | Method family | Scenarios |
|---|---|---:|
| runtime_01_alloc | malloc/calloc/realloc/free | 30 |
| runtime_02_memset | memset | 24 |
| runtime_03_memcpy | memcpy | 25 |
| runtime_04_memmove_memcmp | memmove / memcmp | 26 |
| runtime_05_strcpy | strcpy / strncpy | 26 |
| runtime_06_strcat_strlen | strcat / strncat / strlen / strnlen | 30 |
| runtime_07_stdio | stdio / fread / fwrite / streams | 29 |
| runtime_08_format | printf / scanf / checked formats | 30 |
| runtime_09_wchar | wide characters | 29 |
| runtime_10_math | math (float/long double/complex) | 28 |
| runtime_11_fenv_complex | fenv / complex | 29 |
| runtime_12_logging | logging / telemetry | 24 |
| runtime_13_threads | concurrency / TLS | 24 |
| runtime_14_conversions | conversions / ctype / wctype | 30 |

**Total: 384 distinct runtime scenarios + ~81,000 generated cases**, all passing in both plain and UBSan builds.

## Real defects found by these scenarios and fixed

| Defect | Fix | Evidence |
|---|---|---|
| `toupper(EOF)`/`tolower(EOF)` returned 255, not EOF | ISO guard before the unsigned-char cast | `runtime_14_conversions` |
| Adjacent live block reported capacity 0 at a shared boundary | exact base match precedes one-past-end scan (both allocators) | `runtime_01_alloc`, `runtime_13_threads` |
| `realloc_s()` ABA stripped another thread's metadata | generation guard in second critical section | `runtime_13_threads` |
| String wrappers ignored strict unknown-capacity mode and used base-only capacity | remaining-capacity + strict mode + min(BOS, tracked) | `runtime_05_strcpy` |
| Sub-object write allowed using whole-block capacity | capacity = min(tracked remaining, `__builtin_object_size`) in both layers | `runtime_05_strcpy` |

## Fuzzing / sanitizers

- Deterministic fuzz corpora: `run_corpus_tests.sh` (20/20), `run_attack_tests.sh` (23/23), including 13,256-input differential fuzz.
- UBSan: every scenario suite built and run with `-fsanitize=undefined -fno-sanitize-recover=all` — clean.
- ASan/TSan: unavailable on this macOS arm64 host (toolchain startup failures); Linux CI remains the verification path. Recorded in `COVERAGE.md`.

Passing these tests is evidence of tested behaviour on this host, not a proof of memory safety or of raw-pointer provenance.

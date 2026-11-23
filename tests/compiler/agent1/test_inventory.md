# Test infrastructure inventory

## Entry points
- `CMakeLists.txt:285-291` ctest -> `tests/run_tests.sh`, `demos/run_demos.sh`.
- `.github/workflows/compiler.yml`: CTest then `sh tests/core/run.sh` (Linux + macOS).
- `tools/verify.sh`: build, CTest, repo tests, fuzz/metrics, strict_mode, breaker, stress, demos.
- `tests/core/run_all_fuzz.py`: fuzz meta-runner (heavy gated by INCLUDE_HEAVY=1).

Note: `tests/core/run.sh` is CI-invoked but NOT part of CTest or tools/verify.sh.

## Discoveries by runner
- `tests/run_tests.sh`: globs `tests/cases/*.c|*.cp` (45 positives),
  `tests/compile_fail/*.c|*.cp` (58, each with `.err`),
  `tests/runtime_fail/*.c|*.cp` (5, `.exit`/`.stderr`). All discovered.
- `tests/core/run.sh`: explicit list of ~46 `tests/core/positive/*`, ~70
  `tests/safety/*`, fmt25 glob (25), two native suites, logging/quarantine.
- `tests/native/cplus_safety_tests.c`: 44 embedded negative/positive cases,
  exact diagnostic assertions, check --strict + build --strict --sanitize=none.
- `tests/native/strict40_runner.c`: authoritative 40-test suite.

## Undiscovered / orphaned test assets (gaps)
- 11 top-level `tests/safety/*.c` files are metric inputs only, never asserted:
  borrow_conflict, format_string_nonliteral, format_string_s_nonliteral,
  interproc_double_free, libc_double_free, libc_memcpy_overflow,
  libc_strcpy_overflow, realloc_failure_alias_live, uaf_alias,
  uninit_addr_non_output, uninit_compound_assign.
- 100 sources under `tests/safety/{fnptr_bounds,integration,p0new,
  runtime_crashes,sanitizer,rust_compare}` have no present assertion runner.
- 2 orphan `.stderr` files in `tests/runtime_fail/`.
- 74 of 79 `tests/metrics/*.py` scripts not wired into an aggregator.
- Docs reference 8 `tests/core/*_suite.py` files that no longer exist (removed
  when native harnesses took over).

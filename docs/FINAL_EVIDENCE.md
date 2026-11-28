# Final API-closure evidence

## Fresh regression re-run (2026-11-28) — current record

Captured in an isolated worktree at HEAD
`c0f9aa7f4411ff57547334be551148af9422690b`. Every number below comes from a
command actually executed for this pass; the 2026-11-23 record below it is
historical and superseded. Environment for every command, from the repo root:

```sh
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export CPLUS_CLANG=/usr/bin/clang
export CPLUS_BIN=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build-final/c+
unset SDKROOT
```

| Scope | Exact command | Exit | Final line / result |
| --- | --- | ---: | --- |
| #214 | `sh tests/stdlib/issue214/run.sh` | 0 | `ISSUE214 SUITE PASS` (1 suite passed) |
| #215 | `sh tests/stdlib/issue215/run.sh` | 0 | `ISSUE215 SUITE PASS` (1 suite passed) |
| #216 | `sh tests/stdlib/issue216/run.sh` | 0 | `ISSUE216 SUITE PASS` (1 suite passed) |
| #217/#218/#219 | `sh tests/stdlib/run_stdlib_tests.sh issue21` | 0 | `C+ stdlib tests: 3 passed, 0 failed, 0 skipped` |
| #220 | `sh tests/stdlib/run_stdlib_tests.sh audio_heap_stability` | 1 | `C+ stdlib tests: 0 passed, 1 failed, 0 skipped` (**FAIL**, compile) |
| #221 | `sh tests/stdlib/run_stdlib_tests.sh net_sigpipe` | 0 | `C+ stdlib tests: 1 passed, 0 failed, 0 skipped` |
| #222 (async) | `sh tests/stdlib/run_stdlib_tests.sh async_reentrant_drop` | 1 | `C+ stdlib tests: 0 passed, 1 failed, 0 skipped` (**FAIL**, compile) |
| #222 (multi-TU) | `sh tests/stdlib/run_stdlib_tests.sh multi_tu` | 0 | `C+ stdlib tests: 2 passed, 0 failed, 0 skipped` (`multi_tu`, `multi_tu_meta`) |
| #211 (libc) | `sh tests/libc/run_libc_tests.sh` | 0 | `libc tests: 112 passed, 0 failed` (note below) |
| #212/#213 (CRT) | `sh tests/crt/unit/run_unit_tests.sh` | 0 | `crt unit tests: 103 passed, 0 failed` |
| All suites | `sh tests/run_all_evidence.sh` | 1 | `evidence aggregate: 22 passed, 6 failed` |

Details that affect the verdict:

- #211: the final libc run contains `ok test_strncat_bounded_overflow.c`. The
  first invocation from a clean worktree was `libc tests: 101 passed, 11 failed`
  (exit 1); all 11 were build failures with
  `clang: error: no such file or directory: 'build/safe/libcplus_safecrt.a'`,
  not assertion failures and not the #211 test. The CRT runner builds that
  archive; re-running the identical libc command afterwards gave 112/0.
- #212/#213: the CRT runner prints only a summary, so the three binaries in
  `build/safe/unit/` were also executed directly (all exit 0):
  `issue212_scanfmt_rewrite: ok`, `issue212_scanf_c_capacity: ok`,
  `test_issue213_source_scan_bound: ok`. The last also emits the expected
  `[CPLUS-SAFE] strnlen_s: refused before safeclib: scan bound 256 > proven
  capacity 16 (ESNOSPC 406)` diagnostic before `ok`.
- #217–#219: the `issue21` run prints `ok issue217_view_tombstone_reuse`,
  `ok issue218_lifetime_bounds`, `ok issue219_pool_alignment_generations`.
  The aggregate also reports `ok issue214`, `ok issue215`, `ok issue216` and
  `ok issue217_219: C+ stdlib tests: 3 passed, 0 failed, 0 skipped`.
- #220/#222 FAIL under the standard runner: `audio_heap_stability.build.log`
  and `async_reentrant_drop.build.log` both end in
  `error: failed to compile instrumented C+ runtime`, preceded by
  `-lminiaudio: 'linker' input unused [-Werror,-Wunused-command-line-argument]`
  (plus the three `-Wl,-framework` pairs) and
  `-luv: 'linker' input unused [-Werror,-Wunused-command-line-argument]`.
  Diagnostic only (not a replacement gate): with the same driver flags used by
  the issue215/216 suites (`--default --no-werror --sanitize=none` plus the
  miniaudio / libuv dependency flags) both sources build, run with exit 0 and
  match their `.expected` files exactly (`audio-heap-accounting 1`,
  `reentrant-refs 1`). The standard runner remains FAIL.
- Aggregate: `sh tests/run_all_evidence.sh` exited 1,
  `evidence aggregate: 22 passed, 6 failed`. Failures:
  `abi-layouts` — `./build/c+: No such file or directory`;
  `integration` — `integration suite: 3 passed, 5 failed` (missing `./build/c+`);
  `rust-vs-cplus` — `0 matched, 0 documented differences, 0 both-skip, 6 failures`
  (`rustc: command not found`);
  `cplus-rust-gates` — `conformance: 5 matched, 7 mismatched (12 gates)`, case 12
  C executable segfaulted and Rust failed to compile;
  `contract-validator` — `tests/libc/validate_contracts.cp` rejected with
  `[CPLUS-LIFETIME] ... null pointer argument` diagnostics;
  `contract-alignment` — native alignment tool build blocked (missing
  `./build/c+`), so the runner reports `No such file or directory`.
  The aggregate does not run the #220/#221/#222 sources.

These are observed results for one revision and one prebuilt compiler, not
proof of memory safety, absence of UB, or Rust equivalence. Remote CI, Windows
x86 ABI, sanitizer matrices and live-game results remain **UNVERIFIED**. No
test was edited, skipped or relaxed to obtain these numbers.

## Historical run (2026-11-23) — superseded by the section above

- Run started (UTC): `2026-11-23T01:44:16Z`
- Run finished (UTC): `2026-11-23T01:53:27Z`
- Repository branch: `agent/modern-stdlib`
- Overall result: **FAIL**
- Summary JSON: `build/final-evidence/summary.json`
- Per-suite logs: `build/final-evidence/logs/<suite>.log`

This report is generated from an ACTUAL run by `tools/run_final_evidence.sh`. The commands and final output lines below are captured verbatim from each suite's stdout/stderr; none of the runners were modified.

## Aggregate

PASS=15 FAIL=1 TIMEOUT=0 BLOCKED=0 (total 16 suites).

The aggregator exits 1 if any suite is FAIL or TIMEOUT; BLOCKED is reported but does not affect the exit status.

## Suite results

| Suite | Command | Status | Exit | Time (s) | Final line |
|---|---|---|---:|---:|---|
| compiler-tests | `sh tests/run_tests.sh` | PASS | 0 | 74 | `C+ tests: 150 passed, 0 failed` |
| libc-tests | `sh tests/libc/run_libc_tests.sh` | FAIL | 1 | 33 | `libc tests: 87 passed, 3 failed` |
| crt-unit | `sh tests/crt/unit/run_unit_tests.sh` | PASS | 0 | 29 | `crt unit tests: 83 passed, 0 failed` |
| stdlib-agent2 | `sh tests/stdlib/agent2/run_all.sh --clean` | PASS | 0 | 90 | `SUMMARY PASS=56 FAIL=0 BUILD_FAILURE=0 TIMEOUT=0 SKIPPED=0 BLOCKED=0 TOTAL=56` |
| stdlib-deep | `sh tests/stdlib/run_deep_tests.sh` | PASS | 0 | 148 | `deep stdlib tests: 87 passed, 0 failed, 0 skipped (29 file(s) x 3 modes)` |
| compile-time | `sh tests/stdlib/compile_time/run_compile_time.sh` | PASS | 0 | 36 | `note: RUNTIME cases are compiler capability gaps and must be covered by runtime suites separately.` |
| abi-compat | `sh tests/abi/run_abi_compat.sh` | PASS | 0 | 11 | `abi_compat: 69 passed, 0 failed` |
| abi-layouts | `sh tests/abi/run_abi_layouts.sh` | PASS | 0 | 12 | `abi_layouts: 70/70 layouts match` |
| crt-fuzz-smoke | `sh tests/crt/fuzz/run_fuzz_smoke.sh` | PASS | 0 | 9 | `fuzz smoke: 11 passed, 0 failed` |
| libc-fuzz | `sh tests/libc/run_fuzz.sh` | PASS | 0 | 8 | `libc fuzz: 2 passed, 0 failed` |
| stdlib-fuzz | `sh tests/stdlib/fuzz/run_fuzz.sh` | PASS | 0 | 4 | `ok   fuzz_containers (500 inputs, mutate)` |
| fuzz100 | `sh tests/fuzz100/run.sh` | PASS | 0 | 85 | `fuzz100: logs: build/fuzz100/cases` |
| contract-validator | `python3 tests/libc/validate_contracts.py` | PASS | 0 | 1 | `contract validation: ok (5 files validated, 203 C17 APIs)` |
| contract-alignment | `python3 tests/libc/test_compiler_contract_alignment.py` | PASS | 0 | 0 | `compiler contract alignment: ok` |
| contract-consumption | `python3 tests/libc/test_contract_consumption.py` | PASS | 0 | 1 | `}` |
| contract-integrity | `python3 tests/libc/test_contract_integrity.py` | PASS | 0 | 6 | `contract_integrity: ok (52 attacks detected, all cross-checks clean)` |

## Exact commands and final lines

### compiler-tests

- Status: **PASS**
- Exit code: `0`
- Duration: `74s`
- Log: `build/final-evidence/logs/compiler-tests.log`

Command:

```sh
sh tests/run_tests.sh
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
  ok   classic_bug21_alignment
  ok   classic_bug25_divzero
  ok   classic_bug9_overflow
  ok   legacy_plain
C+ tests: 150 passed, 0 failed
```

### libc-tests

- Status: **FAIL**
- Exit code: `1`
- Duration: `33s`
- Reason: suite exited nonzero (rc=1)
- Log: `build/final-evidence/logs/libc-tests.log`

Command:

```sh
sh tests/libc/run_libc_tests.sh
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
ok test_wchar_gaps.c
ok test_wchar_multibyte.c
ok test_wchar_numeric.c
ok test_wctype_extra.c
libc tests: 87 passed, 3 failed
```

### crt-unit

- Status: **PASS**
- Exit code: `0`
- Duration: `29s`
- Log: `build/final-evidence/logs/crt-unit.log`

Command:

```sh
sh tests/crt/unit/run_unit_tests.sh
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
crt unit tests: 83 passed, 0 failed
```

### stdlib-agent2

- Status: **PASS**
- Exit code: `0`
- Duration: `90s`
- Log: `build/final-evidence/logs/stdlib-agent2.log`

Command:

```sh
sh tests/stdlib/agent2/run_all.sh --clean
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
log_test                                 existing  existing    PASS               0 0
negative                                 existing  existing    PASS               0 0
net_test                                 existing  existing    PASS               0 0
stdlib_hardening_test                    existing  existing    PASS               0 0
SUMMARY PASS=56 FAIL=0 BUILD_FAILURE=0 TIMEOUT=0 SKIPPED=0 BLOCKED=0 TOTAL=56
```

### stdlib-deep

- Status: **PASS**
- Exit code: `0`
- Duration: `148s`
- Log: `build/final-evidence/logs/stdlib-deep.log`

Command:

```sh
sh tests/stdlib/run_deep_tests.sh
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
MODE           PASSED   FAILED  SKIPPED  BUILD-FAIL  RUN-FAIL  OUT-FAIL
default            29        0        0           0         0         0
strict-none        29        0        0           0         0         0
ubsan              29        0        0           0         0         0
deep stdlib tests: 87 passed, 0 failed, 0 skipped (29 file(s) x 3 modes)
```

### compile-time

- Status: **PASS**
- Exit code: `0`
- Duration: `36s`
- Log: `build/final-evidence/logs/compile-time.log`

Command:

```sh
sh tests/stdlib/compile_time/run_compile_time.sh
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
  vector_mutable_alias.cp                expect=RUNTIME  observed=ACCEPT
  vector_invalid_slice.cp                expect=RUNTIME  observed=ACCEPT
  total gap cases: 27 (see docs/libc/COMPILE_TIME_GAPS.md)
note: backend warnings without [CPLUS- are not counted as C+ ownership/lifetime enforcement.
note: RUNTIME cases are compiler capability gaps and must be covered by runtime suites separately.
```

### abi-compat

- Status: **PASS**
- Exit code: `0`
- Duration: `11s`
- Log: `build/final-evidence/logs/abi-compat.log`

Command:

```sh
sh tests/abi/run_abi_compat.sh
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
PASS T6_neutral_plain_run
PASS T6_neutral_cplus_run
PASS T6_neutral_diff
PASS T6_neutral_line_count
abi_compat: 69 passed, 0 failed
```

### abi-layouts

- Status: **PASS**
- Exit code: `0`
- Duration: `12s`
- Log: `build/final-evidence/logs/abi-layouts.log`

Command:

```sh
sh tests/abi/run_abi_layouts.sh
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
clang: warning: build/libc/libcplus_hardened.a: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: build/safe/libcplus_safecrt.a: 'linker' input unused [-Wunused-command-line-argument]
abi_layouts: 70/70 layouts match
```

### crt-fuzz-smoke

- Status: **PASS**
- Exit code: `0`
- Duration: `9s`
- Log: `build/final-evidence/logs/crt-fuzz-smoke.log`

Command:

```sh
sh tests/crt/fuzz/run_fuzz_smoke.sh
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
fuzz smoke: 11 passed, 0 failed
```

### libc-fuzz

- Status: **PASS**
- Exit code: `0`
- Duration: `8s`
- Log: `build/final-evidence/logs/libc-fuzz.log`

Command:

```sh
sh tests/libc/run_fuzz.sh
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
ok fuzz_libc.c
ok fuzz_libc2.c
libc fuzz: 2 passed, 0 failed
```

### stdlib-fuzz

- Status: **PASS**
- Exit code: `0`
- Duration: `4s`
- Log: `build/final-evidence/logs/stdlib-fuzz.log`

Command:

```sh
sh tests/stdlib/fuzz/run_fuzz.sh
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
fuzz mode: mutate
ok   fuzz_json (500 inputs, mutate)
ok   fuzz_string (500 inputs, mutate)
ok   fuzz_containers (500 inputs, mutate)
```

### fuzz100

- Status: **PASS**
- Exit code: `0`
- Duration: `85s`
- Log: `build/final-evidence/logs/fuzz100.log`

Command:

```sh
sh tests/fuzz100/run.sh
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
F073     f100_vec_f073_faults.cp            cplus  no    PASS          run         0/0
------------------------------------------------------------------------------------------
fuzz100: pass=100, fail=0, build_failure=0, timeout=0, skipped=10, blocked=0
fuzz100: JSON summary: build/fuzz100/summary.json
fuzz100: logs: build/fuzz100/cases
```

### contract-validator

- Status: **PASS**
- Exit code: `0`
- Duration: `1s`
- Log: `build/final-evidence/logs/contract-validator.log`

Command:

```sh
python3 tests/libc/validate_contracts.py
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
contract validation: ok (5 files validated, 203 C17 APIs)
```

### contract-alignment

- Status: **PASS**
- Exit code: `0`
- Duration: `0s`
- Log: `build/final-evidence/logs/contract-alignment.log`

Command:

```sh
python3 tests/libc/test_compiler_contract_alignment.py
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
compiler contract alignment: ok
```

### contract-consumption

- Status: **PASS**
- Exit code: `0`
- Duration: `1s`
- Log: `build/final-evidence/logs/contract-consumption.log`

Command:

```sh
python3 tests/libc/test_contract_consumption.py
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
    }
  ],
  "purpose": "evidence of compiler consumption of libc diagnostic contracts",
  "schema_version": 1
}
```

### contract-integrity

- Status: **PASS**
- Exit code: `0`
- Duration: `6s`
- Log: `build/final-evidence/logs/contract-integrity.log`

Command:

```sh
python3 tests/libc/test_contract_integrity.py
```

Final output lines (verbatim, last up to 5 non-empty lines):

```text
contract_integrity: ok (52 attacks detected, all cross-checks clean)
```


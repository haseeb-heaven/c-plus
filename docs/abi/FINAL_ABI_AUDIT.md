# Final ABI-preservation verification — `agent/modern-stdlib`

Date: 2026-11-23 (Mon). Branch under test: `agent/modern-stdlib`.

This document records a **final, serial re-run** of the repository's ABI-preservation
suite on the current branch state. Evidence below is transcribed verbatim from the
captured stdout/stderr of each command; no passing result is inferred, copied from a
sibling report, or fabricated. Full logs were captured to an external temp directory
(`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/abi_audit_logs/`)
and are not part of the repository.

## Environment (exact)

| Item | Value |
| --- | --- |
| Working directory | `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` |
| Compiler | `/usr/bin/clang` — `Apple clang version 16.0.0 (clang-1600.0.26.6)`, `Target: arm64-apple-darwin27.2.0`, installed via Command Line Tools |
| Architecture | `arm64` (`uname -m`) |
| `CPLUS_CLANG` | exported as `/usr/bin/clang` for every command |
| `SDKROOT` | unset (`unset SDKROOT`) for every command |
| Shells | system `/bin/bash` = GNU bash 3.2.57(1); `/opt/homebrew/bin/bash` = GNU bash 5.3.15(1) |
| Per-command wrapper timeout | 300 s (shell tool `timeout=300000` ms) |

Commands were run **serially**, one at a time, with the same environment. Exit codes
are the shell-reported exit status of each command.

## Results at a glance

| # | Command | Exit code | Final line (verbatim) |
| --- | --- | --- | --- |
| 1 | `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt` | 0 | `abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt` |
| 2 | `sh tests/abi/run_abi_compat.sh` | 0 | `abi_compat: 69 passed, 0 failed` |
| 3 | `sh tests/abi/run_abi_layouts.sh` | 0 | `abi_layouts: 70/70 layouts match` |
| 4a | `/bin/bash tests/abi/run_legacy_labs.sh` (bash 3.2 — **invalid, see below**) | 0 (wrapper), but script errored | `tests/abi/run_legacy_labs.sh: line 352: legacy_c: unbound variable` |
| 4b | `/opt/homebrew/bin/bash tests/abi/run_legacy_labs.sh` (bash 5.3 — used instead) | 0 | `required_failures=0  summary=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/abi-labs/summary.json  logs=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/abi-labs/logs` |
| 5 | `sh tests/abi/stdc_abi/run.sh` | 0 | `stdc_abi: RESULT PASS -- ordinary C ABI unchanged by the safety layer` |
| 6 | `sh tests/abi/stdc_precompiled/run.sh` | 0 | `stdc_precompiled: ALL CHECKS PASSED` |

All six required suites passed under the prescribed toolchain. Command 4 required the
`bash 5` fallback explicitly allowed by the task: system bash 3.2 does not support the
associative arrays the script uses.

## Exact evidence per command

### 1. Fingerprint comparison — `tools/abi_compare/run.sh compare` (exit 0)

Full log (6 lines, verbatim):

```
abi_fp: generating C sources in tools/abi_compare/.out/compare
abi_fp: compile probe   : /usr/bin/clang -O2 -std=c11 -Wall -Wextra -Werror <includes> tools/abi_compare/.out/compare/abi_fp_probe.c
abi_fp: compile json    : /usr/bin/clang -O2 -std=c11 -Wall -Wextra tools/abi_compare/.out/compare/fp_to_json.c
abi_fp: wrote tools/abi_compare/.out/compare/fingerprint.txt (1235 records) and fingerprint.json
abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).
abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt
```

The regenerated fingerprint has **1235 records** and reports **MATCH** against the
committed `tests/abi/fingerprints/baseline.txt`, with an **empty diff**. The baseline
file is 1238 lines, of which 3 are `#` comment/header lines, so the record counts
align. This is the single strongest whole-surface check: sizes, alignments, field
offsets, compiler-ABI properties, exported symbols, and function signatures are all
byte-for-byte identical to the baseline.

### 2. C/C++ ABI compatibility — `tests/abi/run_abi_compat.sh` (exit 0)

Final 30 lines include these representative passes (verbatim):

```
PASS T5_libc_symbols:cplus_printf
PASS T5_libc_symbols:cplus_fopen
PASS T5_libc_symbols:cplus_sqrt
PASS T5_safecrt_symbols:cplus_memcpy_s
PASS T5_safecrt_symbols:cplus_strcpy_s
PASS T5_safecrt_symbols:cplus_sc_memcpy_s
PASS T5_safecrt_symbols:cplus_safe_alloc_get_meta
PASS T5_safecrt_symbols:cplus_safe_stats_get
PASS T5_safecrt_symbols:cplus_safe_platform_probe
PASS T5_safecrt_symbols:cplus_runtime_set_handler
PASS T5_safecrt_symbols:cplus_safe_log_set_level
PASS T5_safecrt_symbols:cplus_fopen_s
PASS T5_safecrt_symbols:cplus_printf_s
PASS T5_compat_symbols:abi_cplus_add
PASS T5_compat_symbols:abi_cplus_record_by_value
PASS T5_compat_symbols:abi_cplus_record_by_ptr
PASS T5_compat_symbols:abi_cplus_meta_write
PASS T5_compat_symbols:abi_c_mul
PASS T5_compat_symbols:abi_c_record_by_value
PASS T5_compat_symbols:abi_c_record_by_ptr
PASS T5_no_cxx_mangling:nm_libc.txt
PASS T5_no_cxx_mangling:nm_safecrt.txt
PASS T5_no_cxx_mangling:nm_compat.txt
PASS T6_neutral_plain_compile
PASS T6_neutral_cplus_compile
PASS T6_neutral_plain_run
PASS T6_neutral_cplus_run
PASS T6_neutral_diff
PASS T6_neutral_line_count
abi_compat: 69 passed, 0 failed
```

Notable: `T5_no_cxx_mangling` passes for all three archives (flat C symbols, no C++
name mangling) and `T6_neutral_diff` passes (header-neutrality: plain vs `cplus`
header compiles/runs produce no difference).

### 3. Layout checks — `tests/abi/run_abi_layouts.sh` (exit 0)

Full log (3 lines, verbatim):

```
clang: warning: build/libc/libcplus_hardened.a: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: build/safe/libcplus_safecrt.a: 'linker' input unused [-Wunused-command-line-argument]
abi_layouts: 70/70 layouts match
```

The two `clang` warnings are benign `-Wunused-command-line-argument` notices from the
probe compile; they do not affect the result. All 70 layout assertions match.

### 4. Legacy labs — `tests/abi/run_legacy_labs.sh` (bash 3.2 invalid → bash 5.3 exit 0)

**Attempt 4a — system `/bin/bash` 3.2.** Full log (5 lines, verbatim):

```
tests/abi/run_legacy_labs.sh: line 170: declare: -A: invalid option
declare: usage: declare [-afFirtx] [-p] [name[=value] ...]
tests/abi/run_legacy_labs.sh: line 172: declare: -A: invalid option
declare: usage: declare [-afFirtx] [-p] [name[=value] ...]
tests/abi/run_legacy_labs.sh: line 352: legacy_c: unbound variable
```

Bash 3.2 has no associative arrays (`declare -A`), so the script could not initialise
its result maps and terminated with an unbound-variable error; **no pass summary was
emitted**. This run is recorded exactly as observed (the wrapper reported exit 0, but
the script output is an unmistakable failure) and is **not** counted as a passing
result. This is precisely the case the task's fallback instruction covers.

**Attempt 4b — `/opt/homebrew/bin/bash` 5.3 (used as the real result).** Final 34 lines
include, verbatim:

```
legacy_c/lc49_linked_list                legacy_c       PASS           0      
legacy_c/lc50_state_machine              legacy_c       PASS           0      
c_to_cp                                  c_to_cp        PASS           0      
cp_to_c/cp_to_c                          cp_to_c        PASS           0      
precompiled/precompiled_lab              precompiled    PASS           0      
abi_compare                              abi_compare    PASS           0      

run_legacy_labs: total=54 pass=54 fail=0 build_failure=0 timeout=0 skipped=0 blocked=0
required_failures=0  summary=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/abi-labs/summary.json  logs=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/abi-labs/logs
```

**54 total / 54 pass / 0 fail / 0 build_failure / 0 timeout / 0 skipped / 0 blocked**;
`required_failures=0`.

### 5. Standard-C ABI under the safety layer — `tests/abi/stdc_abi/run.sh` (exit 0)

Key evidence lines (verbatim):

```
INFO plain: 7 defined-global symbols, 6 ordinary C symbols
INFO safe: 138 defined-global symbols, 6 ordinary C symbols
INFO disabled: 7 defined-global symbols, 6 ordinary C symbols
PASS T1_ordinary_symbols:plain
PASS T1_ordinary_symbols:safe
PASS T1_ordinary_symbols:disabled
PASS T2_no_cxx_mangling:plain
PASS T2_no_cxx_mangling:safe
PASS T2_no_cxx_mangling:disabled
INFO safe-only added symbols (131):
...
PASS T3_no_ordinary_symbol_removed
INFO archives define 1101 global symbols
PASS T4_added_symbols_are_internal_helpers
PASS T4b_no_ordinary_symbol_added
PASS T5_run:plain
PASS T5_run:safe
PASS T5_run:disabled
PASS T6_runtime_layout_identical:safe
PASS T6_runtime_layout_identical:disabled
PASS T7_pointer_is_8_bytes
PASS T8_abi_sample_offsets
PASS T9_machine_code_byte_identical:_abi_add
PASS T9_machine_code_byte_identical:_abi_sum
PASS T9_machine_code_byte_identical:_abi_fill
stdc_abi: 0 failed
stdc_abi: RESULT PASS -- ordinary C ABI unchanged by the safety layer
```

This is the direct evidence for the "additive symbols only" claim. Every build —
plain, safe, and safety-disabled — exposes the **same 6 ordinary C symbols**. The safe
archive has 138 defined globals vs 7 for plain/disabled, but `T3_no_ordinary_symbol_removed`,
`T4_added_symbols_are_internal_helpers`, and `T4b_no_ordinary_symbol_added` all pass:
none of the 131 safe-only additions is an ordinary C symbol. Runtime layout, pointer
size, struct offsets, and even emitted machine code for `_abi_add`, `_abi_sum`, and
`_abi_fill` are byte-identical across builds.

### 6. Precompiled C library with caller-side macros — `tests/abi/stdc_precompiled/run.sh` (exit 0)

Final lines (verbatim):

```
PASS app_plain exit 0
PASS app_safe exit 0
PASS stdout byte-identical (plain vs safe, cmp -s)
stdout sha256 plain=2919b20d948858a07cf17c107af894cf47b9a463dd19e822b5c815ab98ae7d64
stdout sha256 safe =2919b20d948858a07cf17c107af894cf47b9a463dd19e822b5c815ab98ae7d64
PASS negative_safe exit 70 (refusal)
PASS negative stderr reports ESNOSPC refusal
PASS negative stderr names ESNOSPC 406
PASS negative stdout shows unchanged C library was called
PASS negative overflow refused before UNEXPECTED line
----
exit codes: app_plain=0 app_safe=0 negative_safe=70
failures=0
CONCLUSION: caller-side -include safe_c.h is compile-time macro mapping;
            NO rebuild of the precompiled C library is needed; ABI unchanged.
stdc_precompiled: ALL CHECKS PASSED
```

Plain and safe callers produce **byte-identical stdout** (matching SHA-256), proving
the safety mapping is a caller-side compile-time macro layer that does not change the
precompiled library's ABI. The negative case refuses an overflow with exit 70 and an
`ESNOSPC 406` diagnostic, with zero failures.

## The `cplus_libc.h` C++17 fix is ABI-neutral (verified MATCH)

The branch commit `46e81e4` ("Fix C++17 compatibility in cplus_libc.h; add
example-vs-Rust comparisons") resolved the former C++17 incompatibility by introducing
four **compile-time spelling macros** at `libc/install-include/cplus_libc.h:11-19`:

```c
#ifdef __cplusplus
#define CPLUS_RESTRICT          __restrict
#define CPLUS_FLOAT_COMPLEX     _Complex float
#define CPLUS_DOUBLE_COMPLEX    _Complex double
#define CPLUS_LDOUBLE_COMPLEX   _Complex long double
#else
#define CPLUS_RESTRICT          restrict
#define CPLUS_FLOAT_COMPLEX     float complex
#define CPLUS_DOUBLE_COMPLEX    double complex
#define CPLUS_LDOUBLE_COMPLEX   long double complex
#endif
```

These substitute C++-legal spellings for C-only tokens (`restrict`, `double complex`)
purely at preprocessing time; the header comment itself states "No layout, signature,
or symbol name changes in either language." This is corroborated by the existing
record in `docs/abi/STDC_ABI_SAFETY_FINAL.md:306-309` (the fix is "ABI-neutral" through
`CPLUS_RESTRICT`/`CPLUS_*_COMPLEX` macros) and by the current run: **command 1 reports
fingerprint MATCH with an empty diff** against the committed baseline, and command 2's
`T6_neutral_*` checks pass. No layout, field offset, type size, function signature,
symbol name, or calling convention is altered by that fix.

## Explicit ABI-preservation statement

Based on the six serial runs above, on the tested configuration:

> **No ABI layout, signature, symbol-name, or calling-convention change occurred.**
> Type sizes, alignments and struct/union field offsets are unchanged; exported
> function signatures and names are unchanged (flat C linkage, no C++ mangling);
> calling conventions are unchanged. Where the safety layers add symbols, those are
> **additive internal helpers only** — the ordinary C ABI surface is identical across
> plain, safe, and disabled builds (command 5: same 6 ordinary C symbols,
> `T3_no_ordinary_symbol_removed` / `T4b_no_ordinary_symbol_added` PASS), and the
> fingerprint diff is empty (command 1).

## Scope and non-claims

* **Tested configuration only:** macOS **arm64**, **Apple clang 16.0.0**
  (`clang-1600.0.26.6`), Command Line Tools SDK, `SDKROOT` unset,
  `CPLUS_CLANG=/usr/bin/clang`. Nothing here generalises beyond this platform/compiler
  pairing without a re-run.
* **Windows and Linux are UNTESTED in this audit.** No cross-compile, Wine/MSVC/MinGW,
  or Linux ELF ABI verification was performed in this run; no claim is made about
  those targets.
* The `bash 3.2` attempt of command 4 is disclosed as a failed run and is superseded
  by the bash 5.3 run; it is not hidden and not counted as a pass.
* Two benign clang warnings in command 3 are recorded verbatim above; they do not
  indicate a test failure.
* This audit re-runs existing tooling against the current branch state; it does not
  modify source, headers, baselines, or scripts. The only repository file created for
  this task is this document.

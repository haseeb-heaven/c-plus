# ABI preservation of the original C sources and pointer representation — final verification

Run: 2026-11-23 (UTC). Branch `agent/modern-stdlib`, HEAD `c1bd15e`, compare baseline `cbcea57`
(merge commit `cbcea57174e5c6b26d07bcc4e1b5c8f7501b340a`, direct parent of safety-integration
commit `960c997`).

Every command below was run with `CPLUS_CLANG=/usr/bin/clang`, `SDKROOT` unset, and a hard
`timeout 300` wrapper. All commands were run from the repository root.

## 1. Environment / preconditions (observed, not assumed)

| Item | Observed value |
| --- | --- |
| Compiler | Apple clang version 16.0.0 (clang-1600.0.26.6) |
| Target triple | `arm64-apple-darwin27.2.0` |
| Host | macOS, `uname -m` = `arm64`, `uname -s` = `Darwin` |
| `bash` for the legacy-lab runner | `/opt/homebrew/bin/bash` 5.3.15 (this is what `bash` resolves to in this shell; `/bin/bash` 3.2.57 is also present but was not used) |
| `nm` | `/usr/bin/nm` |

## 2. Exact command results

| # | Command | Exit code | Exact final summary line(s) |
| --- | --- | --- | --- |
| 1 | `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt` | **0** | `abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).` / `abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt` |
| 2 | `sh tests/abi/run_abi_compat.sh` | **0** | `abi_compat: 69 passed, 0 failed` |
| 3 | `sh tests/abi/run_abi_layouts.sh` | **0** | `abi_layouts: 70/70 layouts match` |
| 4 | `sh tests/abi/stdc_source/run.sh` | **1** | `FAIL: a non-additive change or a modified core file was detected.` |
| 5 | `sh tests/abi/stdc_abi/run.sh` | **0** | `stdc_abi: 0 failed` / `stdc_abi: RESULT PASS -- ordinary C ABI unchanged by the safety layer` |
| 6 | `bash tests/abi/run_legacy_labs.sh` | **0** | `run_legacy_labs: total=54 pass=54 fail=0 build_failure=0 timeout=0 skipped=0 blocked=0` |

Five of the six commands exited 0. Command 4 exited 1 for one specific, disclosed reason
(detail in section 4.2). No result below is inferred from a build or emulator alone; each claim
is tied to a compiled-and-run program, a symbol table, or a byte-for-byte comparison.

## 3. Verified claims

### 3.1 `sizeof(void*) == 8` and ordinary C pointer representation unchanged

- `tests/abi/stdc_abi/run.sh` printed, from the program compiled by plain `/usr/bin/clang`:

  ```
  sizeof(void*)=8
  sizeof(abi_sample)=40
  offsets tag=0 count=4 value=8 ptr=16 name=24 big=32
  ```

  It then asserted `T7_pointer_is_8_bytes` (literal `sizeof(void*)=8`) and
  `T8_abi_sample_offsets` (literal offsets line), both PASS, and confirmed via
  `T6_runtime_layout_identical:safe` and `T6_runtime_layout_identical:disabled` that the
  **plain**, **safe**, and **disabled** builds print byte-identical stdout for these values.
- `run_abi_compat.sh` `T1_pointers_compile` / `T1_pointers_run` / `T1_pointers_summary` all PASS
  (`pointer ABI checks match`) — C99 `-pedantic` program compiled by the real host compiler and
  executed, checking pointer sizes and byte representation. `T6_neutral_plain_compile`,
  `T6_neutral_cplus_compile`, `T6_neutral_diff` all PASS: pre-including `cplus_libc.h` and
  `cplus_libc_safe.h` produces no difference in the printed layout lines.
- The fingerprint compare (command 1) includes the compiled-probe layout records (`L` section)
  and the ABI-relevant preprocessor defines (`C` section, e.g. `__SIZEOF_POINTER__`, `__LP64__`)
  and MATCHES the committed baseline exactly (section 3.3).

### 3.2 Seven core C files byte-identical to baseline `cbcea57`

`tests/abi/stdc_source/run.sh` reported (all seven files):

```
UNCHANGED libc/src/string/memory.c                46de2624d29cfc15848d8d4d624b145192365148
UNCHANGED libc/src/string/string.c                104accdec51d92dc88066b7e5271f7c3d451a574
UNCHANGED libc/src/string/extra.c                 4f98cca100bc503ebb8f93ba9464dbe61c75cf59
UNCHANGED libc/src/string/bounded.c               231e7a9928efc992a0ca0c2666d2a468de68f281
UNCHANGED libc/src/stdlib/alloc.c                 cd2c44687e565ab6fec18520a4b985a4660f69fd
UNCHANGED libc/src/stdio/stdio.c                  830b32f212db11bcdab21f24509005157d84188f
UNCHANGED runtime/cplus_runtime.c                 c6a921d5c306a89594b667057c1d4a62a5d3cfab
```

Every one also produced `EMPTY DIFF cbcea57..HEAD` (git blob IDs equal at baseline and HEAD =
byte-for-byte identical contents), and the "uncommitted working-tree changes" section reported
`NONE — no uncommitted changes to any core file`.

Change categories in the same `cbcea57..HEAD` range:

- `libc/src/`: `18 A` — **18 added, 0 modified/deleted/renamed sources** (script line:
  `No MODIFIED / DELETED / RENAMED entries in libc/src/ — all changes are ADDED.`).
- `libc/install-include/`: `19 A` plus `1 M` — **19 added, 1 modified public header** (see
  section 4.2 for the honest deviation from a "0 modified headers" claim).

The script's broad additive-only assertion therefore fails on that single header, not on any
core C file and not on any source file.

### 3.3 Fingerprint MATCH — 1235 records

- Command 1 generated a fresh fingerprint: `wrote tools/abi_compare/.out/compare/fingerprint.txt
  (1235 records) and fingerprint.json`.
- `diff -u baseline.filtered current.filtered` produced an **empty** diff (`diff.txt` is 0 bytes;
  independently confirmed with `wc -c` = 0). Both the baseline and the freshly generated
  fingerprint contain 1235 non-comment records (independently confirmed with `awk`/`wc -l`).
- Output: `abi_fp: ABI fingerprint MATCHES baseline` and `abi_fp: diff is empty`. Exit 0.
- This fingerprint covers layout/size/offset/enum records, compile-time function-signature
  checks, ABI-relevant compiler defines, and exported symbols from both shipped archives
  (`build/libc/libcplus_hardened.a`, `build/safe/libcplus_safecrt.a`).

### 3.4 Exported symbol names/signatures/calling conventions unchanged; no C++ mangling

- `run_abi_compat.sh` `T5_libc_symbols:*`, `T5_safecrt_symbols:*`, `T5_compat_symbols:*` — all
  expected exported symbols found with their exact `nm` names (e.g. `cplus_malloc`, `cplus_memcpy`,
  `cplus_printf`, `cplus_fopen`, `abi_cplus_add`, `abi_c_mul`, etc.). `T5_no_cxx_mangling` PASS for
  all three symbol dumps (`nm_libc.txt`, `nm_safecrt.txt`, `nm_compat.txt`): no ` T __Z` entries.
- `stdc_abi/run.sh` compiled one ordinary C source three ways (plain / safe / disabled) and
  compared real `nm -gU` output:
  - plain: 7 defined-global symbols, 6 ordinary C symbols; safe: 138 defined-global symbols,
    6 ordinary C symbols; disabled: 7 defined-global symbols, 6 ordinary C symbols.
  - `T1_ordinary_symbols` PASS for all three (identical to `EXPECTED_SYMBOLS.txt`);
    `T2_no_cxx_mangling` PASS for all three (no `_Z`).
  - `T3_no_ordinary_symbol_removed` PASS, `T4_added_symbols_are_internal_helpers` PASS,
    `T4b_no_ordinary_symbol_added` PASS: the 131 symbols present only in the SAFE build are all
    accounted for by the two shipped archives (internal `cplus_*`/SafeCRT helper implementations
    pulled in by remapped calls), not by any new/redefined ordinary C API symbol.
  - Signatures/layout: `T6_runtime_layout_identical` and `T8_abi_sample_offsets` PASS in all
    builds (section 3.1).
  - Calling convention: `T9_machine_code_byte_identical` PASS for `_abi_add`, `_abi_sum`, and
    `_abi_fill` — the byte-for-byte machine code (disassembled with `/usr/bin/objdump`) is
    identical in plain, safe, and disabled builds for functions whose bodies contain no
    macro-remapped library call (direct AAPCS64 evidence on this arm64 target).
- `run_abi_layouts.sh` also PASS: the public-type layout program compiled by the host C compiler
  and by the C+ driver produced identical output (`70/70 layouts match`), including by-value / by-
  pointer struct passing and function-pointer cases.

### 3.5 Legacy C programs — 54/54

`run_legacy_labs.sh` discovered and ran 54 labs, all classified PASS with exit 0:

```
run_legacy_labs: total=54 pass=54 fail=0 build_failure=0 timeout=0 skipped=0 blocked=0
required_failures=0
```

That is the 50 `legacy_c/lc01…lc50` programs (primitives, pointers, pointer-to-pointer, arrays,
structs/unions/bitfields, function pointers, variadics, callbacks, thread-local storage, opaque
handles, stdio/stdlib/string/allocator interop, exported vtables, etc.) plus `c_to_cp`,
`cp_to_c/cp_to_c`, `precompiled/precompiled_lab`, and `abi_compare`. Every lab ran under a bounded
timeout with no `|| true` masking; stdout/stderr and machine-readable `summary.json` /
`results.jsonl` were emitted under `build/abi-labs/`.

## 4. Deviations and caveats (reported, not hidden)

### 4.1 Command 4 is FAIL, not PASS

`sh tests/abi/stdc_source/run.sh` exited **1**, ending with
`FAIL: a non-additive change or a modified core file was detected.` The failure is triggered
**only** by the single modified public header described in 4.2. All core-file and source-file
checks inside that same script passed. No exit code was rewritten to hide this.

### 4.2 The one modified header is a source-spelling-only C++ compatibility edit

`libc/install-include/cplus_libc.h` is the `1 M` entry (commit `46e81e4` "Fix C++17 compatibility
in cplus_libc.h", 89 insertions / 73 deletions). Its diff:

- Introduces macros `CPLUS_RESTRICT`, `CPLUS_FLOAT_COMPLEX`, `CPLUS_DOUBLE_COMPLEX`,
  `CPLUS_LDOUBLE_COMPLEX`. Under `#ifdef __cplusplus` they expand to `__restrict` and
  `_Complex float` / `_Complex double` / `_Complex long double` (spellings valid in C++); in C
  translation units they expand to the original ISO C spellings `restrict`, `float complex`,
  `double complex`, `long double complex`.
- Replaces `restrict` with `CPLUS_RESTRICT` and `double complex`/`float complex` with the complex
  macros in existing `cplus_*` declarations.

Consequence for this ABI claim: in **C** translation units the preprocessed declarations are
identical to baseline — same names, same parameter/return types, same layouts — so no exported
C symbol name, signature, or calling convention changed. This is corroborated independently by
command 1 (fingerprint MATCH, including function-signature and symbol records) and command 5
(`stdc_abi: RESULT PASS`: ordinary C symbols unchanged, no mangling, identical runtime layouts and
identical machine code). The edit was needed because the verbatim C99 spellings are not valid C++,
and it is a header-only source change; it does not touch any of the seven core C files.

The task's expected "19 added / 0 modified headers" figure is **not** what the repository
currently contains; the observed figure is 19 added / 1 modified. This document records the
observed result rather than asserting the expected one.

### 4.3 Benign warnings

`run_abi_layouts.sh` emitted two compiler warnings
(`build/libc/libcplus_hardened.a: 'linker' input unused [-Wunused-command-line-argument]` and the
same for `libcplus_safecrt.a`) during the C+ driver compile step; the link step consumes the
archives correctly and the script still reported `70/70 layouts match` with exit 0.

## 5. Explicit scope

- **OS/arch/compiler:** macOS, `arm64`, Apple clang 16.0.0 only (`arm64-apple-darwin27.2.0`).
  All conclusions are limited to this environment.
- **Windows and Linux are untested here.** No cross-compilation, Wine, or CI-on-other-OS run was
  performed as part of this verification; no claim is made for those platforms.
- **Safety mechanism:** the safety layer works by **caller-side, compile-time macro remapping**
  (header macros such as `malloc` -> `cplus_malloc` when the safety header is included), plus
  optional `-DCPLUS_SAFE_C_DISABLE` to turn it off. It is **not symbol interposition**: there is no
  `LD_PRELOAD`/`DYLD_INSERT_LIBRARIES`-style override, no PLT/GOT hooking, and no redefinition of
  the ordinary C API. Evidence: the plain and disabled builds have the same 7 defined-global
  symbols, all ordinary C names are present and unchanged in every build, no ordinary symbol is
  removed, and the only added symbols are internal helpers supplied by the two shipped archives.
- A build or emulator success alone is not treated as live-game/live-process success; these
  results come from real compiled-and-executed C programs and real symbol/layout comparisons on
  the stated host.

## 6. Bottom line

On macOS arm64 with Apple clang 16, the original C ABI is preserved: `sizeof(void*)` is 8 and
pointer representation/layouts are unchanged, the 1235-record ABI fingerprint matches the
committed `cbcea57`-derived baseline exactly, exported C symbol names/signatures/calling
conventions are unchanged with no C++ mangling, all seven core C files are byte-identical to
`cbcea57` with 18 added/0 modified sources, and 54/54 legacy labs pass. The single caveat is that
`libc/install-include/cplus_libc.h` has one source-spelling-only edit (C++17 compatibility) that
leaves the C ABI untouched but causes `stdc_source/run.sh` to exit 1 on its strict "all changes
must be additions" check; 19 added/1 modified headers is the observed figure, not the expected
19/0.

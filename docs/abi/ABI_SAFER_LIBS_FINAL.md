# Final ABI verification — SAFER C libraries (checked `_s` APIs + vendored implementations)

**Branch:** `agent/modern-stdlib`  **Capture date:** 2026-11-23  **HEAD:** `c1bd15eb22c33d2d3d8a914712988dadc6ccf37f`

**Scope.** This document verifies that the "safer C" surface — the checked `_s`
APIs, the vendored safeclib implementations, and the C23 / runtime-hardening
additions — is **purely additive** to the ordinary C ABI of this project. It
records the exact results of the four required commands plus a symbol-by-symbol
inventory of the two built archives. It is evidence for **macOS `arm64` only**;
it makes no claim for Windows x86 / MSVC / UCRT or any other target.

**Environment for every command below:** `CPLUS_CLANG=/usr/bin/clang`, `SDKROOT`
unset, per-command `timeout 300`, run from the repository root. Host:
`Darwin` / `arm64`, `/usr/bin/clang` = Apple clang version 16.0.0
(`clang-1600.0.26.6`), target triple `arm64-apple-darwin27.2.0`.

---

## 1. Commands and exact results

All four commands exited `0` on the first attempt. Nothing was masked,
filtered, retried, or fabricated.

### 1.1 `sh tools/abi_compare/run.sh capture`

```
abi_fp: generating C sources in tools/abi_compare/.out
abi_fp: compile probe   : /usr/bin/clang -O2 -std=c11 -Wall -Wextra -Werror <includes> tools/abi_compare/.out/abi_fp_probe.c
abi_fp: compile json    : /usr/bin/clang -O2 -std=c11 -Wall -Wextra tools/abi_compare/.out/fp_to_json.c
abi_fp: wrote tools/abi_compare/.out/fingerprint.txt (1235 records) and fingerprint.json
abi_fp: capture complete. baseline candidates:
  tools/abi_compare/.out/fingerprint.txt
  tools/abi_compare/.out/fingerprint.json
```

Final line: `abi_fp: capture complete...` — **1235 records** captured by a real
`-O2 -std=c11 -Wall -Wextra -Werror` C probe.

### 1.2 `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt`

```
abi_fp: generating C sources in tools/abi_compare/.out/compare
abi_fp: compile probe   : /usr/bin/clang -O2 -std=c11 -Wall -Wextra -Werror <includes> tools/abi_compare/.out/compare/abi_fp_probe.c
abi_fp: compile json    : /usr/bin/clang -O2 -std=c11 -Wall -Wextra tools/abi_compare/.out/compare/fp_to_json.c
abi_fp: wrote tools/abi_compare/.out/compare/fingerprint.txt (1235 records) and fingerprint.json
abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).
abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt
```

**Final lines:** `abi_fp: ABI fingerprint MATCHES baseline ...` and
`abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt`. Exit `0`.
The diff transcript `tools/abi_compare/.out/compare/diff.txt` is **0 bytes**.
A byte-level check confirms record-for-record identity:

```
$ shasum -a 256 tests/abi/fingerprints/baseline.txt tools/abi_compare/.out/fingerprint.txt
ab496f8a62d525fefd8fa1b700477790436be0d91a53ec7d9aeee05bc11ba8d8  tests/abi/fingerprints/baseline.txt
ab496f8a62d525fefd8fa1b700477790436be0d91a53ec7d9aeee05bc11ba8d8  tools/abi_compare/.out/fingerprint.txt
```

Fingerprint composition (current capture, 1235 records): `V` metadata 4,
`L` layout facts 89, `C` ABI-relevant compiler defines 27, `F` compile-time
function-signature assertions 12, `S` exported-symbol names 1103. The two
archive counts in the `S` section are:

```
S	archive build/libc/libcplus_hardened.a	count=812
S	archive build/safe/libcplus_safecrt.a	count=289
```

### 1.3 `sh tests/abi/run_abi_compat.sh`

Real C compiler + real linker + real archives; stdout/exit codes recorded in
`build/abi/compat/results.log`. **Final line:**

```
abi_compat: 69 passed, 0 failed
```

Exit `0` — **69/69**. This includes the no-C++-mangling assertions, which pass
on all three `nm` outputs:

```
PASS T5_no_cxx_mangling:nm_libc.txt
PASS T5_no_cxx_mangling:nm_safecrt.txt
PASS T5_no_cxx_mangling:nm_compat.txt
```

It also includes T1 pointers (`abi_compat_pointers: 20/20`), T2 layouts
(`abi_compat_layouts: 80/80`), both C↔C+ boundary directions (T3a/T3b) with
real object files and archives, T5 exact-name symbol presence, and T6
header-neutrality (`T6_neutral_diff`, `T6_neutral_line_count` — force-including
`cplus_libc.h` and `cplus_libc_safe.h` does not perturb C layouts).

### 1.4 `sh tests/abi/run_abi_layouts.sh`

```
clang: warning: build/libc/libcplus_hardened.a: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: build/safe/libcplus_safecrt.a: 'linker' input unused [-Wunused-command-line-argument]
abi_layouts: 70/70 layouts match
```

**Final line:** `abi_layouts: 70/70 layouts match` — **70/70**, exit `0`. Both
host and C+-driver executables print the same line and the script's `diff -u`
between them is empty. The two clang warnings are benign (the C+ driver passes
archive paths to both its compile step, where they are unused inputs, and its
link step, where they are consumed).

### 1.5 `nm -g build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a`

Exit `0`; 2259 raw output lines (archive member headers + symbols). Extracting
all `cplus_sc_*`, `cplus_v_*`, and `cplus_*_s` names gives the inventory in §2.

---

## 2. Symbol inventory — every safer-lib export is ADDITIVE

All names below are new, distinct, flat C identifiers. **None is a rename,
retype, or removal of any pre-existing export.** On macOS `nm` prefixes
external symbols with `_`; the counts below strip that Mach-O underscore
(e.g. `_cplus_sc_memcpy_s` → `cplus_sc_memcpy_s`).

### 2.1 `cplus_sc_*` — 36 core + 2 later upstream additions = 38 defined exports

A naive text grep of raw `nm -g` output returns **39**, but one match is the
archive member filename marker `sc_cplus_sc_wrappers.o:`, **not** a symbol
(it is absent from the fingerprint's `nm -gU` scan and from
`tests/abi/fingerprints/baseline.txt`). The true count of *defined exported*
`cplus_sc_*` symbols is **38**:

| Category | Count | Samples |
|---|---:|---|
| Internal `_chk` helpers | 13 | `cplus_sc__memcpy_s_chk`, `cplus_sc__strcpy_s_chk`, `cplus_sc__strnlen_s_chk`, `cplus_sc__memcmp_s_chk`, `cplus_sc__strtok_s_chk` |
| Public checked memory/string `_s` | 13 | `cplus_sc_memcpy_s`, `cplus_sc_memset_s`, `cplus_sc_strcpy_s`, `cplus_sc_strcat_s`, `cplus_sc_strtok_s`, `cplus_sc_strnlen_s` |
| Timing-safe helpers | 2 | `cplus_sc_timingsafe_bcmp`, `cplus_sc_timingsafe_memcmp` |
| Constraint-handler API | 6 | `cplus_sc_abort_handler_s`, `cplus_sc_ignore_handler_s`, `cplus_sc_set_mem_constraint_handler_s`, `cplus_sc_set_str_constraint_handler_s`, `cplus_sc_invoke_safe_mem_constraint_handler`, `cplus_sc_invoke_safe_str_constraint_handler` |
| Wrapper aliases | 2 | `cplus_sc_set_mem_constraint_handler_s_wrapper`, `cplus_sc_set_str_constraint_handler_s_wrapper` |
| **Core subtotal** | **36** | — |
| Later upstream additions | 2 | `cplus_sc_upstream_bsearch_s`, `cplus_sc_upstream_memrchr_s` |
| **Total** | **38** | — |

The **36** core figure matches the expected count; the two extra `upstream`
symbols are themselves purely additive and are already recorded in
`docs/abi/compatibility-matrix.md` as the earlier baseline-refresh additions.

### 2.2 `cplus_v_*` — 26 defined exports (= 22 vendored `_chk` helpers + 4 public)

| Category | Count | Samples |
|---|---:|---|
| Vendored group internal `_chk` helpers | 22 | `cplus_v__memccpy_s_chk`, `cplus_v__memchr_s_chk`, `cplus_v__strcasestr_s_chk`, `cplus_v__strcmpfld_s_chk`, `cplus_v__strfirstchar_s_chk`, `cplus_v__strtolowercase_s_chk` |
| Public vendored entry points | 4 | `cplus_v_memccpy_s`, `cplus_v_memchr_s`, `cplus_v_strlcat`, `cplus_v_strlcpy` |
| **Total** | **26** | — |

The **22** vendored safe functions are the `cplus_v__* _chk` group above; the
larger vendored safeclib family is also visible as the `cplus_upstream_*` set
(`cplus_upstream_memcpy_s`, `cplus_upstream_strcpy_s`,
`cplus_upstream_set_mem_constraint_handler_s`, …). Separately, the checked
allocator extension family is 22 functions in `stdlib/safe/safe_alloc.h`
(recorded in `docs/libc/WRAPPER_SAFETY_AUDIT.md`). All are additions.

### 2.3 `cplus_*_s` and C23 / runtime-hardening new symbols

Beyond the `sc`/`v` families, the archives add ordinary checked spellings and
C23 / runtime-hardening entry points, again all new names:

- **Checked ordinary `_s` spellings:** `cplus_memcpy_s`, `cplus_strcpy_s`,
  `cplus_fopen_s`, `cplus_printf_s`, `cplus_fclose_s`, `cplus_fprintf_s`,
  `cplus_fread_s`, `cplus_fwrite_s`, `cplus_scanf_s`, `cplus_snprintf_s`,
  `cplus_sprintf_s`, `cplus_sscanf_s`, `cplus_vsnprintf_s`,
  `cplus_vsprintf_s`, …
- **C23 / ISO closure:** `cplus_memset_explicit`, `cplus_strlcpy`,
  `cplus_strlcat`, `cplus_c23_exp16`, `cplus_c23_fma16`, `cplus_c23_frexp16`,
  `cplus_c23_hypot3`, `cplus_c23_logb16`, …
- **Runtime hardening:** `cplus_runtime_set_handler`,
  `cplus_runtime_set_failstop`, `cplus_runtime_push_cleanup`,
  `cplus_runtime_unwind_cleanups`, `cplus_runtime_violation`,
  `cplus_safe_alloc_get_meta`, `cplus_safe_stats_get`,
  `cplus_safe_platform_probe`, `cplus_safe_ptr_check_generation`,
  `cplus_safe_set_constraint_mode`, `cplus_alloc_checked_free`,
  `cplus_swprintf_checked`, …

None of these replaces an existing `cplus_*` export; they extend the symbol
table only.

---

## 3. Why this is additive — no rename, retype, or removal

1. **Fingerprint MATCH (§1.2).** A freshly compiled capture matches the
   committed baseline **record for record** (identical SHA-256, 0-byte diff).
   That covers 89 `L` layout records, 12 `F` signature records, 27 `C` compiler
   ABI defines, and 1103 `S` symbol records. Any rename, retype, or removal of
   a pre-existing export would appear as a diff and fail the comparison. It did
   not.
2. **Compile-time signature enforcement (`F` records).** The 12 `F` records are
   produced by `_Static_assert(__builtin_types_compatible_p(...))` in
   `tools/abi_compare/abi_fp_probe.c`: a signature change is a **compile error**,
   so their presence is positive proof the checked public signatures are
   unchanged.
3. **Layouts unchanged (§1.4 + §1.3 T2/T6).** 70/70 layouts match between host C
   compiler and C+ driver; 80/80 in the compat suite; T6 shows force-including
   the safe headers produces byte-identical output. `sizeof`/`_Alignof`/
   `offsetof`/enum values did not move.
4. **No C++ mangling (§1.3).** `T5_no_cxx_mangling` passes on libc, safecrt, and
   compat `nm` outputs: every name is a flat C symbol, no `_Z`/`__Z`.
5. **No interposition of ordinary names.** Per corroborating evidence already
   recorded in `docs/abi/STDC_ABI_SAFETY_FINAL.md` (§1; *not re-run in this
   pass*), the `stdc_abi` lab reports `plain minus safe = 0` — no ordinary
   symbol removed, added, or renamed — and dispatch opcodes for `_abi_add`,
   `_abi_sum`, `_abi_fill` are byte-identical across plain/safe/disabled builds.
   Safety is delivered by caller-TU macro remapping, not by overriding
   `malloc`/`memcpy`/`printf` symbols.
6. **Source integrity.** Also from `docs/abi/STDC_ABI_SAFETY_FINAL.md` §2
   (corroborating; *not re-run here*): over the audited integration range
   `cbcea57..HEAD`, the 7 core implementation files are unchanged and
   `libc/install-include/cplus*.h` shows **19 ADDED, 0 MODIFIED, 0 DELETED,
   0 RENAMED** — no existing declaration signature changed. (Honest scope: this
   is "unchanged since the safety-integration baseline", not "never modified in
   project history".)

**Calling convention:** unchanged. The additions use the same C ABI as the
existing library (System V AMD64 / AAPCS64 as appropriate for the host); the
compat suite links real C objects against the archives in both directions
(T3a/T3b) with no ABI mismatch.

---

## 4. `cplus_libc.h` C++17 fix is ABI-neutral

Commit `46e81e4` ("Fix C++17 compatibility in cplus_libc.h") replaced C-only
`restrict` and `double complex` tokens in the header with language-selected,
ABI-identical macros:

```c
#ifdef __cplusplus
#define CPLUS_RESTRICT       __restrict
#define CPLUS_FLOAT_COMPLEX  _Complex float
#define CPLUS_DOUBLE_COMPLEX _Complex double
#define CPLUS_LDOUBLE_COMPLEX _Complex long double
#else
#define CPLUS_RESTRICT       restrict
#define CPLUS_FLOAT_COMPLEX  float complex
#define CPLUS_DOUBLE_COMPLEX double complex
#define CPLUS_LDOUBLE_COMPLEX long double complex
#endif
```

`restrict` / `__restrict` are type qualifiers that do not affect parameter
type compatibility, size, alignment, name, or calling convention;
`_Complex double` and `double complex` are two spellings of the same C complex
type. The change is confined to a public header: **no function name, argument
list, return type, or layout changed in either language**, and no archive
symbol was touched. Empirically, the current fingerprint still MATCHes (§1.2)
with all 12 `F` signature records and 89 `L` layout records intact, and T6
header-neutrality passes (§1.3) — direct confirmation that the C++17 fix
introduced no ABI drift.

---

## 5. Conclusion and scope limits

- **Ordinary C ABI preserved.** Every pre-existing export keeps its exact
  name, type, layout, and calling convention; the fingerprint is a
  record-for-record MATCH against the committed baseline.
- **Safety is additive.** It is exposed through new checked `_s` APIs and
  vendored implementations (`cplus_sc_*` 38 defined exports — 36 core + 2
  upstream; `cplus_v_*` 26 — 22 vendored `_chk` helpers + 4 public; plus C23 and
  runtime-hardening symbols), and through **caller-TU macro remapping** that
  defines no interposing symbol.
- **Flat C names, no C++ mangling**; layouts, signatures, and calling
  conventions unchanged; the `cplus_libc.h` C++17 macro fix is ABI-neutral.
- **Scope: macOS `arm64` only.** This is evidence, not a safety certificate, and
  it does not extend to windows x86 / MSVC / UCRT or to any non-host target. It
  also does not by itself assert the runtime correctness of any new function.

### Reproduce

```sh
unset SDKROOT
export CPLUS_CLANG=/usr/bin/clang
sh tools/abi_compare/run.sh capture
sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt
sh tests/abi/run_abi_compat.sh
sh tests/abi/run_abi_layouts.sh
nm -g build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
```

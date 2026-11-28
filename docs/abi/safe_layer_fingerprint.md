# ABI fingerprint evidence for the checked ordinary-C layer

**Branch:** `agent/modern-stdlib`  **Date:** 2026-11-23
**Scope:** the C-compatible public ABI surface of the bundled hardened libc
(`libc/`), the optional SafeCRT layer (`stdlib/safe/`), and the checked
ordinary-C mapping headers `stdlib/safe/safe_c.h` and
`libc/install-include/cplus_libc_safe.h`.

This document records the exact results of re-running the repository's ABI
fingerprint and compatibility tooling against the checked ordinary-C layer.
Its claim is deliberately narrow: the checked layer is implemented as
**`static inline` helpers and macro remapping only**, so it introduces **no
ABI drift**. It is evidence, not a safety certificate, and it does not extend
to windows x86 / MSVC / UCRT, which is covered by separate evidence in
`tests/abi/` and is deliberately out of scope for this host run.

---

## 1. Commands and exact results

Four commands were run, in this order, from the repository root. All four
exited 0. No result was masked, filtered, or retried.

### 1.1 `sh tools/abi_compare/run.sh capture`

```
abi_fp: generating C sources in tools/abi_compare/.out
abi_fp: compile probe   : /usr/bin/clang -O2 -std=c11 -Wall -Wextra -Werror <includes> tools/abi_compare/.out/abi_fp_probe.c
abi_fp: compile json    : /usr/bin/clang -O2 -std=c11 -Wall -Wextra tools/abi_compare/.out/fp_to_json.c
abi_fp: wrote tools/abi_compare/.out/fingerprint.txt (1162 records) and fingerprint.json
abi_fp: capture complete. baseline candidates:
  tools/abi_compare/.out/fingerprint.txt
  tools/abi_compare/.out/fingerprint.json
```

The probe is a real C program compiled with `/usr/bin/clang -O2 -std=c11
-Wall -Wextra -Werror`; its layout/type/enum output plus compile-time function
signature assertions form the fingerprint. Result: **1162 records** captured.

### 1.2 `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt`

```
abi_fp: generating C sources in tools/abi_compare/.out/compare
abi_fp: compile probe   : /usr/bin/clang -O2 -std=c11 -Wall -Wextra -Werror <includes> tools/abi_compare/.out/compare/abi_fp_probe.c
abi_fp: compile json    : /usr/bin/clang -O2 -std=c11 -Wall -Wextra tools/abi_compare/.out/compare/fp_to_json.c
abi_fp: wrote tools/abi_compare/.out/compare/fingerprint.txt (1162 records) and fingerprint.json
abi_fp: ABI fingerprint MATCHES baseline (tests/abi/fingerprints/baseline.txt).
abi_fp: diff is empty: tools/abi_compare/.out/compare/diff.txt
```

Exit 0. The diff transcript `tools/abi_compare/.out/compare/diff.txt` is
**0 bytes**. This is the primary "no drift" assertion: a fresh, independently
compiled capture matches the authoritative committed baseline record-for-record.

An additional byte-level check confirmed the same fact:

```
$ shasum -a 256 tests/abi/fingerprints/baseline.txt tools/abi_compare/.out/fingerprint.txt
352b10e8ce6b7f2c1f071ff6d0240bb08aa99de7f9132027314634331681027c  tests/abi/fingerprints/baseline.txt
352b10e8ce6b7f2c1f071ff6d0240bb08aa99de7f9132027314634331681027c  tools/abi_compare/.out/fingerprint.txt
```

### 1.3 `sh tests/abi/run_abi_compat.sh`

Real compiler + real linker + real archives; stdout/stderr/exit codes captured
in `build/abi/compat/results.log`. Final summary:

```
abi_compat: 69 passed, 0 failed
```

Full PASS list (verbatim):

```
PASS build_libc
PASS build_safecrt
PASS T1_pointers_compile
PASS T1_pointers_run
PASS T1_pointers_summary
PASS T2_layouts_compile
PASS T2_layouts_run
PASS T2_layouts_summary
PASS T3a_cplus_lib_compile
PASS T3a_cplus_lib_archive
PASS T3a_c_driver_compile
PASS T3a_c_driver_run
PASS T3a_c_driver_summary
PASS T3b_c_part_compile
PASS T3b_c_part_archive
PASS T3b_direct_link_probe
PASS T3b_cplus_caller_build
PASS T3b_cplus_caller_run
PASS T3b_cplus_caller_summary
PASS T5_nm_libc
PASS T5_nm_safecrt
PASS T5_libc_symbols:cplus_malloc
PASS T5_libc_symbols:cplus_calloc
PASS T5_libc_symbols:cplus_realloc
PASS T5_libc_symbols:cplus_free
PASS T5_libc_symbols:cplus_free_checked
PASS T5_libc_symbols:cplus_alloc_get_meta
PASS T5_libc_symbols:cplus_alloc_remaining_capacity
PASS T5_libc_symbols:cplus_alloc_is_live
PASS T5_libc_symbols:cplus_alloc_size
PASS T5_libc_symbols:cplus_alloc_length
PASS T5_libc_symbols:cplus_alloc_live_count
PASS T5_libc_symbols:cplus_alloc_double_free_count
PASS T5_libc_symbols:cplus_alloc_invalid_free_count
PASS T5_libc_symbols:cplus_alloc_query_length
PASS T5_libc_symbols:cplus_transfer_ownership
PASS T5_libc_symbols:cplus_memcpy
PASS T5_libc_symbols:cplus_memset
PASS T5_libc_symbols:cplus_strlen
PASS T5_libc_symbols:cplus_strcpy
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
```

Sub-results worth calling out:

- `abi_compat_pointers: 20/20 pointer ABI checks match`
  (`build/abi/compat/pointers.out`).
- `abi_compat_layouts: 80/80 layouts match`
  (`build/abi/compat/layouts.out`).
- T3a and T3b prove both boundary directions with real object files and
  archives: ordinary C calls functions emitted by the C+ driver, and the C+
  driver links and calls a precompiled C archive.
- T5 confirms every expected exported symbol is present under its exact name,
  and `T5_no_cxx_mangling` passes on all three `nm` outputs.
- T6 force-includes `cplus_libc.h` and `cplus_libc_safe.h` into an ordinary C
  layout probe; `T6_neutral_diff` passes and the two outputs are identical in
  size (1822 bytes each), showing the headers do not perturb C layouts.
- `build/abi/compat/results.log` ends with
  `RESULT: abi_compat: 69 passed, 0 failed`.

### 1.4 `CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh`

```
clang: warning: build/libc/libcplus_hardened.a: 'linker' input unused [-Wunused-command-line-argument]
clang: warning: build/safe/libcplus_safecrt.a: 'linker' input unused [-Wunused-command-line-argument]
abi_layouts: 70/70 layouts match
```

Exit 0. The two clang warnings are benign: the driver forwards the archive
paths to both its compile step (where they are unused inputs) and its link
step (where they are consumed). Both host and C+ executables print the same
final line, `abi_layouts: 70/70 layouts match`
(`build/abi/host.out` and `build/abi/cplus.out`), and the script's `diff -u`
between them is empty.

---

## 2. What the fingerprint contains

The capture used Apple clang 16 (`Apple clang version 16.0.0
(clang-1600.0.26.6)`), target `arm64-apple-darwin27.2.0` — an LP64 host with
8-byte pointers. Record counts by section (1162 total):

| Section | Meaning | Records |
|---|---|---:|
| `V` | tool/format metadata | 4 |
| `L` | layout facts (`sizeof`, `_Alignof`, `offsetof`, enum values) | 89 |
| `C` | ABI-relevant compiler defines | 27 |
| `F` | compile-time function-signature assertions | 12 |
| `S` | exported-symbol names from the two built archives | 1030 |

Representative invariants from the `L` section (all unchanged from the
authoritative baseline):

```
L	size void *	8
L	align void *	8
L	size cplus_alloc_meta	40
L	size cplus_thrd_t	32
L	size cplus_safe_alloc_meta	40
L	size cplus_safe_stat	48
L	size cplus_safe_platform_info	28
L	value CPLUS_SAFE_LOG_OFF	0
L	value CPLUS_SAFE_CONSTRAINT_IGNORE	0
```

Exported-symbol counts from the `S` section:

```
S	archive build/libc/libcplus_hardened.a	count=784
S	archive build/safe/libcplus_safecrt.a	count=244
```

The `F` records are enforced by `_Static_assert(__builtin_types_compatible_p(...))`
in `tools/abi_compare/abi_fp_probe.c`: a signature change is a **compile
error**, so a captured `F` record is positive proof the twelve checked public
signatures are still the expected ones.

---

## 3. Why the checked layer adds no ABI drift

The checked ordinary-C layer is implemented in headers only:

- `stdlib/safe/safe_c.h` and `libc/install-include/cplus_libc_safe.h` use
  `#undef` + `#define` to remap *ordinary* spellings (`malloc`, `memset`,
  `strcpy`, `fread`, `printf`, …) to checked siblings while **keeping the
  original argument lists and return types** (e.g. `#define memset(d,c,n)
  ((void)cplus_memset_bounded((d),(c),(n),CPLUS_LIBC_OBJ_SIZE(d)))`). These are
  compile-time textual substitutions; they define no data object and export no
  symbol. They are opt-in via header inclusion and can be disabled with
  `CPLUS_SAFE_C_DISABLE` / `CPLUS_LIBC_SAFE_DISABLE`.
- The checked `_s` families (`stdlib/safe/safe_alloc.h`, `safe_memory.h`,
  `safe_string.h`, `safe_io.h`, `safe_format.h`) expose their helpers as
  `static inline`, i.e. **internal linkage**: each translation unit gets a
  private copy, nothing is added to the archive symbol table. Newer standard
  library `static inline` helpers follow the same rule (see
  `docs/MODERN_STDLIB.md:105-113` and `docs/stdlib/abi-preserving-runtime-safety.md:221-232`).
- Capacity inference uses `__builtin_object_size(p, 1)` plus the existing
  out-of-band allocation side table; metadata structs themselves are unchanged.

Consequently each ABI dimension called out in the task is preserved:

| Dimension | Why unchanged | Evidence in this run |
|---|---|---|
| `sizeof(void*)` | No pointer representation or data-model change; no new pointer-sized field in a public struct. | `L size void * = 8`, `L align void * = 8` in the capture; `abi_compat_pointers: 20/20`; T1 PASS. |
| Struct layouts | No field added, removed, reordered, or retyped in a public C-compatible struct; side table stays out-of-band. | All 89 `L` layout/offset/enum records identical to baseline; `abi_compat_layouts: 80/80`; `abi_layouts: 70/70`; T6 neutral diff empty. |
| Function signatures | Macro remaps deliberately preserve argument lists and return types; `static inline` helpers are new names, not edits to existing signatures. | 12 `F` records present and compile-time asserted; fresh capture compiles with `-Werror`; T3a/T3b cross-language calls link and run. |
| Calling conventions | No declaration changed, so no calling-convention change; C and C+ objects interoperate with the platform default convention. | T3a (C calls C+), T3b (C+ calls C), `T6_neutral_diff` all PASS; T5 no C++ mangling on all three `nm` outputs. |
| Existing exported symbols | Macros/`static inline` add nothing to the archive symbol table; no existing `cplus_*` name was renamed or removed. | All 1030 `S` records identical to baseline (784 + 244 symbols); `compare` diff is 0 bytes; all T5 expected-symbol checks PASS. |

In short: the checked layer changes *what a call does at runtime*, not *how a
call is laid out, named, or linked*. That is exactly the class of change this
fingerprint is designed to show as a no-op, and it did.

---

## 4. Mismatch report

**No mismatch appeared.** All four commands succeeded and every comparison was
clean: the fresh fingerprint matched the authoritative baseline byte-for-byte
(identical SHA-256), the comparison diff was empty (0 bytes), the compatibility
suite reported `69 passed, 0 failed`, and the layout suite reported
`70/70 layouts match`. Nothing was hidden, ignored, or allow-listed — no ignore
file was passed to `compare`. The only non-success-looking text in any output
was the two benign clang `-Wunused-command-line-argument` warnings in the
layout script (§1.4), which are a known property of the driver's compile/link
argument forwarding, not an ABI finding.

---

## 5. Additive evidence file

`tests/abi/fingerprints/safe_layer_baseline.txt` is a **copy of the current
fingerprint text** (the `tools/abi_compare/.out/fingerprint.txt` capture from
§1.1), with an added header comment stating that it is *additive evidence and
not a replacement for the authoritative baseline*. The section header note is
verified in the file itself:

- Apart from the added `#` comment lines, the records are byte-identical to
  `tests/abi/fingerprints/baseline.txt` (verified with `diff` after stripping
  the note; `tail -n +12 safe_layer_baseline.txt | diff -u baseline.txt -` is
  empty).
- The added lines are comments beginning with `#`, which
  `tools/abi_compare/fp_to_json.c` explicitly skips; a JSON-converter smoke
  test on the file succeeds.
- The authoritative target for `tools/abi_compare/run.sh compare` remains
  `tests/abi/fingerprints/baseline.txt`. This file is supplied only as extra
  evidence that the checked layer was present and still produced the same
  fingerprint; it is not meant to be substituted as the comparison baseline.

---

## 6. Related evidence

- Canonical fingerprint tooling and record format: `tools/abi_compare/README.md`.
- Authoritative baseline and prior command/result record:
  `tests/abi/fingerprints/baseline.txt`, `baseline.json`, `RESULTS.md`.
- Layer definitions and checked-vs-ordinary families: `docs/libc/NORMAL_VS_CHECKED.md`.
- Ordinary-C mapping activation, guarantees and limits:
  `docs/libc/ORDINARY_C_SAFETY.md`.
- Header-only `static inline` ABI rule: `docs/MODERN_STDLIB.md`,
  `docs/stdlib/abi-preserving-runtime-safety.md`.
- Cross-language ABI suites: `tests/abi/run_abi_compat.sh`,
  `tests/abi/run_abi_layouts.sh`.

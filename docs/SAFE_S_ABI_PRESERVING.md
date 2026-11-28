# Safer `_s` APIs that preserve the C ABI

Status: **shipped, checked slice**. This is the dedicated guide to safer APIs
that can replace an ordinary C call **without changing the exported C ABI**.
The canonical machine-readable source is the `safeSAbiCatalog` object in
[`compiler/Analysis/libc_contracts.json`](../compiler/Analysis/libc_contracts.json);
[`SAFE_S_COMPATIBILITY_MATRIX.md`](SAFE_S_COMPATIBILITY_MATRIX.md) is the
one-page decision table and
[`SAFE_S_NON_ABI_PRESERVING.md`](SAFE_S_NON_ABI_PRESERVING.md) covers the
other category. Do not edit these tables by hand: regenerate them from the
catalog and run `python3 tests/compiler/abi_gate/check_safe_s_catalog.py`.

## What "preserves the C ABI" means here

A shipped `_s` entry is listed as `ABI_PRESERVING_DROP_IN` only when **both**
hold:

1. its parameter types, order and return type match the legacy C spelling on
   the supported LP64 hosts (`rsize_t` is `size_t`; `FILE *` remains `FILE *`),
   or a `stdlib/safe/safe_c.h` source macro keeps the original call shape in the
   including translation unit while supplying the capacity argument internally;
2. it exports no replacement for the legacy symbol. The checked implementation
   has its own `_s` name (or a namespaced `cplus_*` name); existing precompiled
   archives calling `malloc`, `memcpy`, `printf`, ... are untouched.

The `*_s_auto` helpers are additive symbols with an extra `known_dmax`
parameter. They are ABI-preserving **at the call site** through the `safe_c.h`
macro, not as link-time interposition. Link
`build/safe/libcplus_safecrt.a` plus `build/libc/libcplus_hardened.a` (or the
hardened libc alone when using `cplus_libc_safe.h`). Never hand-export a
`*_s_auto` helper under a legacy C name.

## ABI-preserving shipped APIs

| Safer API | Replaces | Mechanism | Exact signature | Preconditions |
| --- | --- | --- | --- | --- |
| `malloc_s` | `malloc` | `direct-symbol` | `void *malloc_s(size_t size)` | Allocation is tracked; call through safe_c.h macros when replacing an existing call site, not at a public C export boundary. |
| `calloc_s` | `calloc` | `direct-symbol` | `void *calloc_s(size_t count, size_t size)` | Allocation is tracked; call through safe_c.h macros when replacing an existing call site, not at a public C export boundary. |
| `realloc_s` | `realloc` | `direct-symbol` | `void *realloc_s(void *ptr, size_t size)` | Allocation is tracked; call through safe_c.h macros when replacing an existing call site, not at a public C export boundary. |
| `free_s` | `free` | `direct-symbol` | `void free_s(void *ptr)` | Allocation is tracked; call through safe_c.h macros when replacing an existing call site, not at a public C export boundary. |
| `aligned_alloc_s` | `aligned_alloc` | `direct-symbol` | `void *aligned_alloc_s(size_t alignment, size_t size)` | Allocation is tracked; call through safe_c.h macros when replacing an existing call site, not at a public C export boundary. |
| `cplus_fopen_s` | `fopen` | `direct-symbol` | `FILE *cplus_fopen_s(const char *name, const char *mode)` | Stream handles are owned by the caller; close with cplus_fclose_s or fclose consistently. |
| `cplus_fclose_s` | `fclose` | `direct-symbol` | `int cplus_fclose_s(FILE *stream)` | Do not close the same stream twice. |
| `cplus_printf_s` | `printf` | `direct-symbol` | `int cplus_printf_s(const char *fmt, ...)` | Format must be a literal (or already trusted); %n is rejected by the compiled adapters. |
| `cplus_fprintf_s` | `fprintf` | `direct-symbol` | `int cplus_fprintf_s(FILE *stream, const char *fmt, ...)` | Format must be a literal (or already trusted); %n is rejected by the compiled adapters. |
| `cplus_scanf_s` | `scanf` | `direct-symbol` | `int cplus_scanf_s(const char *fmt, ...)` | Format must be a literal (or already trusted); %n is rejected by the compiled adapters. |
| `cplus_fscanf_s` | `fscanf` | `direct-symbol` | `int cplus_fscanf_s(FILE *stream, const char *fmt, ...)` | Format must be a literal (or already trusted); %n is rejected by the compiled adapters. |
| `cplus_sscanf_s` | `sscanf` | `direct-symbol` | `int cplus_sscanf_s(const char *str, const char *fmt, ...)` | Format must be a literal (or already trusted); %n is rejected by the compiled adapters. |
| `cplus_snprintf_s` | `snprintf` | `direct-symbol` | `int cplus_snprintf_s(char *dest, size_t dmax, const char *fmt, ...)` | Format must be a literal (or already trusted); %n is rejected by the compiled adapters. dmax must be the real destination capacity. |
| `cplus_vsnprintf_s` | `vsnprintf` | `direct-symbol` | `int cplus_vsnprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap)` | Format must be a literal (or already trusted); %n is rejected by the compiled adapters. dmax must be the real destination capacity. |
| `cplus_strnlen_s` | `strnlen` | `direct-symbol` | `rsize_t cplus_strnlen_s(const char *str, rsize_t smax)` | smax must not exceed the readable extent of str. |
| `cplus_memcpy_s_auto` | `memcpy` | `source-macro` | `void *cplus_memcpy_s_auto(void *dest, const void *src, size_t n, size_t known_dmax)` | Destination capacity must be proven by __builtin_object_size or tracked allocation metadata; unknown capacity is refused in strict mode. |
| `cplus_memmove_s_auto` | `memmove` | `source-macro` | `void *cplus_memmove_s_auto(void *dest, const void *src, size_t n, size_t known_dmax)` | Destination capacity must be proven by __builtin_object_size or tracked allocation metadata; unknown capacity is refused in strict mode. |
| `cplus_memset_s_auto` | `memset` | `source-macro` | `void *cplus_memset_s_auto(void *dest, int value, size_t n, size_t known_dmax)` | Destination capacity must be proven by __builtin_object_size or tracked allocation metadata; unknown capacity is refused in strict mode. |
| `cplus_memcmp_s_auto` | `memcmp` | `source-macro` | `int cplus_memcmp_s_auto(const void *s1, const void *s2, size_t n, size_t known_s1, size_t known_s2)` | Destination capacity must be proven by __builtin_object_size or tracked allocation metadata; unknown capacity is refused in strict mode. |
| `cplus_strcpy_s_auto` | `strcpy` | `source-macro` | `char *cplus_strcpy_s_auto(char *dest, const char *src, size_t known_dmax)` | Destination capacity must be proven by __builtin_object_size or tracked allocation metadata; unknown capacity is refused in strict mode. |
| `cplus_strncpy_s_auto` | `strncpy` | `source-macro` | `char *cplus_strncpy_s_auto(char *dest, const char *src, size_t n, size_t known_dmax)` | Destination capacity must be proven by __builtin_object_size or tracked allocation metadata; unknown capacity is refused in strict mode. |
| `cplus_strcat_s_auto` | `strcat` | `source-macro` | `char *cplus_strcat_s_auto(char *dest, const char *src, size_t known_dmax)` | Destination capacity must be proven by __builtin_object_size or tracked allocation metadata; unknown capacity is refused in strict mode. |
| `cplus_strncat_s_auto` | `strncat` | `source-macro` | `char *cplus_strncat_s_auto(char *dest, const char *src, size_t n, size_t known_dmax)` | Destination capacity must be proven by __builtin_object_size or tracked allocation metadata; unknown capacity is refused in strict mode. |

### Source-macro stream helpers

`cplus_fread` and `cplus_fwrite` are declared in
`libc/install-include/cplus_libc.h` and implemented in
`build/libc/libcplus_hardened.a`. `stdlib/safe/safe_c.h` maps `fread`/`fwrite`
onto these helpers so the ordinary C parameter list and return type are kept in
the including translation unit. They are not `_s` symbols and export no legacy
name.

| Helper | Legacy call shape | Signature |
| --- | --- | --- |
| `cplus_fread` | `fread` | `size_t cplus_fread(void *buf, size_t size, size_t count, FILE *stream)` |
| `cplus_fwrite` | `fwrite` | `size_t cplus_fwrite(const void *buf, size_t size, size_t count, FILE *stream)` |

## Linking and preconditions

- Include `stdlib/safe/safe_c.h` first (or force-include it) to map ordinary
  spellings in a single translation unit. Define `CPLUS_SAFE_C_DISABLE` for
  declarations only; a precompiled `.a` built without the header keeps its
  original calls.
- Allocation entries (`malloc_s`, `calloc_s`, `realloc_s`, `free_s`,
  `aligned_alloc_s`) are tracked. Pass legitimate pointers only; interior
  pointers resolve to remaining capacity.
- `*_s_auto` entries require a proven capacity
  (`__builtin_object_size` or tracked metadata). In default mode unknown
  capacity keeps raw behaviour with a warning; strict mode refuses.
- `cplus_printf_s`, `cplus_fprintf_s`, `cplus_scanf_s`, `cplus_fscanf_s`,
  `cplus_sscanf_s`, `cplus_snprintf_s`, `cplus_vsnprintf_s` and
  `cplus_strnlen_s` are type-compatible symbol names, not source renames.
  Format adapters reject non-literal formats and `%n`; use them intentionally.
- ABI evidence: `sh tests/compiler/abi_gate/run.sh` (layout, symbols, calling
  conventions, `tests/abi/c_to_cp/`, `tests/abi/precompiled/`) and
  `sh tests/abi/host_abi_purity/run.sh` (host-type parity with and without
  headers). See [`docs/abi/ABI_GATE.md`](abi/ABI_GATE.md).

## Non-claims

Signature parity is not a proof of pointer provenance, lifetime or alias
safety, and it does not make a checked function equivalent for invalid input.
Annex K spellings such as `printf_s`, `scanf_s`, `fopen_s` and `snprintf_s` are
**not** exported under those names; see the companion guide.

### Source-macro stream helpers

`cplus_fread` and `cplus_fwrite` are declared in
`libc/install-include/cplus_libc.h`, implemented in
`build/libc/libcplus_hardened.a`, and mapped from the ordinary `fread` /
`fwrite` spellings by `stdlib/safe/safe_c.h`. They keep the legacy C parameter
list and return type in the including translation unit and are catalogued in
`sourceHelpers` rather than `symbols` because they are not `_s` names.

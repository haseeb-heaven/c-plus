# Safer `_s` APIs that do NOT preserve the original C ABI

Status: **shipped, checked slice**. This is the dedicated guide to safer APIs
whose signature differs from the legacy C call. Calling them at an exported
C boundary changes parameters, return type or out-parameters and therefore
breaks consumers that were linked against the original symbol. The canonical
dataset is the `safeSAbiCatalog` object in
[`compiler/Analysis/libc_contracts.json`](../compiler/Analysis/libc_contracts.json);
the drop-in category is in [`SAFE_S_ABI_PRESERVING.md`](SAFE_S_ABI_PRESERVING.md)
and the quick table is [`SAFE_S_COMPATIBILITY_MATRIX.md`](SAFE_S_COMPATIBILITY_MATRIX.md).

## Why the ABI differs

Annex K style `_s` functions commonly add an explicit `rsize_t dmax` (or
`size_t bmax`) capacity, return `errno_t` where C returned a pointer or `void`,
replace a return value with an out-parameter, or add a `context` argument to a
comparator. `errno_t`/`rsize_t` are integer typedefs, so the *pointer width* is
unchanged, but the **number, order and meaning of arguments** is not. A caller
compiled against legacy C will read the wrong registers/stack slots.

Rule: keep the original symbol at every public C boundary; call the checked
API only in code you control, or wrap it behind a C-ABI-preserving function
with the legacy signature.

## Shipped `_s` APIs that change the signature

| Safer API | Legacy shape replaced | Exact signature | Why it differs |
| --- | --- | --- | --- |
| `free_checked_s` | `free` | `int free_checked_s(void *ptr)` | Returns an int status instead of void and refuses invalid releases; it is an additional API, not a drop-in replacement for free. |
| `free_checked_generation_s` | `free` | `int free_checked_generation_s(void *ptr, unsigned long long generation)` | Adds a generation argument and an int status; pointer-ABI only, not a replacement at a public C boundary. |
| `cplus_memcpy_s` | `memcpy` | `errno_t cplus_memcpy_s(void *dest, rsize_t dmax, const void *src, rsize_t smax)` | Adds rsize_t dmax/smax and returns errno_t instead of void *. |
| `cplus_memmove_s` | `memmove` | `errno_t cplus_memmove_s(void *dest, rsize_t dmax, const void *src, rsize_t smax)` | Adds rsize_t dmax/smax and returns errno_t instead of void *. |
| `cplus_memset_s` | `memset` | `errno_t cplus_memset_s(void *dest, rsize_t dmax, int value, rsize_t n)` | Adds rsize_t dmax and returns errno_t instead of void *. |
| `cplus_memcmp_s` | `memcmp` | `errno_t cplus_memcmp_s(const void *dest, rsize_t dmax, const void *src, rsize_t smax, int *diff)` | Adds two capacities and an out-parameter; returns errno_t instead of the comparison result. |
| `cplus_memzero_s` | `memset` | `errno_t cplus_memzero_s(void *dest, rsize_t dmax)` | Additive zeroing API with explicit dmax and errno_t; no legacy counterpart. |
| `cplus_fread_s` | `fread` | `size_t cplus_fread_s(void *buf, size_t bmax, size_t size, size_t count, FILE *stream)` | Adds a bmax capacity argument before size/count. |
| `cplus_fwrite_s` | `fwrite` | `size_t cplus_fwrite_s(const void *buf, size_t bmax, size_t size, size_t count, FILE *stream)` | Adds a bmax capacity argument before size/count. |
| `cplus_fgets_s` | `fgets` | `char *cplus_fgets_s(char *buf, size_t bmax, int n, FILE *stream)` | Adds a size_t bmax argument and changes the parameter order. |
| `cplus_getenv_s` | `getenv` | `int cplus_getenv_s(size_t *len, char *value, size_t valuesz, const char *name)` | Out-parameters plus int status replace the returned char *. |
| `cplus_sprintf_s` | `sprintf` | `int cplus_sprintf_s(char *dest, size_t dmax, const char *fmt, ...)` | Adds the destination capacity dmax. |
| `cplus_vsprintf_s` | `vsprintf` | `int cplus_vsprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap)` | Adds the destination capacity dmax. |
| `cplus_strcpy_s` | `strcpy` | `errno_t cplus_strcpy_s(char *dest, rsize_t dmax, const char *src)` | Adds rsize_t dmax and returns errno_t instead of char *. |
| `cplus_strncpy_s` | `strncpy` | `errno_t cplus_strncpy_s(char *dest, rsize_t dmax, const char *src, rsize_t slen)` | Adds rsize_t dmax and returns errno_t instead of char *. |
| `cplus_strcat_s` | `strcat` | `errno_t cplus_strcat_s(char *dest, rsize_t dmax, const char *src)` | Adds rsize_t dmax and returns errno_t instead of char *. |
| `cplus_strncat_s` | `strncat` | `errno_t cplus_strncat_s(char *dest, rsize_t dmax, const char *src, rsize_t slen)` | Adds rsize_t dmax and returns errno_t instead of char *. |
| `cplus_strcmp_s` | `strcmp` | `errno_t cplus_strcmp_s(const char *dest, rsize_t dmax, const char *src, int *indicator)` | Adds dmax and an out-parameter; returns errno_t instead of the comparison result. |
| `cplus_strtok_s` | `strtok` | `char *cplus_strtok_s(char *dest, rsize_t *dmax, const char *delim, char **ptr)` | Adds rsize_t *dmax and an explicit save pointer. |
| `cplus_strerror_s` | `strerror` | `errno_t cplus_strerror_s(char *dest, rsize_t dmax, int errnum)` | Adds an output buffer/capacity and returns errno_t instead of char *. |
| `cplus_v_memchr_s` | `memchr` | `errno_t cplus_v_memchr_s(const void *dest, rsize_t dmax, int ch, void **resultp)` | Adds dmax and an out-parameter; returns errno_t instead of void *. |
| `cplus_v_memccpy_s` | `memccpy` | `errno_t cplus_v_memccpy_s(void *dest, rsize_t dmax, const void *src, int c, rsize_t n)` | Adds dmax and returns errno_t instead of void *. |
| `cplus_sc_upstream_bsearch_s` | `bsearch` | `void *cplus_sc_upstream_bsearch_s(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *, void *), void *context)` | Adds the Annex K context argument and changes the comparator type; the 5-argument legacy shape is not exported under this name. |
| `cplus_sc_upstream_memrchr_s` | `memrchr` | `int cplus_sc_upstream_memrchr_s(const void *dest, size_t dmax, int ch, void **resultp)` | Adds an explicit dmax/out-parameter and returns int status instead of void *. |

## Annex K spellings that are not shipped

These names are documented so a migration does not assume they are linkable.
Aliasing any of them to a different signature would be an ABI break; the
shipped adapter is named `cplus_*_s` where one exists.

| Annex K spelling | Status | Reason |
| --- | --- | --- |
| `printf_s` | not shipped | Annex K adds runtime constraints; the shipped type-compatible adapter is cplus_printf_s. |
| `fprintf_s` | not shipped | Annex K adds runtime constraints; the shipped type-compatible adapter is cplus_fprintf_s. |
| `sprintf_s` | not shipped | Adds rsize_t dmax and errno_t; not exported under this spelling. |
| `snprintf_s` | not shipped | Annex K spelling is not exported; cplus_snprintf_s has type parity but stricter format checks. |
| `vsprintf_s` | not shipped | Adds dmax; not exported under this spelling. |
| `scanf_s` | not shipped | Adds size arguments for %s/%c; not exported under this spelling. |
| `fscanf_s` | not shipped | Adds size arguments for %s/%c; not exported under this spelling. |
| `sscanf_s` | not shipped | Adds size arguments for %s/%c; not exported under this spelling. |
| `fopen_s` | not shipped | Returns errno_t and writes through FILE **; not exported under this spelling. |
| `fread_s` | not shipped | Adds bmax; not exported under this spelling. |
| `fwrite_s` | not shipped | Adds bmax; not exported under this spelling. |
| `gmtime_s` | not shipped | Adds a caller-supplied struct tm *; not shipped. |
| `localtime_s` | not shipped | Adds a caller-supplied struct tm *; not shipped. |
| `qsort_s` | not shipped | Adds a context argument and errno_t; not shipped. |
| `bsearch_s` | not shipped | Macro maps to cplus_sc_upstream_bsearch_s with context; no 5-argument bsearch_s symbol exists. |
| `mbrtowc_s` | not shipped | Adds dmax/out-parameters; not shipped. |
| `getenv_s` | not shipped | Out-parameters + errno_t; shipped spelling is cplus_getenv_s. |
| `strtok_s` | not shipped | Shipped spelling is cplus_strtok_s with rsize_t *dmax and a save pointer. |
| `strerror_s` | not shipped | Shipped spelling is cplus_strerror_s with dmax and errno_t. |
| `memchr_s` | not shipped | No compiled symbol; cplus_v_memchr_s is the checked vendor API. |
| `memccpy_s` | not shipped | No compiled symbol; cplus_v_memccpy_s is the checked vendor API. |

## Wrapping pattern

```c
/* Public C ABI stays exactly as before. */
int exported_copy(char *dst, size_t cap, const char *src) {
    return cplus_strcpy_s(dst, (rsize_t)cap, src) == 0 ? 0 : -1;
}
```

Use `-Wcplus-suggest-safe-s` (`CPLUS-SUGGEST-SAFE-S`) at the call site: every
suggestion is tagged `ABI_PRESERVING_DROP_IN` or `ABI_CHANGING_ANNEX_K`, and
when the destination extent is known the compiler can offer a
`sizeof(dst)`-filled fix-it. The catalog and these docs are checked together by
`python3 tests/compiler/abi_gate/check_safe_s_catalog.py`.

## Non-claims

No linker interposition is performed; no legacy symbol is rewritten in a
precompiled archive. Runtime constraint codes are documented in
[`SAFE_CRT.md`](SAFE_CRT.md) and are not a substitute for a correct ABI
boundary.

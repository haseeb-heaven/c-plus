# Safer `_s` compatibility matrix

One-page decision table for `-Wcplus-suggest-safe-s`
(`CPLUS-SUGGEST-SAFE-S`). Every legacy call name below is a key in the
`suggestions` map of the `safeSAbiCatalog` in
[`compiler/Analysis/libc_contracts.json`](../compiler/Analysis/libc_contracts.json).
`Keep legacy symbol` means: do not replace the exported C function; call the
checked API only inside your own translation units.

| Legacy C API | ABI-preserving safer option | Annex K / signature-changing option | Preserves C ABI? | Header / archive |
| --- | --- | --- | --- | --- |
| `strcpy` | `cplus_strcpy_s_auto` | `cplus_strcpy_s` | NO — keep legacy symbol | stdlib/safe/safe_string.h (build/safe/libcplus_safecrt.a); stdlib/safe/safe_string.h (build/safe/libcplus_safecrt.a) |
| `strncpy` | `cplus_strncpy_s_auto` | `cplus_strncpy_s` | NO — keep legacy symbol | stdlib/safe/safe_string.h (build/safe/libcplus_safecrt.a); stdlib/safe/safe_string.h (build/safe/libcplus_safecrt.a) |
| `strcat` | `cplus_strcat_s_auto` | `cplus_strcat_s` | NO — keep legacy symbol | stdlib/safe/safe_string.h (build/safe/libcplus_safecrt.a); stdlib/safe/safe_string.h (build/safe/libcplus_safecrt.a) |
| `strncat` | `cplus_strncat_s_auto` | `cplus_strncat_s` | NO — keep legacy symbol | stdlib/safe/safe_string.h (build/safe/libcplus_safecrt.a); stdlib/safe/safe_string.h (build/safe/libcplus_safecrt.a) |
| `memcpy` | `cplus_memcpy_s_auto` | `cplus_memcpy_s` | NO — keep legacy symbol | stdlib/safe/safe_memory.h (build/safe/libcplus_safecrt.a); stdlib/safe/safe_memory.h (build/safe/libcplus_safecrt.a) |
| `memmove` | `cplus_memmove_s_auto` | `cplus_memmove_s` | NO — keep legacy symbol | stdlib/safe/safe_memory.h (build/safe/libcplus_safecrt.a); stdlib/safe/safe_memory.h (build/safe/libcplus_safecrt.a) |
| `memset` | `cplus_memset_s_auto` | `cplus_memset_s` | NO — keep legacy symbol | stdlib/safe/safe_memory.h (build/safe/libcplus_safecrt.a); stdlib/safe/safe_memory.h (build/safe/libcplus_safecrt.a) |
| `memcmp` | `cplus_memcmp_s_auto` | `cplus_memcmp_s` | YES for `cplus_memcmp_s_auto` | stdlib/safe/safe_memory.h (build/safe/libcplus_safecrt.a); stdlib/safe/safe_memory.h (build/safe/libcplus_safecrt.a) |
| `sprintf` | — | `cplus_sprintf_s` | NO — keep legacy symbol | stdlib/safe/safe_format.h (build/safe/libcplus_safecrt.a) |
| `vsprintf` | — | `cplus_vsprintf_s` | NO — keep legacy symbol | stdlib/safe/safe_format.h (build/safe/libcplus_safecrt.a) |
| `snprintf` | `cplus_snprintf_s` | — | YES for `cplus_snprintf_s` | stdlib/safe/safe_format.h (build/safe/libcplus_safecrt.a) |
| `vsnprintf` | `cplus_vsnprintf_s` | — | YES for `cplus_vsnprintf_s` | stdlib/safe/safe_format.h (build/safe/libcplus_safecrt.a) |
| `printf` | `cplus_printf_s` | — | YES for `cplus_printf_s` | stdlib/safe/safe_format.h (build/safe/libcplus_safecrt.a) |
| `fprintf` | `cplus_fprintf_s` | — | YES for `cplus_fprintf_s` | stdlib/safe/safe_format.h (build/safe/libcplus_safecrt.a) |
| `scanf` | `cplus_scanf_s` | — | YES for `cplus_scanf_s` | stdlib/safe/safe_format.h (build/safe/libcplus_safecrt.a) |
| `fscanf` | `cplus_fscanf_s` | — | YES for `cplus_fscanf_s` | stdlib/safe/safe_format.h (build/safe/libcplus_safecrt.a) |
| `sscanf` | `cplus_sscanf_s` | — | YES for `cplus_sscanf_s` | stdlib/safe/safe_format.h (build/safe/libcplus_safecrt.a) |
| `fopen` | `cplus_fopen_s` | — | YES for `cplus_fopen_s` | stdlib/safe/safe_io.h (build/safe/libcplus_safecrt.a) |
| `fclose` | `cplus_fclose_s` | — | YES for `cplus_fclose_s` | stdlib/safe/safe_io.h (build/safe/libcplus_safecrt.a) |
| `fread` | `cplus_fread` | `cplus_fread_s` | NO — keep legacy symbol | libc/install-include/cplus_libc.h (build/libc/libcplus_hardened.a); stdlib/safe/safe_io.h (build/safe/libcplus_safecrt.a) |
| `fwrite` | `cplus_fwrite` | `cplus_fwrite_s` | NO — keep legacy symbol | libc/install-include/cplus_libc.h (build/libc/libcplus_hardened.a); stdlib/safe/safe_io.h (build/safe/libcplus_safecrt.a) |
| `fgets` | — | `cplus_fgets_s` | NO — keep legacy symbol | stdlib/safe/safe_io.h (build/safe/libcplus_safecrt.a) |
| `strtok` | — | `cplus_strtok_s` | NO — keep legacy symbol | stdlib/safe/safe_string.h (build/safe/libcplus_safecrt.a) |
| `strerror` | — | `cplus_strerror_s` | NO — keep legacy symbol | stdlib/safe/safe_string.h (build/safe/libcplus_safecrt.a) |
| `getenv` | — | `cplus_getenv_s` | NO — keep legacy symbol | stdlib/safe/safe_io.h (build/safe/libcplus_safecrt.a) |
| `gmtime` | — | `gmtime_s` | NO — keep legacy symbol | not shipped |
| `localtime` | — | `localtime_s` | NO — keep legacy symbol | not shipped |
| `qsort` | — | `qsort_s` | NO — keep legacy symbol | not shipped |
| `mbrtowc` | — | `mbrtowc_s` | NO — keep legacy symbol | not shipped |
| `bsearch` | — | `cplus_sc_upstream_bsearch_s` | NO — keep legacy symbol | libc/install-include/cplus_safecrt_upstream.h (build/libc/libcplus_hardened.a) |
| `malloc` | `malloc_s` | — | YES for `malloc_s` | stdlib/safe/safe_alloc.h (build/safe/libcplus_safecrt.a) |
| `calloc` | `calloc_s` | — | YES for `calloc_s` | stdlib/safe/safe_alloc.h (build/safe/libcplus_safecrt.a) |
| `realloc` | `realloc_s` | — | YES for `realloc_s` | stdlib/safe/safe_alloc.h (build/safe/libcplus_safecrt.a) |
| `free` | `free_s` | — | YES for `free_s` | stdlib/safe/safe_alloc.h (build/safe/libcplus_safecrt.a) |
| `aligned_alloc` | `aligned_alloc_s` | — | YES for `aligned_alloc_s` | stdlib/safe/safe_alloc.h (build/safe/libcplus_safecrt.a) |
| `memchr` | — | `cplus_v_memchr_s` | NO — keep legacy symbol | libc/install-include/cplus_vendor_mem.h (build/libc/libcplus_hardened.a) |
| `memccpy` | — | `cplus_v_memccpy_s` | NO — keep legacy symbol | libc/install-include/cplus_vendor_mem.h (build/libc/libcplus_hardened.a) |

## Tags printed with every diagnostic

- `ABI_PRESERVING_DROP_IN`: the suggested name is type/source-shape compatible;
  it is safe at an exported boundary (subject to the header preconditions).
- `ABI_CHANGING_ANNEX_K`: the suggested name adds capacity/return/out/context
  arguments; keep the legacy symbol at the public boundary.

When the destination extent is statically known (`char dst[64]`, tracked
`malloc(N)`), the catalog carries a `fixItKnownExtent` that fills the capacity
with `sizeof(dst)` or equivalent. Otherwise the fix-it is a template and the
developer must supply a proven capacity.

Dedicated guides: [`SAFE_S_ABI_PRESERVING.md`](SAFE_S_ABI_PRESERVING.md),
[`SAFE_S_NON_ABI_PRESERVING.md`](SAFE_S_NON_ABI_PRESERVING.md).
Boundary contract and CI: [`docs/abi/ABI_GATE.md`](abi/ABI_GATE.md),
[`docs/abi/STRICT_C_ABI_BOUNDARY.md`](abi/STRICT_C_ABI_BOUNDARY.md).
Sync check: `python3 tests/compiler/abi_gate/check_safe_s_catalog.py`.

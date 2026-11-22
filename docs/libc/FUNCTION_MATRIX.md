# libc/safe CRT function evidence matrix

Generated from local source/test scan. "10 scenarios" means covered by 10+ scenario suites; "fuzz" means exercised in fuzz harness; "logs" means log call present in implementation/wrapper.

| Function | 10+ scenarios | Fuzz/stress | Logs in impl | Runtime contract | Compile-time/Rust-like proof |
|---|---:|---:|---:|---|---|
| `calloc_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_calloc` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_clearerr` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_fclose` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_fclose_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_fflush` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_fopen_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_fprintf` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_fprintf_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_fputs` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_fread_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_free` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_fscanf` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_fscanf_s` | yes | no | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_fwrite` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_fwrite_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_malloc` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_memchr` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_memcmp` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_memcmp_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_memcpy` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_memcpy_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_memmove` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_memmove_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_memrchr` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_memset` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_memset_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_memzero_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_printf_s` | yes | no | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_putc` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_realloc` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_rename` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_safe_log` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_safe_log_get_level` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_safe_log_set_level` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_safe_platform_probe` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_scanf_s` | yes | no | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_snprintf_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_sprintf_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strcasecmp` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strcat` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strcat_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strchr` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strcmp` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strcmp_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strcpy` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strcpy_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strdup` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strlen` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strncasecmp` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strncat` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strncat_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strncmp` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strncpy` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strncpy_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strnlen` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strnlen_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strrchr` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strstr` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_tolower` | yes | no | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_vsnprintf` | yes | yes | yes | runtime tests only | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_vsnprintf_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_vsprintf_s` | yes | no | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `free_s` | yes | yes | yes | checked/defined errors | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `free_checked_s` | yes | yes | yes | checked/defined errors + double/invalid-free refusal | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `aligned_alloc_s` | yes | no | yes | alignment + size bounds checked | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `malloc_s` | yes | yes | yes | checked/defined errors + metadata table | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `realloc_s` | yes | yes | yes | checked/preserves owner + metadata table | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_strtok_s` | yes | yes | yes | bounded token state (`strtok_s.c` fixed) | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_safe_alloc_get_meta` / `query_length` / `transfer_ownership` | yes | yes | yes | thread-safe metadata + generation + ownership | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_alloc_get_meta` / `query_length` / `transfer_ownership` / `cplus_free_checked` | yes | yes | yes | thread-safe libc metadata + double/invalid-free | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |
| `cplus_runtime_guard` / `cplus_runtime_push_cleanup` | yes | no | yes | LIFO cleanup unwind + failstop mode | NOT PROVEN (requires Agent 1 compiler contracts/enforcement) |

## Gaps
- Compile-time/Rust-like safety is NOT proven by libc alone; Agent 1 compiler must enforce contracts/lifetimes/provenance.
- Raw C UB (alias UAF, double free, OOB, overlapping memcpy, invalid provenance, races, unknown-capacity pointers) remains unsafe boundary.
- Full ISO C/glibc surface not implemented.

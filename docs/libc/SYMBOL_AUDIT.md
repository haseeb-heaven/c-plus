# C+ libc / safe CRT — symbol, test & logging audit

Read-only audit of every exported `cplus_*` symbol in `libc/install-include/cplus_libc.h` and `stdlib/safe/*.h`, plus the four `*_s` allocation adapters (`malloc_s`, `calloc_s`, `realloc_s`, `free_s`).

Scope: only callable symbols (a name immediately followed by `(` in a header) are audited. Enum/struct type names (`cplus_safe_log_level`, `cplus_safe_constraint_mode`, `cplus_safe_stat`, `cplus_safe_platform_info`) and the `CPLUS_*`/`SAFE_*` macros are declarations, not linkable symbols.

| Snapshot | value |
|---|---|
| branch | `agent/modern-stdlib` |
| HEAD | `9a0b179` |
| generated | 2026-11-22 05:51 |
| symbols audited | 233 |

Header fingerprints (sha256, first 16 hex) at audit time — the working tree was being edited concurrently, so these pin the audited revision:

| header | sha256[:16] |
|---|---|
| `libc/install-include/cplus_libc.h` | `895c5da3f3d37039` |
| `stdlib/safe/safe_alloc.h` | `789d6d8d6f08960b` |
| `stdlib/safe/safe_error.h` | `47bb04ef36f488be` |
| `stdlib/safe/safe_except.h` | `799808a1ae0bbb5c` |
| `stdlib/safe/safe_format.h` | `6b010b3888e1b650` |
| `stdlib/safe/safe_io.h` | `96ac75b3ebcfe397` |
| `stdlib/safe/safe_log.h` | `8ba091656bf26821` |
| `stdlib/safe/safe_memory.h` | `b4e133db5798d5ed` |
| `stdlib/safe/safe_platform.h` | `1da4d299ddfe0779` |
| `stdlib/safe/safe_stats.h` | `fa2780552ffdfcf7` |
| `stdlib/safe/safe_string.h` | `3f730529b3544f92` |

<details><summary>Uncommitted working-tree changes at audit time</summary>

```
M libc/CMakeLists.txt
 M libc/install-include/cplus_libc.h
 M tests/libc/build_libc.sh
?? docs/libc/SYMBOL_AUDIT.md
?? libc/src/math/math3.c
?? libc/src/stdlib/stdlib_extra5.c
?? libc/src/string/string_extra5.c
?? tests/crt/adversarial/test_bugclass_format.c
?? tests/crt/adversarial/test_bugclass_heap_overflow.c
?? tests/crt/adversarial/test_bugclass_int_overflow_alloc.c
?? tests/crt/adversarial/test_bugclass_null.c
?? tests/crt/adversarial/test_bugclass_overlap.c
?? tests/crt/adversarial/test_bugclass_stack_overflow.c
?? tests/crt/adversarial/test_bugclass_string_term.c
?? tests/crt/adversarial/test_bugclass_uaf_handle.c
?? tests/crt/adversarial/test_bugclass_uninit.c
?? tests/libc/test_math_extra5.c
?? tests/libc/test_stdlib_extra5.c
?? tests/libc/test_string_extra5.c
```
</details>

## Method

- **implemented** — the symbol has a function definition (`name(args) {` … matching `}`) in `libc/src/**`, `stdlib/safe/src/**`, or `libc/log/**` (fallback: any textual occurrence in those trees).
- **tested** — the symbol is *called* by at least one non-fuzz test under `tests/**` (comments and bare prototype declarations do not count; `tests/out/**` and the fuzz harnesses are excluded).
- **fuzzed** — the symbol is referenced in `tests/libc/fuzz_libc.c`, `tests/crt/fuzz/*`, or `tests/stdlib/fuzz/*`.
- **logs** — the symbol's own function body calls `cplus_libc_log` or `cplus_safe_log`. The two logger primitives themselves are counted as `logs` by definition.

## Summary

- **233** exported symbols audited.
- **233/233** implemented.
- **230/233** called by a non-fuzz test  (**3** lack a real test call; 3 of these appear in tests only as prototype declarations).
- **79/233** fuzz-exercised.
- **225/233** log via `cplus_libc_log`/`cplus_safe_log`  (**8** implementations emit no log).

## Symbol table

| symbol | implemented | tested | fuzzed | logs |
|---|:---:|:---:|:---:|:---:|
| `cplus_malloc` | Y | Y | Y | Y |
| `cplus_calloc` | Y | Y | Y | Y |
| `cplus_realloc` | Y | Y | Y | Y |
| `cplus_free` | Y | Y | Y | Y |
| `cplus_memcpy` | Y | Y | Y | Y |
| `cplus_memmove` | Y | Y | Y | Y |
| `cplus_memset` | Y | Y | Y | Y |
| `cplus_memcmp` | Y | Y | Y | Y |
| `cplus_memchr` | Y | Y | Y | Y |
| `cplus_strlen` | Y | Y | Y | Y |
| `cplus_strnlen` | Y | Y | Y | Y |
| `cplus_strcpy` | Y | Y | Y | Y |
| `cplus_strncpy` | Y | Y | Y | Y |
| `cplus_strcat` | Y | Y | Y | Y |
| `cplus_strncat` | Y | Y | Y | Y |
| `cplus_strcmp` | Y | Y | Y | Y |
| `cplus_strncmp` | Y | Y | Y | Y |
| `cplus_strchr` | Y | Y | Y | Y |
| `cplus_strrchr` | Y | Y | Y | Y |
| `cplus_strstr` | Y | Y | Y | Y |
| `cplus_memrchr` | Y | Y | Y | Y |
| `cplus_strcasecmp` | Y | Y | Y | Y |
| `cplus_strsignal` | Y | Y | - | Y |
| `cplus_strcoll` | Y | Y | - | Y |
| `cplus_strxfrm` | Y | Y | - | Y |
| `cplus_strncasecmp` | Y | Y | Y | Y |
| `cplus_strdup` | Y | Y | Y | Y |
| `cplus_strerror` | Y | Y | - | Y |
| `cplus_strtok` | Y | Y | - | Y |
| `cplus_strndup` | Y | Y | - | Y |
| `cplus_memccpy` | Y | Y | - | Y |
| `cplus_stpcpy` | Y | Y | - | Y |
| `cplus_stpncpy` | Y | Y | - | Y |
| `cplus_strtok_r` | Y | Y | Y | Y |
| `cplus_strsep` | Y | Y | - | Y |
| `cplus_memmem` | Y | Y | Y | Y |
| `cplus_swab` | Y | Y | - | Y |
| `cplus_div` | Y | Y | - | Y |
| `cplus_ldiv` | Y | Y | - | Y |
| `cplus_lldiv` | Y | Y | - | Y |
| `cplus_llabs` | Y | Y | - | Y |
| `cplus_atoll` | Y | Y | - | Y |
| `cplus_strtoll` | Y | Y | - | Y |
| `cplus_strtoull` | Y | Y | - | Y |
| `cplus_strtod` | Y | Y | - | Y |
| `cplus_atof` | Y | Y | - | Y |
| `cplus_strtof` | Y | Y | - | Y |
| `cplus_strtold` | Y | Y | - | Y |
| `cplus_setenv` | Y | Y | - | Y |
| `cplus_unsetenv` | Y | Y | - | Y |
| `cplus_putenv` | Y | **N** | - | Y |
| `cplus_rand_r` | Y | Y | - | Y |
| `cplus_imaxabs` | Y | Y | - | Y |
| `cplus_imaxdiv` | Y | Y | - | Y |
| `cplus_strverscmp` | Y | Y | Y | Y |
| `cplus_strcasestr` | Y | Y | - | Y |
| `cplus_strchrnul` | Y | Y | - | Y |
| `cplus_isalpha` | Y | Y | - | Y |
| `cplus_isdigit` | Y | Y | - | Y |
| `cplus_isspace` | Y | Y | - | Y |
| `cplus_isalnum` | Y | Y | - | Y |
| `cplus_isupper` | Y | Y | - | Y |
| `cplus_islower` | Y | Y | - | Y |
| `cplus_isxdigit` | Y | Y | - | Y |
| `cplus_isprint` | Y | Y | - | Y |
| `cplus_isgraph` | Y | Y | - | Y |
| `cplus_ispunct` | Y | Y | - | Y |
| `cplus_iscntrl` | Y | Y | - | Y |
| `cplus_isblank` | Y | Y | - | Y |
| `cplus_isascii` | Y | Y | - | Y |
| `cplus_toascii` | Y | Y | - | Y |
| `cplus_toupper` | Y | Y | - | Y |
| `cplus_tolower` | Y | Y | Y | Y |
| `cplus_abs` | Y | Y | - | Y |
| `cplus_labs` | Y | Y | - | Y |
| `cplus_atoi` | Y | Y | - | Y |
| `cplus_atol` | Y | Y | - | Y |
| `cplus_strtol` | Y | Y | - | Y |
| `cplus_strtoul` | Y | Y | - | Y |
| `cplus_ecvt` | Y | Y | - | Y |
| `cplus_fcvt` | Y | Y | - | Y |
| `cplus_gcvt` | Y | Y | - | Y |
| `cplus_drand48` | Y | Y | - | Y |
| `cplus_erand48` | Y | Y | - | Y |
| `cplus_seed48` | Y | Y | - | Y |
| `cplus_lrand48` | Y | Y | - | Y |
| `cplus_qsort` | Y | Y | - | Y |
| `cplus_bsearch` | Y | Y | - | Y |
| `cplus_rand` | Y | Y | - | Y |
| `cplus_srand` | Y | Y | - | Y |
| `cplus_atexit` | Y | Y | - | Y |
| `cplus_getenv` | Y | Y | - | Y |
| `cplus_system` | Y | Y | - | Y |
| `cplus_fopen` | Y | Y | Y | Y |
| `cplus_fclose` | Y | Y | Y | Y |
| `cplus_fread` | Y | Y | Y | Y |
| `cplus_fwrite` | Y | Y | Y | Y |
| `cplus_snprintf` | Y | Y | Y | Y |
| `cplus_vsnprintf` | Y | Y | Y | Y |
| `cplus_printf` | Y | Y | Y | Y |
| `cplus_fprintf` | Y | Y | Y | Y |
| `cplus_scanf` | Y | Y | - | Y |
| `cplus_fscanf` | Y | Y | Y | Y |
| `cplus_sprintf` | Y | Y | - | Y |
| `cplus_vsprintf` | Y | Y | - | Y |
| `cplus_sscanf` | Y | Y | - | Y |
| `cplus_vsscanf` | Y | Y | - | Y |
| `cplus_vfprintf` | Y | Y | - | Y |
| `cplus_vprintf` | Y | **N** | - | Y |
| `cplus_vfscanf` | Y | **N** | - | Y |
| `cplus_fseek` | Y | Y | Y | Y |
| `cplus_ftell` | Y | Y | Y | Y |
| `cplus_fflush` | Y | Y | Y | Y |
| `cplus_remove` | Y | Y | Y | Y |
| `cplus_rename` | Y | Y | Y | Y |
| `cplus_fgets` | Y | Y | Y | Y |
| `cplus_fputs` | Y | Y | Y | Y |
| `cplus_getc` | Y | Y | Y | Y |
| `cplus_putc` | Y | Y | Y | Y |
| `cplus_feof` | Y | Y | Y | Y |
| `cplus_ferror` | Y | Y | Y | Y |
| `cplus_clearerr` | Y | Y | Y | Y |
| `cplus_putchar` | Y | Y | - | Y |
| `cplus_getchar` | Y | Y | - | Y |
| `cplus_puts` | Y | Y | - | Y |
| `cplus_perror` | Y | Y | - | Y |
| `__cplus_errno_location` | Y | Y | - | **N** |
| `cplus_libc_log_set_enabled` | Y | Y | - | **N** |
| `cplus_wcslen` | Y | Y | - | Y |
| `cplus_wcscpy` | Y | Y | - | Y |
| `cplus_wcsncpy` | Y | Y | - | Y |
| `cplus_wcscmp` | Y | Y | - | Y |
| `cplus_wcsncmp` | Y | Y | - | Y |
| `cplus_wcscat` | Y | Y | - | Y |
| `cplus_wcsstr` | Y | Y | - | Y |
| `cplus_wmemcpy` | Y | Y | - | Y |
| `cplus_wmemmove` | Y | Y | - | Y |
| `cplus_wmemset` | Y | Y | - | Y |
| `cplus_wmemcmp` | Y | Y | - | Y |
| `cplus_fabs` | Y | Y | - | Y |
| `cplus_sqrt` | Y | Y | Y | Y |
| `cplus_sin` | Y | Y | - | Y |
| `cplus_cos` | Y | Y | - | Y |
| `cplus_tan` | Y | Y | - | Y |
| `cplus_pow` | Y | Y | - | Y |
| `cplus_exp` | Y | Y | - | Y |
| `cplus_log` | Y | Y | - | Y |
| `cplus_log10` | Y | Y | - | Y |
| `cplus_floor` | Y | Y | - | Y |
| `cplus_ceil` | Y | Y | - | Y |
| `cplus_fmod` | Y | Y | - | Y |
| `cplus_atan2` | Y | Y | - | Y |
| `cplus_asin` | Y | Y | - | Y |
| `cplus_acos` | Y | Y | - | Y |
| `cplus_atan` | Y | Y | - | Y |
| `cplus_sinh` | Y | Y | - | Y |
| `cplus_cosh` | Y | Y | - | Y |
| `cplus_tanh` | Y | Y | - | Y |
| `cplus_log2` | Y | Y | - | Y |
| `cplus_cbrt` | Y | Y | - | Y |
| `cplus_hypot` | Y | Y | - | Y |
| `cplus_round` | Y | Y | - | Y |
| `cplus_trunc` | Y | Y | - | Y |
| `cplus_fmin` | Y | Y | - | Y |
| `cplus_fmax` | Y | Y | - | Y |
| `cplus_copysign` | Y | Y | - | Y |
| `cplus_ldexp` | Y | Y | - | Y |
| `cplus_frexp` | Y | Y | - | Y |
| `cplus_modf` | Y | Y | - | Y |
| `cplus_nan` | Y | Y | - | Y |
| `cplus_nextafter` | Y | Y | - | Y |
| `cplus_fdim` | Y | Y | - | Y |
| `cplus_remainder` | Y | Y | - | Y |
| `cplus_log1p` | Y | Y | - | Y |
| `cplus_expm1` | Y | Y | - | Y |
| `cplus_asinh` | Y | Y | - | Y |
| `cplus_acosh` | Y | Y | - | Y |
| `cplus_atanh` | Y | Y | - | Y |
| `cplus_erf` | Y | Y | - | Y |
| `cplus_erfc` | Y | Y | - | Y |
| `cplus_tgamma` | Y | Y | - | Y |
| `cplus_lgamma` | Y | Y | - | Y |
| `cplus_time` | Y | Y | - | Y |
| `cplus_difftime` | Y | Y | - | Y |
| `cplus_clock` | Y | Y | - | Y |
| `cplus_gmtime` | Y | Y | - | Y |
| `cplus_localtime` | Y | Y | - | Y |
| `cplus_mktime` | Y | Y | - | Y |
| `cplus_strftime` | Y | Y | - | Y |
| `cplus_setlocale` | Y | Y | - | Y |
| `cplus_localeconv` | Y | Y | - | Y |
| `cplus_signal` | Y | Y | - | Y |
| `cplus_raise` | Y | Y | - | Y |
| `malloc_s` | Y | Y | Y | Y |
| `calloc_s` | Y | Y | Y | Y |
| `realloc_s` | Y | Y | Y | Y |
| `free_s` | Y | Y | Y | Y |
| `cplus_safe_set_constraint_mode` | Y | Y | - | Y |
| `cplus_safe_get_constraint_mode` | Y | Y | - | **N** |
| `cplus_except_init` | Y | Y | - | Y |
| `cplus_snprintf_s` | Y | Y | Y | Y |
| `cplus_vsnprintf_s` | Y | Y | Y | Y |
| `cplus_printf_s` | Y | Y | Y | Y |
| `cplus_fprintf_s` | Y | Y | Y | Y |
| `cplus_scanf_s` | Y | Y | - | Y |
| `cplus_fscanf_s` | Y | Y | Y | Y |
| `cplus_sprintf_s` | Y | Y | Y | Y |
| `cplus_vsprintf_s` | Y | Y | Y | Y |
| `cplus_fopen_s` | Y | Y | Y | Y |
| `cplus_fclose_s` | Y | Y | Y | Y |
| `cplus_fread_s` | Y | Y | Y | Y |
| `cplus_fwrite_s` | Y | Y | Y | Y |
| `cplus_safe_log_set_level` | Y | Y | Y | **N** |
| `cplus_safe_log_get_level` | Y | Y | - | Y |
| `cplus_safe_log` | Y | Y | Y | Y |
| `cplus_memcpy_s` | Y | Y | Y | Y |
| `cplus_memmove_s` | Y | Y | Y | Y |
| `cplus_memset_s` | Y | Y | Y | Y |
| `cplus_memcmp_s` | Y | Y | Y | Y |
| `cplus_memzero_s` | Y | Y | Y | Y |
| `cplus_safe_platform_probe` | Y | Y | Y | Y |
| `cplus_safe_stat_record` | Y | Y | - | **N** |
| `cplus_safe_stats_reset` | Y | Y | - | **N** |
| `cplus_safe_stats_get` | Y | Y | - | **N** |
| `cplus_safe_stats_dump` | Y | Y | - | **N** |
| `cplus_strcpy_s` | Y | Y | Y | Y |
| `cplus_strncpy_s` | Y | Y | Y | Y |
| `cplus_strcat_s` | Y | Y | Y | Y |
| `cplus_strncat_s` | Y | Y | Y | Y |
| `cplus_strcmp_s` | Y | Y | Y | Y |
| `cplus_strnlen_s` | Y | Y | Y | Y |
| `cplus_strtok_s` | Y | Y | - | Y |
| `cplus_strerror_s` | Y | Y | - | Y |

## Gaps

### Exported with no test

- `cplus_putenv` — `libc/src/stdlib/stdlib_extra4.c` — appears in tests only as a prototype declaration (never called); not fuzz-exercised either
- `cplus_vprintf` — `libc/src/stdio/stdio.c` — appears in tests only as a prototype declaration (never called); not fuzz-exercised either
- `cplus_vfscanf` — `libc/src/stdio/stdio.c` — appears in tests only as a prototype declaration (never called); not fuzz-exercised either

### Implemented without logging

- `__cplus_errno_location` — `libc/src/errno/errno.c`
- `cplus_libc_log_set_enabled` — `libc/log/log.c`
- `cplus_safe_get_constraint_mode` — `stdlib/safe/src/safe_error.c`
- `cplus_safe_log_set_level` — `stdlib/safe/src/safe_log.c`
- `cplus_safe_stat_record` — `stdlib/safe/src/safe_stats.c`
- `cplus_safe_stats_reset` — `stdlib/safe/src/safe_stats.c`
- `cplus_safe_stats_get` — `stdlib/safe/src/safe_stats.c`
- `cplus_safe_stats_dump` — `stdlib/safe/src/safe_stats.c`

### Implemented but not fuzz-exercised

154 of 233 implemented symbols are not referenced by any fuzz harness; the fuzz corpus covers a subset (mem/string/stdio/alloc + the safe CRT), not the whole slice.

### Notable ISO C families still absent

Whole ISO C / POSIX families with **no** symbols in the slice (verified by searches over `libc/src`, `libc/install-include`, `stdlib/safe`); they remain part of the unfilled "not shipped" surface.

| family | status / evidence |
|---|---|
| stdio buffering & `FILE` internals | no `setvbuf`/`setbuf`/`fpurge`/`flockfile`/`fpos_t`/`fgetpos`/`fsetpos`; only open/read/write/seek wrappers over the host `FILE`. |
| multibyte / encoding conversions | no `mbrtowc`/`mbtowc`/`wcrtomb`/`mbstate_t`/`mbsrtowcs`; the `wchar.h` slice has only `wcs*`/`wmem*` narrow copies, no conversions, `wcsftime`, `wcstod`, … |
| locale | only `cplus_setlocale`/`cplus_localeconv`; no per-category or per-locale data tables, so behaviour is the `"C"` default. |
| threads & atomics | no `pthread_*`, no C11 `thrd_*`; the only atomics are the `stdatomic.h` builtins used internally by the logging/telemetry primitives. |
| floating-point environment (`fenv`) | no `fesetround`/`fegetround`/`feclearexcept`/`fetestexcept`; no `#pragma STDC FENV_ACCESS`. |
| complex arithmetic | no `complex.h`, no `cacos`/`csqrt`/`cabs` … |
| non-local jumps | no `setjmp`/`longjmp`/`sigsetjmp` in the slice. |
| full math | large but **not** complete: no `scalbn`/`ilogb`/`remquo`/`nearbyint`/`lrint`/`nexttoward`, no full `fenv` interaction (`math*` covers the common libm set). |

## Notes

- All four secure-allocation adapters (`malloc_s`/`calloc_s`/`realloc_s`/`free_s`) are implemented, tested and fuzz-exercised; `free_s` consumes the handle so double-free is structurally impossible on the wrapper path.
- The un-logged implementations are telemetry/config/infra, not hardened data-path adapters: the `errno` accessor (`__cplus_errno_location`), the four `safe_stats` entry points, and three pure accessors (`cplus_libc_log_set_enabled`, `cplus_safe_log_set_level`, `cplus_safe_get_constraint_mode`). None performs a bounds/constraint decision, so none emits a diagnostic log. Every emitter/decision function (`cplus_libc_log`, `cplus_safe_log`, `cplus_safe_set_constraint_mode`, every `*_s` adapter) logs.
- `tested` measures that a test *calls* the symbol; it does not measure assertion strength or the number of scenarios exercised.

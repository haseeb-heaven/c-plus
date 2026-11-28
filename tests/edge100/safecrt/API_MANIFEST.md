# API_MANIFEST — EDGE100 safecrt

Enumerated from all 13 public headers in `stdlib/safe/`. 113 public entry
points are exercised (the `safe_c.h` macro-only surface is covered through the
compiled targets its mappings resolve to; it defines no new function of its
own). For each API the harness selects the applicable subset of the 11 required
edge vectors: `valid`, `NULL`, `zero`, `exact_fit`, `one_over`, `SIZE_MAX`,
`overlap`, `unterminated`, `double_free`, `closed_stream`, `percent_n`.

| # | Header | Public API | Vectors exercised |
|---|--------|-----------|-------------------|
| 1 | safe_memory.h | `cplus_memcpy_s` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX,overlap |
| 2 | safe_memory.h | `cplus_memmove_s` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX,overlap |
| 3 | safe_memory.h | `cplus_memset_s` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX |
| 4 | safe_memory.h | `cplus_memcmp_s` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX |
| 5 | safe_memory.h | `cplus_memzero_s` | valid,NULL,zero,SIZE_MAX |
| 6 | safe_memory.h | `cplus_memcpy_s_auto` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX,overlap |
| 7 | safe_memory.h | `cplus_memmove_s_auto` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX,overlap |
| 8 | safe_memory.h | `cplus_memset_s_auto` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX |
| 9 | safe_memory.h | `cplus_memcmp_s_auto` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX |
| 10 | safe_memory.h | `cplus_safe_set_unknown_capacity_mode` | valid,zero |
| 11 | safe_memory.h | `cplus_safe_unknown_capacity_strict` | valid,zero |
| 12 | safe_string.h | `cplus_strcpy_s` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX,overlap,unterminated |
| 13 | safe_string.h | `cplus_strncpy_s` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX,overlap,unterminated |
| 14 | safe_string.h | `cplus_strcat_s` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX,unterminated |
| 15 | safe_string.h | `cplus_strncat_s` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX,unterminated |
| 16 | safe_string.h | `cplus_strcmp_s` | valid,NULL,zero,exact_fit,unterminated |
| 17 | safe_string.h | `cplus_strnlen_s` | valid,NULL,zero,SIZE_MAX,unterminated |
| 18 | safe_string.h | `cplus_strtok_s` | valid,NULL,zero |
| 19 | safe_string.h | `cplus_strerror_s` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX |
| 20 | safe_string.h | `cplus_strcpy_s_auto` | valid,NULL |
| 21 | safe_string.h | `cplus_strncpy_s_auto` | valid,NULL,one_over |
| 22 | safe_string.h | `cplus_strcat_s_auto` | valid,NULL |
| 23 | safe_string.h | `cplus_strncat_s_auto` | valid,NULL,one_over |
| 24 | safe_alloc.h | `malloc_s` | valid,zero,SIZE_MAX |
| 25 | safe_alloc.h | `calloc_s` | valid,zero,SIZE_MAX |
| 26 | safe_alloc.h | `realloc_s` | valid,NULL,zero,SIZE_MAX |
| 27 | safe_alloc.h | `free_s` | valid,NULL,double_free |
| 28 | safe_alloc.h | `free_checked_s` | valid,NULL,double_free |
| 29 | safe_alloc.h | `free_checked_generation_s` | valid,NULL,zero,double_free |
| 30 | safe_alloc.h | `cplus_safe_transfer_ownership_generation` | valid,NULL,zero,double_free |
| 31 | safe_alloc.h | `aligned_alloc_s` | valid,NULL,zero,SIZE_MAX |
| 32 | safe_alloc.h | `cplus_safe_alloc_get_meta` | valid,NULL,zero |
| 33 | safe_alloc.h | `cplus_safe_ptr_is_live` | valid,NULL |
| 34 | safe_alloc.h | `cplus_safe_ptr_was_freed` | valid,NULL,double_free |
| 35 | safe_alloc.h | `cplus_safe_ptr_size` | valid,NULL |
| 36 | safe_alloc.h | `cplus_safe_remaining_capacity` | valid,NULL,exact_fit |
| 37 | safe_alloc.h | `cplus_safe_ptr_elem_size` | valid,NULL |
| 38 | safe_alloc.h | `cplus_safe_ptr_count` | valid,NULL |
| 39 | safe_alloc.h | `cplus_safe_query_length` | valid,NULL,zero,SIZE_MAX |
| 40 | safe_alloc.h | `cplus_safe_ptr_check_generation` | valid,NULL,zero,double_free |
| 41 | safe_alloc.h | `cplus_safe_alloc_live_count` | valid,zero |
| 42 | safe_alloc.h | `cplus_safe_alloc_live_bytes` | valid,zero |
| 43 | safe_alloc.h | `cplus_safe_transfer_ownership` | valid,NULL,double_free |
| 44 | safe_alloc.h | `cplus_safe_alloc_fail_after` | valid,zero |
| 45 | safe_alloc.h | `cplus_safe_alloc_fail_reset` | valid |
| 46 | safe_alloc.h | `cplus_safe_alloc_seq` | valid,zero |
| 47 | safe_io.h | `cplus_fopen_s` | valid,NULL,zero |
| 48 | safe_io.h | `cplus_fclose_s` | valid,NULL,closed_stream |
| 49 | safe_io.h | `cplus_fread_s` | valid,NULL,zero,SIZE_MAX,closed_stream |
| 50 | safe_io.h | `cplus_fwrite_s` | valid,NULL,zero,SIZE_MAX,closed_stream |
| 51 | safe_io.h | `cplus_io_stream_is_closed` | valid,NULL,closed_stream |
| 52 | safe_io.h | `cplus_safe_stream_was_closed` | valid,NULL,closed_stream |
| 53 | safe_io.h | `cplus_safe_open_stream_count` | valid,zero |
| 54 | safe_io.h | `cplus_fgets_s` | valid,NULL,zero,one_over,closed_stream,unterminated |
| 55 | safe_io.h | `cplus_fgets_s_checked` | valid,NULL,zero,one_over,closed_stream,unterminated |
| 56 | safe_io.h | `cplus_fread_s_checked` | valid,NULL,zero,SIZE_MAX,one_over,closed_stream |
| 57 | safe_io.h | `cplus_fwrite_s_checked` | valid,NULL,zero,SIZE_MAX,one_over,closed_stream |
| 58 | safe_io.h | `cplus_getenv_s` | valid,NULL,zero,one_over |
| 59 | safe_format.h | `cplus_snprintf_s` | valid,NULL,exact_fit,percent_n |
| 60 | safe_format.h | `cplus_vsnprintf_s` | valid,NULL,exact_fit,percent_n |
| 61 | safe_format.h | `cplus_printf_s` | valid,NULL,percent_n |
| 62 | safe_format.h | `cplus_fprintf_s` | valid,NULL,percent_n,closed_stream |
| 63 | safe_format.h | `cplus_scanf_s` | valid,NULL,percent_n,unterminated |
| 64 | safe_format.h | `cplus_fscanf_s` | valid,NULL,percent_n,closed_stream,unterminated |
| 65 | safe_format.h | `cplus_sscanf_s` | valid,NULL,percent_n,unterminated |
| 66 | safe_format.h | `cplus_sprintf_s` | valid,NULL,exact_fit,percent_n |
| 67 | safe_format.h | `cplus_vsprintf_s` | valid,NULL,exact_fit,percent_n |
| 68 | safe_format.h | `cplus_printf_s_checked` | valid,NULL,percent_n |
| 69 | safe_format.h | `cplus_fprintf_s_checked` | valid,NULL,percent_n,closed_stream |
| 70 | safe_format.h | `cplus_snprintf_s_checked` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX,percent_n |
| 71 | safe_format.h | `cplus_sprintf_s_checked` | valid,NULL,zero,exact_fit,one_over,SIZE_MAX,percent_n |
| 72 | safe_format.h | `cplus_scanf_s_checked` | valid,NULL,percent_n,unterminated |
| 73 | safe_format.h | `cplus_fscanf_s_checked` | valid,NULL,percent_n,closed_stream,unterminated |
| 74 | safe_format.h | `cplus_sscanf_s_checked` | valid,NULL,percent_n,unterminated |
| 75 | safe_error.h | `cplus_safe_set_constraint_mode` | valid,zero |
| 76 | safe_error.h | `cplus_safe_get_constraint_mode` | valid,zero |
| 77 | safe_except.h | `cplus_except_init` | valid,zero |
| 78 | safe_except.h | `CPLUS_CHECK` (macro) | valid,zero |
| 79 | safe_except.h | `CPLUS_THROW_IF` (macro) | valid,zero |
| 80 | safe_except.h | `RETHROW` (macro) | valid,zero |
| 81 | safe_log.h | `cplus_safe_log_set_level` | valid,zero |
| 82 | safe_log.h | `cplus_safe_log_get_level` | valid,zero |
| 83 | safe_log.h | `cplus_safe_log` | valid,NULL,zero |
| 84 | safe_platform.h | `cplus_safe_platform_probe` | valid,NULL |
| 85 | safe_stats.h | `cplus_safe_stat_record` | valid,NULL,zero |
| 86 | safe_stats.h | `cplus_safe_stats_reset` | valid,zero |
| 87 | safe_stats.h | `cplus_safe_stats_get` | valid,NULL,zero |
| 88 | safe_stats.h | `cplus_safe_stats_dump` | valid,NULL,zero |
| 89 | safe_runtime.h | `cplus_runtime_set_handler` | valid,NULL |
| 90 | safe_runtime.h | `cplus_runtime_set_failstop` | valid,zero |
| 91 | safe_runtime.h | `cplus_runtime_get_failstop` | valid,zero |
| 92 | safe_runtime.h | `cplus_runtime_set_violation_trap` | valid,zero |
| 93 | safe_runtime.h | `cplus_runtime_get_violation_trap` | valid,zero |
| 94 | safe_runtime.h | `cplus_runtime_violation` | valid,NULL,zero |
| 95 | safe_runtime.h | `cplus_runtime_report` | valid,NULL,zero |
| 96 | safe_runtime.h | `cplus_runtime_guard` | valid,NULL,zero |
| 97 | safe_runtime.h | `cplus_runtime_push_cleanup` | valid,NULL,zero |
| 98 | safe_runtime.h | `cplus_runtime_pop_cleanup` | valid,zero |
| 99 | safe_runtime.h | `cplus_runtime_cleanup_mark` | valid,zero |
| 100 | safe_runtime.h | `cplus_runtime_cleanup_unwind_to` | valid,zero |
| 101 | safe_runtime.h | `cplus_runtime_unwind_cleanups` | valid,zero |
| 102 | safe_runtime.h | `cplus_runtime_cleanup_depth` | valid,zero |
| 103 | safe_runtime.h | `cplus_runtime_leaked_cleanups` | valid,zero |
| 104 | safe_runtime.h | `cplus_runtime_unwound_cleanups` | valid,zero |
| 105 | safe_runtime.h | `cplus_runtime_reset_counters` | valid,zero |
| 106 | safe_ext_mem.h | `cplus_sc_timingsafe_bcmp` | valid,NULL,zero,one_over |
| 107 | safe_ext_mem.h | `cplus_sc_timingsafe_memcmp` | valid,NULL,zero,one_over |
| 108 | safe_c.h | `memrchr_s` -> `cplus_sc_upstream_memrchr_s` | valid,NULL,zero,SIZE_MAX |
| 109 | safe_c.h | `bsearch_s` -> `cplus_sc_upstream_bsearch_s` | valid,NULL |
| 110 | safe_c.h | `fread` -> `cplus_fread` | valid,NULL,zero,SIZE_MAX |
| 111 | safe_c.h | `fwrite` -> `cplus_fwrite` | valid,NULL,zero,SIZE_MAX |
| 112 | safe_c.h | `printf` -> `cplus_printf` | valid,NULL |
| 113 | safe_c.h | `fprintf` -> `cplus_fprintf` | valid,NULL |

`safe_c.h` additionally maps `malloc/calloc/realloc/free`, `memset/memcpy/
memmove/memcmp`, `strcpy/strncpy/strcat/strncat`, `fopen_s/fclose_s/fgets_s`,
`getenv_s`, `printf_s/fprintf_s/snprintf_s/sprintf_s/scanf_s/fscanf_s/sscanf_s`
onto entries already enumerated above; no duplicate compiled entry point exists.

Total distinct APIs: **113**. Total cases: **402**.

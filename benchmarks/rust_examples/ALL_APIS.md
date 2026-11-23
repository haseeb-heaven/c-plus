# Full checked-API surface vs Rust examples

This document enumerates **every** exported checked API symbol and maps it to the
Rust safe-library examples that actually exercise it. It deliberately does **not**
claim 100% coverage: a matching outcome token in a Rust example is an *observation*
of behaviour, not a proof of equivalent memory-safety guarantees (see
[Non-equivalence](#non-equivalence-of-outcome-tokens)).

## Enumeration (exact commands and counts)

```sh
nm -g build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a

# Unique, defined global text symbols carrying the public _cplus_ prefix:
nm -g --defined-only build/safe/libcplus_safecrt.a | awk '$2=="T"{print $3}' | sort -u | grep '^_cplus_' | wc -l
nm -g --defined-only build/libc/libcplus_hardened.a  | awk '$2=="T"{print $3}' | sort -u | grep '^_cplus_' | wc -l
```

- `build/safe/libcplus_safecrt.a`: **167** `_cplus_*` `T` symbols
- `build/libc/libcplus_hardened.a`: **783** `_cplus_*` `T` symbols
- **Total checked API surface: 950 symbols**

The full `nm -g` output also contains vendored/private symbols (yyjson, e4c,
ubsan, `_mem_prim_*`, `__*_*_uchk`, `___cplus_errno_location`, etc.). Those are
not part of the public `_cplus_*` checked-API surface and are excluded from the
167 + 783 counts; they are not wrapped by any Rust example.

## Coverage summary

| Category | Symbols | Notes |
|---|---:|---|
| COVERED by a Rust example | 155 | exact symbol appears in a group's compare.sh/harness.c/*.rs |
| BLOCKED (with reason) | 2 | explicit BLOCKED declaration + nm evidence/reason |
| NOT EXERCISED | 793 | enumerated, no example models it; **not** counted as a pass |
| **Total** | **950** | |

Groups represented: `alloc`, `format`, `io`, `json_log`, `runtime_diag`, `safecrt_memstr`, `search`, `threading`, `vendored_v`.

## Non-equivalence of outcome tokens

**Matching outcome tokens are NOT equivalent safety guarantees.** The C harness
observes the checked library's runtime outcome (return code / refusal token) for a
specific input. The Rust examples use safe idioms (slices, `Option`, bounds checks,
no `unsafe`, no FFI) whose *token* is made to match that observation. A token match
therefore demonstrates that the same input class is *recognised*, not that:

- the Rust type system provides the same run-time diagnostics, or
- the C library's handler/constraint machinery is replicated, or
- the two implementations share an exploitability profile.

Null pointers and out-of-range lengths are *unrepresentable* in the safe Rust
examples, so refusals are modelled with guards/`Option`. Conversely, a C refusal
token does not imply the Rust code needed a guard at all. Treat every MATCH as a
behavioural correspondence for the enumerated case set only.

## `build/safe/libcplus_safecrt.a` — 167 symbols

| Symbol | Status | Group / exact reason | Archive | exact nm line |
|---|---|---|---|---|

| `_cplus_except_init` | COVERED | runtime_diag | safecrt | `0000000000000000 T _cplus_except_init` |
| `_cplus_fclose_s` | COVERED | io | safecrt | `000000000000033c T _cplus_fclose_s` |
| `_cplus_fopen_s` | COVERED | io | safecrt | `0000000000000000 T _cplus_fopen_s` |
| `_cplus_fprintf_s` | COVERED | format, io | safecrt | `0000000000000918 T _cplus_fprintf_s` |
| `_cplus_fprintf_s_checked` | COVERED | format | safecrt | `0000000000000aac T _cplus_fprintf_s_checked` |
| `_cplus_fread_s` | COVERED | io | safecrt | `0000000000000544 T _cplus_fread_s` |
| `_cplus_fscanf_s` | COVERED | io | safecrt | `0000000000001094 T _cplus_fscanf_s` |
| `_cplus_fscanf_s_checked` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `00000000000011dc T _cplus_fscanf_s_checked` |
| `_cplus_fwrite_s` | COVERED | io | safecrt | `0000000000000618 T _cplus_fwrite_s` |
| `_cplus_json_free` | COVERED | json_log | safecrt | `0000000000000140 T _cplus_json_free` |
| `_cplus_json_get_int` | COVERED | json_log | safecrt | `00000000000001b0 T _cplus_json_get_int` |
| `_cplus_json_get_str` | COVERED | json_log | safecrt | `00000000000002ac T _cplus_json_get_str` |
| `_cplus_json_parse` | COVERED | json_log | safecrt | `0000000000000000 T _cplus_json_parse` |
| `_cplus_memcmp_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000328 T _cplus_memcmp_s` |
| `_cplus_memcmp_s_auto` | COVERED | safecrt_memstr | safecrt | `0000000000000b5c T _cplus_memcmp_s_auto` |
| `_cplus_memcpy_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_memcpy_s` |
| `_cplus_memcpy_s_auto` | COVERED | safecrt_memstr | safecrt | `00000000000004c8 T _cplus_memcpy_s_auto` |
| `_cplus_memmove_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `00000000000001a4 T _cplus_memmove_s` |
| `_cplus_memmove_s_auto` | COVERED | safecrt_memstr | safecrt | `00000000000007b8 T _cplus_memmove_s_auto` |
| `_cplus_memset_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000270 T _cplus_memset_s` |
| `_cplus_memset_s_auto` | COVERED | safecrt_memstr | safecrt | `0000000000000934 T _cplus_memset_s_auto` |
| `_cplus_memzero_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000408 T _cplus_memzero_s` |
| `_cplus_printf_s` | COVERED | format, io | safecrt | `0000000000000614 T _cplus_printf_s` |
| `_cplus_printf_s_checked` | COVERED | format | safecrt | `000000000000077c T _cplus_printf_s_checked` |
| `_cplus_runtime_cleanup_depth` | COVERED | runtime_diag | safecrt | `00000000000002b4 T _cplus_runtime_cleanup_depth` |
| `_cplus_runtime_cleanup_mark` | COVERED | runtime_diag | safecrt | `0000000000000290 T _cplus_runtime_cleanup_mark` |
| `_cplus_runtime_cleanup_unwind_to` | COVERED | runtime_diag | safecrt | `00000000000002d8 T _cplus_runtime_cleanup_unwind_to` |
| `_cplus_runtime_get_failstop` | COVERED | runtime_diag | safecrt | `0000000000000050 T _cplus_runtime_get_failstop` |
| `_cplus_runtime_get_violation_trap` | COVERED | runtime_diag | safecrt | `0000000000000070 T _cplus_runtime_get_violation_trap` |
| `_cplus_runtime_guard` | COVERED | runtime_diag | safecrt | `00000000000004ac T _cplus_runtime_guard` |
| `_cplus_runtime_leaked_cleanups` | COVERED | runtime_diag | safecrt | `000000000000042c T _cplus_runtime_leaked_cleanups` |
| `_cplus_runtime_pop_cleanup` | COVERED | runtime_diag | safecrt | `000000000000022c T _cplus_runtime_pop_cleanup` |
| `_cplus_runtime_push_cleanup` | COVERED | runtime_diag | safecrt | `00000000000001b0 T _cplus_runtime_push_cleanup` |
| `_cplus_runtime_report` | COVERED | runtime_diag | safecrt | `0000000000000128 T _cplus_runtime_report` |
| `_cplus_runtime_reset_counters` | COVERED | runtime_diag | safecrt | `0000000000000474 T _cplus_runtime_reset_counters` |
| `_cplus_runtime_set_failstop` | COVERED | format, runtime_diag | safecrt | `000000000000003c T _cplus_runtime_set_failstop` |
| `_cplus_runtime_set_handler` | COVERED | runtime_diag | safecrt | `0000000000000000 T _cplus_runtime_set_handler` |
| `_cplus_runtime_set_violation_trap` | COVERED | format, runtime_diag, safecrt_memstr<br>per-case BLOCKED note: [safecrt_memstr] `cplus_runtime_set_violation_trap`, all present. **BLOCKED list is empty** in | safecrt | `000000000000005c T _cplus_runtime_set_violation_trap` |
| `_cplus_runtime_unwind_cleanups` | COVERED | runtime_diag | safecrt | `0000000000000388 T _cplus_runtime_unwind_cleanups` |
| `_cplus_runtime_unwound_cleanups` | COVERED | runtime_diag | safecrt | `0000000000000450 T _cplus_runtime_unwound_cleanups` |
| `_cplus_runtime_violation` | COVERED | runtime_diag | safecrt | `000000000000007c T _cplus_runtime_violation` |
| `_cplus_safe_alloc_fail_after` | COVERED | alloc | safecrt | `00000000000014c8 T _cplus_safe_alloc_fail_after` |
| `_cplus_safe_alloc_fail_reset` | COVERED | alloc | safecrt | `0000000000001504 T _cplus_safe_alloc_fail_reset` |
| `_cplus_safe_alloc_get_meta` | COVERED | runtime_diag | safecrt | `0000000000000cf0 T _cplus_safe_alloc_get_meta` |
| `_cplus_safe_alloc_live_bytes` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000001368 T _cplus_safe_alloc_live_bytes` |
| `_cplus_safe_alloc_live_count` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `000000000000132c T _cplus_safe_alloc_live_count` |
| `_cplus_safe_alloc_seq` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000001544 T _cplus_safe_alloc_seq` |
| `_cplus_safe_get_constraint_mode` | COVERED | runtime_diag | safecrt | `0000000000000088 T _cplus_safe_get_constraint_mode` |
| `_cplus_safe_log` | COVERED | json_log | safecrt | `0000000000000020 T _cplus_safe_log` |
| `_cplus_safe_log_get_level` | COVERED | json_log | safecrt | `0000000000000010 T _cplus_safe_log_get_level` |
| `_cplus_safe_log_set_level` | COVERED | format, json_log, runtime_diag | safecrt | `0000000000000000 T _cplus_safe_log_set_level` |
| `_cplus_safe_platform_probe` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_safe_platform_probe` |
| `_cplus_safe_ptr_check_generation` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000001260 T _cplus_safe_ptr_check_generation` |
| `_cplus_safe_ptr_count` | COVERED | runtime_diag | safecrt | `00000000000010b8 T _cplus_safe_ptr_count` |
| `_cplus_safe_ptr_elem_size` | COVERED | runtime_diag | safecrt | `000000000000100c T _cplus_safe_ptr_elem_size` |
| `_cplus_safe_ptr_is_live` | COVERED | alloc, runtime_diag | safecrt | `0000000000000de8 T _cplus_safe_ptr_is_live` |
| `_cplus_safe_ptr_size` | COVERED | alloc, runtime_diag | safecrt | `0000000000000f60 T _cplus_safe_ptr_size` |
| `_cplus_safe_ptr_was_freed` | COVERED | alloc, runtime_diag | safecrt | `0000000000000e90 T _cplus_safe_ptr_was_freed` |
| `_cplus_safe_query_length` | COVERED | runtime_diag | safecrt | `0000000000001170 T _cplus_safe_query_length` |
| `_cplus_safe_remaining_capacity` | COVERED | runtime_diag | safecrt | `0000000000000c2c T _cplus_safe_remaining_capacity` |
| `_cplus_safe_set_constraint_mode` | COVERED | runtime_diag | safecrt | `0000000000000000 T _cplus_safe_set_constraint_mode` |
| `_cplus_safe_set_unknown_capacity_mode` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `00000000000004a8 T _cplus_safe_set_unknown_capacity_mode` |
| `_cplus_safe_stat_record` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_safe_stat_record` |
| `_cplus_safe_stats_dump` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000254 T _cplus_safe_stats_dump` |
| `_cplus_safe_stats_get` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000194 T _cplus_safe_stats_get` |
| `_cplus_safe_stats_reset` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000160 T _cplus_safe_stats_reset` |
| `_cplus_safe_transfer_ownership` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `00000000000013a4 T _cplus_safe_transfer_ownership` |
| `_cplus_safe_unknown_capacity_strict` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `00000000000004bc T _cplus_safe_unknown_capacity_strict` |
| `_cplus_sc__memcmp_s_chk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_sc__memcmp_s_chk` |
| `_cplus_sc__memcpy_s_chk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_sc__memcpy_s_chk` |
| `_cplus_sc__memmove_s_chk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_sc__memmove_s_chk` |
| `_cplus_sc__memset_s_chk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_sc__memset_s_chk` |
| `_cplus_sc__memzero_s_chk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_sc__memzero_s_chk` |
| `_cplus_sc__strcat_s_chk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_sc__strcat_s_chk` |
| `_cplus_sc__strcmp_s_chk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_sc__strcmp_s_chk` |
| `_cplus_sc__strcpy_s_chk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_sc__strcpy_s_chk` |
| `_cplus_sc__strerror_s_chk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_sc__strerror_s_chk` |
| `_cplus_sc__strncat_s_chk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_sc__strncat_s_chk` |
| `_cplus_sc__strncpy_s_chk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_sc__strncpy_s_chk` |
| `_cplus_sc__strnlen_s_chk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000030 T _cplus_sc__strnlen_s_chk` |
| `_cplus_sc__strtok_s_chk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_sc__strtok_s_chk` |
| `_cplus_sc_abort_handler_s` | COVERED | runtime_diag | safecrt | `0000000000000000 T _cplus_sc_abort_handler_s` |
| `_cplus_sc_ignore_handler_s` | COVERED | runtime_diag | safecrt | `0000000000000000 T _cplus_sc_ignore_handler_s` |
| `_cplus_sc_invoke_safe_mem_constraint_handler` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000048 T _cplus_sc_invoke_safe_mem_constraint_handler` |
| `_cplus_sc_invoke_safe_str_constraint_handler` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000048 T _cplus_sc_invoke_safe_str_constraint_handler` |
| `_cplus_sc_memcmp_s` | COVERED | safecrt_memstr | safecrt | `0000000000000020 T _cplus_sc_memcmp_s` |
| `_cplus_sc_memcpy_s` | COVERED | safecrt_memstr | safecrt | `0000000000000000 T _cplus_sc_memcpy_s` |
| `_cplus_sc_memmove_s` | COVERED | safecrt_memstr | safecrt | `000000000000000c T _cplus_sc_memmove_s` |
| `_cplus_sc_memset_s` | COVERED | safecrt_memstr | safecrt | `0000000000000018 T _cplus_sc_memset_s` |
| `_cplus_sc_memzero_s` | COVERED | safecrt_memstr | safecrt | `000000000000002c T _cplus_sc_memzero_s` |
| `_cplus_sc_set_mem_constraint_handler_s` | COVERED | runtime_diag, search, vendored_v | safecrt | `0000000000000000 T _cplus_sc_set_mem_constraint_handler_s` |
| `_cplus_sc_set_mem_constraint_handler_s_wrapper` | COVERED | safecrt_memstr | safecrt | `0000000000000080 T _cplus_sc_set_mem_constraint_handler_s_wrapper` |
| `_cplus_sc_set_str_constraint_handler_s` | COVERED | runtime_diag, search, vendored_v | safecrt | `0000000000000000 T _cplus_sc_set_str_constraint_handler_s` |
| `_cplus_sc_set_str_constraint_handler_s_wrapper` | COVERED | safecrt_memstr | safecrt | `0000000000000084 T _cplus_sc_set_str_constraint_handler_s_wrapper` |
| `_cplus_sc_strcat_s` | COVERED | safecrt_memstr | safecrt | `0000000000000048 T _cplus_sc_strcat_s` |
| `_cplus_sc_strcmp_s` | COVERED | safecrt_memstr | safecrt | `0000000000000064 T _cplus_sc_strcmp_s` |
| `_cplus_sc_strcpy_s` | COVERED | safecrt_memstr | safecrt | `0000000000000034 T _cplus_sc_strcpy_s` |
| `_cplus_sc_strerror_s` | COVERED | safecrt_memstr | safecrt | `0000000000000070 T _cplus_sc_strerror_s` |
| `_cplus_sc_strncat_s` | COVERED | safecrt_memstr | safecrt | `0000000000000050 T _cplus_sc_strncat_s` |
| `_cplus_sc_strncpy_s` | COVERED | safecrt_memstr | safecrt | `000000000000003c T _cplus_sc_strncpy_s` |
| `_cplus_sc_strnlen_s` | COVERED | safecrt_memstr | safecrt | `000000000000005c T _cplus_sc_strnlen_s` |
| `_cplus_sc_strtok_s` | COVERED | safecrt_memstr | safecrt | `0000000000000078 T _cplus_sc_strtok_s` |
| `_cplus_sc_timingsafe_bcmp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_sc_timingsafe_bcmp` |
| `_cplus_sc_timingsafe_memcmp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `000000000000000c T _cplus_sc_timingsafe_memcmp` |
| `_cplus_scanf_s` | COVERED | io | safecrt | `0000000000000c60 T _cplus_scanf_s` |
| `_cplus_scanf_s_checked` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000f44 T _cplus_scanf_s_checked` |
| `_cplus_snprintf_s` | COVERED | format | safecrt | `00000000000003b0 T _cplus_snprintf_s` |
| `_cplus_snprintf_s_checked` | COVERED | format | safecrt | `0000000000000434 T _cplus_snprintf_s_checked` |
| `_cplus_sprintf_s` | COVERED | format | safecrt | `00000000000016dc T _cplus_sprintf_s` |
| `_cplus_sprintf_s_checked` | COVERED | format | safecrt | `0000000000001794 T _cplus_sprintf_s_checked` |
| `_cplus_sscanf_s` | COVERED | format | safecrt | `0000000000001360 T _cplus_sscanf_s` |
| `_cplus_sscanf_s_checked` | COVERED | format | safecrt | `00000000000014c0 T _cplus_sscanf_s_checked` |
| `_cplus_strcat_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000260 T _cplus_strcat_s` |
| `_cplus_strcat_s_auto` | COVERED | safecrt_memstr | safecrt | `0000000000000920 T _cplus_strcat_s_auto` |
| `_cplus_strcmp_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `00000000000003e8 T _cplus_strcmp_s` |
| `_cplus_strcpy_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_strcpy_s` |
| `_cplus_strcpy_s_auto` | COVERED | safecrt_memstr | safecrt | `000000000000069c T _cplus_strcpy_s_auto` |
| `_cplus_strerror_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `00000000000005f4 T _cplus_strerror_s` |
| `_cplus_strncat_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `000000000000031c T _cplus_strncat_s` |
| `_cplus_strncat_s_auto` | COVERED | safecrt_memstr | safecrt | `0000000000000a80 T _cplus_strncat_s_auto` |
| `_cplus_strncpy_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000194 T _cplus_strncpy_s` |
| `_cplus_strncpy_s_auto` | COVERED | safecrt_memstr | safecrt | `00000000000007dc T _cplus_strncpy_s_auto` |
| `_cplus_strnlen_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `00000000000004bc T _cplus_strnlen_s` |
| `_cplus_strtok_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `000000000000053c T _cplus_strtok_s` |
| `_cplus_upstream_invoke_safe_mem_constraint_handler` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000024 T _cplus_upstream_invoke_safe_mem_constraint_handler` |
| `_cplus_upstream_invoke_safe_str_constraint_handler` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000024 T _cplus_upstream_invoke_safe_str_constraint_handler` |
| `_cplus_upstream_memcmp_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_upstream_memcmp_s` |
| `_cplus_upstream_memcpy_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_upstream_memcpy_s` |
| `_cplus_upstream_memmove_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_upstream_memmove_s` |
| `_cplus_upstream_memset_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_upstream_memset_s` |
| `_cplus_upstream_memzero_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_upstream_memzero_s` |
| `_cplus_upstream_set_mem_constraint_handler_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_upstream_set_mem_constraint_handler_s` |
| `_cplus_upstream_set_str_constraint_handler_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_upstream_set_str_constraint_handler_s` |
| `_cplus_upstream_strcat_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_upstream_strcat_s` |
| `_cplus_upstream_strcmp_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_upstream_strcmp_s` |
| `_cplus_upstream_strcpy_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_upstream_strcpy_s` |
| `_cplus_upstream_strncat_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_upstream_strncat_s` |
| `_cplus_upstream_strncpy_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_upstream_strncpy_s` |
| `_cplus_upstream_strnlen_s` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | safecrt | `0000000000000000 T _cplus_upstream_strnlen_s` |
| `_cplus_v__memccpy_s_chk` | COVERED | vendored_v | safecrt | `0000000000000138 T _cplus_v__memccpy_s_chk` |
| `_cplus_v__memchr_s_chk` | COVERED | vendored_v | safecrt | `0000000000000000 T _cplus_v__memchr_s_chk` |
| `_cplus_v__stpcpy_s_chk` | COVERED | vendored_v | safecrt | `0000000000000000 T _cplus_v__stpcpy_s_chk` |
| `_cplus_v__stpncpy_s_chk` | COVERED | vendored_v | safecrt | `00000000000001fc T _cplus_v__stpncpy_s_chk` |
| `_cplus_v__strcasestr_s_chk` | COVERED | vendored_v | safecrt | `00000000000002fc T _cplus_v__strcasestr_s_chk` |
| `_cplus_v__strchr_s_chk` | COVERED | search, vendored_v | safecrt | `0000000000000000 T _cplus_v__strchr_s_chk` |
| `_cplus_v__strcmpfld_s_chk` | COVERED | vendored_v | safecrt | `0000000000000000 T _cplus_v__strcmpfld_s_chk` |
| `_cplus_v__strcoll_s_chk` | COVERED | vendored_v | safecrt | `00000000000005d0 T _cplus_v__strcoll_s_chk` |
| `_cplus_v__strcpyfld_s_chk` | COVERED | vendored_v | safecrt | `0000000000000150 T _cplus_v__strcpyfld_s_chk` |
| `_cplus_v__strcspn_s_chk` | COVERED | vendored_v | safecrt | `000000000000021c T _cplus_v__strcspn_s_chk` |
| `_cplus_v__strfirstchar_s_chk` | COVERED | vendored_v | safecrt | `0000000000000000 T _cplus_v__strfirstchar_s_chk` |
| `_cplus_v__strfirstdiff_s_chk` | COVERED | vendored_v | safecrt | `00000000000002c0 T _cplus_v__strfirstdiff_s_chk` |
| `_cplus_v__strlastchar_s_chk` | COVERED | vendored_v | safecrt | `000000000000015c T _cplus_v__strlastchar_s_chk` |
| `_cplus_v__strlastdiff_s_chk` | COVERED | vendored_v | safecrt | `0000000000000428 T _cplus_v__strlastdiff_s_chk` |
| `_cplus_v__strnset_s_chk` | COVERED | vendored_v | safecrt | `0000000000000000 T _cplus_v__strnset_s_chk` |
| `_cplus_v__strnterminate_s_chk` | COVERED | vendored_v | safecrt | `0000000000000274 T _cplus_v__strnterminate_s_chk` |
| `_cplus_v__strrchr_s_chk` | COVERED | search, vendored_v | safecrt | `000000000000016c T _cplus_v__strrchr_s_chk` |
| `_cplus_v__strset_s_chk` | COVERED | vendored_v | safecrt | `0000000000000124 T _cplus_v__strset_s_chk` |
| `_cplus_v__strspn_s_chk` | COVERED | vendored_v | safecrt | `0000000000000000 T _cplus_v__strspn_s_chk` |
| `_cplus_v__strstr_s_chk` | COVERED | search, vendored_v | safecrt | `0000000000000000 T _cplus_v__strstr_s_chk` |
| `_cplus_v__strtolowercase_s_chk` | COVERED | vendored_v | safecrt | `0000000000000048 T _cplus_v__strtolowercase_s_chk` |
| `_cplus_v__strtouppercase_s_chk` | COVERED | vendored_v | safecrt | `0000000000000190 T _cplus_v__strtouppercase_s_chk` |
| `_cplus_v_memccpy_s` | COVERED | vendored_v | safecrt | `00000000000003cc T _cplus_v_memccpy_s` |
| `_cplus_v_memchr_s` | COVERED | search, vendored_v | safecrt | `00000000000003c4 T _cplus_v_memchr_s` |
| `_cplus_v_strlcat` | COVERED | vendored_v | safecrt | `0000000000000000 T _cplus_v_strlcat` |
| `_cplus_v_strlcpy` | COVERED | vendored_v | safecrt | `00000000000000ec T _cplus_v_strlcpy` |
| `_cplus_vsnprintf_s` | COVERED | format | safecrt | `0000000000000000 T _cplus_vsnprintf_s` |
| `_cplus_vsprintf_s` | COVERED | format | safecrt | `000000000000165c T _cplus_vsprintf_s` |

## `build/libc/libcplus_hardened.a` — 783 symbols

| Symbol | Status | Group / exact reason | Archive | exact nm line |
|---|---|---|---|---|

| `_cplus__Exit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000011c T _cplus__Exit` |
| `_cplus_abort` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000058 T _cplus_abort` |
| `_cplus_abs` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_abs` |
| `_cplus_acos` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000070 T _cplus_acos` |
| `_cplus_acosf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000d0 T _cplus_acosf` |
| `_cplus_acosh` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000184 T _cplus_acosh` |
| `_cplus_acoshf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000244 T _cplus_acoshf` |
| `_cplus_acoshl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000244 T _cplus_acoshl` |
| `_cplus_acosl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000d0 T _cplus_acosl` |
| `_cplus_aligned_alloc` | COVERED | alloc<br>per-case BLOCKED note: [alloc] `cplus_aligned_alloc` cases are `BLOCKED` too. Metadata queries | hardened | `0000000000000000 T _cplus_aligned_alloc` |
| `_cplus_alloc_check_generation` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001110 T _cplus_alloc_check_generation` |
| `_cplus_alloc_double_free_count` | COVERED | alloc<br>per-case BLOCKED note: [threading] both columns emit `BLOCKED`. (`_cplus_alloc_double_free_count` in the archive | hardened | `0000000000001384 T _cplus_alloc_double_free_count` |
| `_cplus_alloc_elem_size` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000e6c T _cplus_alloc_elem_size` |
| `_cplus_alloc_fail_after` | COVERED | alloc | hardened | `00000000000013fc T _cplus_alloc_fail_after` |
| `_cplus_alloc_fail_reset` | COVERED | alloc | hardened | `000000000000143c T _cplus_alloc_fail_reset` |
| `_cplus_alloc_get_meta` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000bd8 T _cplus_alloc_get_meta` |
| `_cplus_alloc_invalid_free_count` | COVERED | alloc | hardened | `00000000000013c0 T _cplus_alloc_invalid_free_count` |
| `_cplus_alloc_is_live` | COVERED | alloc | hardened | `0000000000000ce8 T _cplus_alloc_is_live` |
| `_cplus_alloc_length` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000f30 T _cplus_alloc_length` |
| `_cplus_alloc_live_bytes` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001348 T _cplus_alloc_live_bytes` |
| `_cplus_alloc_live_count` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000130c T _cplus_alloc_live_count` |
| `_cplus_alloc_meta_register` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_alloc_meta_register` |
| `_cplus_alloc_query_length` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001000 T _cplus_alloc_query_length` |
| `_cplus_alloc_remaining_capacity` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000b14 T _cplus_alloc_remaining_capacity` |
| `_cplus_alloc_size` | COVERED | alloc | hardened | `0000000000000da8 T _cplus_alloc_size` |
| `_cplus_asctime` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_asctime` |
| `_cplus_asctime_r` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000174 T _cplus_asctime_r` |
| `_cplus_asin` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000003c T _cplus_asin` |
| `_cplus_asinf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000009c T _cplus_asinf` |
| `_cplus_asinh` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000150 T _cplus_asinh` |
| `_cplus_asinhf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000210 T _cplus_asinhf` |
| `_cplus_asinhl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000210 T _cplus_asinhl` |
| `_cplus_asinl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000009c T _cplus_asinl` |
| `_cplus_at_quick_exit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000e8 T _cplus_at_quick_exit` |
| `_cplus_atan` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000a4 T _cplus_atan` |
| `_cplus_atan2` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_atan2` |
| `_cplus_atan2f` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000138 T _cplus_atan2f` |
| `_cplus_atan2l` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000138 T _cplus_atan2l` |
| `_cplus_atanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000104 T _cplus_atanf` |
| `_cplus_atanh` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001b8 T _cplus_atanh` |
| `_cplus_atanhf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000278 T _cplus_atanhf` |
| `_cplus_atanhl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000278 T _cplus_atanhl` |
| `_cplus_atanl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000104 T _cplus_atanl` |
| `_cplus_atexit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000078 T _cplus_atexit` |
| `_cplus_atof` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000d4 T _cplus_atof` |
| `_cplus_atoi` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000070 T _cplus_atoi` |
| `_cplus_atol` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000bc T _cplus_atol` |
| `_cplus_atoll` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000f0 T _cplus_atoll` |
| `_cplus_atomic_compare_exchange_strong_explicit_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000ec T _cplus_atomic_compare_exchange_strong_explicit_int` |
| `_cplus_atomic_compare_exchange_strong_explicit_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001d0c T _cplus_atomic_compare_exchange_strong_explicit_llong` |
| `_cplus_atomic_compare_exchange_strong_explicit_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000efc T _cplus_atomic_compare_exchange_strong_explicit_long` |
| `_cplus_atomic_compare_exchange_strong_explicit_ptr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002b1c T _cplus_atomic_compare_exchange_strong_explicit_ptr` |
| `_cplus_atomic_compare_exchange_strong_explicit_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000007f4 T _cplus_atomic_compare_exchange_strong_explicit_uint` |
| `_cplus_atomic_compare_exchange_strong_explicit_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002414 T _cplus_atomic_compare_exchange_strong_explicit_ullong` |
| `_cplus_atomic_compare_exchange_strong_explicit_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001604 T _cplus_atomic_compare_exchange_strong_explicit_ulong` |
| `_cplus_atomic_compare_exchange_strong_int` | COVERED | runtime_diag | hardened | `00000000000000cc T _cplus_atomic_compare_exchange_strong_int` |
| `_cplus_atomic_compare_exchange_strong_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001cec T _cplus_atomic_compare_exchange_strong_llong` |
| `_cplus_atomic_compare_exchange_strong_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000edc T _cplus_atomic_compare_exchange_strong_long` |
| `_cplus_atomic_compare_exchange_strong_ptr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002afc T _cplus_atomic_compare_exchange_strong_ptr` |
| `_cplus_atomic_compare_exchange_strong_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000007d4 T _cplus_atomic_compare_exchange_strong_uint` |
| `_cplus_atomic_compare_exchange_strong_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000023f4 T _cplus_atomic_compare_exchange_strong_ullong` |
| `_cplus_atomic_compare_exchange_strong_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000015e4 T _cplus_atomic_compare_exchange_strong_ulong` |
| `_cplus_atomic_compare_exchange_weak_explicit_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002b4 T _cplus_atomic_compare_exchange_weak_explicit_int` |
| `_cplus_atomic_compare_exchange_weak_explicit_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001ed4 T _cplus_atomic_compare_exchange_weak_explicit_llong` |
| `_cplus_atomic_compare_exchange_weak_explicit_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000010c4 T _cplus_atomic_compare_exchange_weak_explicit_long` |
| `_cplus_atomic_compare_exchange_weak_explicit_ptr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002ce4 T _cplus_atomic_compare_exchange_weak_explicit_ptr` |
| `_cplus_atomic_compare_exchange_weak_explicit_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000009bc T _cplus_atomic_compare_exchange_weak_explicit_uint` |
| `_cplus_atomic_compare_exchange_weak_explicit_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000025dc T _cplus_atomic_compare_exchange_weak_explicit_ullong` |
| `_cplus_atomic_compare_exchange_weak_explicit_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000017cc T _cplus_atomic_compare_exchange_weak_explicit_ulong` |
| `_cplus_atomic_compare_exchange_weak_int` | BLOCKED | [runtime_diag] BLOCKED cplus_atomic_compare_exchange_weak_int nm=present(T) reason=weak CAS may fail spuriously (C11 7.17.7.4); nondeterministic | hardened | `0000000000000294 T _cplus_atomic_compare_exchange_weak_int` |
| `_cplus_atomic_compare_exchange_weak_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001eb4 T _cplus_atomic_compare_exchange_weak_llong` |
| `_cplus_atomic_compare_exchange_weak_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000010a4 T _cplus_atomic_compare_exchange_weak_long` |
| `_cplus_atomic_compare_exchange_weak_ptr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002cc4 T _cplus_atomic_compare_exchange_weak_ptr` |
| `_cplus_atomic_compare_exchange_weak_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000099c T _cplus_atomic_compare_exchange_weak_uint` |
| `_cplus_atomic_compare_exchange_weak_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000025bc T _cplus_atomic_compare_exchange_weak_ullong` |
| `_cplus_atomic_compare_exchange_weak_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000017ac T _cplus_atomic_compare_exchange_weak_ulong` |
| `_cplus_atomic_exchange_explicit_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000088 T _cplus_atomic_exchange_explicit_int` |
| `_cplus_atomic_exchange_explicit_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001ca8 T _cplus_atomic_exchange_explicit_llong` |
| `_cplus_atomic_exchange_explicit_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000e98 T _cplus_atomic_exchange_explicit_long` |
| `_cplus_atomic_exchange_explicit_ptr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002ab8 T _cplus_atomic_exchange_explicit_ptr` |
| `_cplus_atomic_exchange_explicit_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000790 T _cplus_atomic_exchange_explicit_uint` |
| `_cplus_atomic_exchange_explicit_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000023b0 T _cplus_atomic_exchange_explicit_ullong` |
| `_cplus_atomic_exchange_explicit_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000015a0 T _cplus_atomic_exchange_explicit_ulong` |
| `_cplus_atomic_exchange_int` | COVERED | runtime_diag | hardened | `0000000000000080 T _cplus_atomic_exchange_int` |
| `_cplus_atomic_exchange_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001ca0 T _cplus_atomic_exchange_llong` |
| `_cplus_atomic_exchange_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000e90 T _cplus_atomic_exchange_long` |
| `_cplus_atomic_exchange_ptr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002ab0 T _cplus_atomic_exchange_ptr` |
| `_cplus_atomic_exchange_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000788 T _cplus_atomic_exchange_uint` |
| `_cplus_atomic_exchange_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000023a8 T _cplus_atomic_exchange_ullong` |
| `_cplus_atomic_exchange_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001598 T _cplus_atomic_exchange_ulong` |
| `_cplus_atomic_fetch_add_explicit_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000464 T _cplus_atomic_fetch_add_explicit_int` |
| `_cplus_atomic_fetch_add_explicit_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002084 T _cplus_atomic_fetch_add_explicit_llong` |
| `_cplus_atomic_fetch_add_explicit_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001274 T _cplus_atomic_fetch_add_explicit_long` |
| `_cplus_atomic_fetch_add_explicit_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000b6c T _cplus_atomic_fetch_add_explicit_uint` |
| `_cplus_atomic_fetch_add_explicit_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000278c T _cplus_atomic_fetch_add_explicit_ullong` |
| `_cplus_atomic_fetch_add_explicit_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000197c T _cplus_atomic_fetch_add_explicit_ulong` |
| `_cplus_atomic_fetch_add_int` | COVERED | runtime_diag | hardened | `000000000000045c T _cplus_atomic_fetch_add_int` |
| `_cplus_atomic_fetch_add_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000207c T _cplus_atomic_fetch_add_llong` |
| `_cplus_atomic_fetch_add_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000126c T _cplus_atomic_fetch_add_long` |
| `_cplus_atomic_fetch_add_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000b64 T _cplus_atomic_fetch_add_uint` |
| `_cplus_atomic_fetch_add_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002784 T _cplus_atomic_fetch_add_ullong` |
| `_cplus_atomic_fetch_add_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001974 T _cplus_atomic_fetch_add_ulong` |
| `_cplus_atomic_fetch_and_explicit_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000005ac T _cplus_atomic_fetch_and_explicit_int` |
| `_cplus_atomic_fetch_and_explicit_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000021cc T _cplus_atomic_fetch_and_explicit_llong` |
| `_cplus_atomic_fetch_and_explicit_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000013bc T _cplus_atomic_fetch_and_explicit_long` |
| `_cplus_atomic_fetch_and_explicit_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000cb4 T _cplus_atomic_fetch_and_explicit_uint` |
| `_cplus_atomic_fetch_and_explicit_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000028d4 T _cplus_atomic_fetch_and_explicit_ullong` |
| `_cplus_atomic_fetch_and_explicit_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001ac4 T _cplus_atomic_fetch_and_explicit_ulong` |
| `_cplus_atomic_fetch_and_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000005a0 T _cplus_atomic_fetch_and_int` |
| `_cplus_atomic_fetch_and_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000021c0 T _cplus_atomic_fetch_and_llong` |
| `_cplus_atomic_fetch_and_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000013b0 T _cplus_atomic_fetch_and_long` |
| `_cplus_atomic_fetch_and_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000ca8 T _cplus_atomic_fetch_and_uint` |
| `_cplus_atomic_fetch_and_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000028c8 T _cplus_atomic_fetch_and_ullong` |
| `_cplus_atomic_fetch_and_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001ab8 T _cplus_atomic_fetch_and_ulong` |
| `_cplus_atomic_fetch_nand_explicit_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000628 T _cplus_atomic_fetch_nand_explicit_int` |
| `_cplus_atomic_fetch_nand_explicit_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002248 T _cplus_atomic_fetch_nand_explicit_llong` |
| `_cplus_atomic_fetch_nand_explicit_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001438 T _cplus_atomic_fetch_nand_explicit_long` |
| `_cplus_atomic_fetch_nand_explicit_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000d30 T _cplus_atomic_fetch_nand_explicit_uint` |
| `_cplus_atomic_fetch_nand_explicit_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002950 T _cplus_atomic_fetch_nand_explicit_ullong` |
| `_cplus_atomic_fetch_nand_explicit_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001b40 T _cplus_atomic_fetch_nand_explicit_ulong` |
| `_cplus_atomic_fetch_nand_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000600 T _cplus_atomic_fetch_nand_int` |
| `_cplus_atomic_fetch_nand_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002220 T _cplus_atomic_fetch_nand_llong` |
| `_cplus_atomic_fetch_nand_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001410 T _cplus_atomic_fetch_nand_long` |
| `_cplus_atomic_fetch_nand_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000d08 T _cplus_atomic_fetch_nand_uint` |
| `_cplus_atomic_fetch_nand_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002928 T _cplus_atomic_fetch_nand_ullong` |
| `_cplus_atomic_fetch_nand_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001b18 T _cplus_atomic_fetch_nand_ulong` |
| `_cplus_atomic_fetch_or_explicit_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000510 T _cplus_atomic_fetch_or_explicit_int` |
| `_cplus_atomic_fetch_or_explicit_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002130 T _cplus_atomic_fetch_or_explicit_llong` |
| `_cplus_atomic_fetch_or_explicit_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001320 T _cplus_atomic_fetch_or_explicit_long` |
| `_cplus_atomic_fetch_or_explicit_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000c18 T _cplus_atomic_fetch_or_explicit_uint` |
| `_cplus_atomic_fetch_or_explicit_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002838 T _cplus_atomic_fetch_or_explicit_ullong` |
| `_cplus_atomic_fetch_or_explicit_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001a28 T _cplus_atomic_fetch_or_explicit_ulong` |
| `_cplus_atomic_fetch_or_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000508 T _cplus_atomic_fetch_or_int` |
| `_cplus_atomic_fetch_or_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002128 T _cplus_atomic_fetch_or_llong` |
| `_cplus_atomic_fetch_or_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001318 T _cplus_atomic_fetch_or_long` |
| `_cplus_atomic_fetch_or_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000c10 T _cplus_atomic_fetch_or_uint` |
| `_cplus_atomic_fetch_or_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002830 T _cplus_atomic_fetch_or_ullong` |
| `_cplus_atomic_fetch_or_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001a20 T _cplus_atomic_fetch_or_ulong` |
| `_cplus_atomic_fetch_sub_explicit_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000004b4 T _cplus_atomic_fetch_sub_explicit_int` |
| `_cplus_atomic_fetch_sub_explicit_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000020d4 T _cplus_atomic_fetch_sub_explicit_llong` |
| `_cplus_atomic_fetch_sub_explicit_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000012c4 T _cplus_atomic_fetch_sub_explicit_long` |
| `_cplus_atomic_fetch_sub_explicit_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000bbc T _cplus_atomic_fetch_sub_explicit_uint` |
| `_cplus_atomic_fetch_sub_explicit_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000027dc T _cplus_atomic_fetch_sub_explicit_ullong` |
| `_cplus_atomic_fetch_sub_explicit_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000019cc T _cplus_atomic_fetch_sub_explicit_ulong` |
| `_cplus_atomic_fetch_sub_int` | COVERED | runtime_diag | hardened | `00000000000004a8 T _cplus_atomic_fetch_sub_int` |
| `_cplus_atomic_fetch_sub_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000020c8 T _cplus_atomic_fetch_sub_llong` |
| `_cplus_atomic_fetch_sub_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000012b8 T _cplus_atomic_fetch_sub_long` |
| `_cplus_atomic_fetch_sub_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000bb0 T _cplus_atomic_fetch_sub_uint` |
| `_cplus_atomic_fetch_sub_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000027d0 T _cplus_atomic_fetch_sub_ullong` |
| `_cplus_atomic_fetch_sub_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000019c0 T _cplus_atomic_fetch_sub_ulong` |
| `_cplus_atomic_fetch_xor_explicit_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000055c T _cplus_atomic_fetch_xor_explicit_int` |
| `_cplus_atomic_fetch_xor_explicit_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000217c T _cplus_atomic_fetch_xor_explicit_llong` |
| `_cplus_atomic_fetch_xor_explicit_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000136c T _cplus_atomic_fetch_xor_explicit_long` |
| `_cplus_atomic_fetch_xor_explicit_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000c64 T _cplus_atomic_fetch_xor_explicit_uint` |
| `_cplus_atomic_fetch_xor_explicit_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002884 T _cplus_atomic_fetch_xor_explicit_ullong` |
| `_cplus_atomic_fetch_xor_explicit_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001a74 T _cplus_atomic_fetch_xor_explicit_ulong` |
| `_cplus_atomic_fetch_xor_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000554 T _cplus_atomic_fetch_xor_int` |
| `_cplus_atomic_fetch_xor_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002174 T _cplus_atomic_fetch_xor_llong` |
| `_cplus_atomic_fetch_xor_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001364 T _cplus_atomic_fetch_xor_long` |
| `_cplus_atomic_fetch_xor_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000c5c T _cplus_atomic_fetch_xor_uint` |
| `_cplus_atomic_fetch_xor_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000287c T _cplus_atomic_fetch_xor_ullong` |
| `_cplus_atomic_fetch_xor_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001a6c T _cplus_atomic_fetch_xor_ulong` |
| `_cplus_atomic_flag_clear` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002f00 T _cplus_atomic_flag_clear` |
| `_cplus_atomic_flag_clear_explicit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002f08 T _cplus_atomic_flag_clear_explicit` |
| `_cplus_atomic_flag_test_and_set` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002e90 T _cplus_atomic_flag_test_and_set` |
| `_cplus_atomic_flag_test_and_set_explicit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002ea4 T _cplus_atomic_flag_test_and_set_explicit` |
| `_cplus_atomic_init_int` | COVERED | runtime_diag | hardened | `0000000000000000 T _cplus_atomic_init_int` |
| `_cplus_atomic_init_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001c20 T _cplus_atomic_init_llong` |
| `_cplus_atomic_init_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000e10 T _cplus_atomic_init_long` |
| `_cplus_atomic_init_ptr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002a30 T _cplus_atomic_init_ptr` |
| `_cplus_atomic_init_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000708 T _cplus_atomic_init_uint` |
| `_cplus_atomic_init_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002328 T _cplus_atomic_init_ullong` |
| `_cplus_atomic_init_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001518 T _cplus_atomic_init_ulong` |
| `_cplus_atomic_is_lock_free_int` | BLOCKED | [runtime_diag] BLOCKED cplus_atomic_is_lock_free_int nm=present(T) reason=host-specific result; safe Rust has no equivalent guaranteed-matching query | hardened | `0000000000000008 T _cplus_atomic_is_lock_free_int` |
| `_cplus_atomic_is_lock_free_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001c28 T _cplus_atomic_is_lock_free_llong` |
| `_cplus_atomic_is_lock_free_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000e18 T _cplus_atomic_is_lock_free_long` |
| `_cplus_atomic_is_lock_free_ptr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002a38 T _cplus_atomic_is_lock_free_ptr` |
| `_cplus_atomic_is_lock_free_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000710 T _cplus_atomic_is_lock_free_uint` |
| `_cplus_atomic_is_lock_free_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002330 T _cplus_atomic_is_lock_free_ullong` |
| `_cplus_atomic_is_lock_free_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001520 T _cplus_atomic_is_lock_free_ulong` |
| `_cplus_atomic_load_explicit_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000054 T _cplus_atomic_load_explicit_int` |
| `_cplus_atomic_load_explicit_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001c74 T _cplus_atomic_load_explicit_llong` |
| `_cplus_atomic_load_explicit_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000e64 T _cplus_atomic_load_explicit_long` |
| `_cplus_atomic_load_explicit_ptr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002a84 T _cplus_atomic_load_explicit_ptr` |
| `_cplus_atomic_load_explicit_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000075c T _cplus_atomic_load_explicit_uint` |
| `_cplus_atomic_load_explicit_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000237c T _cplus_atomic_load_explicit_ullong` |
| `_cplus_atomic_load_explicit_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000156c T _cplus_atomic_load_explicit_ulong` |
| `_cplus_atomic_load_int` | COVERED | runtime_diag | hardened | `000000000000004c T _cplus_atomic_load_int` |
| `_cplus_atomic_load_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001c6c T _cplus_atomic_load_llong` |
| `_cplus_atomic_load_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000e5c T _cplus_atomic_load_long` |
| `_cplus_atomic_load_ptr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002a7c T _cplus_atomic_load_ptr` |
| `_cplus_atomic_load_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000754 T _cplus_atomic_load_uint` |
| `_cplus_atomic_load_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002374 T _cplus_atomic_load_ullong` |
| `_cplus_atomic_load_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001564 T _cplus_atomic_load_ulong` |
| `_cplus_atomic_signal_fence` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002f5c T _cplus_atomic_signal_fence` |
| `_cplus_atomic_store_explicit_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000002c T _cplus_atomic_store_explicit_int` |
| `_cplus_atomic_store_explicit_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001c4c T _cplus_atomic_store_explicit_llong` |
| `_cplus_atomic_store_explicit_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000e3c T _cplus_atomic_store_explicit_long` |
| `_cplus_atomic_store_explicit_ptr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002a5c T _cplus_atomic_store_explicit_ptr` |
| `_cplus_atomic_store_explicit_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000734 T _cplus_atomic_store_explicit_uint` |
| `_cplus_atomic_store_explicit_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002354 T _cplus_atomic_store_explicit_ullong` |
| `_cplus_atomic_store_explicit_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001544 T _cplus_atomic_store_explicit_ulong` |
| `_cplus_atomic_store_int` | COVERED | runtime_diag | hardened | `0000000000000024 T _cplus_atomic_store_int` |
| `_cplus_atomic_store_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001c44 T _cplus_atomic_store_llong` |
| `_cplus_atomic_store_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000e34 T _cplus_atomic_store_long` |
| `_cplus_atomic_store_ptr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002a54 T _cplus_atomic_store_ptr` |
| `_cplus_atomic_store_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000072c T _cplus_atomic_store_uint` |
| `_cplus_atomic_store_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000234c T _cplus_atomic_store_ullong` |
| `_cplus_atomic_store_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000153c T _cplus_atomic_store_ulong` |
| `_cplus_atomic_thread_fence` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002f28 T _cplus_atomic_thread_fence` |
| `_cplus_bsearch` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001f0 T _cplus_bsearch` |
| `_cplus_c16rtomb` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000178 T _cplus_c16rtomb` |
| `_cplus_c32rtomb` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002f0 T _cplus_c32rtomb` |
| `_cplus_cabs` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_cabs` |
| `_cplus_cabsf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000234 T _cplus_cabsf` |
| `_cplus_cabsl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000005fc T _cplus_cabsl` |
| `_cplus_cacos` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_cacos` |
| `_cplus_cacosf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000168 T _cplus_cacosf` |
| `_cplus_cacosh` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000b4 T _cplus_cacosh` |
| `_cplus_cacoshf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000021c T _cplus_cacoshf` |
| `_cplus_cacoshl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000384 T _cplus_cacoshl` |
| `_cplus_cacosl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002d0 T _cplus_cacosl` |
| `_cplus_call_once` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000a78 T _cplus_call_once` |
| `_cplus_calloc` | COVERED | alloc | hardened | `0000000000000440 T _cplus_calloc` |
| `_cplus_carg` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000003c T _cplus_carg` |
| `_cplus_cargf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000270 T _cplus_cargf` |
| `_cplus_cargl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000638 T _cplus_cargl` |
| `_cplus_casin` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000003c T _cplus_casin` |
| `_cplus_casinf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001a4 T _cplus_casinf` |
| `_cplus_casinh` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000f0 T _cplus_casinh` |
| `_cplus_casinhf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000258 T _cplus_casinhf` |
| `_cplus_casinhl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000003c0 T _cplus_casinhl` |
| `_cplus_casinl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000030c T _cplus_casinl` |
| `_cplus_catan` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000078 T _cplus_catan` |
| `_cplus_catanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001e0 T _cplus_catanf` |
| `_cplus_catanh` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000012c T _cplus_catanh` |
| `_cplus_catanhf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000294 T _cplus_catanhf` |
| `_cplus_catanhl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000003fc T _cplus_catanhl` |
| `_cplus_catanl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000348 T _cplus_catanl` |
| `_cplus_cbrt` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001a8 T _cplus_cbrt` |
| `_cplus_cbrtf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000488 T _cplus_cbrtf` |
| `_cplus_cbrtl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000488 T _cplus_cbrtl` |
| `_cplus_ccos` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000003c T _cplus_ccos` |
| `_cplus_ccosf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000404 T _cplus_ccosf` |
| `_cplus_ccosh` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000f0 T _cplus_ccosh` |
| `_cplus_ccoshf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000004b8 T _cplus_ccoshf` |
| `_cplus_ccoshl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000880 T _cplus_ccoshl` |
| `_cplus_ccosl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000007cc T _cplus_ccosl` |
| `_cplus_ceil` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000210 T _cplus_ceil` |
| `_cplus_ceilf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000052c T _cplus_ceilf` |
| `_cplus_ceill` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000052c T _cplus_ceill` |
| `_cplus_cexp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000011c T _cplus_cexp` |
| `_cplus_cexpf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000350 T _cplus_cexpf` |
| `_cplus_cexpl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000718 T _cplus_cexpl` |
| `_cplus_cimag` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000ac T _cplus_cimag` |
| `_cplus_cimagf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002e0 T _cplus_cimagf` |
| `_cplus_cimagl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000006a8 T _cplus_cimagl` |
| `_cplus_clearerr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000284 T _cplus_clearerr` |
| `_cplus_clock` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000070 T _cplus_clock` |
| `_cplus_clock_gettime` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_clock_gettime` |
| `_cplus_clog` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000168 T _cplus_clog` |
| `_cplus_clogf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000530 T _cplus_clogf` |
| `_cplus_clogl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000008f8 T _cplus_clogl` |
| `_cplus_cnd_broadcast` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000874 T _cplus_cnd_broadcast` |
| `_cplus_cnd_destroy` | COVERED | threading | hardened | `0000000000000a34 T _cplus_cnd_destroy` |
| `_cplus_cnd_init` | COVERED | threading | hardened | `0000000000000768 T _cplus_cnd_init` |
| `_cplus_cnd_signal` | COVERED | threading | hardened | `00000000000007f0 T _cplus_cnd_signal` |
| `_cplus_cnd_timedwait` | COVERED | threading | hardened | `000000000000098c T _cplus_cnd_timedwait` |
| `_cplus_cnd_wait` | COVERED | threading | hardened | `00000000000008f8 T _cplus_cnd_wait` |
| `_cplus_conj` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000e0 T _cplus_conj` |
| `_cplus_conjf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000314 T _cplus_conjf` |
| `_cplus_conjl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000006dc T _cplus_conjl` |
| `_cplus_copysign` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002f0 T _cplus_copysign` |
| `_cplus_copysignf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000008a4 T _cplus_copysignf` |
| `_cplus_copysignl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000009e4 T _cplus_copysignl` |
| `_cplus_cos` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000009c T _cplus_cos` |
| `_cplus_cosf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000034 T _cplus_cosf` |
| `_cplus_cosh` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000010c T _cplus_cosh` |
| `_cplus_coshf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001a8 T _cplus_coshf` |
| `_cplus_coshl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001a8 T _cplus_coshl` |
| `_cplus_cosl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000034 T _cplus_cosl` |
| `_cplus_cpow` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001a4 T _cplus_cpow` |
| `_cplus_cpowf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000056c T _cplus_cpowf` |
| `_cplus_cpowl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000934 T _cplus_cpowl` |
| `_cplus_cproj` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001f8 T _cplus_cproj` |
| `_cplus_cprojf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000005c0 T _cplus_cprojf` |
| `_cplus_cprojl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000988 T _cplus_cprojl` |
| `_cplus_creal` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000078 T _cplus_creal` |
| `_cplus_crealf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002ac T _cplus_crealf` |
| `_cplus_creall` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000674 T _cplus_creall` |
| `_cplus_csin` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_csin` |
| `_cplus_csinf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000003c8 T _cplus_csinf` |
| `_cplus_csinh` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000b4 T _cplus_csinh` |
| `_cplus_csinhf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000047c T _cplus_csinhf` |
| `_cplus_csinhl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000844 T _cplus_csinhl` |
| `_cplus_csinl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000790 T _cplus_csinl` |
| `_cplus_csqrt` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000158 T _cplus_csqrt` |
| `_cplus_csqrtf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000038c T _cplus_csqrtf` |
| `_cplus_csqrtl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000754 T _cplus_csqrtl` |
| `_cplus_ctan` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000078 T _cplus_ctan` |
| `_cplus_ctanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000440 T _cplus_ctanf` |
| `_cplus_ctanh` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000012c T _cplus_ctanh` |
| `_cplus_ctanhf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000004f4 T _cplus_ctanhf` |
| `_cplus_ctanhl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000008bc T _cplus_ctanhl` |
| `_cplus_ctanl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000808 T _cplus_ctanl` |
| `_cplus_ctime` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000054 T _cplus_ctime` |
| `_cplus_ctime_r` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001d4 T _cplus_ctime_r` |
| `_cplus_difftime` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000034 T _cplus_difftime` |
| `_cplus_div` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_div` |
| `_cplus_drand48` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000104 T _cplus_drand48` |
| `_cplus_ecvt` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_ecvt` |
| `_cplus_erand48` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000128 T _cplus_erand48` |
| `_cplus_erf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001ec T _cplus_erf` |
| `_cplus_erfc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000220 T _cplus_erfc` |
| `_cplus_erfcf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000a4c T _cplus_erfcf` |
| `_cplus_erfcl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000073c T _cplus_erfcl` |
| `_cplus_erff` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000a18 T _cplus_erff` |
| `_cplus_erfl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000708 T _cplus_erfl` |
| `_cplus_exit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000ac T _cplus_exit` |
| `_cplus_exp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000140 T _cplus_exp` |
| `_cplus_exp10` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_exp10` |
| `_cplus_exp10f` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000c0 T _cplus_exp10f` |
| `_cplus_exp10l` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000190 T _cplus_exp10l` |
| `_cplus_exp16` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000011c T _cplus_exp16` |
| `_cplus_exp2` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000011c T _cplus_exp2` |
| `_cplus_exp2f` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002e0 T _cplus_exp2f` |
| `_cplus_exp2l` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002e0 T _cplus_exp2l` |
| `_cplus_expf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002ac T _cplus_expf` |
| `_cplus_expl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002ac T _cplus_expl` |
| `_cplus_expm1` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000011c T _cplus_expm1` |
| `_cplus_expm1f` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000314 T _cplus_expm1f` |
| `_cplus_expm1l` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000314 T _cplus_expm1l` |
| `_cplus_fabs` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_fabs` |
| `_cplus_fabsf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000708 T _cplus_fabsf` |
| `_cplus_fabsl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000007d8 T _cplus_fabsl` |
| `_cplus_fadd` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_fadd` |
| `_cplus_faddl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000074 T _cplus_faddl` |
| `_cplus_fclose` | COVERED | io | hardened | `000000000000016c T _cplus_fclose` |
| `_cplus_fcvt` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000005c T _cplus_fcvt` |
| `_cplus_fdim` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000070 T _cplus_fdim` |
| `_cplus_fdimf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000007b4 T _cplus_fdimf` |
| `_cplus_fdiml` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000008f4 T _cplus_fdiml` |
| `_cplus_fdiv` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000210 T _cplus_fdiv` |
| `_cplus_fdivl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000284 T _cplus_fdivl` |
| `_cplus_fdopen` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000178 T _cplus_fdopen` |
| `_cplus_feclearexcept` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_feclearexcept` |
| `_cplus_fegetenv` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000c0 T _cplus_fegetenv` |
| `_cplus_fegetexceptflag` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000034 T _cplus_fegetexceptflag` |
| `_cplus_fegetround` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000068 T _cplus_fegetround` |
| `_cplus_feholdexcept` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000ec T _cplus_feholdexcept` |
| `_cplus_feof` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000021c T _cplus_feof` |
| `_cplus_feraiseexcept` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_feraiseexcept` |
| `_cplus_ferror` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000250 T _cplus_ferror` |
| `_cplus_fesetenv` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000114 T _cplus_fesetenv` |
| `_cplus_fesetexceptflag` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000090 T _cplus_fesetexceptflag` |
| `_cplus_fesetround` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000008c T _cplus_fesetround` |
| `_cplus_fetestexcept` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000034 T _cplus_fetestexcept` |
| `_cplus_feupdateenv` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000140 T _cplus_feupdateenv` |
| `_cplus_fflush` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000080 T _cplus_fflush` |
| `_cplus_fgetc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002e0 T _cplus_fgetc` |
| `_cplus_fgetpos` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000090 T _cplus_fgetpos` |
| `_cplus_fgets` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000124 T _cplus_fgets` |
| `_cplus_fgetwc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001ec T _cplus_fgetwc` |
| `_cplus_fgetws` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000200 T _cplus_fgetws` |
| `_cplus_fileno` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000144 T _cplus_fileno` |
| `_cplus_floor` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001dc T _cplus_floor` |
| `_cplus_floorf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000004f8 T _cplus_floorf` |
| `_cplus_floorl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000004f8 T _cplus_floorl` |
| `_cplus_fma` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_fma` |
| `_cplus_fma16` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000009c T _cplus_fma16` |
| `_cplus_fmaf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000860 T _cplus_fmaf` |
| `_cplus_fmal` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000009a0 T _cplus_fmal` |
| `_cplus_fmax` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002b8 T _cplus_fmax` |
| `_cplus_fmaxf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000828 T _cplus_fmaxf` |
| `_cplus_fmaxl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000968 T _cplus_fmaxl` |
| `_cplus_fmin` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000280 T _cplus_fmin` |
| `_cplus_fminf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000007f0 T _cplus_fminf` |
| `_cplus_fminl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000930 T _cplus_fminl` |
| `_cplus_fmod` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000244 T _cplus_fmod` |
| `_cplus_fmodf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000073c T _cplus_fmodf` |
| `_cplus_fmodl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000080c T _cplus_fmodl` |
| `_cplus_fmul` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000160 T _cplus_fmul` |
| `_cplus_fmull` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001d4 T _cplus_fmull` |
| `_cplus_fopen` | COVERED | io | hardened | `00000000000000fc T _cplus_fopen` |
| `_cplus_fprintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000734 T _cplus_fprintf` |
| `_cplus_fputc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000334 T _cplus_fputc` |
| `_cplus_fputs` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000170 T _cplus_fputs` |
| `_cplus_fputwc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000190 T _cplus_fputwc` |
| `_cplus_fputws` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000024c T _cplus_fputws` |
| `_cplus_fread` | COVERED | io | hardened | `0000000000000494 T _cplus_fread` |
| `_cplus_free` | COVERED | alloc | hardened | `0000000000000acc T _cplus_free` |
| `_cplus_free_checked` | COVERED | alloc | hardened | `00000000000008c0 T _cplus_free_checked` |
| `_cplus_freopen` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001cc T _cplus_freopen` |
| `_cplus_frexp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000374 T _cplus_frexp` |
| `_cplus_frexp16` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000050 T _cplus_frexp16` |
| `_cplus_frexpf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000b94 T _cplus_frexpf` |
| `_cplus_frexpl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000a68 T _cplus_frexpl` |
| `_cplus_fscanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000808 T _cplus_fscanf` |
| `_cplus_fseek` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_fseek` |
| `_cplus_fsetpos` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000cc T _cplus_fsetpos` |
| `_cplus_fsub` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000b0 T _cplus_fsub` |
| `_cplus_fsubl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000124 T _cplus_fsubl` |
| `_cplus_ftell` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000004c T _cplus_ftell` |
| `_cplus_fwprintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000c0 T _cplus_fwprintf` |
| `_cplus_fwrite` | COVERED | io | hardened | `0000000000000530 T _cplus_fwrite` |
| `_cplus_fwscanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000460 T _cplus_fwscanf` |
| `_cplus_gcvt` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000b8 T _cplus_gcvt` |
| `_cplus_getc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001ac T _cplus_getc` |
| `_cplus_getchar` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002ec T _cplus_getchar` |
| `_cplus_getenv` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000d8 T _cplus_getenv` |
| `_cplus_getwc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000029c T _cplus_getwc` |
| `_cplus_getwchar` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000330 T _cplus_getwchar` |
| `_cplus_gmtime` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000094 T _cplus_gmtime` |
| `_cplus_gmtime_r` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000b4 T _cplus_gmtime_r` |
| `_cplus_hypot` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001dc T _cplus_hypot` |
| `_cplus_hypot3` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_hypot3` |
| `_cplus_hypotf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000004bc T _cplus_hypotf` |
| `_cplus_hypotl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000004bc T _cplus_hypotl` |
| `_cplus_ilogb` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000e8 T _cplus_ilogb` |
| `_cplus_ilogbf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000009b0 T _cplus_ilogbf` |
| `_cplus_ilogbl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000bc0 T _cplus_ilogbl` |
| `_cplus_imaxabs` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000160 T _cplus_imaxabs` |
| `_cplus_imaxdiv` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000194 T _cplus_imaxdiv` |
| `_cplus_isalnum` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000108 T _cplus_isalnum` |
| `_cplus_isalpha` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_isalpha` |
| `_cplus_isascii` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_isascii` |
| `_cplus_isblank` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_isblank` |
| `_cplus_iscntrl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000003ac T _cplus_iscntrl` |
| `_cplus_isdigit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000064 T _cplus_isdigit` |
| `_cplus_isgraph` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002e4 T _cplus_isgraph` |
| `_cplus_islower` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001d4 T _cplus_islower` |
| `_cplus_isprint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000280 T _cplus_isprint` |
| `_cplus_ispunct` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000348 T _cplus_ispunct` |
| `_cplus_isspace` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000a4 T _cplus_isspace` |
| `_cplus_isupper` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000170 T _cplus_isupper` |
| `_cplus_iswalnum` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000011c T _cplus_iswalnum` |
| `_cplus_iswalpha` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_iswalpha` |
| `_cplus_iswblank` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000430 T _cplus_iswblank` |
| `_cplus_iswcntrl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000003cc T _cplus_iswcntrl` |
| `_cplus_iswctype` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000550 T _cplus_iswctype` |
| `_cplus_iswdigit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000064 T _cplus_iswdigit` |
| `_cplus_iswgraph` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000304 T _cplus_iswgraph` |
| `_cplus_iswlower` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001e8 T _cplus_iswlower` |
| `_cplus_iswprint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002a0 T _cplus_iswprint` |
| `_cplus_iswpunct` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000368 T _cplus_iswpunct` |
| `_cplus_iswspace` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000b8 T _cplus_iswspace` |
| `_cplus_iswupper` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000184 T _cplus_iswupper` |
| `_cplus_iswxdigit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000024c T _cplus_iswxdigit` |
| `_cplus_isxdigit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000238 T _cplus_isxdigit` |
| `_cplus_kill` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002e0 T _cplus_kill` |
| `_cplus_kill_dependency_int` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000704 T _cplus_kill_dependency_int` |
| `_cplus_kill_dependency_llong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002324 T _cplus_kill_dependency_llong` |
| `_cplus_kill_dependency_long` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001514 T _cplus_kill_dependency_long` |
| `_cplus_kill_dependency_ptr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002e8c T _cplus_kill_dependency_ptr` |
| `_cplus_kill_dependency_uint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000e0c T _cplus_kill_dependency_uint` |
| `_cplus_kill_dependency_ullong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000002a2c T _cplus_kill_dependency_ullong` |
| `_cplus_kill_dependency_ulong` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000001c1c T _cplus_kill_dependency_ulong` |
| `_cplus_labs` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000038 T _cplus_labs` |
| `_cplus_ldexp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000330 T _cplus_ldexp` |
| `_cplus_ldexpf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000008e4 T _cplus_ldexpf` |
| `_cplus_ldexpl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000a24 T _cplus_ldexpl` |
| `_cplus_ldiv` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000040 T _cplus_ldiv` |
| `_cplus_lgamma` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000288 T _cplus_lgamma` |
| `_cplus_lgammaf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000ab4 T _cplus_lgammaf` |
| `_cplus_lgammal` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000007a4 T _cplus_lgammal` |
| `_cplus_libc_log` | COVERED | json_log | hardened | `0000000000000018 T _cplus_libc_log` |
| `_cplus_libc_log_set_enabled` | COVERED | json_log | hardened | `0000000000000000 T _cplus_libc_log_set_enabled` |
| `_cplus_libc_set_unknown_capacity_mode` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_libc_set_unknown_capacity_mode` |
| `_cplus_llabs` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000b8 T _cplus_llabs` |
| `_cplus_lldiv` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000007c T _cplus_lldiv` |
| `_cplus_llrint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001bc T _cplus_llrint` |
| `_cplus_llrintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000668 T _cplus_llrintf` |
| `_cplus_llrintl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000668 T _cplus_llrintl` |
| `_cplus_llround` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000a4 T _cplus_llround` |
| `_cplus_llroundf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000006d4 T _cplus_llroundf` |
| `_cplus_llroundl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000006d4 T _cplus_llroundl` |
| `_cplus_localeconv` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000003c T _cplus_localeconv` |
| `_cplus_localtime` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000c8 T _cplus_localtime` |
| `_cplus_localtime_r` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000114 T _cplus_localtime_r` |
| `_cplus_log` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000174 T _cplus_log` |
| `_cplus_log10` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001a8 T _cplus_log10` |
| `_cplus_log10f` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000003b0 T _cplus_log10f` |
| `_cplus_log10l` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000003b0 T _cplus_log10l` |
| `_cplus_log1p` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000e8 T _cplus_log1p` |
| `_cplus_log1pf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000003e4 T _cplus_log1pf` |
| `_cplus_log1pl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000003e4 T _cplus_log1pl` |
| `_cplus_log2` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000174 T _cplus_log2` |
| `_cplus_log2f` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000037c T _cplus_log2f` |
| `_cplus_log2l` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000037c T _cplus_log2l` |
| `_cplus_logb` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000b4 T _cplus_logb` |
| `_cplus_logb16` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000e0 T _cplus_logb16` |
| `_cplus_logbf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000009e4 T _cplus_logbf` |
| `_cplus_logbl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000bf4 T _cplus_logbl` |
| `_cplus_logf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000348 T _cplus_logf` |
| `_cplus_logl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000348 T _cplus_logl` |
| `_cplus_lrand48` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000190 T _cplus_lrand48` |
| `_cplus_lrint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000184 T _cplus_lrint` |
| `_cplus_lrintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000630 T _cplus_lrintf` |
| `_cplus_lrintl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000630 T _cplus_lrintl` |
| `_cplus_lround` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000070 T _cplus_lround` |
| `_cplus_lroundf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000006a0 T _cplus_lroundf` |
| `_cplus_lroundl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000006a0 T _cplus_lroundl` |
| `_cplus_malloc` | COVERED | alloc | hardened | `0000000000000354 T _cplus_malloc` |
| `_cplus_mblen` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002c0 T _cplus_mblen` |
| `_cplus_mbrtoc16` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000003c T _cplus_mbrtoc16` |
| `_cplus_mbrtoc32` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000238 T _cplus_mbrtoc32` |
| `_cplus_mbrtowc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_mbrtowc` |
| `_cplus_mbsinit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000288 T _cplus_mbsinit` |
| `_cplus_mbsrtowcs` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000a0 T _cplus_mbsrtowcs` |
| `_cplus_mbstowcs` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000384 T _cplus_mbstowcs` |
| `_cplus_mbtowc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002fc T _cplus_mbtowc` |
| `_cplus_memccpy` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_memccpy` |
| `_cplus_memchr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_memchr` |
| `_cplus_memcmp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002cc T _cplus_memcmp` |
| `_cplus_memcmp_bounded` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000290 T _cplus_memcmp_bounded` |
| `_cplus_memcpy` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_memcpy` |
| `_cplus_memcpy_bounded` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000168 T _cplus_memcpy_bounded` |
| `_cplus_memmem` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000006f4 T _cplus_memmem` |
| `_cplus_memmove` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000070 T _cplus_memmove` |
| `_cplus_memmove_bounded` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001fc T _cplus_memmove_bounded` |
| `_cplus_memrchr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_memrchr` |
| `_cplus_memset` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000260 T _cplus_memset` |
| `_cplus_memset_bounded` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000014 T _cplus_memset_bounded` |
| `_cplus_memset_explicit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_memset_explicit` |
| `_cplus_mkdtemp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000034 T _cplus_mkdtemp` |
| `_cplus_mkstemp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_mkstemp` |
| `_cplus_mktime` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000fc T _cplus_mktime` |
| `_cplus_modf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000003dc T _cplus_modf` |
| `_cplus_modff` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000bfc T _cplus_modff` |
| `_cplus_modfl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000ad0 T _cplus_modfl` |
| `_cplus_mtx_destroy` | COVERED | threading | hardened | `0000000000000724 T _cplus_mtx_destroy` |
| `_cplus_mtx_init` | COVERED | threading | hardened | `000000000000037c T _cplus_mtx_init` |
| `_cplus_mtx_lock` | COVERED | threading | hardened | `0000000000000488 T _cplus_mtx_lock` |
| `_cplus_mtx_timedlock` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000050c T _cplus_mtx_timedlock` |
| `_cplus_mtx_trylock` | COVERED | threading | hardened | `000000000000061c T _cplus_mtx_trylock` |
| `_cplus_mtx_unlock` | COVERED | threading | hardened | `00000000000006a0 T _cplus_mtx_unlock` |
| `_cplus_nan` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_nan` |
| `_cplus_nanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000b60 T _cplus_nanf` |
| `_cplus_nanl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000ca0 T _cplus_nanl` |
| `_cplus_nanosleep` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000003c T _cplus_nanosleep` |
| `_cplus_nearbyint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000150 T _cplus_nearbyint` |
| `_cplus_nearbyintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000005c8 T _cplus_nearbyintf` |
| `_cplus_nearbyintl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000005c8 T _cplus_nearbyintl` |
| `_cplus_nextafter` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000034 T _cplus_nextafter` |
| `_cplus_nextafterf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000ae8 T _cplus_nextafterf` |
| `_cplus_nextafterl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000c28 T _cplus_nextafterl` |
| `_cplus_nextdown` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000b4 T _cplus_nextdown` |
| `_cplus_nextdownf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000f0 T _cplus_nextdownf` |
| `_cplus_nextdownl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000012c T _cplus_nextdownl` |
| `_cplus_nexttoward` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000034 T _cplus_nexttoward` |
| `_cplus_nexttowardf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000b24 T _cplus_nexttowardf` |
| `_cplus_nexttowardl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000c64 T _cplus_nexttowardl` |
| `_cplus_nextup` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_nextup` |
| `_cplus_nextupf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000003c T _cplus_nextupf` |
| `_cplus_nextupl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000078 T _cplus_nextupl` |
| `_cplus_perror` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000344 T _cplus_perror` |
| `_cplus_pow` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000104 T _cplus_pow` |
| `_cplus_powf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000418 T _cplus_powf` |
| `_cplus_powl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000418 T _cplus_powl` |
| `_cplus_printf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000006d0 T _cplus_printf` |
| `_cplus_pthread_cond_broadcast` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001d8 T _cplus_pthread_cond_broadcast` |
| `_cplus_pthread_cond_destroy` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000248 T _cplus_pthread_cond_destroy` |
| `_cplus_pthread_cond_init` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000168 T _cplus_pthread_cond_init` |
| `_cplus_pthread_cond_signal` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001a4 T _cplus_pthread_cond_signal` |
| `_cplus_pthread_cond_wait` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000020c T _cplus_pthread_cond_wait` |
| `_cplus_pthread_create` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_pthread_create` |
| `_cplus_pthread_join` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000054 T _cplus_pthread_join` |
| `_cplus_pthread_mutex_destroy` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000134 T _cplus_pthread_mutex_destroy` |
| `_cplus_pthread_mutex_init` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000090 T _cplus_pthread_mutex_init` |
| `_cplus_pthread_mutex_lock` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000cc T _cplus_pthread_mutex_lock` |
| `_cplus_pthread_mutex_unlock` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000100 T _cplus_pthread_mutex_unlock` |
| `_cplus_putc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001e0 T _cplus_putc` |
| `_cplus_putchar` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002b8 T _cplus_putchar` |
| `_cplus_putenv` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000080 T _cplus_putenv` |
| `_cplus_puts` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000310 T _cplus_puts` |
| `_cplus_putwc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000240 T _cplus_putwc` |
| `_cplus_putwchar` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002f0 T _cplus_putwchar` |
| `_cplus_qsort` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000019c T _cplus_qsort` |
| `_cplus_qsort_r` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000a4 T _cplus_qsort_r` |
| `_cplus_quick_exit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000bc T _cplus_quick_exit` |
| `_cplus_raise` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000003c T _cplus_raise` |
| `_cplus_rand` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_rand` |
| `_cplus_rand_r` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000b4 T _cplus_rand_r` |
| `_cplus_realloc` | COVERED | alloc | hardened | `0000000000000554 T _cplus_realloc` |
| `_cplus_realpath` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000068 T _cplus_realpath` |
| `_cplus_remainder` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000ac T _cplus_remainder` |
| `_cplus_remainderf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000778 T _cplus_remainderf` |
| `_cplus_remainderl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000848 T _cplus_remainderl` |
| `_cplus_remove` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000b4 T _cplus_remove` |
| `_cplus_remquo` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000044 T _cplus_remquo` |
| `_cplus_remquof` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000c64 T _cplus_remquof` |
| `_cplus_remquol` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000884 T _cplus_remquol` |
| `_cplus_rename` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000e8 T _cplus_rename` |
| `_cplus_rewind` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000290 T _cplus_rewind` |
| `_cplus_rint` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_rint` |
| `_cplus_rintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000005fc T _cplus_rintf` |
| `_cplus_rintl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000005fc T _cplus_rintl` |
| `_cplus_round` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000218 T _cplus_round` |
| `_cplus_roundeven` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_roundeven` |
| `_cplus_roundevenf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000034 T _cplus_roundevenf` |
| `_cplus_roundevenl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000068 T _cplus_roundevenl` |
| `_cplus_roundf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000594 T _cplus_roundf` |
| `_cplus_roundl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000594 T _cplus_roundl` |
| `_cplus_sc_upstream_bsearch_s` | COVERED | search | hardened | `0000000000000000 T _cplus_sc_upstream_bsearch_s` |
| `_cplus_sc_upstream_memrchr_s` | COVERED | search | hardened | `00000000000000d4 T _cplus_sc_upstream_memrchr_s` |
| `_cplus_scalbln` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000d8 T _cplus_scalbln` |
| `_cplus_scalblnf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000096c T _cplus_scalblnf` |
| `_cplus_scalblnl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000b7c T _cplus_scalblnl` |
| `_cplus_scalbn` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001f4 T _cplus_scalbn` |
| `_cplus_scalbnf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000928 T _cplus_scalbnf` |
| `_cplus_scalbnl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000b38 T _cplus_scalbnl` |
| `_cplus_scanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000007a4 T _cplus_scanf` |
| `_cplus_seed48` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000015c T _cplus_seed48` |
| `_cplus_setbuf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000054 T _cplus_setbuf` |
| `_cplus_setenv` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_setenv` |
| `_cplus_setlocale` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_setlocale` |
| `_cplus_setvbuf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_setvbuf` |
| `_cplus_sigaction` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001f4 T _cplus_sigaction` |
| `_cplus_sigaddset` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000a4 T _cplus_sigaddset` |
| `_cplus_sigdelset` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000114 T _cplus_sigdelset` |
| `_cplus_sigemptyset` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_sigemptyset` |
| `_cplus_sigfillset` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000050 T _cplus_sigfillset` |
| `_cplus_sigismember` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000188 T _cplus_sigismember` |
| `_cplus_signal` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_signal` |
| `_cplus_sigprocmask` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000240 T _cplus_sigprocmask` |
| `_cplus_sigsuspend` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000028c T _cplus_sigsuspend` |
| `_cplus_sigwait` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000031c T _cplus_sigwait` |
| `_cplus_sin` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000068 T _cplus_sin` |
| `_cplus_sinf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_sinf` |
| `_cplus_sinh` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000d8 T _cplus_sinh` |
| `_cplus_sinhf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000174 T _cplus_sinhf` |
| `_cplus_sinhl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000174 T _cplus_sinhl` |
| `_cplus_sinl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_sinl` |
| `_cplus_snprintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000064c T _cplus_snprintf` |
| `_cplus_sprintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000878 T _cplus_sprintf` |
| `_cplus_sqrt` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000034 T _cplus_sqrt` |
| `_cplus_sqrtf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000454 T _cplus_sqrtf` |
| `_cplus_sqrtl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000454 T _cplus_sqrtl` |
| `_cplus_srand` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000024 T _cplus_srand` |
| `_cplus_sscanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000095c T _cplus_sscanf` |
| `_cplus_stdio_register` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_stdio_register` |
| `_cplus_stpcpy` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000264 T _cplus_stpcpy` |
| `_cplus_stpncpy` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000384 T _cplus_stpncpy` |
| `_cplus_strcasecmp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000a4 T _cplus_strcasecmp` |
| `_cplus_strcasestr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000d20 T _cplus_strcasestr` |
| `_cplus_strcat` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000154 T _cplus_strcat` |
| `_cplus_strcat_bounded` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000480 T _cplus_strcat_bounded` |
| `_cplus_strchr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000008c T _cplus_strchr` |
| `_cplus_strchrnul` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000d98 T _cplus_strchrnul` |
| `_cplus_strcmp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000254 T _cplus_strcmp` |
| `_cplus_strcoll` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000034 T _cplus_strcoll` |
| `_cplus_strcpy` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000050 T _cplus_strcpy` |
| `_cplus_strcpy_bounded` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000348 T _cplus_strcpy_bounded` |
| `_cplus_strcspn` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000080 T _cplus_strcspn` |
| `_cplus_strdup` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001b8 T _cplus_strdup` |
| `_cplus_strerror` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_strerror` |
| `_cplus_strerror_r` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_strerror_r` |
| `_cplus_strftime` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000130 T _cplus_strftime` |
| `_cplus_strlcat` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000004c T _cplus_strlcat` |
| `_cplus_strlcpy` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_strlcpy` |
| `_cplus_strlen` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_strlen` |
| `_cplus_strncasecmp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000128 T _cplus_strncasecmp` |
| `_cplus_strncat` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001c4 T _cplus_strncat` |
| `_cplus_strncat_bounded` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000051c T _cplus_strncat_bounded` |
| `_cplus_strncmp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002e0 T _cplus_strncmp` |
| `_cplus_strncpy` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000b0 T _cplus_strncpy` |
| `_cplus_strncpy_bounded` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000003d8 T _cplus_strncpy_bounded` |
| `_cplus_strndup` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_strndup` |
| `_cplus_strnlen` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001bc T _cplus_strnlen` |
| `_cplus_strnstr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000098 T _cplus_strnstr` |
| `_cplus_strpbrk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000f8 T _cplus_strpbrk` |
| `_cplus_strrchr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000e8 T _cplus_strrchr` |
| `_cplus_strsep` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000684 T _cplus_strsep` |
| `_cplus_strsignal` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_strsignal` |
| `_cplus_strspn` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_strspn` |
| `_cplus_strstr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000013c T _cplus_strstr` |
| `_cplus_strtod` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000098 T _cplus_strtod` |
| `_cplus_strtof` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000e8 T _cplus_strtof` |
| `_cplus_strtoimax` | COVERED | format | hardened | `00000000000001d0 T _cplus_strtoimax` |
| `_cplus_strtok` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_strtok` |
| `_cplus_strtok_r` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000005f0 T _cplus_strtok_r` |
| `_cplus_strtol` | COVERED | format | hardened | `0000000000000104 T _cplus_strtol` |
| `_cplus_strtold` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000124 T _cplus_strtold` |
| `_cplus_strtoll` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_strtoll` |
| `_cplus_strtoul` | COVERED | format | hardened | `0000000000000150 T _cplus_strtoul` |
| `_cplus_strtoull` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000004c T _cplus_strtoull` |
| `_cplus_strtoumax` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000248 T _cplus_strtoumax` |
| `_cplus_strverscmp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000bbc T _cplus_strverscmp` |
| `_cplus_strxfrm` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000070 T _cplus_strxfrm` |
| `_cplus_swab` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_swab` |
| `_cplus_swprintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001a4 T _cplus_swprintf` |
| `_cplus_swprintf_checked` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002a8 T _cplus_swprintf_checked` |
| `_cplus_swscanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000544 T _cplus_swscanf` |
| `_cplus_system` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000010c T _cplus_system` |
| `_cplus_tan` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000d0 T _cplus_tan` |
| `_cplus_tanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000068 T _cplus_tanf` |
| `_cplus_tanh` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000140 T _cplus_tanh` |
| `_cplus_tanhf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001dc T _cplus_tanhf` |
| `_cplus_tanhl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001dc T _cplus_tanhl` |
| `_cplus_tanl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000068 T _cplus_tanl` |
| `_cplus_tgamma` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000254 T _cplus_tgamma` |
| `_cplus_tgammaf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000a80 T _cplus_tgammaf` |
| `_cplus_tgammal` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000770 T _cplus_tgammal` |
| `_cplus_thrd_create` | COVERED | threading | hardened | `0000000000000000 T _cplus_thrd_create` |
| `_cplus_thrd_current` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000160 T _cplus_thrd_current` |
| `_cplus_thrd_detach` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002fc T _cplus_thrd_detach` |
| `_cplus_thrd_equal` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000118 T _cplus_thrd_equal` |
| `_cplus_thrd_exit` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000228 T _cplus_thrd_exit` |
| `_cplus_thrd_join` | COVERED | threading | hardened | `0000000000000254 T _cplus_thrd_join` |
| `_cplus_thrd_sleep` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001a0 T _cplus_thrd_sleep` |
| `_cplus_thrd_yield` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000204 T _cplus_thrd_yield` |
| `_cplus_time` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_time` |
| `_cplus_timespec_get` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000078 T _cplus_timespec_get` |
| `_cplus_tmpfile` | COVERED | io | hardened | `000000000000024c T _cplus_tmpfile` |
| `_cplus_toascii` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000038 T _cplus_toascii` |
| `_cplus_tolower` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000045c T _cplus_tolower` |
| `_cplus_toupper` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000410 T _cplus_toupper` |
| `_cplus_towctrans` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000054 T _cplus_towctrans` |
| `_cplus_towlower` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000004c8 T _cplus_towlower` |
| `_cplus_towupper` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000494 T _cplus_towupper` |
| `_cplus_transfer_ownership` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000011f4 T _cplus_transfer_ownership` |
| `_cplus_trunc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000024c T _cplus_trunc` |
| `_cplus_truncf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000560 T _cplus_truncf` |
| `_cplus_truncl` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000560 T _cplus_truncl` |
| `_cplus_tss_create` | COVERED | threading | hardened | `0000000000000ac8 T _cplus_tss_create` |
| `_cplus_tss_delete` | COVERED | threading | hardened | `0000000000000c28 T _cplus_tss_delete` |
| `_cplus_tss_get` | COVERED | threading | hardened | `0000000000000b6c T _cplus_tss_get` |
| `_cplus_tss_set` | COVERED | threading | hardened | `0000000000000ba0 T _cplus_tss_set` |
| `_cplus_uchar_internal_canary_ok` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_uchar_internal_canary_ok` |
| `_cplus_ungetc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000108 T _cplus_ungetc` |
| `_cplus_ungetwc` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000360 T _cplus_ungetwc` |
| `_cplus_unsetenv` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000004c T _cplus_unsetenv` |
| `_cplus_vfprintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000a40 T _cplus_vfprintf` |
| `_cplus_vfscanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000b10 T _cplus_vfscanf` |
| `_cplus_vfwprintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000130 T _cplus_vfwprintf` |
| `_cplus_vfwscanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000004d0 T _cplus_vfwscanf` |
| `_cplus_vprintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000ab4 T _cplus_vprintf` |
| `_cplus_vscanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000011c T _cplus_vscanf` |
| `_cplus_vsnprintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000005cc T _cplus_vsnprintf` |
| `_cplus_vsprintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000008e8 T _cplus_vsprintf` |
| `_cplus_vsscanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000009cc T _cplus_vsscanf` |
| `_cplus_vswprintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000228 T _cplus_vswprintf` |
| `_cplus_vswscanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000005b4 T _cplus_vswscanf` |
| `_cplus_vwprintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000064 T _cplus_vwprintf` |
| `_cplus_vwscanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000404 T _cplus_vwscanf` |
| `_cplus_wcrtomb` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000054 T _cplus_wcrtomb` |
| `_cplus_wcscat` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000144 T _cplus_wcscat` |
| `_cplus_wcschr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000088 T _cplus_wcschr` |
| `_cplus_wcscmp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000bc T _cplus_wcscmp` |
| `_cplus_wcscoll` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_wcscoll` |
| `_cplus_wcscpy` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000034 T _cplus_wcscpy` |
| `_cplus_wcscspn` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000178 T _cplus_wcscspn` |
| `_cplus_wcsftime` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000380 T _cplus_wcsftime` |
| `_cplus_wcslen` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_wcslen` |
| `_cplus_wcsncmp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000f8 T _cplus_wcsncmp` |
| `_cplus_wcsncpy` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000070 T _cplus_wcsncpy` |
| `_cplus_wcspbrk` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000100 T _cplus_wcspbrk` |
| `_cplus_wcsrchr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000c4 T _cplus_wcsrchr` |
| `_cplus_wcsrtombs` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000118 T _cplus_wcsrtombs` |
| `_cplus_wcsspn` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000013c T _cplus_wcsspn` |
| `_cplus_wcsstr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000180 T _cplus_wcsstr` |
| `_cplus_wcstod` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001e0 T _cplus_wcstod` |
| `_cplus_wcstof` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000244 T _cplus_wcstof` |
| `_cplus_wcstoimax` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_wcstoimax` |
| `_cplus_wcstok` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000030c T _cplus_wcstok` |
| `_cplus_wcstol` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_wcstol` |
| `_cplus_wcstold` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002a8 T _cplus_wcstold` |
| `_cplus_wcstoll` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000078 T _cplus_wcstoll` |
| `_cplus_wcstombs` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000003fc T _cplus_wcstombs` |
| `_cplus_wcstoul` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000000f0 T _cplus_wcstoul` |
| `_cplus_wcstoull` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000168 T _cplus_wcstoull` |
| `_cplus_wcstoumax` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000078 T _cplus_wcstoumax` |
| `_cplus_wcsxfrm` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `000000000000003c T _cplus_wcsxfrm` |
| `_cplus_wctomb` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000348 T _cplus_wctomb` |
| `_cplus_wctrans` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_wctrans` |
| `_cplus_wctype` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000004fc T _cplus_wctype` |
| `_cplus_wmemchr` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001b4 T _cplus_wmemchr` |
| `_cplus_wmemcmp` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000002a0 T _cplus_wmemcmp` |
| `_cplus_wmemcpy` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000001bc T _cplus_wmemcpy` |
| `_cplus_wmemmove` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000208 T _cplus_wmemmove` |
| `_cplus_wmemset` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000254 T _cplus_wmemset` |
| `_cplus_wprintf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `0000000000000000 T _cplus_wprintf` |
| `_cplus_wscanf` | NOT EXERCISED | no Rust example models this wrapper; no explicit BLOCKED declaration | hardened | `00000000000003a0 T _cplus_wscanf` |

## Reproduction

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh benchmarks/rust_examples/run_all.sh
```

`run_all.sh` discovers every `<group>/compare.sh`, runs it under `timeout 300`
with a restricted PATH and `CPLUS_CLANG=/usr/bin/clang`, and prints the table
plus the verbatim `RUST_EXAMPLES ...` final lines. See `RESULTS.md` for the exact
table from a real run and the pinned toolchain versions.


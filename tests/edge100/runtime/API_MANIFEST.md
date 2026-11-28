# EDGE100 runtime — API manifest

Source of truth: `runtime/include/cplus/cplus.h` (this repository) and its
implementation `runtime/cplus_runtime.c`. The manifest covers every external
`cplus_*` entry point declared in the header plus the header-only helpers and
macros. The `harness.cp` case table has 104 cases; 39 are fatal-path cases
(expected non-zero exit) and 65 are normal cases.

> Note on toolchain: the C+ driver at `build/c+` delegates to the separate
> `c-plus-core` repository, which links a *different, newer* runtime. This
> suite therefore compiles `harness.cp` directly with `/usr/bin/clang` against
> **this** repository's header and `cplus_runtime.c`, so the results describe
> the runtime this repository actually ships.

## External entry points (27 functions)

| # | API (header line) | Contract | Cases in `harness.cp` |
|---|-------------------|----------|------------------------|
| 1 | `cplus_fatal` (82) | Diagnostic + `_Exit(code)` | `fatal_101`, `fatal_102`, `fatal_103`, `fatal_104`, `fatal_105`, `fatal_106`, `fatal_107`, `fatal_108`, `fatal_109`, `fatal_nullargs` |
| 2 | `cplus_check_bounds` (86) | 101 if `index >= count` | `bounds_valid`, `bounds_null_base_valid`, `bounds_fatal_eq`, `bounds_fatal_zero`, `bounds_fatal_huge` |
| 3 | `cplus_check_nonnull` (90) | 102 if `ptr == NULL` | `nonnull_valid`, `nonnull_fatal_null`, `nonnull_fatal_null_expr` |
| 4 | `cplus_alloc` (101) | Managed block, `size<=0 -> 1` | `alloc_valid`, `alloc_zero`, `alloc_large`, `alloc_many` |
| 5 | `cplus_zalloc` (102) | Zeroed managed block | `zalloc_zeroed`, `zalloc_zero_size` |
| 6 | `cplus_alloc_array` (104) | Overflow-checked zeroed array | `array_valid`, `array_zero_count`, `array_zero_elem`, `array_fatal_overflow` |
| 7 | `cplus_realloc` (105) | Fresh block, copies `min(old,new)` | `realloc_valid`, `realloc_null`, `realloc_zero`, `realloc_zero_null`, `realloc_shrink`, `realloc_fatal_uaf` |
| 8 | `cplus_zero` (108) | `memset` with NULL/0 tolerance | `zero_valid`, `zero_null`, `zero_len0` |
| 9 | `cplus_release` (112) | Single release; double/invalid fatal | `release_valid`, `release_null`, `release_fatal_double`, `release_fatal_stack`, `release_fatal_static` |
| 10 | `cplus_alloc_bytes` (118) | Recorded size of live block | `bytes_valid`, `bytes_fatal_null`, `bytes_fatal_freed`, `bytes_fatal_stack` |
| 11 | `cplus_check_live` (121) | 105 if not a live managed pointer | `live_valid`, `live_fatal_null`, `live_fatal_freed`, `live_fatal_stack` |
| 12 | `cplus_alloc_count` (122) | `bytes / elem_size`; 105 if elem 0 | `count_valid`, `count_elem1`, `count_fatal_zero_elem`, `count_fatal_null`, `count_fatal_freed` |
| 13 | `cplus_check_index` (127) | Returns index or 101 | `index_valid`, `index_fatal_eq`, `index_fatal_zero`, `index_fatal_huge` |
| 14 | `cplus_scope_push` (183) | Push cleanup record; -1 on NULL scope | `push_valid`, `push_null_scope`, `push_null_drop`, `push_null_ptr` |
| 15 | `cplus_scope_cleanup` (188) | Run records LIFO; idempotent | `cleanup_valid`, `cleanup_reverse`, `cleanup_double`, `cleanup_null_scope`, `cleanup_nested`, `cleanup_managed`, `cleanup_managed_drop_uaf`, `cleanup_fatal_double_free`, `cleanup_mixed` |
| 16 | `cplus_resource_check_borrow` (197) | 106 if `self == NULL` | `borrow_valid`, `borrow_fatal_null`, `borrow_fatal_null_expr` |
| 17 | `cplus_mutex_new` (205) | POSIX mutex, fatal 107 on OOM | `mutex_valid`, `mutex_twice`, `mutex_null_ops` |
| 18 | `cplus_mutex_lock` (206) | NULL-tolerant lock | `mutex_valid`, `mutex_twice`, `mutex_null_ops` |
| 19 | `cplus_mutex_unlock` (207) | NULL-tolerant unlock | `mutex_valid`, `mutex_twice`, `mutex_null_ops` |
| 20 | `cplus_mutex_free` (208) | NULL-tolerant free | `mutex_valid`, `mutex_twice`, `mutex_null_ops` |
| 21 | `cplus_tls_new` (211) | POSIX TLS slot | `tls_valid`, `tls_two_slots`, `tls_overwrite` |
| 22 | `cplus_tls_get` (212) | NULL slot -> NULL | `tls_valid`, `tls_null_ops`, `tls_overwrite`, `tls_two_slots` |
| 23 | `cplus_tls_set` (213) | NULL-tolerant set | `tls_valid`, `tls_null_ops`, `tls_overwrite`, `tls_two_slots` |
| 24 | `cplus_tls_free` (214) | NULL-tolerant free | `tls_valid`, `tls_null_ops`, `tls_overwrite`, `tls_two_slots` |
| 25 | `cplus_print_value` (295) | Typed varargs printer | `print_int`, `print_uint`, `print_double`, `print_char`, `print_bool`, `print_str`, `print_null_str`, `print_unknown`, `print_multi` |
| 26 | `cplus_print_end` (296) | End line / reset separator | all `print_*` cases (10) incl. `print_end_only` |
| 27 | `cplus_scan_value` (297) | Typed stdin read; 108/109 on error | `scan_int`, `scan_str_bounded`, `scan_float`, `scan_null_fatal`, `scan_unsupported_fatal`, `scan_eof_fatal` |

## Header-only helpers / macros (enumerated, not counted in `<apis>`)

| Helper (header line) | Cases / where tested |
|----------------------|----------------------|
| `cplus_exit_code` enum (68-80) | `fatal_101`..`fatal_109` exercise all nine domain codes (101-109) |
| `cplus_new(T)` (131) | `helper_new`; also the plain-C interop TU in `run.sh` |
| `cplus_new_array(T,N)` (138) | `helper_new_array` |
| `cplus_move(x)` (149) | `helper_move` (verifies move + zeroed source) |
| `countof(a)` (222) | `helper_countof` |
| `cplus_rotl32` (231) | `helper_rotl` (count 0/32/33 edge wrap) |
| `cplus_rotr32` (237) | `helper_rotr` (count 0/32 edge wrap) |
| `print(...)` / `scan(...)` macros (376/386) | Exercised through `cplus_print_value`/`cplus_print_end`/`cplus_scan_value` entry points directly (macro dispatch is a compile-time concern) |

## Edge-dimension coverage matrix

| Dimension | Representative cases |
|-----------|----------------------|
| valid | `alloc_valid`, `array_valid`, `realloc_valid`, `cleanup_valid`, `mutex_valid`, `tls_valid`, `print_int`, `scan_int`, all `helper_*` |
| NULL | `fatal_nullargs`, `bounds_null_base_valid`, `release_null`, `bytes_fatal_null`, `push_null_scope`, `cleanup_null_scope`, `mutex_null_ops`, `tls_null_ops`, `scan_null_fatal`, `print_null_str` |
| invalid handle | `release_fatal_stack`, `release_fatal_static`, `bytes_fatal_stack`, `live_fatal_stack`, `array_fatal_overflow` (overflow handle) |
| double cleanup | `release_fatal_double` (allocator), `cleanup_double` (scope idempotence), `cleanup_fatal_double_free` (scope -> double release, **finding**) |
| nested scopes | `cleanup_nested`, `cleanup_reverse` |
| fatal path via subprocess | all 39 fatal-expect cases; each runs in a `fork()`ed child that `_Exit`s with the runtime code, and the parent verifies both the exact code and the `C+ runtime error:` diagnostic |
| error code domain | `fatal_101`..`fatal_109` (101 BOUNDS, 102 NULL, 103 UAF, 104 DOUBLE_FREE, 105 TYPE_CONFUSION, 106 OWNERSHIP, 107 RESOURCE, 108 UNSUPPORTED, 109 IO); additionally `bounds_fatal_*` -> 101, `array_fatal_overflow`/`fatal_nullargs` -> 107, `nonnull_fatal_*` -> 102, `borrow_fatal_*` -> 106, `realloc_fatal_uaf` -> 105, `scan_unsupported_fatal` -> 108, `scan_null_fatal`/`scan_eof_fatal` -> 109, `print_unknown` -> `<unprintable>` |

## Interop coverage (`run.sh`)

- Plain C TU (`interop_c.c`) includes `cplus/cplus.h` and calls `cplus_new`,
  `cplus_alloc_bytes`, `cplus_release`.
- C++ TU (`interop_cpp.cpp`) includes the same header (exercising the
  `extern "C"` guard), calls `cplus_alloc`, `cplus_alloc_bytes`,
  `cplus_zalloc`, `cplus_release`, declares a `cplus_scope`, and passes a
  managed pointer across the C/C++ boundary to `c_side_release`.
- Both TUs are linked with the same C-compiled `cplus_runtime.c` object and
  the result must print `INTEROP OK`.

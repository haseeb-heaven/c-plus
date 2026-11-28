# Adversarial null/zero/exact-fit regression family — verification notes

## Files (only these three were created)

- `tests/stdlib/regressions/adv_null_zero.cp` — NULL / zero-length / empty-input family.
- `tests/stdlib/regressions/adv_exact_fit.cp` — exact-fit / one-byte-too-small family.
- `tests/stdlib/regressions/ADV_NULL_ZERO_NOTES.md` — this record.

No headers, other tests, or git index entries were modified.

## API surface verified by reading headers first

All invoked APIs were confirmed to exist with the exact signatures/return codes below:

| Header | APIs used |
| --- | --- |
| `stdlib/error.hp` | `STDLIB_OK/EOF/FAIL`, all `STDLIB_CATEGORY_*`, `CPLUS_EXIT_*`, `error` methods (`ok`, `failed_now`, `code`, `category`, `what`, `clear`, `set`, `set_category`), `error_reset/error_fail/error_fail_cat` with NULL, `stdlib_alloc/calloc/realloc`, `stdlib_mem`-side metadata queries |
| `stdlib/io.hp` | `read_line`, `read_int`, `read_long`, `read_float`, `read_double`, `stdlib_io_token` |
| `stdlib/file.hp` | `file(path, mode)`, `file_borrow`, `file_temp`, `read`, `read_line`, `write`, `write_bytes`, `seek`, `flush`, `close`, `drop`, `is_open` |
| `stdlib/string.hp` | `string(NULL)`, `size`, `capacity`, `valid`, `at`, `c_str`, `equals`, `compare`, `find`, `reserve`, `append`, `copy_from`, `substr`, `field`, `is_valid_utf8`, `drop`, `string_view`, `stdlib_utf8_validate` |
| `stdlib/vector.hp` | `vector`, `size`, `capacity`, `reserve`, `push_back`, `at`, `at_checked`, `pop_back`, `insert`, `erase`, `copy_from`, `clear`, `view`, `drop`, `slice` (`size`, `valid`, `stale`, `at_checked`, `sub`) |
| `stdlib/collections.hp` | `intmap`, `strmap`, `intset`, `deque`, `list`, `stack`, `queue` and their get/put/contains/remove/push/pop/top/front/at_checked/clear/drop methods |
| `stdlib/memory.hp` | `stdlib_mem_capacity/count/token/check/check_token/check_range/copy/move/set/borrow/transfer/release` |

No nonexistent API is called. No raw game-memory offsets or hard-coded user paths are used;
the file tests use `file_temp()` (tmpfile), so no filesystem path is assumed.

## Verification commands (all under `timeout 30`)

Working directory: repository root. Build products were written to the
pre-approved temp dir `/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/advruns`
via `-o`, so the build does not create executables inside the source tree
(only the three owned files exist there). Everything else matches the requested
commands exactly.

### adv_null_zero.cp

```sh
timeout 30 ./build/c+ build tests/stdlib/regressions/adv_null_zero.cp --strict --sanitize=none -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/adv_null_zero_strict
timeout 30 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/adv_null_zero_strict
timeout 30 ./build/c+ build tests/stdlib/regressions/adv_null_zero.cp -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/advruns/adv_null_zero_default
timeout 30 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/advruns/adv_null_zero_default
timeout 30 ./build/c+ build tests/stdlib/regressions/adv_null_zero.cp --sanitize=undefined -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/advruns/adv_null_zero_ubsan
timeout 30 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/advruns/adv_null_zero_ubsan
timeout 30 ./build/c+ build tests/stdlib/regressions/adv_null_zero.cp --default -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/advruns/adv_null_zero_cli_default
timeout 30 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/advruns/adv_null_zero_cli_default
```

### adv_exact_fit.cp

```sh
timeout 30 ./build/c+ build tests/stdlib/regressions/adv_exact_fit.cp --strict --sanitize=none -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/adv_exact_fit_strict
timeout 30 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/adv_exact_fit_strict
timeout 30 ./build/c+ build tests/stdlib/regressions/adv_exact_fit.cp -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/advruns/adv_exact_fit_default
timeout 30 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/advruns/adv_exact_fit_default
timeout 30 ./build/c+ build tests/stdlib/regressions/adv_exact_fit.cp --sanitize=undefined -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/advruns/adv_exact_fit_ubsan
timeout 30 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/advruns/adv_exact_fit_ubsan
timeout 30 ./build/c+ build tests/stdlib/regressions/adv_exact_fit.cp --default -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/advruns/adv_exact_fit_cli_default
timeout 30 /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/advruns/adv_exact_fit_cli_default
```

## Per-mode results

All builds produced **empty build stdout/stderr** (0 bytes) and exit code 0.
All runs produced 0 stderr bytes and exit code 0. Every mode produced
byte-identical stdout (`cmp -s` = IDENTICAL for strict vs default, strict vs
UBSan, and strict vs explicit `--default`).

| Program | Mode | Checks | Failures | Run exit | stdout lines | stderr bytes |
| --- | --- | --- | --- | --- | --- | --- |
| adv_null_zero | strict, `--sanitize=none` | 138 | 0 | 0 | 139 | 0 |
| adv_null_zero | default (no flags) | 138 | 0 | 0 | 139 | 0 |
| adv_null_zero | `--sanitize=undefined` (UBSan) | 138 | 0 | 0 | 139 | 0 |
| adv_null_zero | `--default` (explicit) | 138 | 0 | 0 | 139 | 0 |
| adv_exact_fit | strict, `--sanitize=none` | 95 | 0 | 0 | 96 | 0 |
| adv_exact_fit | default (no flags) | 95 | 0 | 0 | 96 | 0 |
| adv_exact_fit | `--sanitize=undefined` (UBSan) | 95 | 0 | 0 | 96 | 0 |
| adv_exact_fit | `--default` (explicit) | 95 | 0 | 0 | 96 | 0 |

## Exact stdout (strict mode; identical in every other mode)

### adv_null_zero.cp

```
nz_err_default 1
nz_err_null_targets 1
nz_err_set 1
nz_err_setcat 1
nz_err_clear 1
nz_err_repeat_clear 1
nz_io_line_null 1
nz_io_line_null_sentinel 1
nz_io_line_zero 1
nz_io_line_zero_sentinel 1
nz_io_line_repeat_zero 1
nz_io_int_null 1
nz_io_long_null 1
nz_io_float_null 1
nz_io_double_null 1
nz_io_out_unchanged 1
nz_io_token_null 1
nz_io_token_zero 1
nz_io_token_sentinel 1
nz_file_null_path 1
nz_file_borrow_null 1
nz_file_closed_read 1
nz_file_closed_readline 1
nz_file_closed_write 1
nz_file_closed_flush 1
nz_file_closed_seek 1
nz_file_close_closed 1
nz_file_close_repeat 1
nz_file_sentinel_after_closed 1
nz_file_temp_open 1
nz_file_read_null 1
nz_file_read_zero 1
nz_file_readline_null 1
nz_file_readline_zero 1
nz_file_write_null 1
nz_file_writebytes_null 1
nz_file_writebytes_zero 1
nz_file_sentinel_after_null 1
nz_file_double_drop 1
nz_str_null_ctor 1
nz_str_at_empty 1
nz_str_append_null 1
nz_str_append_empty 1
nz_str_equals_null 1
nz_str_compare_null 1
nz_str_find_null 1
nz_str_copy_null 1
nz_str_reserve_zero 1
nz_str_reserve_same 1
nz_str_substr_null_out 1
nz_str_substr_zero_size 1
nz_str_substr_start_oob 1
nz_str_substr_empty 1
nz_str_field_null_out 1
nz_str_field_zero_size 1
nz_str_field_index_oob 1
nz_str_field_empty 1
nz_str_utf8_empty 1
nz_str_utf8_null_zero 1
nz_str_utf8_null_nonzero 1
nz_str_view_null_empty 1
nz_str_view_null_nonzero 1
nz_str_double_drop 1
nz_vec_empty 1
nz_vec_reserve_zero 1
nz_vec_at_oob 1
nz_vec_at_checked_null_out 1
nz_vec_at_checked_empty 1
nz_vec_pop_null 1
nz_vec_pop_empty 1
nz_vec_insert_oob 1
nz_vec_erase_empty 1
nz_vec_copy_null 1
nz_vec_slice_empty 1
nz_vec_slice_empty_at 1
nz_vec_slice_empty_null_out 1
nz_vec_slice_empty_sub 1
nz_vec_insert_end 1
nz_vec_at_checked_ok 1
nz_vec_slice_live 1
nz_vec_slice_live_null_out 1
nz_vec_slice_stale 1
nz_vec_slice_stale_refusal 1
nz_vec_slice_stale_sub 1
nz_vec_repeat_clear 1
nz_vec_pop_after_clear 1
nz_vec_double_drop 1
nz_im_empty 1
nz_im_get_null_unchanged 1
nz_im_get 1
nz_im_get_null_after 1
nz_im_repeat_clear 1
nz_im_double_drop 1
nz_sm_empty 1
nz_sm_null_get_after 1
nz_sm_null_remove 1
nz_sm_double_drop 1
nz_is_empty 1
nz_is_insert 1
nz_is_double_drop 1
nz_dq_empty 1
nz_dq_at 1
nz_dq_at_null 1
nz_dq_clear_drop 1
nz_list_empty 1
nz_list_at 1
nz_list_double_drop 1
nz_stack_empty 1
nz_stack_top 1
nz_stack_double_drop 1
nz_queue_empty 1
nz_queue_front 1
nz_queue_double_drop 1
nz_mem_cap_null 1
nz_mem_count_null 1
nz_mem_token_null 1
nz_mem_check_null 1
nz_mem_range_null 1
nz_mem_copy_null 1
nz_mem_move_null_src 1
nz_mem_set_null 1
nz_mem_borrow_null 1
nz_mem_transfer_null 1
nz_mem_release_null 1
nz_mem_alloc 1
nz_mem_token_nonzero 1
nz_mem_zero_at_end 1
nz_mem_zero_copy 1
nz_mem_zero_move 1
nz_mem_borrow_zero_at_end 1
nz_mem_off_gt_cap 1
nz_mem_stale_token 1
nz_mem_fresh_token 1
nz_mem_cap_after_realloc 1
nz_mem_release_idempotent 1
nz_mem_realloc_null 1
nz_mem_realloc_zero 1
nz_mem_untracked 1
ADV_NULL_ZERO 138 0
```

### adv_exact_fit.cp

```
ef_m1_cap 1
ef_m1_copy_exact 1
ef_m1_copy_over 1
ef_m1_set_exact 1
ef_m1_set_over 1
ef_m1_interior_exact 1
ef_m2_cap 1
ef_m2_copy_exact 1
ef_m2_copy_over 1
ef_m2_set_exact 1
ef_m2_set_over 1
ef_m2_interior_exact 1
ef_m3_cap 1
ef_m3_copy_exact 1
ef_m3_copy_over 1
ef_m3_set_exact 1
ef_m3_set_over 1
ef_m3_interior_exact 1
ef_m4_cap 1
ef_m4_copy_exact 1
ef_m4_copy_over 1
ef_m4_set_exact 1
ef_m4_set_over 1
ef_m4_interior_exact 1
ef_s0_size 1
ef_s0_sub_exact 1
ef_s0_sub_onebyte 1
ef_s0_sub_zero_repeat 1
ef_s0_sub_zero_refusal 1
ef_s0_field_zero_refusal 1
ef_s1_size 1
ef_s1_sub_exact 1
ef_s1_sub_onebyte 1
ef_s1_sub_zero_repeat 1
ef_s1_sub_short 1
ef_s1_field_exact 1
ef_s1_field_short 1
ef_s2_size 1
ef_s2_sub_exact 1
ef_s2_sub_onebyte 1
ef_s2_sub_zero_repeat 1
ef_s2_sub_short 1
ef_s2_field_exact 1
ef_s2_field_short 1
ef_s3_size 1
ef_s3_sub_exact 1
ef_s3_sub_onebyte 1
ef_s3_sub_zero_repeat 1
ef_s3_sub_short 1
ef_s3_field_exact 1
ef_s3_field_short 1
ef_s4_size 1
ef_s4_sub_exact 1
ef_s4_sub_onebyte 1
ef_s4_sub_zero_repeat 1
ef_s4_sub_short 1
ef_s4_field_exact 1
ef_s4_field_short 1
ef_file_open 1
ef_file_write 1
ef_file_seek 1
ef_file_1byte_over 1
ef_file_exact3 1
ef_file_exact4 1
ef_file_short4_st 1
ef_file_short4_cat 1
ef_file_short4_code 1
ef_file_short4_buf0 1
ef_file_short4_sentinel 1
ef_file_aligned 1
ef_file_zero_repeat 1
ef_file_eof 1
ef_vec_reserve4 1
ef_vec_reserve_repeat 1
ef_vec_fill_exact 1
ef_vec_view_exact 1
ef_vec_view_values 1
ef_vec_view_oob 1
ef_vec_push_grow 1
ef_vec_view_stale 1
ef_vec_stale_refusal 1
ef_vec_pop_exact 1
ef_vec_shrink 1
ef_vec_double_drop 1
ef_dq_exact 1
ef_dq_push_over 1
ef_dq_drain 1
ef_dq_empty_refusal 1
ef_st_exact 1
ef_st_drain 1
ef_st_empty_refusal 1
ef_qu_exact 1
ef_qu_front 1
ef_qu_drain 1
ef_qu_empty_refusal 1
ADV_EXACT_FIT 95 0
```

## Contract observations (not library defects)

These were initially surprising and were confirmed against the header source;
the tests assert the documented behavior.

1. **Guarded memory helpers do not reset `error`.** `stdlib_mem_copy/move/set/
   borrow/check` set the error only on failure; a successful zero-length call
   leaves any previous error record intact. The tests clear `error` explicitly
   before asserting a clean record.
2. **`stdlib_realloc` on an untracked pointer reports through `stdlib_note`
   (telemetry/callback) only**, not through the `error` object. The test checks
   the NULL return and the absence of metadata rather than an error code.
3. **`file::read_line` with size > 1 and an overlong line** stores the `fgets`
   prefix inside the destination and then sets `buf[0] = '\0'` when refusing.
   The guaranteed-untouched location is the byte one past the destination
   (`buf[size]`), which is what the exact-fit test asserts. The size == 1 path
   writes only `buf[0]`.
4. **`string::substr` / `string::field` with `out_size` one byte short do not
   fail**: they truncate to `out_size - 1` characters and still NUL-terminate.
   Only `out_size == 0` is refused. The tests assert both branches.
5. **Variable names must be globally unique per translation unit** when their
   values are class instances — see the compiler defect below. Both test files
   use `nz_`/`ef_` prefixed names for every class-typed local to avoid it.

## Defect reproduced (compiler, not the `.hp` headers)

**Method lowering is keyed by variable name across the whole translation unit,
not by the variable's declared type.** If two functions in one file each declare
a local with the same name but different class types, method calls on the first
one are lowered to the second class's functions.

Minimal repro (written to the pre-approved temp dir, not the repo):

```c
#include "cplus/cplus.h"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
static int first(void) {
    string_view v = string_view("abc", 3);
    return (int)v.size();
}
static int second(void) {
    vector v = vector();
    return (int)v.size();
}
int main(void) { print(first(), second()); return 0; }
```

Observed emitted C from `--emit-c`:

```c
static int first(void) {
    string_view v = string_view_ctor("abc", 3);
    return (int)vector_size(&v);      /* WRONG: should be string_view_size */
}
static int second(void) {
    vector v = vector_ctor();
    ...
}
```

Compiler diagnostic on build:

```
repro_method_collision.cp:6:29: warning: incompatible pointer types passing
  'string_view *' to parameter of type 'vector *' [-Wincompatible-pointer-types]
    6 |     return (int)vector_size(&v);
      |                             ^~
```

Impact: silent mis-lowering under default mode (only a warning); can produce a
backend error such as `call to undeclared function 'vector_equals'` when the
wrong class lacks the method. Workaround used in these tests: give every
class-typed local a translation-unit-unique name. No headers were edited.

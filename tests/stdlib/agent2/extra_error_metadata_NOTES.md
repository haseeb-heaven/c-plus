# extra_error_metadata — extra `stdlib/error.hp` metadata coverage (agent 2)

## Owned files

- `tests/stdlib/agent2/extra_error_metadata.cp`
- `tests/stdlib/agent2/extra_error_metadata_NOTES.md` (this file)

No header, existing test, or other agent's file was modified. No
`git add` / `git commit` was performed. No game/data asset was touched.

## Why this test exists

The final audit (`tests/stdlib/agent2/FINAL_AUDIT.md` §17) names "most
documented branches of `error.hp` metadata helpers" as UNTESTED, and the
module audit (`tests/stdlib/agent2_audit/module_audits/error.hp.md` §7,
items 2–4) enumerates the exact gaps. `deep_error_task5.cp` covers message
boundaries, reentrancy, 1000 reset/fail cycles and invalid categories, while
CF3 in `deep_cross_family.cp` covers `stdlib_alloc_length`,
`stdlib_alloc_generation`, `check_generation` and one successful transfer.
This file closes the remaining metadata/ownership queries and rejection
branches, as a single self-checking program that needs no stdin fixture, no
external library and no environment state.

## Coverage map

| Audit gap | Covered by (tags) |
|---|---|
| `stdlib_alloc_size`, `stdlib_alloc_elem_size`, `stdlib_alloc_query_length` (module audit §7.2) | `eem_a_size`, `eem_a_elem_size`, `eem_a_query_default/elem8/elem7_mismatch`, `eem_a_query_null_ptr/null_out`, `eem_b_size/elem_size/query_*`, `eem_c_zero_elem_*`, `eem_c_zero_count_*` |
| `check_generation` with mismatched generation / `elem_size` (§7.2) | `eem_d_check_mismatch_plus1`, `eem_d_check_mismatch_xor`, `eem_d_check_gen0`, `eem_d_check_null`, `eem_d_old_gen_stale`, `eem_b_query_elem8_mismatch` |
| `elem_size==0` fallbacks (§7.2) | `eem_a_query_default` (caller elem_size 0 → recorded 1), `eem_b_query_default` (→ recorded 4), `eem_c_zero_elem_*` (calloc element_size 0 normalized to 1) |
| `stdlib_transfer_ownership` non-NULL dead source (§7.3) | `eem_e_dead_stack_reject` (stack address, never a live block), `eem_e_dead_released_reject` (stale pointer from a released block) |
| `stdlib_transfer_ownership` `dst == src` (§7.3) | `eem_e_dst_src_reject` (live block passed as both handles) |
| Other transfer rejection inputs | `eem_e_dst_nonnull_reject`, `eem_e_src_null_reject`, `eem_e_second_transfer_reject`; success path `eem_e_transfer_ok` / `_handles` / `_fresh_gen` / `_length` |
| `stdlib_category_name` outside the switch (§7.4) | `eem_f_name_fallback_high` (999), `eem_f_name_fallback_neg` (-5), plus all nine valid names via `eem_f_cat_0..8` |
| `stdlib_telemetry_snapshot(NULL)` no-op (§7.4) | `eem_h_snapshot_null_noop` |
| Invalid category/code mapping | `eem_f_invalid_cat_stored`, `eem_f_unknown_code_invalid`, `eem_f_lifecycle_map`, `eem_f_note_ctr_mapping` (per-category `stdlib_note` counters) |
| Repeated reset/fail cycles + callback reentrancy | `eem_g_cycles_reentrant` (64 cycles), `eem_g_no_stuck_guard`, `eem_g_reset_clean` |
| Telemetry balance after drops | `eem_a_balance`, `eem_b_balance`, `eem_c_balance`, `eem_d_balance`, `eem_e_balance`, `eem_h_balance` |

Explicit non-goals (out of scope / deliberately excluded): metadata-table
rehash at scale (§7.1), multi-threaded callback contention (§7.5), and the
deliberate fatal paths (§7.7). Those need either a load fixture, threads, or a
fatal probe; none are safe to force in this self-checking test.

## Safety properties of the test itself

- All allocations are deterministic (`stdlib_alloc` / `stdlib_calloc` with
  fixed sizes); no randomness, no I/O, no clock, no environment read.
- Stale pointers (`eem_e_stale`, `eem_e_live` after transfer) are **never
  dereferenced** — they are only handed to helpers that hash/compare the
  pointer value, and are never released twice.
- Writes are strictly in bounds (`eem_b_view[0..7]` on a 32-byte block).
- The reentrant callback does not recurse: `stdlib_note()`'s per-thread
  `reporting` guard drops the nested `error_fail`. `eem_cb_overflow` and
  `eem_cb_depth` verify this at runtime; both stay 0/0.
- Telemetry is enabled from the start, reset before each independent epoch,
  and every `stdlib_alloc`/`stdlib_calloc` in the test has a matching
  `stdlib_release` inside that epoch. `stdlib_telemetry_snapshot` is taken
  before the final block ends, so no scope-exit finalization is counted.
- No header was edited to force a pass.

## Exact commands (run from the repository root)

`ROOT=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
`TMPD=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/eem_work`
Environment for every command:
`PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin CPLUS_CLANG=/usr/bin/clang`

STRICT `--sanitize=none` (run FIRST, as required):

```sh
timeout 30 $ROOT/build/c+ build $ROOT/tests/stdlib/agent2/extra_error_metadata.cp \
  --strict --sanitize=none -o "$TMPD/eem_strict_none"
timeout 30 "$TMPD/eem_strict_none" > "$TMPD/strict_none.out" 2> "$TMPD/strict_none.err"
```

Default:

```sh
timeout 30 $ROOT/build/c+ build $ROOT/tests/stdlib/agent2/extra_error_metadata.cp \
  -o "$TMPD/eem_default"
timeout 30 "$TMPD/eem_default" > "$TMPD/default.out" 2> "$TMPD/default.err"
```

UBSan:

```sh
timeout 30 $ROOT/build/c+ build $ROOT/tests/stdlib/agent2/extra_error_metadata.cp \
  --strict --sanitize=undefined -o "$TMPD/eem_ubsan"
timeout 30 "$TMPD/eem_ubsan" > "$TMPD/ubsan.out" 2> "$TMPD/ubsan.err"
```

No `-I` / `-L` flags are needed: this test only includes `cplus/cplus.h` and
`stdlib/error.hp` (no yyjson dependency), matching `deep_error_task5.cp`.

## Per-mode results

| Mode | Build rc | Run rc | Final stdout line | stderr | Verdict |
|------|----------|--------|-------------------|--------|---------|
| strict / sanitize=none | 0 | 0 | `EXTRA_ERROR_METADATA 75 0` | empty (0 bytes) | PASS |
| default | 0 | 0 | `EXTRA_ERROR_METADATA 75 0` | empty (0 bytes) | PASS |
| strict / sanitize=undefined (UBSan) | 0 | 0 | `EXTRA_ERROR_METADATA 75 0` | empty (0 bytes) | PASS |

Every build and every run completed well inside `timeout 30`. All three
stdout files are byte-identical (`diff -u` clean; md5
`854596ba3f15b4021251f3fedee04a84` for each). Each stdout has 76 lines:
75 `<tag> <0|1>` check lines (every one `1`) followed by the final summary
line. Exit code is 0 in every mode. UBSan emitted no diagnostic.

## Exact stdout (all three modes)

```
eem_a_alloc 1
eem_a_size 1
eem_a_elem_size 1
eem_a_length 1
eem_a_query_default 1
eem_a_query_elem8 1
eem_a_query_elem7_mismatch 1
eem_a_query_null_ptr 1
eem_a_query_null_out 1
eem_a_untracked_queries 1
eem_a_released_zero 1
eem_a_balance 1
eem_b_alloc 1
eem_b_size 1
eem_b_elem_size 1
eem_b_length 1
eem_b_query_default 1
eem_b_query_elem1 1
eem_b_query_elem4 1
eem_b_query_elem8_mismatch 1
eem_b_zeroed 1
eem_b_in_bounds_write 1
eem_b_balance 1
eem_c_alloc 1
eem_c_zero_elem_meta 1
eem_c_zero_elem_query 1
eem_c_zero_count_meta 1
eem_c_zero_count_query 1
eem_c_balance 1
eem_d_alloc 1
eem_d_gen_nonzero 1
eem_d_gen_null 1
eem_d_check_ok 1
eem_d_check_mismatch_plus1 1
eem_d_check_mismatch_xor 1
eem_d_check_gen0 1
eem_d_check_null 1
eem_d_rerecord_gen 1
eem_d_old_gen_stale 1
eem_d_released_gen 1
eem_d_balance 1
eem_e_alloc 1
eem_e_dead_stack_reject 1
eem_e_dead_released_reject 1
eem_e_dst_src_reject 1
eem_e_dst_nonnull_reject 1
eem_e_src_null_reject 1
eem_e_reject_telemetry 1
eem_e_transfer_ok 1
eem_e_transfer_handles 1
eem_e_transfer_fresh_gen 1
eem_e_transfer_length 1
eem_e_second_transfer_reject 1
eem_e_balance 1
eem_f_cat_0 1
eem_f_cat_1 1
eem_f_cat_2 1
eem_f_cat_3 1
eem_f_cat_4 1
eem_f_cat_5 1
eem_f_cat_6 1
eem_f_cat_7 1
eem_f_cat_8 1
eem_f_note_ctr_mapping 1
eem_f_name_fallback_high 1
eem_f_name_fallback_neg 1
eem_f_invalid_cat_stored 1
eem_f_unknown_code_invalid 1
eem_f_lifecycle_map 1
eem_g_cycles_reentrant 1
eem_g_no_stuck_guard 1
eem_g_reset_clean 1
eem_h_snapshot_null_noop 1
eem_h_alloc 1
eem_h_balance 1
EXTRA_ERROR_METADATA 75 0
```

## Defects / findings

**No defect in `stdlib/error.hp` or the C+ runtime was reproduced.** All
metadata queries, generation checks, transfer rejections, category mappings,
reentrant-callback guard behavior and telemetry balances matched the
documented contracts (`error.hp` comments at lines 414–524 and 581–624).

Two non-library observations are recorded for completeness:

1. **Test-side only (found and fixed in this test, not a library defect).**
   My first draft took the section-E telemetry snapshot *after* three
   allocations but released all three inside the measured window, so
   `eem_e_balance` printed 0 (alloc delta 0, free delta 3) and the run exited
   1 with `EXTRA_ERROR_METADATA 75 1`. Minimal repro of the cause:

   ```c
   stdlib_telemetry_reset();
   stdlib_telemetry_set_enabled(1);
   p = stdlib_alloc(24);          /* counted outside the snapshot        */
   stdlib_telemetry_snapshot(&pre);
   stdlib_release(p);             /* counted inside the snapshot        */
   stdlib_telemetry_snapshot(&post);
   /* post.alloc_count - pre.alloc_count == 0, free delta == 1           */
   ```

   Fix (test only): move the `snapshot(&eem_pre)` call *before* the three
   allocations and expect a 3-allocation/3-free delta. No header change.

2. **Code observation, not a reproduced failure.** In
   `stdlib_transfer_ownership` (`error.hp:496-497`) the `dst == src`
   disjunct is unreachable as the *sole* cause of rejection: when
   `dst == src` the two handles alias, so either `*src == NULL` (fires first)
   or `*dst != NULL` (fires next) is already true. The call is still rejected
   with `-1`, so behavior is correct; the extra condition is redundant. This
   is noted, not counted as a pass or a defect, and no header was edited.

One defensive branch could not be reached through the public API without
editing headers: `stdlib_alloc_length`'s `s->elem_size ? s->elem_size : 1u`
fallback (`error.hp:443`). `stdlib_meta_record` normalizes any zero
`elem_size` to 1 (`:295-296`) and `stdlib_calloc` does the same (`:405`), so a
stored slot never has `elem_size == 0`. The equivalent *caller* fallback
(`elem_size > 0 ? elem_size : ...` in `stdlib_alloc_query_length`) is covered
by `eem_a_query_default` / `eem_b_query_default` / `eem_c_zero_elem_query`.

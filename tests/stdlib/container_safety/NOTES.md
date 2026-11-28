# Container-safety regression suite — verification notes

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Repo root:** `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
**Compiler:** `build/c+` (existing binary, not rebuilt by this task)
**Timeout utility:** `/usr/bin/timeout` (present; `-k 5` escalation, 30s limit)
**File ownership:** only files under `tests/stdlib/container_safety/` were created.
No headers or other files were edited; no `git add` / `git commit` was run.

## Files created

| File | Purpose |
| --- | --- |
| `cs_basic.cp` | empty/single containers, growth/shrink, removal begin/middle/end, duplicate/missing keys, capacity overflow, double drop |
| `cs_stress.cp` | fixed-seed LCG, collisions, rehash, repeated insert/remove cycles, large volumes, 4000-op differential model |
| `cs_alloc.cp` | allocation failure during growth at every allocation point, failure atomicity, partial-init destruction, double destruction, leak telemetry |
| `cs_generation.cp` | slice/allocator invalidation observed **only** via generation/validity metadata; no stale pointer is dereferenced |
| `run.sh` | bounded runner: strict/no-sanitizer first, then UBSan; timeout 30 on every build and run; byte-exact determinism check |
| `NOTES.md` | this file |

## Output protocol (all four tests)

Each check prints `<tag> <1|0>` where `1` = passed, `0` = failed.
Final line: `CONTAINER_SAFETY <checks> <failures>`.
Exit code is `1` when `<failures> > 0`, otherwise `0`. Loops are bounded and
seeds are fixed, so output is deterministic; reaching the final line under the
external timeout is the no-infinite-loop proof.

## Requirement → coverage

| Requirement | Representative tags |
| --- | --- |
| empty/single containers | `b_vec_empty`, `b_vec_single`, `b_map_empty`, `b_smap_emptykey`, `b_set_empty`, `b_deq_empty`, `b_list_empty`, `b_stack_empty`, `b_queue_empty` |
| growth/shrink | `b_vec_grow`, `b_vec_reserve`, `b_vec_shrink`, `b_map_grow`, `b_deq_grow`, `s_vec_bulk`, `s_deq_bulk` |
| capacity overflow | `b_vec_overflow`, `b_map_grow_ovf`, `b_map_pow2_ovf`, `b_smap_grow_ovf`, `b_set_grow_ovf`, `b_deq_overflow` |
| allocation failure during growth | `aim_o*`, `asm_o*`, `ais_o*`, `avg_o*`, `avs_o*`, `avc_o*`, `adq_o*`, `alp_o*`, `asg_o*`, `aqg_o*` |
| removal begin/middle/end | `b_vec_ins_begin/mid/end`, `b_vec_era_begin/mid/end`, `b_deq_pop_front/back`, `b_list_pop_front/back` |
| repeated insert/remove | `s_cycles` (200 rounds), `s_diff_model` (4000 fixed-seed ops) |
| collisions | `s_collect_int`, `s_map_insert`, `s_map_probe`, `s_collect_str`, `s_str_collisions`, `s_str_probe` |
| duplicate/missing keys | `b_map_dup`, `b_map_missing`, `b_smap_dup`, `b_smap_missing`, `b_set_dup`, `b_set_missing` |
| rehash | `b_map_grow`, `s_map_rehash`, `s_map_rehash_probe`, `s_growth_seen` |
| large data volumes | `s_vec_bulk` (4096), `s_set_bulk` (2048), `s_str_bulk` (1000), `s_deq_bulk` (4096), `s_list_bulk` (2048), `s_stack_bulk`/`s_queue_bulk` (2048) |
| destruction after partial init | `apd_sm_partial`, `apd_sm_leak`, `apd_im_partial`, `apd_im_leak` |
| double destruction | `b_vec_drop2`, `b_map_drop2`, `b_smap_drop2`, `b_set_drop2`, `b_deq_drop2`, `b_list_drop2`, `b_sq_drop2`, `add_state`, `add_no_double_free` |
| failure atomicity | every `*_o<N>` check plus `*_l<N>` telemetry balance (`alloc_count == free_count`) |
| pointer/slice invalidation via generation metadata only | `g_slice_valid`, `g_slice_no_realloc`, `g_slice_realloc`, `g_slice_stale_sub`, `g_slice_drop_stale`, `g_pop_stale`, `g_insert_stale`, `g_erase_stale`, `g_clear_stale`, `g_shrink_stale`, `g_copy_stale`, `g_gen_token`, `g_realloc_retires`, `g_freed_meta`, `g_transfer`, `g_transfer_self` |

## Verification 1 — strict / no sanitizer (run FIRST)

Canonical per-file commands (each build and run wrapped in `timeout -k 5 30`),
executed from the repo root:

```sh
mkdir -p tests/stdlib/container_safety/.run

timeout -k 5 30 build/c+ build tests/stdlib/container_safety/cs_basic.cp \
  --strict --sanitize=none -o tests/stdlib/container_safety/.run/cs_basic_strict_none \
  > tests/stdlib/container_safety/.run/cs_basic_strict_none.build.log 2>&1
timeout -k 5 30 tests/stdlib/container_safety/.run/cs_basic_strict_none \
  </dev/null > tests/stdlib/container_safety/.run/cs_basic_strict_none.out \
  2> tests/stdlib/container_safety/.run/cs_basic_strict_none.err

# identical shape for cs_stress, cs_alloc, cs_generation
# (flags: --strict --sanitize=none; run timeout: 30s on each binary)
```

Results (all builds rc=0, all runs rc=0, all stderr empty):

| Test | Final stdout line |
| --- | --- |
| `cs_basic` | `CONTAINER_SAFETY 67 0` |
| `cs_stress` | `CONTAINER_SAFETY 28 0` |
| `cs_alloc` | `CONTAINER_SAFETY 63 0` |
| `cs_generation` | `CONTAINER_SAFETY 24 0` |

Total strict/no-sanitizer: **182 checks, 0 failures, 4/4 runs exited 0**.

## Verification 2 — UBSan (run AFTER strict/no-sanitizer)

Exact loop executed from the repo root, again with a 30s bound on build and run:

```sh
for t in cs_basic cs_stress cs_alloc cs_generation; do
  timeout -k 5 30 build/c+ build tests/stdlib/container_safety/$t.cp \
    --strict --sanitize=undefined -o tests/stdlib/container_safety/.run/${t}_ubsan \
    > tests/stdlib/container_safety/.run/${t}_ubsan.build.log 2>&1
  brc=$?
  timeout -k 5 30 tests/stdlib/container_safety/.run/${t}_ubsan \
    </dev/null > tests/stdlib/container_safety/.run/${t}_ubsan.out \
    2> tests/stdlib/container_safety/.run/${t}_ubsan.err
  rrc=$?
  echo "$t build=$brc run=$rrc final=$(tail -1 tests/stdlib/container_safety/.run/${t}_ubsan.out)"
done
```

Observed output (no UBSan diagnostics on stderr):

```
cs_basic build=0 run=0 final=CONTAINER_SAFETY 67 0
cs_stress build=0 run=0 final=CONTAINER_SAFETY 28 0
cs_alloc build=0 run=0 final=CONTAINER_SAFETY 63 0
cs_generation build=0 run=0 final=CONTAINER_SAFETY 24 0
```

Total UBSan: **182 checks, 0 failures, 4/4 runs exited 0**.

## Verification 3 — combined bounded runner

```sh
sh tests/stdlib/container_safety/run.sh; echo "RUNNER_RC=$?"
```

Output:

```
ok       cs_alloc (strict-none): CONTAINER_SAFETY 63 0
ok       cs_alloc (ubsan): CONTAINER_SAFETY 63 0
deterministic cs_alloc: strict-none == ubsan stdout
ok       cs_basic (strict-none): CONTAINER_SAFETY 67 0
ok       cs_basic (ubsan): CONTAINER_SAFETY 67 0
deterministic cs_basic: strict-none == ubsan stdout
ok       cs_generation (strict-none): CONTAINER_SAFETY 24 0
ok       cs_generation (ubsan): CONTAINER_SAFETY 24 0
deterministic cs_generation: strict-none == ubsan stdout
ok       cs_stress (strict-none): CONTAINER_SAFETY 28 0
ok       cs_stress (ubsan): CONTAINER_SAFETY 28 0
deterministic cs_stress: strict-none == ubsan stdout

MODE             PASSED   FAILED  SKIPPED     CHECKS
strict-none           4        0        0        182
ubsan                 4        0        0        182
container safety: 8 passed, 0 failed, 0 skipped, 364 checks (4 file(s) x 2 modes)
RUNNER_RC=0
```

SHA-256 of stdout (identical in both modes for each test — determinism evidence):

```
c73a6696... cs_alloc_strict_none.out / cs_alloc_ubsan.out
76e1b5b8... cs_basic_strict_none.out / cs_basic_ubsan.out
398f1c04... cs_generation_strict_none.out / cs_generation_ubsan.out
5c53e757... cs_stress_strict_none.out / cs_stress_ubsan.out
```

The runner writes binaries and logs into `tests/stdlib/container_safety/.run/`;
that directory is regenerated on every run and is not a source artifact.

## Defects / corrections during construction

**No library defect was reproduced.** All four suites pass in both modes with
zero failures and empty stderr. The failures seen while authoring were defects
in the test expectations, each diagnosed and corrected before final verification:

1. `cs_basic` first run: 5 failing tags (`b_vec_era_mid`, `b_set_grow`,
   `b_set_remove`, `b_list_grow`, `b_list_pop_back`). Causes: wrong expected
   element after a preceding erase, an off-by-one in the count of distinct set
   keys, and one-off list index expectations. Corrected; clean afterwards.
2. `cs_alloc` first run: runtime exit 134 with
   `C+ error: index 3 is outside the bounds of 'pre' (length 3)`. Cause: C+
   lowering resolves same-named local arrays across functions; the length-3
   `pre` in one function collided with the length-5 `pre` in another. Renamed
   all such locals to unique names (`aim_pre`, `ais_pre`, `alp_pre`,
   `adq_expect`, `aqg_expect`, `asm_keys`, `apd_k`). This is a C+ source-naming
   constraint, not a container defect.
3. `cs_alloc` second run: `apd_sm_partial` / `apd_im_partial` failed (sizes 19
   instead of 2/5). Cause: `stdlib_alloc_fail_at` injects a **single** failure;
   my setup loop ignored the failed `put` and kept inserting. Added `break` on
   the first failure. Corrected; clean afterwards.
4. `cs_generation` strict build: `no member named 'size' in 'struct slice'`
   for `first.sub(9,1).size()`. Cause: C+ does not allow a method call on a
   returned temporary. Assigned the sub-slice to a named local first.

No header, compiler, or runner behaviour was changed to make a check pass.

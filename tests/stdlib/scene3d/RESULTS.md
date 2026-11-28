# scene3d conformance results

All commands were run from the repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` on branch
`agent/modern-stdlib`. The library under test is `stdlib/scene3d.hp`; the
suite is `tests/stdlib/scene3d/test_scene3d.cp` and its runner is
`tests/stdlib/scene3d/run.sh`.

## Headline

**79 checks · 79 passed · 0 failed · stderr empty · exit 0.**
The runner additionally verifies the whole stdout transcript byte-for-byte,
so the traversal order, every refusal code, the allocation-failure points and
the canary probes are pinned to exact values rather than just "passed".

## Commands and exits

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh -n tests/stdlib/scene3d/run.sh          # rc=0
sh tests/stdlib/scene3d/run.sh             # rc=0
```

Build line used by the runner (exactly the requested mode/flags):

```
./build/c+ build tests/stdlib/scene3d/test_scene3d.cp \
  --default --sanitize=none -I. \
  -o tests/stdlib/scene3d/.run/test_scene3d \
  -- build/libc/libcplus_hardened.a
```

Runner stdout (exact):

```
build: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+ build tests/stdlib/scene3d/test_scene3d.cp --default --sanitize=none -I. -- build/libc/libcplus_hardened.a
build: ok
scene3d: ok (79 checks, exact transcript matched, stderr empty)
```

Runner stderr is empty. Compiler diagnostics on the build log are warnings
only (one pre-existing `CPLUS-ALIAS` note in `stdlib/error.hp`, one
`CPLUS-FFI` note on the test's canary helper, and the harness's
`libcplus_hardened.a: linker input unused` note); no errors.

## Exact program stdout

```
a_init 1
a_root 1
a_root_name 1
a_root_parent 1
a_root_visible 1
a_count 1
a_find_root 1
a_identity_scale 1
b_idx_a 1
b_idx_b 1
b_idx_a1 1
b_idx_a2 1
b_idx_b1 1
b_parent_a1 1
b_name_b1 1
b_depth_b1 1
b_children_root 1
b_children_a 1
b_child_at_root_1 1
c_traverse_ok 1
c_traverse_count 1
traverse 0
traverse 1
traverse 3
traverse 4
traverse 2
traverse 5
c_traverse_order 1
c_subtree_ok 1
c_subtree_count 1
subtree 1
subtree 3
subtree 4
c_subtree_order 1
d_transform_set 1
d_transform_get 1
d_transform_roundtrip 1
d_visible_off 1
d_visible_on 1
d_bad_transform_get 1
d_bad_visible 1
e_duplicate 1
e_oversized_name 1
e_invalid_parent 1
e_self_cycle 1
e_descendant_cycle 1
e_remove_root 1
e_small_buffer 1
e_bad_find 1
e_sc2_init 1
e_chain_depth 1
e_depth_refusal 1
f_reparent 1
f_new_parent 1
f_old_parent_children 1
f_new_parent_children 1
f_traverse_ok 1
f_traverse_count 1
reparent 0
reparent 1
reparent 3
reparent 4
reparent 5
reparent 2
f_traverse_order 1
f_root_fixed 1
g_remove_leaf 1
g_leaf_gone 1
g_live_count 1
g_parent_children 1
g_tombstone_transform 1
g_remove_subtree 1
g_subtree_gone 1
g_count_after 1
g_traverse_ok 1
after_remove 0
after_remove 2
g_traverse_order 1
h_bad_init 1
h_bad_init_clean 1
h_si_init 1
h_add_alloc 1
h_add_state 1
h_kid_added 1
h_remove_alloc1 1
h_remove_still_alive 1
h_remove_alloc2 1
h_remove_alloc2_state 1
i_box_init 1
i_box_add 1
i_box_transform 1
i_box_remove 1
i_canary_head 1
i_canary_tail 1
i_canary_after_drop 1
z_meta_balance 1
SCENE3D 79 0
```

The observation lines are the exact values that matter:

| Lines | Meaning |
|---|---|
| `traverse 0 1 3 4 2 5` | full-scene preorder of the initial tree `root{a{a1,a2},b{b1}}` |
| `subtree 1 3 4` | preorder of the `a` subtree |
| `reparent 0 1 3 4 5 2` | after moving `b1` from `b` to `a`: insertion order preserved, relinked subtree now precedes `b` |
| `after_remove 0 2` | after removing leaf `a2` and then the `a` subtree: indices are stable and tombstones are skipped |

## Coverage details

- **Traversal** — full-scene and subtree preorder, child insertion order,
  subtree count, and refusal when the output buffer is too small
  (`e_small_buffer`, code `CPLUS_EXIT_BOUNDS`).
- **Refusals** — duplicate name and oversized name (both
  `CPLUS_EXIT_UNSUPPORTED`), invalid parent index (`CPLUS_EXIT_BOUNDS`),
  self-cycle and descendant-cycle on reparent plus root reparent/remove
  (`CPLUS_EXIT_OWNERSHIP`), and the `SCENE3D_MAX_DEPTH` boundary built by a
  32-link chain (`e_chain_depth` / `e_depth_refusal`). Bad indices are also
  refused by transform get/set and visibility helpers.
- **Allocation failure** — a fully refusing allocator vtable makes
  `scene3d_init` fail and leave the zeroed scene untouched. Checked-heap
  injection (`stdlib_alloc_fail_at`) exercises three separate points:
  1. child-list growth on `node_add` (one allocation) → add refused, scene
     unchanged;
  2. subtree scratch allocation on `node_remove` (one allocation) → removal
     refused, node still findable;
  3. subtree scratch-vector growth on a six-node removal (two allocations,
     fail the second) → removal refused, whole subtree still findable.
  `z_meta_balance` confirms the metadata side table returns to its baseline
  after all scenes are dropped.
- **Canary checks** — a 16-byte `0xA5` head and 16-byte `0x5A` tail bracket a
  `scene3d_scene`; init, add, transform-set, remove and drop all leave both
  regions byte-identical (`i_canary_head`, `i_canary_tail`,
  `i_canary_after_drop`).

## Cross-check (sanitizer determinism)

An additional non-required run with `--sanitize=undefined` built and ran
cleanly (rc=0, empty stderr, `SCENE3D 79 0`) and produced stdout byte-identical
to the no-sanitizer run (`cmp` returned 0). This is a local differential
check only; the accepted configuration remains `--default --sanitize=none`.

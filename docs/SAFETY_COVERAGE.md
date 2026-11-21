# Safety test coverage map (v0.1)

This maps each SAFE-subset rule to executable tests. It is a coverage map,
not a claim of complete memory safety.

| SAFE rule | test(s) | type |
| --- | --- | --- |
| ordinary C / libc compatibility | cases/c_abi, cases/hello, core/libc_valid, core/libc_matrix/valid_malloc | positive |
| resource copy/assignment rejected | compile_fail/resource_copy, resource_assign | negative |
| move + moved-from rejection | cases/resource_move, compile_fail/use_after_move, core/move_chain_ok, safety/moved_from_warning | positive/negative |
| borrow escape / conflict | compile_fail/borrow_escape, safety/borrow_after_move, safety/borrow_invalid_destructor, safety/borrow_conflict, core/borrow_field_ok | positive/negative |
| double free | safety/malloc_double_free, compile_fail/double_release, core/double_release_alias, safety/interproc_double_free | negative |
| direct / alias UAF | safety/uaf_direct, safety/uaf_alias, core/uaf_conditional | positive/negative |
| managed double/invalid release | runtime_fail/double_release, runtime_fail/freed_len, core/freed_len | runtime trap |
| bounds (constant, loop, multidim, pointer arithmetic) | compile_fail/constant_oob, loop_oob, multidim_bounds, classic_bug26_ptr_arith; runtime_fail/bounds | positive/negative/runtime |
| null dereference | compile_fail/null_deref, safety/null_deref, core/null_outparam | positive/negative |
| definite initialization | compile_fail/uninit, safety/uninit_branch, core/init_all_branches | positive/negative |
| realloc safety | safety/realloc_success, safety/realloc_failure, safety/realloc_success_alias_uaf, safety/double_release_after_realloc, core/libc_matrix/realloc_* | positive/negative |
| checked copies | core/libc_matrix/memcpy_overlap, strcpy_literal, strcpy_s_valid/invalid_size | positive/negative |
| RAII exits and order | cases/resource_scope, nested_scopes, loop_cleanup, core/raii_basic, nested_raii, raii_early_return | positive |
| multi-TU / .hp linkage | core/multi_tu, multi_tu_resource, multi_tu_hp, multi_tu_lib | positive |
| threads / guards | cases/concurrency_mutex, core/thread_guard_ok, safety/thread_global_write, thread_invalid_sharing | positive/negative |
| quarantine / stale alias | core/quarantine, core/quarantine/run.sh | runtime semantics |
| invalid free | safety/invalid_free_stack, invalid_free_global, invalid_free_string, invalid_free_array, invalid_free_interior, release_stack_pointer; core/positive/free_zero_offset (p+0 accepted); mass_fuzz free_* families | positive/negative |
| bounds: constant-propagated index / memset | safety/bounds_var_index, safety/memset_overflow, core/positive/bounds_var_index_ok, core/positive/memset_ok, mass_fuzz var_index_*/memset_* families | positive/negative |
| interprocedural return state | safety/interproc_return_uaf, core/positive/interproc_return_ok, mass_fuzz interproc_return_* families | positive/negative |
| libc bounds/nullability | safety/strcat_overflow, strncpy_overflow, snprintf_overflow, memcpy_null_source, core/positive/libc_bounds_ok, mass_fuzz strcat_*/strncpy_*/snprintf_*/memcpy_null_src_* families | positive/negative |
| branch/loop may-release | safety/branch_double_free, safety/branch_uaf, core/positive/branch_free_null_ok, core/strict_mode.sh branch_uaf, mass_fuzz branch_* families | positive/negative |
| thread shared-state | safety/thread_volatile_race, core/strict_mode.sh thread_volatile_race, mass_fuzz thread_single_ok_* families | positive/negative |
| pointer provenance | safety/pointer_from_int, core/strict_mode.sh pointer_from_int | positive/negative |
| fuzz / checker robustness | core/fuzz_checker.sh (120 inputs), core/adversarial_fuzz.py (144 unsafe + 80 positive), core/mass_fuzz_all.sh (3 seeds x 5,240 = 15,720 generated programs; 15,720 DEFAULT checks + 15,720 STRICT checks + ~4,427 differential clang syntax checks + ~576 debug/release build-and-run executions; 0 failures), core/cross_tu_fuzz.py (1,200 two-file programs; 4,800 per-TU checks, 1,200 C+ links/runs, 1,200 differential clang builds; 0 failures) | fuzz |
| DEFAULT / STRICT modes | core/strict_mode.sh (16 cases: proven errors rejected in both, unverified ops warned in DEFAULT and rejected in STRICT, safe realloc accepted in STRICT, UAF source-location chain) | positive/negative |
| logging categories | tests/logging_test.sh | positive |

Runtime line coverage target: >=80%. Latest measured: **320/395 lines (81%)**
via `tests/core/coverage.sh`, including allocator growth, zero-size paths,
fatal paths via test hook, printing/scanning and the quarantine branch.

Coverage does not prove safety; unsupported cases remain in
`docs/SAFE_SUBSET.md` and `tests/known_gaps/`.

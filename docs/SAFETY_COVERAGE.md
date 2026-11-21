# Safety test coverage map (v0.1)

This maps each SAFE-subset rule to executable tests. It is a coverage map,
not a claim of complete memory safety.

| SAFE rule | test(s) | type |
| --- | --- | --- |
| ordinary C / libc compatibility | cases/c_abi, cases/hello, core/libc_valid, core/libc_matrix/valid_malloc | positive |
| resource copy/assignment rejected | compile_fail/resource_copy, resource_assign | negative |
| move + moved-from rejection | cases/resource_move, compile_fail/use_after_move, core/move_chain_ok, safety/moved_from_warning | positive/negative |
| borrow escape / conflict | compile_fail/borrow_escape, safety/borrow_after_move, safety/borrow_conflict, core/borrow_field_ok | positive/negative |
| double free | safety/malloc_double_free, compile_fail/double_release, core/double_release_alias, safety/interproc_double_free | negative |
| direct / alias UAF | safety/uaf_direct, safety/uaf_alias, core/uaf_conditional | positive/negative |
| managed double/invalid release | runtime_fail/double_release, runtime_fail/freed_len, core/freed_len | runtime trap |
| bounds (constant, loop, multidim, pointer arithmetic) | compile_fail/constant_oob, loop_oob, multidim_bounds, classic_bug26_ptr_arith; runtime_fail/bounds | positive/negative/runtime |
| null dereference | compile_fail/null_deref, safety/null_deref, core/null_outparam | positive/negative |
| definite initialization | compile_fail/uninit, safety/uninit_branch, core/init_all_branches | positive/negative |
| realloc safety | safety/realloc_success, safety/realloc_failure, safety/double_release_after_realloc, core/libc_matrix/realloc_* | positive/negative |
| checked copies | core/libc_matrix/memcpy_overlap, strcpy_literal, strcpy_s_valid/invalid_size | positive/negative |
| RAII exits and order | cases/resource_scope, nested_scopes, loop_cleanup, core/raii_basic, nested_raii, raii_early_return | positive |
| multi-TU / .hp linkage | core/multi_tu, multi_tu_resource, multi_tu_hp, multi_tu_lib | positive |
| threads / guards | cases/concurrency_mutex, core/thread_guard_ok, safety/thread_global_write, thread_invalid_sharing | positive/negative |
| quarantine / stale alias | core/quarantine, core/quarantine/run.sh | runtime semantics |
| fuzz / checker robustness | core/fuzz_checker.sh (120 generated inputs) | fuzz |
| logging categories | tests/logging_test.sh | positive |

Runtime line coverage target: >=80%. Latest measured: **320/395 lines (81%)**
via `tests/core/coverage.sh`, including allocator growth, zero-size paths,
fatal paths via test hook, printing/scanning and the quarantine branch.

Coverage does not prove safety; unsupported cases remain in
`docs/SAFE_SUBSET.md` and `tests/known_gaps/`.

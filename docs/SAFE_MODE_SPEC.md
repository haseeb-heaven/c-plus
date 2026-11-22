# C+ SAFE mode specification v1 (Agent 1)

This is the exact, versioned contract for the C+ safety modes.
`--legacy-c` preserves ordinary C semantics and makes no safety claim.

## Operating modes

| mode | flag | proven memory errors | unverified operations |
| --- | --- | --- | --- |
| DEFAULT | `--default` (implicit if no `--strict`) | rejected | warning (not advertised as fully memory-safe) |
| STRICT | `--strict` | rejected | rejected (fail closed, never a silent downgrade) |
| LEGACY | `--legacy-c` | not analyzed | no safety claim |

Both safety modes share the same analysis. The only difference is how
*unverified* operations are handled: `warnAt()` emits a warning in DEFAULT and
an error in STRICT. Proven errors (use-after-free, double free, invalid free, overflow,
null dereference, definite-initialization failures) are always rejected in
both safety modes. The documented-safe `realloc` success/failure pattern is
pre-detected so it is not reported as an unverified operation in either mode.

Regression suite: `tests/core/strict_mode.sh` (16 cases).

## Pipeline

```text
Clang AST -> Clang CFG -> pointer/ownership state -> library contracts ->
flow-sensitive verification -> source lowering -> RAII cleanup ->
Clang/LLVM native codegen
```

Clang libraries are linked directly; no parser, CFG engine or optimiser is
reimplemented. Ownership rules never rely on string/token matching.

## Abstract state

Each tracked pointer/owner has one of:
`UNINITIALIZED`, `OWNED`, `BORROWED_SHARED`, `BORROWED_MUTABLE`, `MOVED`,
`FREED`, `NULL`, `UNKNOWN`. Unknown is conservative and fails closed in the
declared safe subset.

## Guarantee status

| guarantee | status | evidence |
| --- | --- | --- |
| unique ownership of resource/managed locals | IMPLEMENTED | resource_copy, resource_assign, move tests |
| ownership transfer on return / cplus_move | IMPLEMENTED | resource_move, move_return |
| use-after-move rejection | IMPLEMENTED | use_after_move, moved_from_warning |
| use-after-free (direct + simple alias, straight-line, realloc success branch, interprocedural return of freed pointer) | IMPLEMENTED | uaf_direct, uaf_alias, libc_alias_uaf, realloc_success_alias_uaf, interproc_return_uaf |
| double-free (same var + simple alias + interproc free + branch paths) | IMPLEMENTED | malloc_double_free, interproc_double_free, double_release_alias, branch_double_free |
| invalid frees (stack/global object, string literal, array, interior pointer) | IMPLEMENTED | invalid_free_stack, invalid_free_global, invalid_free_string, invalid_free_array, invalid_free_interior, release_stack_pointer; `p + 0` accepted |
| shared vs exclusive mutable borrows (fields) | PARTIAL | borrow_mut_conflict, borrow_field_ok; containers not covered |
| non-lexical lifetimes | NOT APPLICABLE (by design) | C+ does not impose Rust-style exclusive borrows, so a borrow that is no longer used needs no region end. Alias invalidation is tied to definite/may release, not to a lexical scope end; dead borrows therefore produce no diagnostics. Full region-based NLL is deliberately not implemented. |
| pointer alias tracking across branches/loops | PARTIAL -> IMPROVED | may-release state collected from if/loop bodies; definite double-free rejected, possible UAF warned in DEFAULT and rejected in STRICT; reassignment to fresh/null clears may-state |
| definite initialization | IMPLEMENTED | uninit, uninit_branch, init_all_branches (CFG dataflow) |
| unsigned comparison with zero via `<` (always false) | IMPLEMENTED | interproc_heap_extent |
| nullability | IMPLEMENTED (provable cases) | null_deref, null_outparam, null_return_deref (functions whose every return is null) |
| provenance / arbitrary casts | PARTIAL -> IMPROVED | allocation/object/alias/string/array origins tracked; integer-forged pointers rejected in STRICT and warned in DEFAULT; casts from int still not proven safe |
| buffer bounds (constant, constant-propagated local index, simple loops, multidim, heap extents including interprocedural argument extents, memcpy/memset/strcpy/strncpy/strcat/snprintf) | IMPLEMENTED | constant_oob, loop_oob, multidim_bounds, bounds_var_index, memset_overflow, strcat_overflow, strncpy_overflow, snprintf_overflow |
| libc nullability (null source pointers) | IMPLEMENTED | memcpy_null_source, mass_fuzz memcpy_null_src_* |
| dynamic bounds checks | PARTIAL | C+ inserted __cplus_bounds_check for fixed arrays plus opt-in sanitizers; not a complete spatial guarantee |
| struct/field ownership | PARTIAL | field borrow conflict; field move not complete |
| container ownership | UNSUPPORTED | Agent 2 library layer owns this; compiler contract only |
| function-effect summaries | PARTIAL | libcEffect JSON + simple user `frees(arg)` |
| cross-TU contracts | PARTIAL | JSON installed; no cross-TU ownership ABI check |
| thread ownership / race prevention | PARTIAL -> IMPROVED | stack-borrow warning, unsynchronised global write rejection, shared global mutated by multiple pthread_create entries detected (DEFAULT warning / STRICT error); no full race proof |
| deterministic RAII | IMPLEMENTED (supported exits) | raii_basic, nested_raii, raii_early_return, loop_cleanup |
| goto/longjmp cleanup | PARTIAL | STRICT rejects jumps bypassing cleanup of a live allocation (classic_bug34); plain jumps warn |
| hardened libc contract recognition | IMPLEMENTED (v2 schema) | libc_contracts.json, libc_matrix |

## Safe-mode diagnostics

Categories: `CPLUS-OWNER`, `CPLUS-BORROW`, `CPLUS-LIFETIME`, `CPLUS-ALIAS`,
`CPLUS-BOUNDS`, `CPLUS-REALLOC`, `CPLUS-LIBC`, `CPLUS-RAII`, `CPLUS-FFI`.
Legacy `CPLUS-SAFE-*` prefixes are retained for fixture continuity.

Diagnostics include file, line, column, API/ownership reason and expected vs
inferred bounds where known. Opt-in logging emits the same categories without
printing buffer contents.

## Unsafe/FFI boundary

Unknown functions are not assumed safe. Calls whose effects cannot be proven
from the v2 contract schema remain outside the safe subset. Standard C source
and ABI are preserved; no mandatory `_Owner`, `defer`, `borrow`, `unsafe`,
`malloc_s` or `free_s` syntax is introduced.

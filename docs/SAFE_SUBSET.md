# C+ SAFE subset specification (v0.1, Agent 1)

This is the **exact** subset for which safe mode makes claims. Everything else
is unsupported, legacy (`--legacy-c`), or requires an explicit unsafe boundary.
This is **not** yet Rust-level safety; it is a specified, tested subset.
Current as of the `agent/core-safety` branch with `origin/develop` merged;
see `docs/ISSUE21_EVIDENCE.md` for the base/version evidence.

## Accepted in SAFE mode
- ordinary C scalars, arrays, structs, unions, function calls and libc/POSIX
  calls whose contracts are modelled or explicitly bounded;
- unique owning locals (`resource struct`, class with `drop`, `managed`);
- moves via `cplus_move` and automatic move on return;
- borrows of locals (`&x`, `&s.field`) provided no conflicting mutable borrow
  exists and the borrow does not escape;
- deterministic RAII on scope exit, return, break, continue and nested scopes;
- compile-time bounds for constant indices and simple counted loops (including
  function-pointer arrays); C+ inserted `__cplus_bounds_check` for dynamic
  indices into fixed-size arrays; configurable
  ASan/UBSan/TSan/LSan/CFI/integer sanitizers as additional evidence, never as
  the sole safety proof.

## Compile-time rejected
- copy/assignment of an owning handle;
- use-after-move, borrow used after move, borrow escape;
- double free of the same variable, direct/aliased use-after-free in
  straight-line code, interprocedural `frees(arg)` double free;
- constant/pointer-arithmetic out-of-bounds on known arrays;
- null dereference from a provably null initializer;
- provably oversized `memcpy`/`memmove` byte count and literal `strcpy`;
- STRICT rejects `goto`/`longjmp` that bypasses cleanup of a live allocation;
  plain `goto`/`longjmp` with no cleanup obligation only warns;
- safe-subset restrictions: raw `realloc(p,n)`, zero-size raw allocation.

- signed integer overflow and division/remainder by zero are diagnosed at
  compile time by C+ analysis (CPLUS-ARITH): STRICT rejects when safety cannot
  be established, DEFAULT warns; unsigned modulo arithmetic is accepted.

## Dynamically enforced
- managed double/invalid release (exit 104/105);
- explicit `cplus_check_live` for stale managed aliases;
- opt-in quarantine (`CPLUS_QUARANTINE=1`) delays reuse in a bounded ring;
- `__cplus_bounds_check` prints a controlled C+ diagnostic and aborts before
  the access (see `tests/core/fnptr_bounds_suite.py`);
- optional sanitizers with exact-diagnostic assertions
  (`tests/core/sanitizer_suite.py`).

## Unknown / unsafe (must not be claimed safe)
- arbitrary raw pointer provenance, casts and pointer arithmetic escapes;
- frees through unknown callbacks or aliases not tracked by the analysis;
- cross-TU ownership ABI mismatches;
- fields/containers with untracked element lifetimes;
- general data races and shared mutable thread state;
- `exit()` cleanup bypass and `setjmp` paths where no live resource can be
  identified; STRICT does reject live-resource `longjmp` violations;
- any libc call without a known effect summary or explicit bound.

## Fail-closed status (issue #21 task 4)

STRICT rejects proven violations and every unsupported contract field, but
**does not reject every unverifiable pointer operation**: pointers with unknown
bounds/lifetime, field/container element lifetimes, general races and cross-TU
ownership remain outside the subset. Rejecting them would reject ordinary C,
which the product direction forbids. There is no general `unsafe` boundary
marker in C+ today; the documented trusted interfaces are the JSON contracts,
`cplus_check_live`, and explicit bounds (`--sanitize` / dynamic bounds checks).
No Rust-equivalence or blanket memory-safety claim is made. The paired
compile-time comparison (`docs/RUST_COMPARISON.md`,
`tests/core/rust_compare_suite.py`) measures only the overlapping subset: C+
15/20 and Rust 15/20 rejections; it is not parity.

## Upstream research (pinned; licences checked; no source copied)
| project | pinned revision | license | use |
| --- | --- | --- | --- |
| CORAL (`tiagodusilva/coral`) | `b047d6dab621` | no license found | design research only; **not copied** |
| Checked C | `a2843eae17d9` | Apache-2.0 LLVM | design reference for bounds contracts |
| Safe C++ | website only | website | lifetime/borrow model research |
| Xr0 | `cd306c23def6` | Apache-2.0 | effect-contract design reference |
| Rust | system rustc | MIT/Apache-2.0 | semantics reference and benchmark baseline |
| LLVM/Clang (installed) | Apple clang 16 | Apache-2.0 LLVM | AST/CFG/dataflow linked directly |

## Standard libc / checked-function contract table (compiler recognition)

| API | nullability | ownership | extents / effects | failure behavior | static diagnostic |
| --- | --- | --- | --- | --- | --- |
| `malloc`, `calloc` | arg n/a | returns owned allocation | size expression recorded where constant | may return NULL; errno unchanged | allocation effect summary; overflow delegated to libc |
| `realloc` | arg may be NULL | returns owned; old alias invalid only on success | copies min(old,new) bytes | failure keeps original valid | `CPLUS-SAFE-REALLOC` warning; success/failure branch tracking pending |
| `free`, `cplus_release` | NULL allowed | consumes owner | n/a | no return | `CPLUS-SAFE-FREE` on double free |
| `memcpy`, `memmove` | non-NULL for n>0 | borrow only | dst extent + n; writes dst | native semantics | `CPLUS-SAFE-BOUNDS` on provable overflow |
| `memset` | non-NULL | borrow only | dst extent + n; writes | native | `CPLUS-SAFE-BOUNDS` |
| `strcpy`, `strncpy`, `strcat` | non-NULL | borrow only | dst capacity + literal/source length | native | `CPLUS-SAFE-CRT` on literal overflow |
| `strlen` | non-NULL | read-only borrow | reads until NUL | native | `CPLUS-SAFE-CRT` |
| `fopen` | path non-NULL | returns owned FILE* | n/a | may return NULL | `CPLUS-SAFE-CRT` effect summary |
| `fclose` | non-NULL | consumes FILE* | n/a | native | `CPLUS-SAFE-CRT` |
| `fread`, `fwrite` | non-NULL buffers | borrow; writes dest | count*size arithmetic | short reads/writes are native | `CPLUS-SAFE-CRT` unknown-size cases not claimed |
| `strcpy_s`, `strncpy_s`, `strcat_s`, `memcpy_s`, `memmove_s`, `memset_s`, `strnlen_s`, `sprintf_s`, `snprintf_s`, `fopen_s` | platform-specific | borrow/own per native API | explicit capacity argument | native CRT semantics | `CPLUS-SAFE-CRT`; exact signature from Agent 2 capability layer |
| `malloc_s`, `calloc_s`, `realloc_s`, `free_s` | proposed C+ names only | same model as ordinary calls | allocation size | native-like failure semantics | `CPLUS-SAFE-ALLOC/FREE/REALLOC`; **not** Microsoft/Apple APIs |

Apple Bounds Safety (`__counted_by`, `__sized_by`, dynamic bounds and
compiler-inserted checks) was studied from the public documentation and pinned
swiftlang/llvm-project tree, but the installed Apple Clang 16 rejects
`-fbounds-safety`. Copying `ptrcheck.h` alone is not enabled or claimed.
Microsoft Secure CRT and Intel safestringlib are treated as checked-runtime
references only; Agent 2 owns any library adaptation. Intel safestringlib
(f4491915) is MIT; CORAL has no licence and was not copied.

Structured diagnostic categories emitted by the Clang AST checker:
`CPLUS-SAFE-ALLOC`, `CPLUS-SAFE-FREE`, `CPLUS-SAFE-REALLOC`,
`CPLUS-SAFE-BOUNDS`, `CPLUS-SAFE-BORROW`, `CPLUS-SAFE-LIFETIME`,
`CPLUS-SAFE-FFI`, `CPLUS-SAFE-CRT`. Each includes file, line, API reason and
expected/inferred bounds where known. Logging remains opt-in and does not log
buffer contents.
## Array-decay ABI note

The generated C is unchanged; no metadata is passed at runtime and no calling
convention or ABI changes are required. Array extents are inferred entirely at
compile time from local declarations and simple pointer-return helpers, then
used to reject provable out-of-bounds subscripts. STRICT rejects pointer
subscripts whose extent cannot be established; DEFAULT warns. Legal one-past
pointer formation remains accepted.

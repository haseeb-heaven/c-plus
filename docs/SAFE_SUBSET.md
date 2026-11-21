# C+ SAFE subset specification (v0.1, Agent 1)

This is the **exact** subset for which safe mode makes claims. Everything else
is unsupported, legacy (`--legacy-c`), or requires an explicit unsafe boundary.
This is **not** yet Rust-level safety; it is a specified, tested subset.

## Accepted in SAFE mode
- ordinary C scalars, arrays, structs, unions, function calls and libc/POSIX
  calls whose contracts are modelled or explicitly bounded;
- unique owning locals (`resource struct`, class with `drop`, `managed`);
- moves via `cplus_move` and automatic move on return;
- borrows of locals (`&x`, `&s.field`) provided no conflicting mutable borrow
  exists and the borrow does not escape;
- deterministic RAII on scope exit, return, break, continue and nested scopes;
- compile-time bounds for constant indices and simple counted loops; dynamic
  bounds/null/alignment checks via UBSan where compile-time proof is absent.

## Compile-time rejected
- copy/assignment of an owning handle;
- use-after-move, borrow used after move, borrow escape;
- double free of the same variable, direct/aliased use-after-free in
  straight-line code, interprocedural `frees(arg)` double free;
- constant/pointer-arithmetic out-of-bounds on known arrays;
- null dereference from a provably null initializer;
- mutable borrow conflicts on the same owner field;
- provably oversized `memcpy`/`memmove` byte count and literal `strcpy`;
- `goto`/`longjmp` warnings in safe mode; full rejection is pending;
- safe-subset restrictions: raw `realloc(p,n)`, zero-size raw allocation.

## Dynamically enforced
- managed double/invalid release (exit 104/105);
- explicit `cplus_check_live` for stale managed aliases;
- opt-in quarantine (`CPLUS_QUARANTINE=1`) delays reuse in a bounded ring;
- UBSan bounds/null/alignment/undefined with abort-on-first-failure.

## Unknown / unsafe (must not be claimed safe)
- arbitrary raw pointer provenance, casts and pointer arithmetic escapes;
- frees through unknown callbacks or aliases not tracked by the analysis;
- cross-TU ownership ABI mismatches;
- fields/containers with untracked element lifetimes;
- general data races and shared mutable thread state;
- `setjmp`/`longjmp`/`exit` cleanup bypass;
- any libc call without a known effect summary or explicit bound.

## Upstream research (pinned; licences checked; no source copied)
| project | pinned revision | license | use |
| --- | --- | --- | --- |
| CORAL (`tiagodusilva/coral`) | `b047d6dab621` | no license found | design research only; **not copied** |
| Checked C | `a2843eae17d9` | Apache-2.0 LLVM | design reference for bounds contracts |
| Safe C++ | website only | website | lifetime/borrow model research |
| Xr0 | `cd306c23def6` | Apache-2.0 | effect-contract design reference |
| Rust | system rustc | MIT/Apache-2.0 | semantics reference and benchmark baseline |
| LLVM/Clang (installed) | Apple clang 16 | Apache-2.0 LLVM | AST/CFG/dataflow linked directly |

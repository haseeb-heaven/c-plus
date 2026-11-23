# ABI-preserving runtime memory safety

C+ keeps ordinary one-word C pointers, C-compatible struct layouts, function
signatures and symbol names. No fat pointers, capability registers, custom
calling convention or shadow address space is introduced. Metadata lives in a
compiler-visible side table and in extra arguments to *internal* runtime
helpers — never in pointer bits.

## What is enforced natively (no sanitizer required)

| Operation | Mechanism | Failure |
| --- | --- | --- |
| Dynamic index into a statically known array | `__cplus_bounds_check(index, length, name)` injected by `CPlusRewriter.cpp` | fatal, exit 101 |
| Pointer formation `p + i` with a provable extent | `cplus_check_ptr_formation(base, count, index, file, line, expr)`; one-past allowed | fatal, exit 101 |
| Signed add/sub/mul, div/rem by zero, `INT_MIN/-1`, shifts, narrowing conversions | inline `__builtin_*_overflow` fast paths plus runtime helpers | exit 110 / 111 |
| Managed allocation, release, double/invalid release | `cplus_alloc*`, `cplus_release`, side table plus recent-free cache | exit 103/104/105 |
| Managed allocation inside a managed pointer's `.len` | `cplus_check_live` / `cplus_alloc_count` | fatal type-confusion exit |
| Raw `malloc/calloc/realloc/free` in Safe mode | compiler-emitted `cplus_register_allocation`, `cplus_realloc_registered`, `cplus_free_registered` | double/invalid free exit 104/105 |
| Opt-in quarantine | `CPLUS_QUARANTINE=1`, 1024 delayed frees | delays address reuse |

## Registration design (Phase 3/4)

`runtime/cplus_runtime.c` keeps an open-addressed `cplus_live` table of
`{ptr, size}` plus a 4096-slot direct-mapped recent-free cache. The compiler
wraps constant-sized allocator calls in ordinary C helpers that return the
same pointer:

```c
p = cplus_register_allocation(malloc(2*sizeof(int)), 8, __FILE__, __LINE__);
q = cplus_realloc_registered(p, realloc(p, 32), 32, __FILE__, __LINE__);
cplus_free_registered(q, __FILE__, __LINE__);
```

`cplus_realloc_registered` leaves the old registration live when `realloc`
returns null, and transfers it on success. This closes the out-parameter and
realloc-failure cases without changing the pointer ABI.

Sites are collected in `AstSafetyCheck.cpp` (`recordAllocSite`,
`recordFreeSite`, `checkedAllocSites_`, `checkedFreeSites_`) and published to
`CPlusRewriter.cpp` through the existing registry handoff. Instrumentation
runs only in Safe mode and only for non-`check` commands.

## Unknown pointers: explicit policy

C+ never invents a bound. An extent exists only when derived from a fixed
array, a registered allocation with a known size, array decay with an
established count, or a consistent returned extent. Unknown extent is a state,
not a default value: STRICT rejects a provably uncheckable operation, DEFAULT
warns, LEGACY is silent. The runtime helpers abort on a missing registration
rather than returning a fabricated length.

## Foreign C boundaries (Phase 5)

Pointers from unannotated external declarations, callbacks, precompiled
archives and opaque handles are `unknown extent + unknown ownership + unknown
release effect`. They may be passed through, compared and stored, but are not
dereferenced or subscripted with a compiler-derived bound. Cross-allocator
release (C `free` of a C+ managed pointer, or `cplus_release` of a raw
`malloc` pointer) is an error where provable; runtime identity checks cover
the remaining cases. See `docs/compiler/pointer-provenance.md`.

## Verification

- `tests/compiler/runtime_memory_safety/run_suite.py`: 28 cases × 2 modes,
  all passing. Primary mode is `--strict --sanitize=none`; sanitizer-enabled
  runs are supplementary only.
- `tests/compiler/abi_safety/run_suite.py`: 11 fixtures × 3 modes = 33/33
  checks, verifying `sizeof(void*)`, `offsetof`, signatures, callbacks,
  function pointers, `nm` symbol names and precompiled library interop.
- Full regression: repo 109/0, core 152/0, CTest 2/2, Agent 1 156 PASS /
  0 FAIL / 3 SKIP, bounds 13/13, native 24/24, metrics 90/90 with 0 FP.

## Honest limitations

The side table does not give byte-granular interior-pointer coverage, does not
track provenance through integer casts or arbitrary pointer arithmetic, has no
generation token yet (so address reuse after the recent cache evicts can be
misattributed), and does not provide race safety. Pointer-subscript OOB on
heap/decay/interior/`int**`/struct-field shapes is still only caught by
supplementary ASan; those cases are explicitly labelled `asan_only` in the
runtime suite. No Fil-C-equivalent or CHERI-equivalent guarantee is claimed.

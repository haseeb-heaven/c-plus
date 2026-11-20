# Upstream reuse audit

Baseline recorded before integration on `develop`: **101 tests, 16 demos,
ctest 2/2** (after the `_Countof` alias commit). After the changes in this
branch: **102 tests, 16 demos, ctest 2/2**, plus an unchanged 1,308-line C
project (`kilo.c`) compiled in `--legacy-c` mode.

## Pinned upstreams

| project | pinned commit | license | finding |
| --- | --- | --- | --- |
| Cake (`thradams/cake`) | `2da2afd1759a` | GPLv3 + addendum forbidding AI training/reference use | **Rejected.** Legally incompatible with this MIT project and the addendum. No code inspected beyond README/license. |
| Cforall (`cforall/cforall`) | `a726980cfaa5` | BSD 3-clause | Compatible licence, but it is a separate compiler with a different object model. Porting its destructor lowering would duplicate the existing `RaiiLowering.cpp`. No code copied. |
| Clang Bounds Safety (`swiftlang/llvm-project`) | `ca74e48841e5` | Apache-2.0 WITH LLVM exceptions | Host `/usr/bin/clang` (Apple clang 16) rejects `-fbounds-safety` and has no `ptrcheck.h`; the local LLVM 23 binary cannot start (missing `libLLVM.23.1.dylib`). Blocked by toolchain. Adapted alternative: token-level bounds pass + UBSan. |
| Checked C (`checkedc/checkedc-clang`) | `a2843eae17d9` | Apache-2.0 WITH LLVM exceptions | Requires a forked Clang AST and checked-pointer types. The host compiler has neither; importing a fork would duplicate the backend. Blocked by architecture. |
| Fil-C (`pizlonator/fil-c`) | `d9041afd6b11` | Apache-2.0 WITH LLVM exceptions + GC runtime | Requires a full LLVM fork, custom ABI, concurrent GC and capability pointers. Violates "no mandatory GC", "reuse existing toolchain", and native C ABI preservation. Blocked by design. |
| SafeC (`safec-org/SafeC`) | `e40ea4c92171` | MIT | Compatible licence, but its region/ownership checker is a separate whole-program analysis. Its *ideas* are already implemented as C+ resources, managed allocations and the borrow subset. No code copied. |

## What was actually integrated

No third-party source files were copied into this repository in this work.
Where an upstream technique was applicable, the existing C+ component was
extended instead:

- `compiler/Sema/StaticBoundsCheck.cpp` — bounds, ownership, aliasing and
  control-flow checks (adapted from the *idea* of bounds/checked-pointer
  analysis, not upstream code).
- `compiler/Sema/RaiiLowering.cpp` — deterministic cleanup, `defer`, borrow
  subset (adapted from proven RAII/ownership concepts).
- `runtime/cplus_runtime.c` — allocator side table, freed-address cache and
  now overflow-checked array allocation.
- `runtime/include/cplus/cplus.h` — public ABI used by generated C.

## Compatibility evidence

Unchanged real-world C: `kilo.c` (1,308 lines, POSIX/termios, macros, `goto`,
`realloc`) compiles with `c+ build --legacy-c`. In safe mode it is intentionally
rejected only where it uses raw `realloc`, `memcpy` overlap and `ctype` with
plain `char` — exactly the documented safe-mode rules, not accidental parser
failures.

The same applies to `2048.c`: the safe-mode diagnostic about `printf` in a
signal handler is correct (pitfall 30); `--legacy-c` compiles it unchanged.

`tetris.c` (42 KB, ncurses) was also compiled unchanged in both `--legacy-c`
and safe mode and linked with `-lncurses`. Fixing this exposed three static
checker false positives (shared array names across functions, declarations
mistaken for accesses, and field-name collisions in `memcpy` overlap
detection); all three are fixed and regression-tested.

## Remaining unresolved safety problems

- Use-after-free cannot be detected automatically: ASan aborts inside its own
  runtime on this host, and TSan reports broken interceptors. `cplus_check_live`
  provides an explicit check only.
- General data races, cross-TU ABI mismatches, flexible-array-member copy and
  macro double evaluation remain documented gaps.

## Performance note

Benchmarks after the restructure still run against `clang -O2`. The managed
allocator costs roughly 3.4x in the allocation microbenchmark; method dispatch
and RAII loops are near 1.00x in legacy mode, with safe-mode overhead coming
from UBSan instrumentation. See `LIMITATIONS.md` for the latest table.

# C+ core safety model (Agent 1)

Scope: compiler, runtime, ownership, RAII. No new language features or stdlib
changes in this branch. Agent 2 owns `stdlib/**`, `tests/stdlib/**` and
`docs/MODERN_STDLIB.md`; those paths were not modified.

## Upstream references (pinned, external only)

| project | commit | license | use |
| --- | --- | --- | --- |
| llvm/llvm-project | `14fee62eb543` | Apache-2.0 WITH LLVM exceptions | installed Clang/LLVM libraries used directly; no source copied |
| swiftlang/llvm-project | `ca74e48841e5` | Apache-2.0 WITH LLVM exceptions | inspected `ptrcheck.h`/bounds docs only |
| checkedc/checkedc-clang | `a2843eae17d9` | Apache-2.0 WITH LLVM exceptions | studied checked-pointer contract ideas only |
| cforall/cforall | `a726980cfaa5` | BSD 3-clause | studied destructor-lowering concepts only |
| pizlonator/fil-c | `d9041afd6b11` | LLVM Apache-2.0 + runtime terms | studied capability/UAF architecture only; no code/GC copied |

None of these repositories are committed into C+. The external clones live in
`../c-plus-upstreams`.

Toolchain: Apple clang 16.0.0 (clang-1600.0.26.6), CMake 4.4.5.
`-fbounds-safety` is **unsupported** by the installed Clang (`unknown argument`),
so the Apple bounds-safety backend was not pretended to work. Bounds are
enforced with C+ AST/token analysis and UBSan.

## Safety model

1. **Unique owner.** A `resource struct` or class with `drop` is owned by the
   declaring scope. Copying or assigning an owning handle is a compile error.
2. **Move semantics.** `cplus_move(x)` zeroes the source and transfers
   ownership. Returning a resource local auto-moves it. Reusing a moved-from
   handle produces a warning; re-borrow the new owner.
3. **Borrows.** A pointer borrowed from a local owner may be used while the
   owner is alive and un-moved. A borrow escaping the function or used after
   move is rejected.
4. **Managed memory.** `cplus_alloc`/`cplus_new`/`cplus_new_array` allocate
   tracked memory. `cplus_release` is exactly-once; double/invalid release is
   fatal (104/105). `cplus_check_live` gives an explicit dangling-pointer
   check.
5. **Deterministic RAII.** Cleanup runs on scope exit, return, break and
   continue in reverse construction order. Return expressions are evaluated
   before cleanup. `goto`/`longjmp` bypass cleanup and are warnings.
6. **Definite initialization / null / bounds.** Clang's uninitialized and
   null diagnostics are promoted; constant and simple-loop bounds are checked
   by AST/token analysis; UBSan aborts on dynamic violations.

## Runtime API contracts (`cplus/cplus.h`)

- `cplus_alloc(size)` — live side-table entry; fatal on OOM.
- `cplus_alloc_array(count, elem)` — overflow-checked multiplication.
- `cplus_release(ptr)` — releases once; double/invalid is fatal.
- `cplus_check_live(ptr)` — aborts if not a live C+ allocation.
- `cplus_new(T)` / `cplus_new_array(T,N)` — zeroed managed allocation.
- `cplus_move(x)` — zero source; destructor must tolerate zeroed handle.
- `countof(a)` — array-only compile-time element count.
- `cplus_memcpy`/`cplus_memmove` — overlap-safe copies with null checks for
  non-zero counts (zero-count null calls are a no-op).
- `cplus_strnlen(s,max)` — bounded string length, never reads past `max`.
- `cplus_strcpy(dst,size,src)` — bounded copy that aborts on overflow.
- `cplus_fatal_hook` — test-only hook invoked before termination; production
  leaves it NULL.

## Evidence

- `tests/core/run.sh` → 13 passed: RAII order, move/re-borrow, multi-TU link,
  libc interop, release mode, moved-from/double-move warnings, rejected
  bounds/uninit/null, traps 104/105.
- Existing suites → 109 tests, 24 breaker probes, 10 stress cases, 25 V3
  breaker cases: all pass.
- Use-after-move and double-move warnings added in `RaiiLowering.cpp`.

## Measurements

- Compile time (`c+ check` of a small core test): ~0.10 s warm.
- Release binary size: ~50 KB for a small class/resource program.
- Alloc microbench: safe C+ ~5.7x Clang C (side-table bookkeeping);
  bounds_sum and function_call within ~1.1x.
- Memory overhead: one side-table record per live allocation (not separately
  measured here).

## Known unsafe / unsupported

- Automatic use-after-free detection: not available (ASan broken on host);
  explicit `cplus_check_live` only.
- General data races and lock discipline: not verified.
- Cross-TU ABI mismatches: not detected (single-TU compiler).
- `goto`/`longjmp`/`exit` bypass RAII; warnings only.
- Arbitrary pointer arithmetic and legacy C FFI are outside the safe subset.

## P0 issue progress (agent/core-safety)

- **#12 RAII** — covered by deterministic cleanup lowering, moved-from
  warnings, exactly-once tests and non-local-jump warnings. Not fully CFG-
  verified for every exotic exit; documented as partial.
- **#13 Definite initialization** — added a Clang CFG dataflow check in
  `compiler/Analysis/OwnershipAnalysis.cpp` with path-sensitive intersection
  at joins. Address-taken locals are treated as initialized (output-parameter
  convention), which is conservative and documented.
- **#14 Checked pointers** — added AST-based pointer-arithmetic bounds checks
  (direct array variables, one-past-end allowed) alongside existing UBSan and
  static bounds analysis. General provenance/casts remain unsupported.
- **#15 Thread safety** — added an AST warning for passing a stack borrow to
  `pthread_create`; existing RAII mutex/Guard and data-race token checks remain.
  This is a warning, not full race-freedom enforcement.

Core-safety runner now has 25 cases, plus a 14-case scratch-style probe script (`tests/core/scratch_probe.sh`). Full suites remain green.

## Coverage

`tests/core/coverage.sh` builds `runtime/cplus_runtime.c` with gcov
instrumentation, runs `tests/core/coverage_driver.c` (including intercepted
fatal paths via `cplus_fatal_hook`) and reports line coverage. Target: >=80%
for the runtime library. Current result: **337/414 lines (81%)**, covering
fatal paths through the test hook, allocator growth, zero-size paths and I/O. Runtime logging is gated by a global level set from
`CPLUS_LOG_LEVEL` when `--log` is used; without it, logging has no effect.

## libc boundary benchmark

`tests/core/libc_bench.sh` compares the safe bounded-memory wrappers against
raw libc with Clang -O2 on the same 2,000,000-iteration workload:

```text
c+ safe : real 0.58s
clang C : real 0.16s
clang UB: real 0.33s
```

The raw-clang difference includes C+ UBSan instrumentation; the `clang UB`
line is the equivalent sanitizer baseline. Overhead varies with machine load;
the script is the source of truth. This is a narrow measured
boundary, not a claim that all of libc is safe or Rust-equivalent.

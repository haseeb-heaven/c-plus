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

Core-safety runner now has 49 cases, plus a 14-case scratch-style probe script (`tests/core/scratch_probe.sh`). Full suites remain green.

## Coverage

`tests/core/coverage.sh` builds `runtime/cplus_runtime.c` with gcov
instrumentation, runs `tests/core/coverage_driver.c` (including intercepted
fatal paths via `cplus_fatal_hook`) and reports line coverage. Target: >=80%
for the runtime library. Current result: **308/382 lines (80%)** after removing redundant wrappers, covering
fatal paths through the test hook, allocator growth, zero-size paths and I/O. Runtime logging is gated by a global level set from
`CPLUS_LOG_LEVEL` when `--log` is used; without it, logging has no effect.

## libc boundary benchmark

`tests/core/libc_bench.sh` compares the safe bounded-memory wrappers against
raw libc with Clang -O2 on the same 2,000,000-iteration workload:

Latest library-boundary run:

```text
c+ safe : real 0.55s
clang C : real 0.48s
clang UB: real 0.40s
```

The raw-clang difference includes C+ UBSan instrumentation; the `clang UB`
line is the equivalent sanitizer baseline. Overhead varies with machine load;
the script is the source of truth. This is a narrow measured
boundary, not a claim that all of libc is safe or Rust-equivalent.

## Bug fixes found by new tests

- `cplus_realloc` previously kept the old pointer live, so releasing an old
  alias was silently accepted. It now releases the old allocation, making
  stale aliases detectable (double release / invalid length query).

## Additional evidence

- Multi-TU resource: a resource declared in a plain `.h` header builds and
  links across two translation units when the RAII-relevant declarations are
  represented in C form. A `resource struct` inside a plain `.h` is not
  lowered (headers are lowered only when written as `.hp`), which is a
  documented cross-TU limitation.
- Thread guard: synchronized shared counter across two threads passes with
  RAII lock/unlock. An unsynchronized global write in a thread function is
  rejected.

## Provenance and reuse audit (PR #16 architecture correction)

| upstream | pinned commit | license | precise reused interface/file | actual integration |
| --- | --- | --- | --- | --- |
| llvm/llvm-project | `14fee62eb543` | Apache-2.0 WITH LLVM exceptions | `clang/include/clang/AST`, `clang/include/clang/Analysis/CFG.h` | linked installed Clang 16 static libs; `OwnershipAnalysis.cpp` uses `CFG::buildCFG`; no source copied |
| swiftlang/llvm-project | `ca74e48841e5` | Apache-2.0 WITH LLVM exceptions | `clang/lib/Sema/SemaBoundsSafety.cpp`, `clang/lib/Headers/ptrcheck.h` | inspected only; installed clang has no `-fbounds-safety`; fallback documented |
| checkedc/checkedc-clang | `a2843eae17d9` | Apache-2.0 WITH LLVM exceptions | `clang/docs/checkedc/Implementation-Notes.md` | design reference for checked-pointer contracts only; no code copied |
| cforall/cforall | `a726980cfaa5` | BSD 3-clause | constructor/destructor lowering docs | design reference for RAII; no code copied |
| pizlonator/fil-c | `d9041afd6b11` | LLVM Apache-2.0 + runtime terms | `https://fil-c.org/how`, capability architecture | studied only; no GC/ABI/runtime copied |
| thradams/cake | `2da2afd1759a` | GPLv3 + AI-use restriction | docs/README only | **excluded**: license forbids AI/reference use |
| 0xA1M/clog | default branch | MIT | `clog.h` | vendored as `cplus/clog.h`, MIT preserved in `third_party/clog/LICENSE` |

### Reclassification of runtime APIs

| API | classification | evidence / decision |
| --- | --- | --- |
| `cplus_alloc`, `cplus_zalloc`, `cplus_alloc_array`, `cplus_release`, `cplus_check_live`, `cplus_alloc_bytes/count` | **compiler-required runtime primitive** | required by managed ownership side table; overflow checked |
| `cplus_check_bounds`, `cplus_check_index`, `cplus_fatal`, `cplus_check_nonnull`, `cplus_resource_check_borrow` | **compiler-required ABI** | emitted by lowering for deterministic safety failures |
| `cplus_move`, `cplus_zero` | **compiler-required helper** | ownership transfer zeroes source handle |
| `cplus_memcpy`, `cplus_memmove` | **redundant handwritten wrapper -> libc** | inline aliases to system `memmove`; do NOT prove region validity |
| `cplus_strnlen` | **redundant handwritten wrapper -> libc** | system `strnlen` |
| `cplus_strcpy` | **optional compatibility helper -> libc** | system `snprintf` bounded copy + truncation abort |
| `cplus_mutex_*`, `cplus_tls_*` | **platform primitive** | thin wrappers over pthread APIs |

The four memory/string helpers are not compiler code-generation requirements;
no lowering emits them. They remain only for source compatibility and are
explicitly documented as not providing spatial safety. Real bounds enforcement
is AST/CFG analysis plus Clang UBSan instrumentation.

### Unsupported guarantees (explicit)

- No complete use-after-free detection (ASan/TSan broken on this host).
- No Rust-style provenance for arbitrary casts/pointer arithmetic.
- No whole-program cross-TU ABI/ownership verification.
- `goto`/`longjmp` bypass deterministic cleanup.
- The libc replacement wrappers do not validate buffer sizes.

## Compiler performance evidence (`tests/core/perf.sh`)

Measured on macOS arm64, Apple clang 16, same tiny C source:

| configuration | compile time | peak compiler RSS | executable size |
| --- | --- | --- | --- |
| c+ safe | 0.32 s | 38 MB | 33,736 B |
| c+ legacy | 0.10 s | 28 MB | 33,744 B |
| clang -O2 | 0.02 s | 28 MB | 33,432 B |

The safe-mode driver currently costs ~0.3 s mostly because it constructs a
second Clang AST for safety checks on every translation unit. This is the
primary measured compile-time bottleneck, not the lowering stages. No checks
were removed to improve these numbers. Optimisation work remains open.

## Multi-TU status

Issue #4 duplicate symbols from `.hp` class members are fixed by emitting
header-lowered class members as `static inline` (`headerMode` in
`CPlusRewriter.cpp`), adapting the proposal in Agent 2's PR #17. Regression:
`tests/core/multi_tu_hp/` (two `.cp` TUs including the same `.hp`, class
constructor/method/destructor, clean link and exact-once cleanup). The same
fix also eliminates the need for separate-token `-I dir` parsing, which was
fixed in `compiler/Main.cpp`.

## Compiler contracts for ordinary libc / C+ Safe CRT (pillar 1)

Agent 1 owns compiler recognition and the minimal runtime contract; Agent 2
owns the public Safe CRT library surface (issue #19). These are **compiler
checks**, not a vendored libc.

| existing C API | recognised pattern in safe mode | guarantee | unknown cases |
| --- | --- | --- | --- |
| `malloc(n)`, `calloc(n,s)` | tracked variable receiving fresh allocation | later `free`/`realloc` of same variable checked; overflow for `calloc` delegated to libc where available | alias escapes, unknown n, custom allocators |
| `realloc(p,n)` | `p = realloc(p,n)` marks old pointer invalid | repeated release of stale `p` rejected | failed realloc leaving `p` valid is not distinguished |
| `free(p)`, `cplus_release(p)` | same variable released twice without reset to NULL | compile-time double-free error | aliases, heap reuse, freed-through-other-pointer |
| `memcpy(dst,src,n)`, `memmove` | direct fixed array destination + constant byte count | reject count exceeding destination; FORTIFY builtins recognized | pointer destinations, computed n, unknown source extent |
| `strcpy(dst,src)` | direct fixed array destination + string literal | reject literal+NUL exceeding destination | runtime strings, pointer destinations |
| `fopen`/`fclose` | ownership by variable | basic double-close warning path via RAII | FILE* aliases and multi-TU escapes |

FORTIFY rewrites `memcpy`/`strcpy` to `__builtin___*_chk`; the AST visitor
maps those back to the original call names. Assignment of NULL after release
resets the double-release state, which is what generated RAII cleanup emits.

### Final scope (PR comment 7063159395)

There are **two** library workstreams, not three. Pillar 1 is not a new CRT
API. `malloc_s`/`free_s` are **not deliverables**. Ordinary C APIs keep their
names, native ABI and valid behaviour; C+ improves their use with compiler
analysis only. Agent 2 owns the single modern C+ library (`stdlib/**`, logging,
JSON, HTTP, GUI, audio, etc.) under issue #3.

Compiler responsibilities retained:
- AST/CFG/dataflow ownership, alias and invalidation tracking for ordinary
  `malloc`/`calloc`/`realloc`/`free` and memory/string/I/O calls;
- object-size/bounds checks for direct fixed arrays and FORTIFY builtins;
- no source rewrite of valid C, no changed `free(NULL)`, `realloc` failure,
  overlap, EOF or errno semantics;
- minimal compiler-required runtime metadata only; no libc replacement;
- `.hp` linkage and multi-TU safety coordinated with Agent 2.

## Remaining gaps for issues #1/#4/#19

- no automatic use-after-free detection for raw C pointers;
- no full provenance/cast verification;
- no complete cross-TU ownership ABI checking;
- no whole-program data-race proof;
- `goto`/`longjmp` bypass cleanup.

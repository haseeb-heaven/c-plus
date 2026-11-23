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
for the runtime library. Current result: **320/395 lines (81%)** including the opt-in quarantine path, covering
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

- No complete use-after-free detection (Apple clang 16 ASan/TSan broken; opt-in with a working backend on this host).
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

- no automatic use-after-free detection for raw C pointers; opt-in
  `CPLUS_QUARANTINE=1` delays frees in a bounded ring so stale managed aliases
  after address reuse are detected (exit 105) at the cost of delayed reclamation;
- no full provenance/cast verification;
- no complete cross-TU ownership ABI checking;
- no whole-program data-race proof;
- `goto`/`longjmp` bypass cleanup.

## libc contract matrix evidence

`tests/core/libc_matrix/run.sh` compiles and runs ordinary C snippets in safe
mode. Latest outcomes: `valid_malloc` runs cleanly; `strcpy_literal` and
`memcpy_overlap` are rejected at compile time; `malloc_zero`, `realloc_zero`
and `p = realloc(p,n)` are rejected by the declared safe-subset contract but
compile unchanged in `--legacy-c`; raw alias double `free` is **not** statically
detected and aborts through libc/UBSan, while managed `cplus_release` detects
it deterministically. `tests/core/quarantine/run.sh` documents that the opt-in
quarantine only delays reuse: it makes `cplus_check_live` reliable for stale
managed aliases, does not instrument ordinary dereferences, and its 1024-slot
ring can evict entries (after which reuse may mask a stale alias).

These are explicitly not claims of universal temporal safety.

## Function-level existing-libc safety matrix (evidence, not coverage claim)

| C API | C+ compiler recognition | runtime/native enforcement | safe-subset status | remaining bypasses |
| --- | --- | --- | --- | --- |
| `malloc(n)` | allocation effect; size expression recorded where constant | libc malloc; C+ metadata only for `cplus_alloc` family | valid C unchanged | raw alias escapes |
| `calloc(n,s)` | allocation effect; overflow delegated to libc | native calloc | valid C unchanged | unknown provenance |
| `realloc(p,n)` | effect summary marks `frees(arg)` only on success (conservative); token checker rejects `p = realloc(p,n)` in safe mode | native realloc semantics preserved | safe-mode restriction documented | success/failure branch not fully tracked |
| `free(p)` / `cplus_release(p)` | same-variable double release rejected; interprocedural `frees(arg)` summary for user functions | deterministic 104/105 for managed allocations; libc for raw free | managed safe subset | raw alias double free not statically proven |
| `memcpy` / `memmove` | constant count vs fixed array destination; FORTIFY builtins recognized | native memmove/memcpy + UBSan | valid C unchanged; unsafe counts rejected | pointer destinations, dynamic lengths, unknown source extent |
| `strcpy` / `strcat` | literal vs fixed destination capacity | native libc + UBSan | literal overflow rejected | runtime strings |
| `strlen` | read-only borrow effect only | native libc | valid C unchanged | none claimed beyond Clang checks |
| `fopen`/`fclose` | owned handle effect summary | native FILE* + RAII wrappers | valid C unchanged | FILE* aliases across TUs |
| `fread`/`fwrite` | writes/count effect; ignored count warning only for bare call | native stdio, EOF/errno unchanged | valid C unchanged | partial I/O semantics remain libc-owned |

This is a **per-function status table**, not a claim like “81% of libc is safe”.
The 81% figure earlier in this document referred only to C+ runtime line
coverage. See `docs/CAPABILITIES.md` for the probed native toolchain features.

## Interprocedural summary prototype

`compiler/Analysis/OwnershipAnalysis.cpp` exposes a small effect table
(`allocates`, `frees(arg)`, `borrows`, `writes`, `returnsOwned`) for known libc
calls. `AstSafetyCheck.cpp` collects the same shape for simple user functions
that call `free(parameter)` and propagates it to call sites. Tests:
`interproc_double_free` (rejected) and `interproc_borrow` (accepted).
Unknown functions are not assumed safe.

## Coordination with Agent 2 (shared P0 issue #19)

Agent 2 owns platform/CRT conformance fixtures under `tests/crt/**` and the
`docs/CRT_MATRIX.md` verification matrix. Agent 1 owns compiler recognition,
effect summaries, diagnostics and minimal runtime contracts. Compiler-owned
diagnostics consumed by those fixtures:

- `double free of <var>`
- `copy of N bytes exceeds destination <var>`
- `string copy of N chars plus NUL exceeds destination <var>`
- `array index N exceeds length M`
- `null pointer dereference`
- safe-subset restrictions: `raw realloc...`, `zero-size allocation...`

Known limitations reported by Agent 2 and verified here: expression-sized
arrays resolve correctly via Clang AST (regression added); `calloc(SIZE_MAX,k)`
can be folded by Clang -O2, so allocation-failure fixtures need
`noinline`/volatile designs. Neither agent assumes unknown functions are safe.

## Compile-time temporal safety (safe mode)

The Clang AST visitor now tracks straight-line release state and simple
pointer aliases:
- `free(p)` / `cplus_release(p)` followed by `*p` or `p->field` is rejected as
  `use-after-free`;
- `T *q = p` or `q = p` joins the same obligation group, so releasing `p` marks
  `q` released too and `*q` is rejected;
- assignment of NULL resets the group state (this is what generated RAII
  emits), and address-taking / comparison with NULL clears provably-null state;
- frees inside conditionals/loops do **not** propagate release state, so valid
  code like `if (argc > 1) free(p); return *p;` is not falsely rejected.

This is a sound subset, not Rust-style borrow checking: it does not handle
heap aliases through containers, calls that free through unknown pointers,
or paths where a branch frees and the pointer is not reset. Those remain
runtime/UBSan or unsupported cases. Tests: `uaf_direct`, `uaf_alias`,
`uaf_conditional`, `double_release_alias`, `interproc_double_free`.

## Safe subset and comparison baseline

`docs/SAFE_SUBSET.md` defines the exact accepted/rejected/unknown operations.
`tests/core/perf_compare.sh` compares a 20M-iteration integer sum compiled with
clang -O2, C+ legacy -O2, C+ safe -O2 and rustc -O. Latest local run:

```text
clang_o2       real 1.15s
cplus_legacy   real 0.33s
cplus_safe     real 0.44s
rust_o         real 0.45s
```

The clang number is anomalous relative to earlier benches and needs repeated
runs on a quiet machine; no performance victory is claimed. Safe C+ carries
UBSan overhead, similar to Rust's debug-like check cost in this narrow probe.

## P0 heap-bounds and alias pass (Agent 1)

The compiler-side extent tables now propagate through pointer copies, explicit
casts, conflicting call sites (fail-closed in STRICT) and heap factories
(`return malloc(N)` / `int *p=malloc(N); return p;`). `heapStateOf` no longer
loses provenance at an explicit cast. Regression evidence is
`tests/compiler/agent1/run_bounds_matrix.sh` (13/13 PASS), covering the
mandatory `malloc(1)` 10-element write, alias/cast/conflict/factory cases and
three accepted safe controls.

P0 temporal safety closes four previously escaping single-TU UAF shapes via
cast-transparent aliases, `returnedParam_` alias transfer, a released-pointer
call-boundary rule and a conservative runtime-index element rule. Evidence:
`tests/metrics/uaf_evidence_run.py` 12/12 (10 static rejects). This is not a
general temporal-safety guarantee; CFG-based ownership and symbol/region-keyed
state remain the next foundation work.

## Evidence-based progress audit (branch `agent/core-safety`, HEAD `9df3507`)

This section records an audit of executable code and machine-generated result
files, not documentation percentages. Branch diff vs `develop`:
3,542 files changed; compiler/runtime source changes are concentrated in
`AstSafetyCheck.cpp` (+6,681), `StaticBoundsCheck.cpp` (+2,667),
`CPlusRewriter.cpp` (+1,964), `RaiiLowering.cpp` (+1,382),
`OwnershipAnalysis.cpp` (+1,222), `Driver.cpp` (+989) and
`runtime/cplus_runtime.c` (+796).

### Feature inventory (explicit denominator: 22 audited features)

Counted from source + artifacts: **7 IMPLEMENTED AND VERIFIED**,
**11 IMPLEMENTED BUT PARTIALLY VERIFIED**, **3 PARTIALLY IMPLEMENTED**,
**1 BLOCKED** (provenance under allocator address reuse, F2), **0 NOT
IMPLEMENTED** in the audited set.

| Feature | Classification | Evidence |
| --- | --- | --- |
| Static bounds checking | IMPLEMENTED BUT PARTIALLY VERIFIED | bounds matrix 13/13; native matrix 24/24; 6 interprocedural OOB cases still accepted |
| Array decay | IMPLEMENTED BUT PARTIALLY VERIFIED | `a_array_decay_bounds` 4/4; `array_decay` 2/2; lost through `int **` and struct fields |
| Pointer arithmetic | IMPLEMENTED BUT PARTIALLY VERIFIED | `f_pointer_arithmetic_beyond_array` 4/4; cross-object cases BLOCKED |
| Pointer provenance | BLOCKED (F2) | `cplus.h:259-271`; `tests/known_gaps/allocator_reuse.c`; needs ABI token |
| Nullability | IMPLEMENTED AND VERIFIED | interprocedural 6/6; `memory_safety_compare` 14/14; `S4_null_wrapper` closed |
| Uninitialized variables | IMPLEMENTED BUT PARTIALLY VERIFIED | Clang `runUninitializedVariablesAnalysis` linked at `OwnershipAnalysis.cpp:541`; 90/90 safe corpus; some fixtures BLOCKED |
| Integer overflow | IMPLEMENTED BUT PARTIALLY VERIFIED | native matrix `b_runtime_signed_overflow` rc 110/134; 3 check-mode gaps |
| Division by zero | IMPLEMENTED AND VERIFIED | native matrix `c_runtime_div_mod_zero` 4/4; `A1_stale_nonzero` closed |
| Invalid shifts | IMPLEMENTED AND VERIFIED | native matrix `d_invalid_shift_count` 4/4 |
| Narrowing/conversions | IMPLEMENTED BUT PARTIALLY VERIFIED | native matrix `e_narrowing_conversion` 4/4; 2 check-mode gaps |
| Ownership | IMPLEMENTED AND VERIFIED | 10/10 closed adversarial cases; double-free controls rejected |
| Lifetimes | IMPLEMENTED BUT PARTIALLY VERIFIED | UAF evidence 12/12; runtime memory 28/28; F2 unsound at reuse |
| Use-after-move | IMPLEMENTED AND VERIFIED | `cf_use_after_move`; exact "moved-from" needles in core |
| Escape analysis | IMPLEMENTED AND VERIFIED | `cf_borrow_escape`, `cf_managed_escape`; managed return rejected |
| Interprocedural analysis | PARTIALLY IMPLEMENTED | 55/61 cases pass; 6 soundness gaps accepted |
| Function pointer safety | IMPLEMENTED BUT PARTIALLY VERIFIED | fnptr double-free cases closed; `fnptr_bounds` BLOCKED |
| Struct/union safety | IMPLEMENTED BUT PARTIALLY VERIFIED | ABI 9/9+6/6; `struct_ptr_oob` accepted |
| Control-flow analysis | IMPLEMENTED BUT PARTIALLY VERIFIED | `dataflow` 4/6; `loop_while_oob`, `dataflow_phi_oob` accepted |
| Cleanup analysis | IMPLEMENTED AND VERIFIED | optimizer 66 builds/66 runs/174 guard checks, 0 differences |
| Goto handling | IMPLEMENTED BUT PARTIALLY VERIFIED | plain goto control passes; forward-goto-over-owner has no counted negative artifact |
| Recursion | PARTIALLY IMPLEMENTED | literal-argument recursion closed; `call_recursive_oob`/`call_mutual_oob` accepted |
| Aliasing | IMPLEMENTED BUT PARTIALLY VERIFIED | `interior_pointers` 2/2; `ptr2_oob`, `struct_ptr_oob` accepted |

### Testing inventory (not blended)

Test fixtures discovered: ~840 authored `.c`/`.cp` + runner scripts; executed
suites and counts at HEAD: repo 109 pass/0 fail, core 152/0, agent1 156 pass/
0 fail/3 skip, CTest 2/2, native matrix 24/24, bounds matrix 13/13,
runtime memory 28/28, ABI 33/33, ABI real-world 36/36, differential 125/0
mismatches, compatibility 21/21, optimizer 11/11, upstream 20/20, fuzz 200/0,
metrics 90/90 accepted with 0 false positives and STRICT 97/107 detection,
UAF evidence 12/12, source preservation 4/4, real-world C 6/6 and wave3
175 pass/5 mode-rejected/0 hard failures, C standards 48 pass/0 fail/8
unsupported, memory-safety compare 14/14. Timeouts: 0 recorded.
`agent1/results_extra` has 71 BLOCKED fixtures — inventory, not passes.
Sanitizer-enabled runs: 28 supplementary ASan checks, 6 UBSan native-matrix
builds, 3 agent1 sanitizer configs; sanitizer-disabled runs dominate.

### Most serious verified gaps still open

1. Six interprocedural spatial cases accepted in DEFAULT and STRICT
   (`call_recursive_oob`, `call_mutual_oob`, `ptr2_oob`, `struct_ptr_oob`,
   `loop_while_oob`, `dataflow_phi_oob`).
2. F2 provenance under address reuse — ABI-bound.
3. Three `check`-mode arithmetic/conversion cases (compound, implicit narrowing,
   float→int); `build` traps at 110/111/111.

## Completion gate (2026-11-24, HEAD `agent/core-safety`)

Final measured state after Phase 4–6 implementation. All commands were run on
this host with `CPLUS_BIN=$PWD/build/c+` and `CPLUS_CLANG` unset unless a
config explicitly selected the Homebrew clang for sanitizer runs.

**Implemented fixes**
- Interprocedural spatial: the six previously accepted OOB cases
  (`call_recursive_oob`, `call_mutual_oob`, `ptr2_oob`, `struct_ptr_oob`,
  `loop_while_oob`, `dataflow_phi_oob`) are now rejected with the exact
  `[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS]` tag. `interprocedural_safety`:
  **61 cases, 183/183 mode checks, 24/24 runtime**.
- Array decay across call boundaries: Checked-C-style argument-slot extents
  (pointer-to-pointer and struct pointer+count fields) are concretized at call
  sites and resolved by parameter slot, so extents survive casts and aliases.
- Runtime null-deref checks: `cplus_check_nonnull` (declared in `cplus.h` but
  previously never emitted) is now inserted at `*p`, `p->f` and genuine pointer
  `p[i]` sites with single evaluation and obvious-guard suppression. Matrix
  null-deref now traps rc 102 with a C+ diagnostic in DEBUG/RELEASE.
- STRICT build now fails closed when the safety AST cannot be built (previously
  only `check` did).
- STRICT false positives fixed: fresh assignment clears release state
  (`f = fopen(); fclose(f)`), and explicit unsigned casts (`(uint8_t)300u`)
  are accepted because C defines modulo wrap.
- RAII regressions pinned by four new core fixtures (cast drop, conditional
  drop, conditional defer, switch-continue).

**Final gate (all green)**
```
repo tests        109 passed / 0 failed
core safety       158 passed / 0 failed
agent1            159 total, 156 PASS / 0 FAIL / 3 SKIP
CTest             2/2
interprocedural   183 mode checks passed / 0 failed
safety matrix     168 checks: 154 PASS, 4 WARN_ONLY, 6 KNOWN_OPEN,
                  4 STATIC_ONLY_BY_DESIGN, 0 FAIL, 0 timeouts
full coverage     220 fixtures executed / 220 discovered, 43 PASS, 0 FAIL,
                  177 OBSERVED (explicitly listed), 0 toolchain unavailable
metrics           90/90 safe corpus accepted, 0 false positives,
                  STRICT detection 98/108
runtime memory    28/28; UAF evidence 12/12; ABI 33/33; ABI real-world 36/36
differential      125 comparisons, 0 mismatches; compatibility 21/21
optimizer         11 cases, 0 failures; upstream ports 20/20
memory compare    14/14; fuzz 200 cases, 0 mismatches
source preserve   4/4; real-world C 6/6; real-world wave3 175 PASS / 0 hard fail
C standards       48 pass / 0 fail / 8 unsupported (c23)
benchmarks        35 measurements, 0 failures
```

**Remaining, explicitly not claimed as complete**
- ABI-bound: F2 (stale free at a reused address) and unmanaged temporal safety
  need a pointer-borne provenance token; declined to preserve ABI.
- `check` mode accepts three runtime-value arithmetic/conversion cases whose
  `build` counterparts trap (110/111/111); statically proving them would
  create false positives.
- WARN_ONLY / KNOWN_OPEN entries in the safety matrix are documented, recorded
  with exact diagnostics, and are not silently skipped.
- No general race/provenance/C23/UB-free guarantee. No overall safety
  percentage is claimed: the 22-feature inventory above uses explicit counts.

# Limitations and honest status

C+ is a small, working feature layer over C, not a complete new language
implementation. This page states what is actually implemented, what is
partially implemented, and what is explicitly out of scope. Where a guarantee
depends on a runtime check, the check is named.

## Capability table

| capability | status | notes |
| --- | --- | --- |
| Compile ordinary C `.c` files | supported | passthrough to the host C compiler; no source changes needed |
| Native executables, C ABI / libc / POSIX interop | supported | normal `-o` linking, `-include cplus/cplus.h` only |
| `resource struct` with `T_drop(T *)` | supported | type must be declared in the same translation unit |
| Cleanup at scope exit, reverse declaration order | supported | inserted as plain C calls, inspectable with `--emit-c` |
| Cleanup before `return`, `break`, `continue` | supported | statements wrapped in `{ }` to preserve `if`/`else` binding |
| Explicit `cplus_move` and automatic move on return | supported | source is zeroed; destructor must tolerate that |
| Compile-time copy and assignment rejection | supported | for owning locals tracked by the lowering pass |
| Borrow escape rejection | supported (subset) | returning a pointer borrowed from a local resource/managed allocation is an error |
| Use-after-move rejection | supported (subset) | using a tracked borrow after `cplus_move(owner)` is an error |
| Full Rust-style borrow checker | **not implemented** | no lifetime annotations, aliasing/exclusivity rules or container-aware borrows |
| Compile-time managed-pointer escape rejection | supported | `return p;` where `p` is managed is an error |
| Managed allocation with double/invalid release detection | supported | O(1) hash table + bounded freed-address cache |
| Statically known out-of-bounds array index | supported at compile time | `-Werror=array-bounds` plus a C+ static pass; no executable is produced |
| Simple counted-loop index past array end | supported at compile time | `for (i = 0; i < N; i++) arr[i]` with known extent < N |
| Allocation size from literal call argument | supported at compile time | `sizeof(T) * param` with a literal argument; extent also follows function returns to call sites |
| Return-propagated allocation extents | supported | single returned local; ambiguous or multiple differing call sites are skipped |
| Fixed array `.len` | supported | compile-time literal extent |
| C+ allocation `.len` | supported | `(size_expr) / sizeof(*ptr)` when the allocation is visible (compile-time folded for fixed sizes); side-table fallback otherwise |
| `.len` on a struct type | not automatic | a struct must declare its own `len` field; `.len` on a pointer from `cplus_new(T)` reports 1 object |
| `auto` type inference | supported | lowers to `__typeof__`; also works with `(Class){...}`; not used for ownership inference |
| `.cp` / `.hp` extensions | supported | `.c`/`.h` still accepted; `.hp` lowered on include |
| Format-free `print(...)` | supported | type tags via `_Generic`; scalars, bool, char and C strings only |
| Format-free `scan(...)` | supported | lvalues, bounded string reads, fatal on conversion failure |
| Default function arguments | supported | same translation unit only; filled at call sites; defaults should be trailing |
| Range-based `for` | supported | simple lvalue ranges with a visible `.len`; no side-effecting range expressions |
| Dynamic out-of-bounds index, null/misaligned dereference | supported at run time | UBSan checks, abort on first failure |
| Deterministic exit codes for safety failures | supported | `101`–`108` in `runtime/include/cplus/cplus.h` |
| Lightweight classes with methods | supported | inline methods only; lower to `T_m(T *self, ...)` |
| `p.m()` / `p->m()` sugar | supported | resolved from simple `T p;` / `T *p;` declarations in the same TU |
| Pthreads + RAII mutex guard | supported | primitives and deterministic unlock; see race note below |
| `--legacy-c` compatibility mode | supported | no safety checks; **not** memory safe |
| Use-after-free detection | **not implemented** | ASan unavailable on this host; see below |
| Static verification that shared data is locked | **not implemented** | concurrency is primitive-based, not verified |
| `goto` / `longjmp` cleanup | warning only | non-local exits bypass inserted destructors |
| Constructors as class methods | supported | `T(...)` → `T_ctor`, no return type, no early return |
| Destructors as class methods | supported | `void drop(void)` → `T_drop`; makes the class an owning type |
| Inheritance, interfaces, virtual dispatch, access control | **not supported** | classes are structs + functions |
| Full type inference for method calls | **not supported** | receiver class must be visible as a simple declaration |
| Leak detection for borrowed pointers | **not supported** | borrowing is a raw pointer; no alias tracking |
| Cross-translation-unit resource types | **not supported** | declare the resource in every TU that owns it |
| Full C++-style ownership system | **not supported** | the model covers locals only, not containers or fields |

## Classic C bugs: what C+ actually catches

| bug | status in safe C+ | mechanism |
| --- | --- | --- |
| 1. Buffer overflow / bad index | caught | compile-time for constants and simple loops; UBSan `bounds` at run time |
| 2. Use-after-free | **not caught** for raw `malloc`/`free`; no ASan on this host | C+ managed allocations detect double/invalid release, not stale dereferences |
| 3. Returning a pointer to a local | caught | `-Werror=return-stack-address` |
| 4. Memory leak on early return | prevented for managed/RAII allocations; not detected for raw `malloc` | scope-exit cleanup |
| 5. Double free | caught for C+ allocations; not for raw `free` | allocator side table + freed-address cache |
| 6. Null pointer dereference | caught at compile time when provable | Clang AST; UBSan backstop for dynamic cases |
| 7. Uninitialized variable read | caught | `-Werror=uninitialized` |
| 8. Missing null terminator | **not caught** | use bounded string APIs and reserve `strlen + 1` bytes yourself |
| 9. Signed integer overflow | caught at run time | UBSan `undefined` |
| 10. Pointer reassignment leak | **not detected** | token liveness heuristic was unsound and removed |
| 11. `realloc` failure overwrite | caught | raw `realloc` rejected; use `cplus_realloc` |
| 12. Strict aliasing violation | caught | type-punned dereference diagnostic |
| 13. Modifying a string literal | warning only | qualifier error broke valid legacy C; see `tests/known_gaps/` |
| 14. File descriptor leak on error path | prevented with RAII resources | deterministic `T_drop` |
| 15. `sizeof` on decayed array parameter | caught | `-Werror=sizeof-array-argument` / `sizeof-pointer-div` |
| 16. Format string vulnerability | caught | `-Werror=format-security` |
| 17. Macro side-effect double evaluation | **not caught** | use an inline function; see `tests/known_gaps/macro_double_eval.c` |
| 18. Unsequenced modification | caught | `-Werror=unsequenced` |
| 19. Shift count / negative shift | caught (count) | `-Werror=shift-count-overflow`; negative runtime shifts use UBSan |
| 20. Signed-to-unsigned comparison | warning only | needs real type info; see `tests/known_gaps/` |
| 21. Unaligned pointer dereference | caught at run time | UBSan `alignment` |
| 22. Overlapping memcpy | caught | static check recommending `memmove` |
| 23. Flexible array member copy | **not caught** | see `tests/known_gaps/flexible_array_copy.c`; copy whole allocation |
| 24. Data race (direct global write) | caught (conservative subset) | static check for unsynchronized writes in thread entry functions |
| 25. Division by zero / INT_MIN/-1 | caught at run time | UBSan `undefined` / `division-by-zero` |
| 26. Pointer arithmetic past one-past-end | caught | static extent check |
| 27. Relational compare of unrelated pointers | caught | static check for `&a < &b` |
| 28. `va_arg` promoted-type mismatch | caught | `-Werror=varargs` |
| 29. Static-buffer `strtok` state | caught | static check for repeated stateful calls |
| 30. Unsafe signal-handler call | caught | static check against a list of non-async-signal-safe calls |
| 31. Plain `char` into `<ctype.h>` | caught | static check recommending `(unsigned char)` |
| 32. Unchecked `fread`/`fwrite` | caught | static check for ignored return value |
| 33. Empty infinite loop | caught | static check; add a volatile/atomic side effect |
| 34. `longjmp` bypassing cleanup | warning | valid C compiles; RAII is not guaranteed across non-local jumps |
| 35. Cross-TU declaration/ABI mismatch | **not detected** | single-TU compiler; see `tests/known_gaps/abi_mismatch/` |
| 36. Casting away const to mutate | caught | `-Werror=cast-qual` |
| 37. NaN equality comparison | caught | static check recommending `isnan()` |
| 38. `sizeof` evaluating a VLA side effect | caught | static check |
| 39. Multi-statement macro without do/while | caught | static check on `#define` lines |
| 40. Zero-size malloc/realloc | caught | static check; use `cplus_alloc` |
| 41. Missing volatile signal flag | caught | static check recommending `volatile sig_atomic_t` |
| 42. C23 `realloc(ptr, 0)` UB | caught | raw `realloc` rejected; zero-size allocation rejected |
| 43. C23 `unreachable()` cascade | caught (rejected) | static check |
| 44. Lying `[[unsequenced]]`/`[[reproducible]]` | caught (rejected) | static check |
| 45. C23 empty parameter list change | avoided by design | C+ targets gnu17; explicit `(void)` recommended |
| 46. C23 `auto` decay/qualifier loss | safer than C23 | `__typeof__` keeps const; arrays decay via pointer type |
| 47. C23 `nullptr` mismatch | caught (rejected) | static check; use `NULL` |
| 48. `_BitInt` promotion/overflow | caught (rejected) | static check; use `int64_t` |
| 49. `fork()` after threads | caught | static check recommending `posix_spawn` |
| 50. `restrict` aliasing violation | caught | static check for overlapping call arguments |
| 51. Bit-field layout/signedness | valid C | implementation-defined but not rejected; see `tests/known_gaps/` |
| 52. `fflush(stdin)` | caught | static check |
| 53. `alloca`/unbounded VLA | caught | static check; use `cplus_alloc` |
| 54. Control flow out of `defer` | caught | `defer` cleanup rejects return/break/continue/goto |

All 54 supplied bugs have named tests in `tests/` (`classic_bug1_*` through
`classic_bug54_*`). The ones that are not automatically caught also have raw
reference programs in `tests/known_gaps/` documenting the limitation.

Items marked "not caught" remain undefined behaviour in safe C+ when written
with raw C idioms. Use C+ managed allocations and resources, not bare
`malloc`/`free`, for the guarantees in rows 2, 4 and 5.

## Memory safety: what the guarantee means

Safe mode compiles with:

```text
-Werror=array-bounds
-fsanitize=undefined,bounds,null,alignment -fno-sanitize-recover=all
```

`-Werror=array-bounds` makes a provably out-of-bounds constant subscript a
compile error (as in your `arr[10]` example). This is reused from the existing
C compiler, not a separate C+ type system.

This gives abort-on-violation checks for fixed-extent array subscripts, null
and misaligned pointer use, and the undefined-behaviour checks that ship in
UBSan's `undefined` group (including signed integer overflow). It does **not**
give a complete spatial/temporal memory-safety guarantee:

- **Use-after-free detection requires a working ASan backend.** Apple clang
  16's ASan aborts during initialisation on this host
  (`AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189`), so the
  default build does not enable it. `--sanitize=address` (or
  `address,undefined`) is available with a working backend such as Homebrew
  clang 23 plus an explicit `-isysroot`; see `docs/CAPABILITIES.md`. The
  sanitizer suite verifies the diagnostics on that backend, but the default
  build still makes no UAF-detection claim.
- A stale managed pointer whose address has already been reused can be
  misattributed to the new allocation. The bounded cache catches the common
  immediate double-release case (tested), not every stale-pointer case.
- `*p` and `p->field` through a raw pointer are ordinary C operations. The
  null/alignment checks catch the obvious invalid cases, not dangling ones.
- Out-of-bounds pointers produced by arithmetic (`p + i`) are not statically
  checked the way an array subscript is; this is not Rust-style borrow
  checking.
- Return-extent propagation only handles a single returned local with one
  consistent extent. Aliases and multiple return paths with differing extents
  are not inferred.
- Allocation-size propagation only handles `sizeof(T) * parameter` with a
  literal call argument and one consistent value per parameter name. If the
  function is called with different sizes, the check is skipped rather than
  guessing.
- The static loop checker handles counted loops of the form
  `i = C; i < N; i++` (or `<=`) with literal bounds. Loops with computed
  bounds, multiple index variables or indirect subscripts fall back to the
  run-time check, not a compile error.
- Bug 35 (cross-TU ABI mismatch) needs whole-program type information that a
  single-TU lowering compiler does not have.
- Bugs 23 (flexible array member copy) and general data races beyond direct
  global writes remain outside the enforced subset.
- `.hp` headers are lowered per translation unit; quoted includes are resolved
  relative to the including file or the usual include directories. There is
  no module system or precompiled-header support yet.
- Default arguments are a source-level rewriting feature. A function defined
  in another translation unit must be called with all arguments.
- Range-based `for` needs a simple lvalue whose `.len` is known. It does not
  support iterators, generator expressions or modifying the range during
  iteration.
- `print` does not reflect arbitrary structs/classes: print fields or return a
  scalar from a method. `scan` supports scalars and C strings, not arrays of
  numbers or structs; a `char *` destination gets a fixed 256-byte bound
  because a pointer carries no capacity.
- The printer's line state is global and not thread-safe. Interleaved
  `print` calls from different threads may mix values on one line.
- The borrow checker only tracks `T *p = &owner;` and `T *p = managed_owner;`
  declarations in the same translation unit. It does not track borrows stored
  in structs, arrays or function arguments, and it does not enforce aliasing
  or exclusivity rules. Borrowing a pointer that was not created by C+ is not
  checked.
- `auto` is type inference only. It does not infer ownership, so an owning
  resource declared with `auto` will not get deterministic cleanup.
- `.len` on a C+ allocation needs the declaration's allocator call to be
  visible in the same translation unit and a complete element type. It is a
  hash lookup at run time; calling it in a tight loop has a real cost.
- There is no generic reflection for structs: a struct's own `len`/`size`
  fields must be written by the programmer.
- Arbitrary pointer arithmetic and casts remain C behaviour.

`--legacy-c` turns all of the above checks off. It is provided for
compatibility experiments, not for safe code.

## Concurrency

The runtime supplies mutex primitives, and a mutex/guard pair written as
resources gets deterministic unlock on scope exit. What is **not** provided is
any static or dynamic data-race verification: C+ will not tell you that a
shared variable was mutated without a lock. Threads, atomics and locks are
still the underlying C/POSIX facilities.

## Control flow

`return`, `break` and `continue` cleanup is inserted and tested. `goto`,
`longjmp` and `setjmp` are outside the supported subset: a `goto` produces a
warning, and a non-local jump bypasses inserted destructors. `exit()` also
bypasses them, as in C.

## Language surface

The four C+ constructs are recognised by a token-level rewriter. It is a
generic rewriter over source text, not a table of example transformations,
but it is also not a full C parser:

- a resource type must be visible in the translation unit where it is owned;
- a class must be declared before a method call using sugar;
- only one owning resource per declaration statement is allowed;
- the destructor `T_drop` must have external linkage and tolerate a zeroed
  handle;
- `resource`, `class`, `managed`, `owned` and `self` are reserved in C+ mode.

`LangFeatures.h` still defines names for planned keywords/attributes
(`move`, `borrow`, `cplus_trusted`, `cplus_no_cleanup`) that are **not**
implemented; do not rely on them.

## Performance measurements

Measured with `./benchmarks/run_bench.sh`, median of 5 runs, on this machine
(arm64 macOS, Apple clang 16), lower is better:

| benchmark | c+ safe | c+ legacy | clang -O2 | safe / clang | legacy / clang |
| --- | --- | --- | --- | --- | --- |
| `alloc_churn` | 0.1877s | 0.1834s | 0.0532s | 3.53x | 3.45x |
| `bounds_sum` | 0.0327s | 0.0303s | 0.0312s | 1.05x | 0.97x |
| `function_call` | 0.1222s | 0.1176s | 0.1168s | 1.05x | 1.01x |
| `integer_compute` | 0.0958s | 0.0942s | 0.0940s | 1.02x | 1.00x |
| `method_dispatch` | 0.0189s | 0.0081s | 0.0051s | 3.75x | 1.60x |
| `parallel_scale` | 0.0157s | 0.0146s | 0.0141s | 1.11x | 1.03x |
| `pointer_chase` | 0.0352s | 0.0535s | 0.0528s | 0.67x | 1.01x |
| `raii_loop` | 0.0189s | 0.0105s | 0.0084s | 2.26x | 1.25x |

How to read these numbers:

- The **legacy** column uses the same C+ lowering with checks disabled. Its
  near-1.00x results show that class-method lowering and destructor insertion
  are essentially free once the C compiler optimises the emitted C.
- The **safe** column is slower on tight loops. Most of that is UBSan's
  instrumentation (notably signed-integer-overflow checks in the `undefined`
  group), not the language features. This is the price of abort-on-violation
  checks.
- `alloc_churn` measures the managed allocator, which performs extra hash-table
  bookkeeping per allocation. It is genuinely about 3.4x slower than raw
  `malloc`/`free` in this microbenchmark.
- Timings vary between runs on a shared machine. Rerun the suite rather than
  treating these numbers as universal.

## Build environment notes

- The supported linker path is the system Clang driver; `CPLUS_CLANG` can
  override it. Homebrew LLVM's `clang` binary in this environment fails at
  runtime with a missing `@rpath/libLLVM.23.1.dylib`, so it is not selected by
  default.
- LLVM/Clang development packages are needed to build the driver itself; they
  are not needed to run programs produced by it.
- Some skeleton files from the initial repository (`ScopeAnalyzer.*`,
  `RaiiAnalyzer.h`, placeholder sources) are not part of the active pipeline.

## Roadmap (not promised)

- use-after-free detection with a working sanitizer backend;
- typed borrows that can be checked statically;
- data-race detection or a verified lock discipline;
- constructors and destructors as first-class class members;
- multi-TU resource type registration.

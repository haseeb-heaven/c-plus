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
| Allocation size from literal call argument | supported at compile time | `sizeof(T) * param` with a literal argument; ambiguous or multiple differing call sites are skipped |
| Fixed array `.len` | supported | compile-time literal extent |
| C+ allocation `.len` | supported | `(size_expr) / sizeof(*ptr)` when the allocation is visible (compile-time folded for fixed sizes); side-table fallback otherwise |
| `.len` on a struct type | not automatic | a struct must declare its own `len` field; `.len` on a pointer from `cplus_new(T)` reports 1 object |
| `auto` type inference | supported | lowers to `__typeof__`; also works with `(Class){...}`; not used for ownership inference |
| Format-free `print(...)` | supported | type tags via `_Generic`; scalars, bool, char and C strings only |
| Format-free `scan(...)` | supported | lvalues, bounded string reads, fatal on conversion failure |
| Default function arguments | supported | same translation unit only; filled at call sites; defaults should be trailing |
| Range-based `for` | supported | simple lvalue ranges with a visible `.len`; no side-effecting range expressions |
| Dynamic out-of-bounds index, null/misaligned dereference | supported at run time | UBSan checks, abort on first failure |
| Deterministic exit codes for safety failures | supported | `101`–`108` in `include/cplus/cplus.h` |
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

- **No use-after-free detection.** AddressSanitizer was tested and is broken
  on this machine: even a trivial program aborts during initialisation with
  `AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189`. Enabling it
  would produce a tool that crashes in its own runtime, so it is not used and
  no use-after-free claim is made.
- A stale managed pointer whose address has already been reused can be
  misattributed to the new allocation. The bounded cache catches the common
  immediate double-release case (tested), not every stale-pointer case.
- `*p` and `p->field` through a raw pointer are ordinary C operations. The
  null/alignment checks catch the obvious invalid cases, not dangling ones.
- Out-of-bounds pointers produced by arithmetic (`p + i`) are not statically
  checked the way an array subscript is; this is not Rust-style borrow
  checking.
- Allocation-size propagation only handles `sizeof(T) * parameter` with a
  literal call argument and one consistent value per parameter name. If the
  function is called with different sizes, the check is skipped rather than
  guessing.
- The static loop checker handles counted loops of the form
  `i = C; i < N; i++` (or `<=`) with literal bounds. Loops with computed
  bounds, multiple index variables or indirect subscripts fall back to the
  run-time check, not a compile error.
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
| `alloc_churn` | 0.1799s | 0.1777s | 0.0518s | 3.47x | 3.43x |
| `bounds_sum` | 0.0343s | 0.0313s | 0.0297s | 1.16x | 1.06x |
| `function_call` | 0.1200s | 0.1188s | 0.1204s | 1.00x | 0.99x |
| `integer_compute` | 0.0972s | 0.0949s | 0.0980s | 0.99x | 0.97x |
| `method_dispatch` | 0.0188s | 0.0083s | 0.0059s | 3.16x | 1.40x |
| `parallel_scale` | 0.0153s | 0.0124s | 0.0121s | 1.27x | 1.03x |
| `pointer_chase` | 0.0329s | 0.0535s | 0.0535s | 0.61x | 1.00x |
| `raii_loop` | 0.0190s | 0.0104s | 0.0102s | 1.86x | 1.02x |

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

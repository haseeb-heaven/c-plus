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
| Compile-time managed-pointer escape rejection | supported | `return p;` where `p` is managed is an error |
| Managed allocation with double/invalid release detection | supported | O(1) hash table + bounded freed-address cache |
| Out-of-bounds array index, null/misaligned dereference | supported at run time | UBSan checks, abort on first failure |
| Deterministic exit codes for safety failures | supported | `101`–`108` in `include/cplus/cplus.h` |
| Lightweight classes with methods | supported | inline methods only; lower to `T_m(T *self, ...)` |
| `p.m()` / `p->m()` sugar | supported | resolved from simple `T p;` / `T *p;` declarations in the same TU |
| Pthreads + RAII mutex guard | supported | primitives and deterministic unlock; see race note below |
| `--legacy-c` compatibility mode | supported | no safety checks; **not** memory safe |
| Use-after-free detection | **not implemented** | ASan unavailable on this host; see below |
| Static verification that shared data is locked | **not implemented** | concurrency is primitive-based, not verified |
| `goto` / `longjmp` cleanup | warning only | non-local exits bypass inserted destructors |
| Constructors, destructors as class members | **not supported** | use aggregate init and `T_drop` |
| Inheritance, interfaces, virtual dispatch, access control | **not supported** | classes are structs + functions |
| Full type inference for method calls | **not supported** | receiver class must be visible as a simple declaration |
| Leak detection for borrowed pointers | **not supported** | borrowing is a raw pointer; no alias tracking |
| Cross-translation-unit resource types | **not supported** | declare the resource in every TU that owns it |
| Full C++-style ownership system | **not supported** | the model covers locals only, not containers or fields |

## Memory safety: what the guarantee means

Safe mode compiles with:

```text
-fsanitize=undefined,bounds,null,alignment -fno-sanitize-recover=all
```

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
| `alloc_churn` | 0.1945s | 0.1894s | 0.0559s | 3.48x | 3.39x |
| `bounds_sum` | 0.0348s | 0.0327s | 0.0313s | 1.11x | 1.05x |
| `method_dispatch` | 0.0256s | 0.0049s | 0.0051s | 4.98x | 0.96x |
| `raii_loop` | 0.0190s | 0.0087s | 0.0077s | 2.48x | 1.14x |

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

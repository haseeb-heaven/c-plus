# Cross-module ownership and allocator boundaries (compiler side)

Status: **supported rules + measured verification** (compiler portion of issue
#31). This document describes what the C+ compiler guarantees at module
boundaries with Clang/GCC objects and prebuilt C libraries. Runtime/library
work is out of scope here. The permanent fixtures and runner are in
`tests/compiler/abi_ownership/`.

## Supported rules

**R1 — Ordinary C pointer representation.** Across C+, Clang/GCC and prebuilt
C libraries, object pointers, `void *`, function pointers and integer
provenance use the platform C representation: no fat pointers, no capability or
generation token in the pointer value. Measured here: `sizeof(void *) ==
sizeof(fnptr) == sizeof(uintptr_t) == 8` on Darwin/arm64. A pointer is a plain
bit pattern; a stale alias of a freed address is bit-identical to a fresh
pointer to a reused address (runtime open limitation F2). Do not encode
ownership in the bits of an ordinary pointer.

**R2 — Struct layout and calling convention.** Shared headers must contain C
types only (`struct`, `union`, scalars, function pointers). Layout, alignment,
offsets and by-value passing follow the platform C ABI (System V AMD64 /
AAPCS64), including `#pragma pack` where both sides agree. Measured:
`struct AbiPair {int; double;}` is `size=16, tag=0, value=8`; `union AbiUnion`
is `size=8`; values written in a Clang TU are read correctly by a C+ TU and
the struct is returned by value. Variadic calls use the ordinary C varargs ABI.

**R3 — Ownership transfer only via explicit APIs.** A module retains ownership
unless an explicit API says otherwise: `cplus_alloc`/`cplus_release` for
managed memory, `malloc`/`free` for system memory, `cplus_move` for moving an
owning C+ variable, and named `*_create`/`*_destroy` pairs for opaque handles.
Borrowed pointers (for example an opaque handle passed through a Clang bridge)
must not be released by the borrower. STRICT analysis is per translation unit
and emits `[CPLUS-MULTITU]` warnings for cross-TU callees; the warning is a
disclosure of the boundary, not a cross-TU proof.

**R4 — Permitted allocator pairings (closed set).**

| Allocation | Release | Where | Verdict |
| --- | --- | --- | --- |
| `cplus_alloc` / `cplus_zalloc` / `cplus_alloc_array` / `cplus_realloc` | `cplus_release` (or `cplus_realloc`) | C+ runtime linked into the image | **Supported**, including across C+ and Clang/GCC TUs |
| `malloc` / `calloc` / `realloc` / `strdup` | `free` | system libc | **Supported**, including across C+ and Clang/GCC TUs |
| any other mixing (e.g. `cplus_alloc` → `free`, `malloc` → `cplus_release`) | — | — | **Prohibited** |

In Safe modes (`--default`, `--strict`) the C+ rewriter lowers `free(p)` to
`cplus_free_registered(p, file, line)`. For a pointer with no live C+ runtime
registration — precisely the ordinary `malloc` case from another TU — that
wrapper falls through to libc `free(p)`. Source-level `malloc`/`free` pairing
therefore remains a libc pairing; the wrapper is ABI plumbing, not a transfer
of ownership to the C+ runtime. `--legacy-c` emits a direct `free` call.

**R5 — Mismatched allocators are prohibited and diagnosed, never repaired.**
`cplus_release` on system-heap memory aborts with exit **105** and the exact
stderr `C+ runtime error: invalid free: pointer was not produced by the C+
allocator`. Releasing one managed pointer from two TUs aborts with exit
**104** and `C+ runtime error: double free: managed memory released twice`.
Both were measured in all three modes. Callers must not rely on libc silently
accepting a managed pointer (or vice versa).

**R6 — Unsupported transfers require an explicit trusted contract.** Passing
ownership across a module boundary is supported only when an API documents
who allocates, who releases, and with which allocator (e.g. the opaque-handle
fixture: `opaque_create`/`opaque_destroy` in the owner TU, a borrowed
`void *` in the bridge TU). A stack-owned address escaping its module is not
an ownership transfer: it is rejected at compile time by
`-Werror=return-stack-address` (measured, all three modes). Passing a managed
pointer to an unresolved function-pointer callee is not a supported transfer;
it must be wrapped in a declared create/destroy contract instead.

**R7 — Linkage and symbol visibility.** Static linkage (`.o`, `.a`) and shared
linkage (`.dylib`/`.so`) are both supported; the C+ driver links the C+ runtime
for any C+ object, including in Safe modes with `--sanitize=none` (native guard
calls are emitted even without sanitizer instrumentation). Exported symbols
have exact unmangled C names (Mach-O: one leading `_`). `static` functions and
`__attribute__((visibility("hidden")))` functions are not exported from a
shared object. `nm -g --defined-only` and `nm -u` assertions are automated.

**R8 — errno and libc state.** `errno` is process/thread state owned by the
system libc and is shared across modules; a value set by a Prebuilt C library
TU is visible to the C+ TU through explicit C accessor functions. Measured:
`strtol` overflow yields `v=9223372036854775807 errno=34 erange=1`.
STRICT mode rejects arithmetic in a macro expansion (`errno = 0`, `errno ==
ERANGE`) as `[CPLUS-FFI] arithmetic in a macro expansion cannot be
instrumented`; the supported boundary is a plain C function that reads/writes
`errno`, not the macro in the C+ TU.

## Verification suite (measured)

Command (exact environment used for the recorded run):

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/abi_ownership/run_suite.py
```

Host: Darwin/arm64; `build/c+` = C+ 0.1.0; `REF_CC=/usr/bin/clang`
(Apple clang 16.0.0); `nm=/usr/bin/nm`, `ar=/usr/bin/ar`;
`build/libcplus_runtime.a`; `--sanitize=none`; build/nm timeout 90 s,
execution timeout 30 s.

**Measured result: 14 fixtures × 3 modes = 42 checks; 42 passed, 0 failed;
201 real subprocess steps; 9 nonzero-exit steps, all 9 expected negative
diagnoses (3 negatives × 3 modes), 0 unexpected.** Per mode: 14/14 pass in
each of `--legacy-c`, `--default`, `--strict`. Families: allocator_pairing
12/12, calling_convention 9/9, layout 3/3, linkage 6/6, negative 9/9,
ownership_transfer 3/3.

Fixtures (all under `cases/`, compiled as separate TUs):

| Fixture | Covers |
| --- | --- |
| `managed_cross_tu`, `managed_cross_compiler` | `cplus_alloc` → `cplus_release` across C+ TUs and from a Clang TU |
| `libc_malloc_to_free`, `libc_malloc_from_cplus` | `malloc` → `free` in both directions |
| `callbacks_fnptr` | function-pointer size and callbacks in both directions |
| `opaque_handle` | opaque handle: C+ owner, Clang borrower, explicit destroy |
| `struct_union_layout` | sizes, offsets, struct-by-value and union across compilers |
| `variadic_call` | C varargs callee in a Clang TU called from C+ |
| `errno_behavior` | libc errno set in Clang, observed via C accessors from C+ |
| `static_link`, `shared_visibility` | `.a` and `.dylib`; exported vs hidden/static symbols |
| `neg_allocator_mismatch` | NEGATIVE: `malloc` → `cplus_release`, exit 105 |
| `neg_double_release_cross_tu` | NEGATIVE: cross-TU double release, exit 104 |
| `neg_stack_transfer` | NEGATIVE: stack address escape, compile-time rejection |

All positive cases are checked by exact stdout and exact exit 0; negative
cases by exact exit code/stderr (or exact compiler diagnostic). Object/binary
global and undefined symbol sets are checked with `nm`. Hard timeouts apply to
every subprocess; there is no `|| true`, no skip and no fabricated result.

Results: `results/summary.txt` (counts + every exact command), `results/summary.json`
(full per-step records), `results/logs/` (complete command/stdout/stderr per
step), `results/bin/` (built artifacts).

## Recorded limitations

* Only LP64 platforms are pinned (`Darwin/arm64`, `Darwin/x86_64`,
  `Linux/x86_64`, `Linux/aarch64`); an unrecognised platform is a configuration
  error, not a silent skip. Only Darwin/arm64 was measured here.
* STRICT's cross-TU analysis is deliberately per-TU; `[CPLUS-MULTITU]`
  warnings on cross-module calls are expected and recorded in build logs.
* `errno` macro expansions cannot be instrumented in STRICT mode (R8), so the
  fixture uses C accessor functions. Runtime errno semantics themselves are
  owned by system libc.
* Shared-library execution requires the dynamic loader path
  (`DYLD_LIBRARY_PATH` on Darwin, `LD_LIBRARY_PATH` on Linux) pointing at the
  fixture's `results/bin/<case>.<mode>` directory; the runner sets it and
  records it in the exact command environment.
* The runtime's stale-alias limitation F2 (no provenance token in ordinary
  pointers) is out of scope for this compiler-side suite; see
  `runtime/include/cplus/cplus.h`.

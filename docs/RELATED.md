# Related projects: Fil-C and the inspiration language

The user asked how Fil-C and the inspiration language approach memory safety. This page records
what their public documentation says (checked 2026-11-21) and where C+ sits.
C+ does **not** claim their guarantees.

## Fil-C

Site: <https://fil-c.org>

Fil-C is a memory-safe implementation of C and C++ built on Clang 20.1.8.
Its stated approach:

- **InvisiCaps** (invisible capabilities) check every possibly-unsafe C/C++
  operation; there is no `unsafe` escape hatch.
- **Concurrent garbage collection** keeps memory alive; memory-safety errors
  are reported as "Fil-C panics" rather than undefined behaviour.
- It aims for high compatibility: CPython, OpenSSH, Emacs and Wayland are
  reported to work, along with threads, atomics, signals, `longjmp`, shared
  memory and even inline assembly.

That is a *new compiler + runtime + GC*, i.e. a full reimplementation of the
C toolchain. C+ deliberately does not do this: it keeps the existing C
compiler and adds a small source lowering plus targeted checks.

## the inspiration language

Site: <https://aardappel.github.io/scoped/> · samples: <https://aardappel.github.io/scoped/samples/>

the inspiration language is a new memory-safe systems language with no heap allocator, no GC and
no lifetime annotations. Its documented model:

- Every dynamic value lives inline on compiler-managed **data stacks**;
  freeing a scope is one store to a stack pointer.
- Growth never moves data, so references stay valid; the compiler infers what
  each reference is rooted in and rejects references that outlive their owner.
- Arrays come in several sized/unsized forms; slices and narrow relative
  references are built into the language.
- Threads have no shared memory: workers are compiled as separate programs
  and exchange flat values through typed queues.
- The compiler emits **one C file** for the whole program and can call C via
  `extern fn`. Its benchmark page reports speedups over idiomatic C++ and
  safe Rust; those numbers come from the project's own suite.

the inspiration language is a new language with its own compiler, type system and memory model.
It is **not** C. C+ shares one design idea with it — deterministic cleanup at
scope exit — but keeps C pointers, C arrays and the ordinary C ABI, so it
cannot offer the inspiration language's whole-program lifetime guarantees.

## Where C+ sits

| | C+ | Fil-C | the inspiration language |
| --- | --- | --- | --- |
| C source compatibility | yes (C17 + small additions) | yes, C/C++ | no, new language |
| Existing C compiler reused | yes, host Clang | no, Clang fork + runtime | emits C, but has its own compiler |
| Ownership/lifetime model | scoped resources + managed allocations | capabilities + GC | data stacks + inferred roots |
| Compile-time bad array index | yes, provable cases | runtime capability checks | bounds checks, removed when provable |
| Use-after-free | not detected (ASan broken here) | caught by capabilities/panics | references cannot dangle |
| New heap allocator/GC | no | yes (GC) | no |

The practical takeaway: C+ is the smallest of the three. Its promise is only
what `docs/LIMITATIONS.md` states — ordinary C keeps working, common memory
mistakes become compile errors or safe aborts, and cleanup is deterministic.
It is not a replacement for a full memory-safe toolchain.

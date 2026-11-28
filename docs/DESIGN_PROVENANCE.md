# Design provenance and acknowledgements

C+ is an experimental C-compatible language. Its design has been informed by
the ideas and architecture of several established projects: [Fil-C](https://fil-c.org),
[C3](https://c3-lang.org), [GooseLang](https://github.com/aardappel/goose) and
[Rust](https://www.rust-lang.org). We are grateful for the public documentation
these projects provide and acknowledge that intellectual debt below.

Throughout this document, *inspiration* means **architectural and ergonomic
ideas only** — not copied code. The C+ compiler, runtime, standard library and
tests in this repository are original implementations written for this project;
see [What we did NOT take](#what-we-did-not-take).

## At a glance

| Project | What we studied | Design idea reflected in C+ |
|---|---|---|
| [Fil-C](https://fil-c.org) ([source](https://github.com/pizlonator/fil-c)) | Invisible capabilities, out-of-band pointer metadata, liveness queries | Side tables and generation-tagged tokens instead of changing `sizeof(T*)`; recoverable refusal codes |
| [C3](https://c3-lang.org) ([source](https://github.com/c3lang/c3c)) | Modern, low-ceremony C-like syntax; modular standard library; explicit contracts | Module-style `stdlib/*.hp`, stable diagnostic categories, deterministic cleanup |
| [GooseLang](https://github.com/aardappel/goose) | Data stacks, arena-first allocation, one-store scope release, stable handles | `datastack.hp`, `arena.hp`, `mempool.hp`, single-store `drop()`, slot references |
| [Rust](https://www.rust-lang.org) ([source](https://github.com/rust-lang/rust)) | Ownership, borrow discipline and compile-time refusals as a comparison point | `error*` return contracts, `[CPLUS-*]` categories, paired safety comparisons |

## Fil-C — pointer validity and runtime safety

[Fil-C](https://fil-c.org) ([github.com/pizlonator/fil-c](https://github.com/pizlonator/fil-c))
is the primary inspiration for the runtime-safety direction of C+:

- **Out-of-band metadata.** Pointer bounds and liveness are tracked in side
  tables rather than inside `T*`, so `sizeof(T*)` and C struct layouts stay
  untouched. C+ implements its own registry, live table and retired ledger in
  `runtime/cplus_runtime.c` and `stdlib/view_lifetime.hp`.
- **Liveness queries at the ABI boundary.** A freed handle is marked stale and
  refused (codes 103/104/105) instead of silently recycling storage.
- **ABI preservation as a non-negotiable guardrail.** Capability-style
  metadata never changes the layout of ordinary C structs or raw pointers;
  see [AGENTS.md](../AGENTS.md) and [CORE_SAFETY.md](CORE_SAFETY.md).

Source-cited notes: [RUNTIME_SAFETY_FILC.md](security/RUNTIME_SAFETY_FILC.md)
and [upstream-techniques.md](security/upstream-techniques.md).

## C3 — modern C-compatible syntax and safety ergonomics

[C3](https://c3-lang.org) ([github.com/c3lang/c3c](https://github.com/c3lang/c3c))
informed the source-level ergonomics of C+:

- C-compatible syntax with lower-ceremony constructs: classes with `this`,
  deterministic resource cleanup and generic-style patterns.
- Diagnostics with stable categories (`[CPLUS-BOUNDS]`, `[CPLUS-LIFETIME]`,
  `[CPLUS-FFI]`) instead of silent rewrites.
- A standard library organised as modules rather than one monolithic header.

C+ uses its own Clang/LLVM-based lowering and its own `stdlib/*.hp` modules;
see [MODERN_STDLIB.md](MODERN_STDLIB.md) and [LANGUAGE.md](LANGUAGE.md).

## GooseLang — arena-first and scoped allocation

[GooseLang](https://github.com/aardappel/goose) is the reference for the
scoped-memory architecture (Apache-2.0). We studied the design directly, not
the compiler implementation:

- **Arena-first, heap-last.** Bump-allocated data stacks and dynamic arenas are
  the default for C+ hot paths (`stdlib/datastack.hp`, `stdlib/arena.hp`); the
  checked heap remains the compatibility boundary for ordinary C.
- **One-store release.** A scope records the stack top and generation;
  `drop()` restores it with a single store — no per-object frees.
- **Stable handles.** `stdlib/mempool.hp` pairs a grow-only slot array with a
  free list and per-slot generation, so a stale handle is refused rather than
  corrupting memory.

Evidence: [SCOPED_MEMORY_PROVENANCE.md](stdlib/SCOPED_MEMORY_PROVENANCE.md),
[SCOPED_MEMORY_DESIGN.md](stdlib/SCOPED_MEMORY_DESIGN.md),
[CORE_PHILOSOPHY.md](CORE_PHILOSOPHY.md), and
[MODERN_SAFER_EVIDENCE.md](stdlib/MODERN_SAFER_EVIDENCE.md). The shared
workload also has a checked-in Goose implementation,
[bench.goose](../benchmarks/language_rounds/bench.goose) with
[run_goose.sh](../benchmarks/language_rounds/run_goose.sh).

## Rust — a reference point for safe API design

[Rust](https://www.rust-lang.org) ([github.com/rust-lang/rust](https://github.com/rust-lang/rust))
is used as a comparison point, not a source of code. Its ownership and borrow
discipline inform how C+ frames its own compile-time refusals and recoverable
`error*` contracts:

- Paired scenario results are recorded honestly in
  [RUST_COMPARISON.md](RUST_COMPARISON.md), including differences.
- Cross-language benchmark outcomes, including cases where Rust wins, are in
  [the benchmark evidence](../proof/benchmarks/README.md).

## What we did NOT take

- **No copied compiler, GC, runtime or standard-library source.** No source
  file from Fil-C, C3, GooseLang or Rust is vendored, linked or pasted into C+.
  Only independently written C+ code is checked in here.
- **All C+ implementation is original.** The compiler lowering, side tables,
  allocator, `stdlib/*.hp` modules and tests were written for this repository.
  Third-party libraries that *are* vendored are listed below, each under its
  own license.
- **Inspiration means architectural ideas only.** We borrow concepts -
  out-of-band metadata, scoped allocation, modular libraries, diagnostic
  discipline — and re-express them to preserve the ordinary C ABI. We do not
  reproduce upstream code, compilers, type systems or garbage collectors.
- **No equivalence or superiority claims.** C+ does not claim Fil-C-level
  memory safety, Rust parity, C3 compatibility or Goose whole-program lifetime
  guarantees. Benchmark numbers are host-specific and reported as measured.
- **Unavailable toolchains are reported as `NOT_TESTED`**, never fabricated
  (for example Fil-C in [the benchmark evidence](../proof/benchmarks/README.md)).

## Third-party libraries vendored separately

These are compiled or linked as their own licensed C code and are **not**
language-design inspiration: raylib 6.2 (GUI/3D demos), Nuklear (immediate-mode
GUI), microui (calculator/notes GUI), olive.c (2D software renderer), yyjson,
safeclib, miniaudio and stb. Each keeps its own license under `third_party/` or
the owning example directory.

## Further reading

- [CORE_SAFETY.md](CORE_SAFETY.md) — safety model and ABI guardrails
- [CORE_PHILOSOPHY.md](CORE_PHILOSOPHY.md) — scoped-memory design rules
- [RELATED.md](RELATED.md) — how C+ sits relative to Fil-C and Goose
- [RUST_COMPARISON.md](RUST_COMPARISON.md) — paired safety scenarios
- [upstream-techniques.md](security/upstream-techniques.md) — upstream technique survey

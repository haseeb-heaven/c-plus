# Design provenance and acknowledgements

C+ is an experimental C-compatible language. Its design is **mostly inspired
by Fil-C, the C3 language, and GooseLang (aardappel/goose, Apache-2.0)**,
which also supplied the arena/scoped-allocation ideas. This document
records *what* was taken from each source. It is a statement of intellectual
debt, not a claim of code reuse: no source from these projects was copied into
this repository unless explicitly vendored with its own license.

## Fil-C (pointer validity / runtime safety)

Fil-C (`pizlonator/fil-c`, studied at commit `d9041afd6b11`) is the primary
inspiration for the runtime safety model:

- **Invisible capabilities / side tables** - pointer bounds and liveness are
  tracked out-of-band instead of inside `T*`, so `sizeof(T*)` stays the
  native 8 bytes on 64-bit targets. C+ implements this with its own
  registry / live-table / retired-ledger (`runtime/cplus_runtime.c`) and
  `stdlib/view_lifetime.hp`.
- **Bounds on free** - a freed pointer is marked invalid/stale rather than
  silently reusable; C+ uses generation-tagged tokens and deterministic
  refusal codes (103/104/105).
- **Low-level ABI preservation** - capability metadata must never change
  the layout of ordinary C structs or raw pointers. This is a non-negotiable
  guardrail in this repository (see `AGENTS.md` and `docs/CORE_SAFETY.md`).

Detailed, source-cited notes are in `docs/security/upstream-techniques.md`
and `docs/security/RUNTIME_SAFETY_FILC.md`. No Fil-C code, GC or runtime was
copied; the LLVM-based implementation and all C+ metadata structures are
original to this project.

## C3 (modern C-compatible syntax and safety ergonomics)

C3 is an inspiration for the **source-level design** of C+:

- C-compatible syntax with modern, low-ceremony constructs (classes with
  `this`, deterministic resource cleanup, generic-ish patterns, contracts).
- Refusals that are diagnostics with stable categories (`[CPLUS-BOUNDS]`,
  `[CPLUS-LIFETIME]`, `[CPLUS-FFI]`) rather than silent rewrites.
- A standard library organised in modules rather than one monolithic header.

C+ implements its own lowering (Clang/LLVM based, C++20 compiler) and its own
`stdlib/*.hp` modules; no C3 source or compiler code is present here.

## Goose (safe-systems ergonomics and cross-language reference)

Goose is an inspiration for the **ergonomics and evaluation** side:

- Expressive, safe-by-default surface syntax for everyday code; C+ mirrors
  this with scoped allocation, `Vec`/`Slice` and checked views.
- Cross-language comparison: the repository includes a reproducible Goose
  implementation of the shared benchmark workload
  (`benchmarks/language_rounds/bench.goose`, `run_goose.sh`) and reports its
  numbers alongside C, C+, Rust and Zig in `proof/benchmarks/README.md`.

No Goose compiler or runtime code is vendored. Fil-C and TrapC toolchains are
absent on this host and are reported as `NOT_TESTED`, never fabricated.

## GooseLang / aardappel/goose (arena and one-store release)

The scoped-memory architecture (`stdlib/datastack.hp`, `stdlib/arena.hp`,
`stdlib/scoped_mem.hp`) is inspired by GooseLang (`aardappel/goose`, Apache-2.0),
read directly: arena-first allocation, one-store scope release and a
stable-handle model. See `docs/CORE_PHILOSOPHY.md` and
`docs/stdlib/SCOPED_MEMORY_PROVENANCE.md`. No upstream source was copied.

## Third-party libraries actually vendored

These are compiled/linked as their own MIT/other-licensed C code and are
**not** claims of language design inspiration: raylib 6.2 (GUI/3D demos),
Nuklear (immediate-mode GUI), microui (calculator/notes GUI), olive.c (2D
software renderer), yyjson, safeclib, miniaudio, stb. Each keeps its own
license under `third_party/` or the owning example directory.

## Non-claims

- No 100% memory-safety proof and no equivalence to Fil-C, Rust, C3 or Goose.
- No copied compiler, GC, runtime or standard-library source from those
  projects; all C+ implementation code is original to this repository.
- Benchmark numbers are host-specific and never presented as language-level
  superiority claims.

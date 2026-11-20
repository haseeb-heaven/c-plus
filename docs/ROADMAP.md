# Roadmap to the full language requirements

This is a status map, not a promise of dates. Items marked **not started** are
mandatory in the specification but absent from the current MVP. The capability
matrix in [STATUS.md](STATUS.md) is the evidence-backed view of what exists.

## Phase 0 — delivered MVP

| spec requirement | status |
| --- | --- |
| `.c` files compile to native executables | delivered |
| ordinary C + libc/POSIX interop | delivered |
| safe mode + `--legacy-c` | delivered |
| RAII on scope exit / return / break / continue | delivered |
| resource structs with user destructors | delivered |
| struct-based classes, methods, constructors, destructors | delivered |
| compile-time and runtime bounds checks | delivered |
| managed allocation with tracked `.len` | delivered |
| borrow subset (escape + use-after-move) | delivered |
| format-free `print` / `scan` | delivered |
| networking + pthread demos | delivered |
| automated positive/negative/runtime tests | delivered |
| reproducible benchmarks | delivered |
| docs: language, architecture, limitations, threat model, foundation | delivered |

## Phase 1 — correctness and safety hardening

- [ ] Use-after-free detection with a working sanitizer backend (ASan is
      broken on this host; evaluate a different backend or a quarantine).
- [ ] Concurrency safety: data-race detection or a checked lock discipline;
      currently mutex RAII is only a primitive, marked experimental.
- [ ] Extend borrow checking to fields, containers and function arguments.
- [ ] Handle `goto`/`longjmp` cleanup with documented diagnostics rather than
      a warning.
- [ ] Adversarial fuzzing of the rewriter, bounds pass and allocator.
- [ ] Verify Linux x86-64 and ARM64 targets in CI; add Windows x86-64 later.

## Phase 2 — toolchain maturity

- [ ] Dependency tracking and incremental compilation cache.
- [ ] Parallel compilation of multiple translation units.
- [ ] Build-time and executable-size measurements against equivalent C/Rust.
- [ ] C23 mode progression beyond the current C17 + GNU subset.
- [ ] Optional AST-based frontend instead of source lowering, once the
      maintenance cost justifies it.

## Phase 3 — performance and workloads

- [ ] Broaden benchmarks to network throughput, database-style workloads and
      executable size / memory usage / CPU utilisation.
- [ ] Compare with Clang C, GCC C, Rust, C3 and Fil-C where toolchains exist;
      record results rather than asserting targets.
- [ ] Investigate removing sanitizer cost in safe mode without weakening the
      guarantee (e.g. proving checks unnecessary at compile time).

## Phase 4 — production scope (out of scope for the MVP)

- [ ] Standard safe abstractions for async I/O, TLS and database clients
      built on existing C libraries.
- [ ] Checked adapters / trusted wrappers for calling unsafe native libraries.
- [ ] Game-engine-oriented facilities (GPU handle resources, checked mmap).
- [ ] Optional interfaces / explicit method tables for classes, without
      virtual dispatch or RTTI.

## Explicitly non-goals

- Reimplementing C, libc, LLVM, a new ABI or a C++ object model.
- Rust-style lifetime annotations in ordinary application code.
- Claiming 100% or mathematically proven safety from passing tests.
- Marketing unimplemented backend/database/game-engine frameworks as present.

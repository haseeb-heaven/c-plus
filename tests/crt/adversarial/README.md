# Adversarial bug catalogue corpus

This directory proves how the C+ hardened layer handles a 104-pattern raw-C bug
catalogue (leaks, null derefs, UAF, double free, buffer overflow/underflow,
uninitialised reads, aliasing, provenance, races, resource/RAII failures, file,
mmap, dlopen, allocator, and variadic/format bugs).

- `test_safe_mitigations.c` — **executable proof**: for every runtime-detectable
  pattern, the checked C+ safe API returns a defined error (or behaves safely)
  instead of invoking undefined behaviour. Run via `sh tests/crt/run_all_crt.sh`.

- `../../docs/libc/ADVERSARIAL_MATRIX.md` — full classification of all 104
  patterns into SAFE-CATCH (checked API), COMPILER (Agent 1 static analysis),
  or BOUNDARY (explicit unsafe boundary no library can catch).

Honest scope: the checked API layer catches the runtime-detectable subset
(null, bounds, overlap, truncation, zero-alloc, realloc-failure, double-close,
overflow). The rest require compile-time enforcement and/or sanitizers; no
Rust-grade memory-safety claim is made for raw C.

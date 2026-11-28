# Rust-like runtime outcomes per hardened method (measured)

Runner: `tests/rustlike/rustlike_methods.c`. Each of the 39 scenarios runs in a
forked child; the parent classifies `ACCEPTED` (exit 0), `REFUSED` (exit 2 with
a defined error) or `CRASH` (signal). Both shipped layers were rebuilt and
tested with `/usr/bin/clang` at `-O1`.

| Method | Scenario | SafeCRT layer | Plain libc layer |
|---|---|---|---|
| malloc | valid / zero / reuse | ACCEPTED | ACCEPTED |
| malloc | `SIZE_MAX` | REFUSED | REFUSED |
| calloc | valid / zero / large | ACCEPTED | ACCEPTED |
| calloc | `n*size` overflow | REFUSED | REFUSED |
| realloc | grow / shrink | ACCEPTED | ACCEPTED |
| realloc | huge (original preserved) / invalid pointer | REFUSED | REFUSED |
| free | valid / NULL | ACCEPTED | ACCEPTED |
| free | double-free alias / foreign pointer | REFUSED | REFUSED |
| memset | exact / zero | ACCEPTED | ACCEPTED |
| memset | over-capacity / one-past-end / NULL | REFUSED | REFUSED |
| memcpy | exact / zero | ACCEPTED | ACCEPTED |
| memcpy | over-capacity / NULL | REFUSED | REFUSED |
| memcpy | overlapping | REFUSED | **OTHER (not detected)** |
| strcpy | exact / empty / self | ACCEPTED | ACCEPTED |
| strcpy | overflow / NULL | REFUSED | REFUSED |
| strnlen | bounded / zero / embedded NUL | ACCEPTED | ACCEPTED |
| strnlen | NULL | **CRASH (both layers)** | **CRASH (both layers)** |
| fread | valid | ACCEPTED | ACCEPTED |
| fread | size*count overflow / NULL buffer | REFUSED | REFUSED |
| fwrite | size*count overflow | REFUSED | REFUSED |

Totals: SafeCRT 21 accepted / 17 refused / 1 crash; plain libc 21 accepted /
16 refused / 1 crash / 1 other.

## Honest interpretation

- 37/39 scenarios end in a **defined** outcome in the SafeCRT layer; the two
  exceptions are raw-C UB cases (`strnlen(NULL)` and the plain-layer overlap).
- Rust-like behaviour here means "controlled refusal instead of a crash", not
  a language-level guarantee. The runner measures behaviour, not proof.
- `strnlen(NULL, n)` is UB by ISO C: a libc wrapper cannot invent pointer
  provenance. Use the checked `_s` entry point or a compiler contract.
- Overlapping raw `memcpy` is UB by ISO C. The SafeCRT auto wrapper detects it
  (ESOVRLP); the plain libc layer delegates to host `memcpy` and does not.
- Unknown capacity is explicit in both layers (warning by default, refusal in
  strict mode); see `docs/libc/ORDINARY_C_SAFETY.md`.

This matrix complements `RUSTC_DIRECT_COMPARISON.md` and the 12-gate
`CPLUS_RUST_CONFORMANCE.md` status, neither of which claims Rust equivalence.

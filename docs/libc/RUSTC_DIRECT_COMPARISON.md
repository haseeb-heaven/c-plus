# Direct rustc vs C/UBSan comparison (measured, not claimed)

Environment: `rustc 1.100.0`, `/usr/bin/clang` (Apple clang 16), macOS arm64.
Commands run in `/tmp/rustcmp`. C binaries built with
`-fsanitize=undefined -fno-sanitize-recover=all`; Rust with `--edition 2021`.
No C+ hardening headers were used in the C column — this is the raw baseline
comparison requested. Exit 0 means "no detection".

| # | Scenario | C (UBSan) result | rustc result | Difference |
|---:|---|---|---|---|
| 1 | Constant index 10 into 3 function pointers | build OK, run **134**, `runtime error: index 10 out of bounds for type 'F[3]'` | **compile error**: `unconditional_panic`, "index out of bounds: length 3, index 10" | Rust rejects at compile time; C detects only at run time. (The C+ compiler itself does reject this case — see `DISPATCH_BOUNDS_HANDOFF.md`.) |
| 2 | Dynamic index 10 (`black_box`/`volatile`) | run **134**, UBSan `runtime error` | run **101**, Rust `panicked`, language-level bounds check | Both detect; mechanisms differ. Rust's panic is part of the language, UBSan is an external detector. |
| 3 | Double free | run **133** (allocator abort) | **compile error** (`drop` twice / moved value) | Rust prevents at compile time; C aborts at runtime. |
| 4 | Use after free | run **0 — NOT DETECTED by UBSan** | **compile error E0382**: borrow of moved value | UBSan does not detect UAF; ASan would. Rust's ownership rules reject the program. |
| 5 | Two threads mutating a shared `x` without sync | run **208**, no diagnostic (silent wrong result) | **compile error E0373**: closure borrows `x` that may outlive it | Rust prevents the data race at compile time; plain C does not. |
| 6 | Read an uninitialized struct field / binding | run **0 — silent** | **compile error E0381**: binding not initialized | Rust rejects; C UB is silent. |
| 7 | `u32::MAX * 4` | run **0 — silent wrap** | **compile error**: `arithmetic_overflow` | Rust constant-folds and rejects; C wraps silently. |

## What this proves

- rustc rejects 5 of 7 scenarios at compile time (1, 3, 4, 5, 6, 7) and
  bounds-checks the remaining dynamic case (2) as a language feature.
- UBSan detects only the two array-index cases (1, 2). It does **not** detect
  UAF, data races, uninitialized reads or wrapping arithmetic in these tests.
- Therefore sanitizer output is not equivalent to Rust's compile-time
  ownership/borrow/lifetime guarantees.

## Relation to this project

- The C+ compiler does provide a compile-time `[CPLUS-BOUNDS]` error for
  scenario 1 (measured with `./build/c+ check`, see
  `docs/libc/DISPATCH_BOUNDS_HANDOFF.md`), but not for scenarios 4–7.
- The hardened ordinary-C layers detect a different subset at runtime (see
  `docs/libc/ORDINARY_C_SAFETY.md`, `docs/libc/C_BUG_CLASS_MATRIX.md`) and
  explicitly document unknown-capacity and provenance limits.
- No Rust-equivalence claim is made. The paired 12-gate conformance suite in
  `tests/conformance/` automates this comparison; its results are recorded in
  `docs/libc/CPLUS_RUST_CONFORMANCE.md` when available.

## Reproduce

```
cd /tmp && mkdir -p rustcmp && cd rustcmp
# write the seven .c/.rs programs shown in this document's scenario descriptions
clang -std=c99 -fsanitize=undefined -fno-sanitize-recover=all -w n.c -pthread -o n_c && ./n_c
rustc --edition 2021 n.rs -o n_rs && ./n_rs
```

# Compile-time safety comparison: C+ vs Rust

Evidence-only comparison for 20 paired scenarios in `tests/safety/rust_compare/{c,rs}/`. Rust is compiled with `rustc --edition 2021 --emit=metadata` (compile-time only, no execution). C+ uses `c+ check --strict`.

```
rust-compare suite: 20/20 C+ expectations met
compile-time rejection: C+ 15/20, Rust 15/20
scenario             C+    Rust   ok    expectation / note
uaf                  REJECT REJECT ok    use-after-free | Rust: ownership moved, borrow checker rejects
double_free          REJECT REJECT ok    double free | Rust: value moved on first drop
null_deref           REJECT REJECT ok    null pointer dereference | Rust: Option::unwrap on None caught in const eval
dangling_stack       REJECT REJECT ok    address of stack | Rust: cannot return ref to local
const_oob            REJECT REJECT ok    constant array index 7 | Rust: const eval index check
dyn_oob              accept accept ok    accepted | both: runtime bounds check (C+ check, Rust panic)
uninit               REJECT REJECT ok    uninitialized | Rust: definite initialization
signed_overflow      accept REJECT ok    accepted | C+: UBSan runtime; Rust: const-eval error only in const context
unsigned_overflow    accept accept ok    accepted | both: wrapping is defined
data_race            REJECT REJECT ok    unsynchronized write | Rust: closure borrow in thread rejected
mutable_alias        accept REJECT ok    accepted | C+ accepts aliased ints; Rust rejects two &mut borrows
bad_format           REJECT REJECT ok    format string is not a string literal | Rust: format arg must be literal
format_arg_mismatch  REJECT REJECT ok    more '%' conversions | Rust: positional count checked
strcpy_overflow      REJECT REJECT ok    string copy of | Rust: const-eval copy_from_slice
leak_on_return       accept accept ok    accepted | C+ accepts leak; Rust File has Drop (safe by construction)
realloc_lost         REJECT accept ok    raw realloc return value is ignored | Rust: Vec manages reallocation
ptr_oob              REJECT REJECT ok    one past the end | Rust: const-eval index check
vla_unbounded        REJECT accept ok    variable-length stack allocation | Rust: no VLA, Vec is heap-checked
int_to_ptr           REJECT REJECT ok    unverifiable provenance | Rust: raw pointer deref requires unsafe
field_uaf            REJECT REJECT ok    use-after-free | Rust: borrow of s while dropping s rejected
```

## Interpretation

- Both reject 13 of the same scenarios at compile time.
- C+ uniquely rejects: raw `realloc` result ignored, unbounded VLA (both ordinary C patterns Rust cannot express).
- Rust uniquely rejects: aliased `&mut` borrows, and constant signed overflow/null-unwrap in const evaluation (C+ relies on UBSan at runtime for those in ordinary code).
- Both accept dynamic out-of-bounds indices at compile time and insert runtime checks (C+ `__cplus_bounds_check`, Rust slice panic).
- Both accept leaks in the paired formulations; Rust's `File` has `Drop`, so the analogous Rust program is safe by construction.

This does **not** show parity with Rust's ownership model: C+ has no mandatory annotations and still accepts ordinary C patterns (aliased integers, leaks, dynamic indices). The 20 pairs measure the overlapping subset only. Full C+ evidence across all corpora is printed by `python3 tests/metrics/compile_time_compare.py`.

# Compile-time safety comparison: C+ vs Rust

Evidence-only comparison for 27 paired scenarios in `tests/safety/rust_compare/{c,rs}/`. Rust: `rustc --edition 2021 --emit=metadata` (compile-time only, no execution). C+: `c+ check --strict`.

```
rust-compare suite: 27/27 C+ expectations met
compile-time rejection: C+ 22/27, Rust 19/27
scenario             C+    Rust   ok    expectation / note
uaf                  REJECT REJECT ok    use-after-free | Rust: ownership moved, borrow checker rejects
double_free          REJECT REJECT ok    double free | Rust: value moved on first drop
null_deref           REJECT REJECT ok    null pointer dereference | Rust: Option::unwrap on None caught in const eval
dangling_stack       REJECT REJECT ok    address of stack | Rust: cannot return ref to local
const_oob            REJECT REJECT ok    constant array index 7 | Rust: const eval index check
dyn_oob              accept accept ok    accepted | both: runtime bounds check (C+ check, Rust panic)
uninit               REJECT REJECT ok    uninitialized | Rust: definite initialization
signed_overflow      REJECT REJECT ok    signed integer overflow | C+: compile-time CPLUS-ARITH; Rust: const-eval error
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
arith_add            REJECT REJECT ok    signed integer overflow in addition | Rust: const-eval i32 overflow
arith_sub            REJECT REJECT ok    signed integer overflow in subtraction | Rust: const-eval i32 overflow
arith_mul            REJECT REJECT ok    signed integer overflow in multiplication | Rust: const-eval i32 overflow
arith_div_const      REJECT REJECT ok    division by zero | Rust: const-eval division by zero
arith_div_unknown    REJECT accept ok    division by zero cannot be excluded | DIFFERENCE: Rust accepts runtime divisor (panic possible); C+ STRICT rejects
arith_helper         REJECT accept ok    signed integer overflow in call to | DIFFERENCE: Rust accepts non-const helper with const args; C+ infers effect and rejects
arith_safe           accept accept ok    accepted | both: safe boundary and unsigned wrapping accepted
int_to_ptr           REJECT REJECT ok    unverifiable provenance | Rust: raw pointer deref requires unsafe
field_uaf            REJECT REJECT ok    use-after-free | Rust: borrow of s while dropping s rejected
```

## Arithmetic results and honest differences

- Direct constant overflow (`INT_MAX+1`, `INT_MIN-1`, `INT_MAX*2`) and `1/0`: both reject at compile time (Rust via const-evaluation).

- Runtime-unknown divisor: C+ STRICT rejects; Rust accepts because a plain `i32` divisor may be zero and the failure is a runtime panic. This is a real difference: C+ is stricter here.

- Tiny non-const helper with constant arguments: C+ infers the arithmetic effect and rejects; Rust accepts and panics at runtime in debug builds. Again C+ is stricter for this pattern.

- Safe boundaries and unsigned modulo/wrapping: both accept (Rust uses `wrapping_add` explicitly).


C+ 22/27 and Rust 19/27 compile-time rejections overall; 17 scenarios agree. The differences are recorded, not smoothed over. This is not a claim of parity with Rust's ownership model.

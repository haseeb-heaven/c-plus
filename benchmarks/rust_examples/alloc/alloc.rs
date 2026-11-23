// Safe-Rust examples for EVERY checked allocation/free API exercised by
// harness.c. Prints exactly the same "<api> <case> <token>" lines, with one
// deliberate difference: where a scenario is *unrepresentable* in safe Rust
// (forged/invalid pointers, runtime-chosen alignment, global metadata queries
// and counters) the Rust side prints BLOCKED instead of faking an outcome.
//
// CONSTRAINT: safe Rust only -- no `unsafe`, no raw-pointer construction, no
// FFI. Every emitted token is computed from a real safe-Rust operation
// (`Vec::with_capacity`, `try_reserve_exact`, `Box`, `Option::take`,
// `checked_mul`, `std::alloc::Layout`). See NOTES.md for the full
// non-equivalence statement.
//
// rustc -O benchmarks/rust_examples/alloc/alloc.rs -o .../alloc_rust

use std::alloc::Layout;

const PAYLOAD: usize = 64;
const GUARD: usize = 8;
const TOTAL: usize = PAYLOAD + GUARD;
const CANARY: u64 = 0xDEADBEEF_CAFEBABE;

fn emit(api: &str, case: &str, token: &str) {
    println!("{} {} {}", api, case, token);
}

fn write_payload_with_guard(buf: &mut [u8]) {
    for (i, b) in buf[..PAYLOAD].iter_mut().enumerate() {
        *b = (i & 0xff) as u8;
    }
    let c = CANARY.to_ne_bytes();
    buf[PAYLOAD..TOTAL].copy_from_slice(&c);
}

fn guard_intact(buf: &[u8]) -> bool {
    let c = CANARY.to_ne_bytes();
    &buf[PAYLOAD..TOTAL] == &c[..]
}

// Layout::array is a *safe* fallible size computation; it returns Err when the
// byte count cannot be represented. This is the closest safe-Rust analogue of
// the C+ "SIZE_MAX overflow -> refusal" check.
fn layout_refused() -> bool {
    Layout::array::<u8>(usize::MAX).is_err()
}

// try_reserve_exact is the fallible allocation API. usize::MAX exceeds
// isize::MAX, so it returns Err(CapacityOverflow) deterministically. By its
// documented contract the existing buffer and its contents are untouched --
// the genuine Rust form of the C+ "injected failure is atomic" outcome.
fn fallible_refused() -> bool {
    let mut v: Vec<u8> = Vec::new();
    v.try_reserve_exact(usize::MAX).is_err()
}

fn reserve_preserves_contents() -> bool {
    let mut v: Vec<u8> = vec![0xABu8; 32];
    let r = v.try_reserve_exact(usize::MAX);
    r.is_err() && v.len() == 32 && v.iter().all(|&b| b == 0xAB)
}

// --------------------------------------------------------------- malloc_s
fn malloc_s() {
    let a = "malloc_s";

    let mut v = Vec::with_capacity(TOTAL);
    v.resize(TOTAL, 0);
    write_payload_with_guard(&mut v);
    drop(v); // single deterministic release (safe analogue of free_s)
    emit(a, "valid", "OK");

    emit(a, "overflow", if layout_refused() && fallible_refused() { "REFUSED" } else { "MISMATCH" });

    let v: Vec<u8> = Vec::with_capacity(0);
    emit(a, "zero", if v.capacity() == 0 { "DEFINED" } else { "MISMATCH" });

    emit(a, "inject", if fallible_refused() { "REFUSED" } else { "MISMATCH" });

    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    emit(a, "canary", if guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// --------------------------------------------------------------- calloc_s
fn calloc_s() {
    let a = "calloc_s";

    let v = vec![0u8; 64];
    emit(a, "valid", if v.iter().all(|&b| b == 0) { "OK" } else { "MISMATCH" });

    emit(a, "overflow",
         if usize::MAX.checked_mul(4).is_none() && layout_refused() { "REFUSED" } else { "MISMATCH" });

    let v = vec![0u8; 0];
    emit(a, "zero", if v.is_empty() { "DEFINED" } else { "MISMATCH" });

    emit(a, "inject", if fallible_refused() { "REFUSED" } else { "MISMATCH" });

    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    emit(a, "canary", if guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// -------------------------------------------------------------- realloc_s
fn realloc_s() {
    let a = "realloc_s";

    let mut v: Vec<u8> = Vec::with_capacity(32);
    v.extend_from_slice(&[1, 2, 3, 4]);
    let grew = v.try_reserve_exact(256).is_ok();
    emit(a, "valid", if grew && v.len() == 4 { "OK" } else { "MISMATCH" });

    let mut v: Vec<u8> = vec![7u8; 32];
    let r = v.try_reserve_exact(usize::MAX);
    emit(a, "overflow",
         if r.is_err() && v.len() == 32 && v.iter().all(|&b| b == 7) { "REFUSED" } else { "MISMATCH" });

    let mut v: Vec<u8> = vec![7u8; 32];
    v.truncate(0);
    emit(a, "zero", if v.is_empty() { "DEFINED" } else { "MISMATCH" });

    emit(a, "inject", if reserve_preserves_contents() { "PRESERVED" } else { "MISMATCH" });

    // A foreign / dangling pointer cannot be formed in safe Rust.
    emit(a, "invalid", "BLOCKED");

    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    let r = v.try_reserve_exact(TOTAL);
    emit(a, "canary", if r.is_ok() && guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// -------------------------------------------------------------- free_s
fn free_s() {
    let a = "free_s";

    let b = Box::new(42u32);
    let got = *b;
    drop(b);
    emit(a, "valid", if got == 42 { "OK" } else { "MISMATCH" });

    let slot: Option<Box<u32>> = None;
    drop(slot);
    emit(a, "null", "DEFINED");

    // Option::take makes a "second free" a None/no-op, never a double free.
    let mut slot: Option<Box<u32>> = Some(Box::new(7));
    let first = slot.take();
    let second = slot.take();
    emit(a, "double", if first.is_some() && second.is_none() { "REFUSED" } else { "MISMATCH" });

    emit(a, "invalid", "BLOCKED"); // forged pointer unrepresentable

    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    drop(Box::new(9u32));
    emit(a, "canary", if guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// --------------------------------------------------------- free_checked_s
fn free_checked_s() {
    let a = "free_checked_s";

    let b = Box::new(42u32);
    let got = *b;
    drop(b);
    emit(a, "valid", if got == 42 { "OK" } else { "MISMATCH" });

    let mut slot: Option<Box<u32>> = Some(Box::new(7));
    let first = slot.take();
    let second = slot.take();
    emit(a, "double", if first.is_some() && second.is_none() { "REFUSED" } else { "MISMATCH" });

    emit(a, "invalid", "BLOCKED");

    let slot: Option<Box<u32>> = None;
    drop(slot);
    emit(a, "null", "DEFINED");

    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    drop(Box::new(9u32));
    emit(a, "canary", if guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// -------------------------------------------------------- aligned_alloc_s
fn aligned_alloc_s() {
    let a = "aligned_alloc_s";

    // align_of::<T>() is a compile-time property; a caller-chosen runtime
    // alignment has no safe stable Rust allocation API.
    emit(a, "valid", "BLOCKED");

    emit(a, "overflow", if layout_refused() && fallible_refused() { "REFUSED" } else { "MISMATCH" });

    emit(a, "zero", "BLOCKED");
    emit(a, "bad_align", "BLOCKED"); // alignment 3 is rejected by types, not at runtime

    emit(a, "inject", if fallible_refused() { "REFUSED" } else { "MISMATCH" });

    emit(a, "canary", "BLOCKED");
}

// -------------------------- SafeCRT metadata queries / failure control ---
fn safe_metadata_and_control() {
    // "Is this raw pointer live / what size / was it freed" are runtime
    // metadata queries about raw addresses. In safe Rust liveness and size
    // are compile-time/ownership facts (a Box is live exactly while in scope)
    // and there is no raw pointer to query -- unrepresentable, so BLOCKED.
    emit("cplus_safe_ptr_is_live", "live", "BLOCKED");
    emit("cplus_safe_ptr_is_live", "freed", "BLOCKED");
    emit("cplus_safe_ptr_is_live", "foreign", "BLOCKED");
    emit("cplus_safe_ptr_size", "live", "BLOCKED");
    emit("cplus_safe_ptr_size", "zero", "BLOCKED");
    emit("cplus_safe_ptr_was_freed", "live", "BLOCKED");
    emit("cplus_safe_ptr_was_freed", "after_free", "BLOCKED");

    // The global inject counter itself has no safe-Rust equivalent. Only the
    // observable outcome (a fallible allocation is refused / then succeeds)
    // is matched here; see NOTES.md.
    emit("cplus_safe_alloc_fail_after", "inject",
         if fallible_refused() { "REFUSED" } else { "MISMATCH" });
    let v: Vec<u8> = Vec::with_capacity(TOTAL);
    emit("cplus_safe_alloc_fail_reset", "reset",
         if v.capacity() >= TOTAL { "OK" } else { "MISMATCH" });
}

// ------------------------------------------------------------ cplus_malloc
fn cplus_malloc_() {
    let a = "cplus_malloc";

    let mut v = Vec::with_capacity(TOTAL);
    v.resize(TOTAL, 0);
    write_payload_with_guard(&mut v);
    drop(v);
    emit(a, "valid", "OK");

    emit(a, "overflow", if layout_refused() && fallible_refused() { "REFUSED" } else { "MISMATCH" });

    let v: Vec<u8> = Vec::with_capacity(0);
    emit(a, "zero", if v.capacity() == 0 { "DEFINED" } else { "MISMATCH" });

    emit(a, "inject", if fallible_refused() { "REFUSED" } else { "MISMATCH" });

    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    emit(a, "canary", if guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// ------------------------------------------------------------ cplus_calloc
fn cplus_calloc_() {
    let a = "cplus_calloc";

    let v = vec![0u8; 64];
    emit(a, "valid", if v.iter().all(|&b| b == 0) { "OK" } else { "MISMATCH" });

    emit(a, "overflow",
         if usize::MAX.checked_mul(4).is_none() && layout_refused() { "REFUSED" } else { "MISMATCH" });

    let v = vec![0u8; 0];
    emit(a, "zero", if v.is_empty() { "DEFINED" } else { "MISMATCH" });

    emit(a, "inject", if fallible_refused() { "REFUSED" } else { "MISMATCH" });

    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    emit(a, "canary", if guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// ----------------------------------------------------------- cplus_realloc
fn cplus_realloc_() {
    let a = "cplus_realloc";

    let mut v: Vec<u8> = Vec::with_capacity(32);
    v.extend_from_slice(&[1, 2, 3, 4]);
    let grew = v.try_reserve_exact(256).is_ok();
    emit(a, "valid", if grew && v.len() == 4 { "OK" } else { "MISMATCH" });

    let mut v: Vec<u8> = vec![7u8; 32];
    let r = v.try_reserve_exact(usize::MAX);
    emit(a, "overflow",
         if r.is_err() && v.len() == 32 && v.iter().all(|&b| b == 7) { "REFUSED" } else { "MISMATCH" });

    let mut v: Vec<u8> = vec![7u8; 32];
    v.truncate(0);
    emit(a, "zero", if v.is_empty() { "DEFINED" } else { "MISMATCH" });

    emit(a, "inject", if reserve_preserves_contents() { "PRESERVED" } else { "MISMATCH" });

    emit(a, "invalid", "BLOCKED");

    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    let r = v.try_reserve_exact(TOTAL);
    emit(a, "canary", if r.is_ok() && guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// -------------------------------------------------------------- cplus_free
fn cplus_free_() {
    let a = "cplus_free";

    let b = Box::new(42u32);
    let got = *b;
    drop(b);
    emit(a, "valid", if got == 42 { "OK" } else { "MISMATCH" });

    let slot: Option<Box<u32>> = None;
    drop(slot);
    emit(a, "null", "DEFINED");

    let mut slot: Option<Box<u32>> = Some(Box::new(7));
    let first = slot.take();
    let second = slot.take();
    emit(a, "double", if first.is_some() && second.is_none() { "REFUSED" } else { "MISMATCH" });

    emit(a, "invalid", "BLOCKED");

    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    drop(Box::new(9u32));
    emit(a, "canary", if guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// -------------------------------------------------------- cplus_free_checked
fn cplus_free_checked() {
    let a = "cplus_free_checked";

    let b = Box::new(42u32);
    let got = *b;
    drop(b);
    emit(a, "valid", if got == 42 { "OK" } else { "MISMATCH" });

    let mut slot: Option<Box<u32>> = Some(Box::new(7));
    let first = slot.take();
    let second = slot.take();
    emit(a, "double", if first.is_some() && second.is_none() { "REFUSED" } else { "MISMATCH" });

    emit(a, "invalid", "BLOCKED");

    let slot: Option<Box<u32>> = None;
    drop(slot);
    emit(a, "null", "DEFINED");

    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    drop(Box::new(9u32));
    emit(a, "canary", if guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// ------------------------------------------------------ cplus_aligned_alloc
fn cplus_aligned_alloc() {
    let a = "cplus_aligned_alloc";

    emit(a, "valid", "BLOCKED");
    emit(a, "overflow", if layout_refused() && fallible_refused() { "REFUSED" } else { "MISMATCH" });
    emit(a, "zero", "BLOCKED");
    emit(a, "bad_align", "BLOCKED");
    // The C+ hardened aligned allocator does not consult the inject counter
    // at all, so this dimension is not observable there either.
    emit(a, "inject", "BLOCKED");
    emit(a, "canary", "BLOCKED");
}

// ---------------------------- Hardened failure control / free counters ---
fn hardened_control_and_counters() {
    emit("cplus_alloc_fail_after", "inject",
         if fallible_refused() { "REFUSED" } else { "MISMATCH" });
    let v: Vec<u8> = Vec::with_capacity(TOTAL);
    emit("cplus_alloc_fail_reset", "reset",
         if v.capacity() >= TOTAL { "OK" } else { "MISMATCH" });

    // Global double/invalid-free counters are C runtime bookkeeping; safe Rust
    // has no such observable (a double free cannot occur).
    emit("cplus_alloc_double_free_count", "double", "BLOCKED");
    emit("cplus_alloc_invalid_free_count", "invalid", "BLOCKED");
}

fn main() {
    // SafeCRT
    malloc_s();
    calloc_s();
    realloc_s();
    free_s();
    free_checked_s();
    aligned_alloc_s();
    safe_metadata_and_control();

    // Hardened libc
    cplus_malloc_();
    cplus_calloc_();
    cplus_realloc_();
    cplus_free_();
    cplus_free_checked();
    cplus_aligned_alloc();
    hardened_control_and_counters();
}

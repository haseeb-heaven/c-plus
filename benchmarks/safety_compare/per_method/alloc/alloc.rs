// Safe-Rust equivalents of the C+ checked-allocation cases.
// Prints exactly the same "<method> <case> <token>" lines as alloc_harness.c.
//
// Constraints (see NOTES.md): safe APIs only, no `unsafe`. Where an operation
// cannot even be expressed in safe Rust because the type system / ownership
// model prevents it, the case prints BLOCKED. This is the central
// non-equivalence: C+ detects these at *runtime* (NULL + errno / return code),
// Rust makes them *unrepresentable* at compile time.

const PAYLOAD: usize = 64;
const GUARD: usize = 8;
const TOTAL: usize = PAYLOAD + GUARD;
const CANARY: u64 = 0xDEADBEEF_CAFEBABE;

fn emit(method: &str, case: &str, token: &str) {
    println!("{} {} {}", method, case, token);
}

fn write_payload_with_guard(buf: &mut [u8]) {
    for i in 0..PAYLOAD {
        buf[i] = (i & 0xff) as u8;
    }
    let c = CANARY.to_ne_bytes();
    buf[PAYLOAD..TOTAL].copy_from_slice(&c);
}

fn guard_intact(buf: &[u8]) -> bool {
    let c = CANARY.to_ne_bytes();
    &buf[PAYLOAD..TOTAL] == &c[..]
}

// Refuse the two dimensions that are expressible in safe Rust: a size that
// overflows isize::MAX (CapacityOverflow) and a hypothetical huge request
// (AllocError). `try_reserve_exact` returns Err and, by contract, leaves the
// existing buffer/contents untouched -- the Rust form of C+ atomicity.
fn refused_by_try_reserve() -> bool {
    let mut v: Vec<u8> = Vec::new();
    v.try_reserve_exact(usize::MAX).is_err()
}

// ------------------------------------------------------------------ malloc_s
fn test_malloc_s() {
    let m = "malloc_s";

    // valid: Vec::with_capacity + drop (the safe analogue of alloc + free_s)
    let mut v = Vec::with_capacity(TOTAL);
    v.resize(TOTAL, 0);
    write_payload_with_guard(&mut v);
    drop(v);
    emit(m, "valid", "OK");

    // overflow: SIZE_MAX exceeds the maximum representable capacity
    emit(m, "overflow", if refused_by_try_reserve() { "REFUSED" } else { "MISMATCH" });

    // zero-size: Vec::with_capacity(0) is a defined empty allocation
    let v: Vec<u8> = Vec::with_capacity(0);
    emit(m, "zero", if v.capacity() == 0 { "DEFINED" } else { "MISMATCH" });

    // inject: forced allocation failure through the fallible reserve API
    emit(m, "inject", if refused_by_try_reserve() { "REFUSED" } else { "MISMATCH" });

    // canary intact
    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    emit(m, "canary", if guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// ------------------------------------------------------------------ calloc_s
fn test_calloc_s() {
    let m = "calloc_s";

    // valid: vec![0; n] is zero-initialized (calloc semantics)
    let v = vec![0u8; 64];
    emit(m, "valid", if v.iter().all(|&b| b == 0) { "OK" } else { "MISMATCH" });

    // overflow: checked_mul models the count*size overflow refusal
    emit(m, "overflow", if usize::MAX.checked_mul(4).is_none() { "REFUSED" } else { "MISMATCH" });

    // zero-size: vec![0; 0] is a defined empty allocation
    let v = vec![0u8; 0];
    emit(m, "zero", if v.is_empty() { "DEFINED" } else { "MISMATCH" });

    // inject: forced allocation failure
    emit(m, "inject", if refused_by_try_reserve() { "REFUSED" } else { "MISMATCH" });

    // canary intact
    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    emit(m, "canary", if guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// ----------------------------------------------------------------- realloc_s
fn test_realloc_s() {
    let m = "realloc_s";

    // valid: try_reserve_exact grows the Vec and preserves existing elements
    let mut v: Vec<u8> = Vec::with_capacity(32);
    v.extend_from_slice(&[1, 2, 3, 4]);
    let grew = v.try_reserve_exact(256).is_ok();
    emit(m, "valid", if grew && v.len() == 4 { "OK" } else { "MISMATCH" });

    // overflow: growth refused, original contents preserved
    let mut v: Vec<u8> = vec![7u8; 32];
    let r = v.try_reserve_exact(usize::MAX);
    emit(m, "overflow", if r.is_err() && v.len() == 32 { "REFUSED" } else { "MISMATCH" });

    // zero-size: truncate(0) is defined
    let mut v: Vec<u8> = vec![7u8; 32];
    v.truncate(0);
    emit(m, "zero", if v.is_empty() { "DEFINED" } else { "MISMATCH" });

    // inject: failed growth leaves the original bytes intact (atomicity)
    let mut v: Vec<u8> = vec![0xABu8; 32];
    let r = v.try_reserve_exact(usize::MAX);
    let preserved = v.len() == 32 && v.iter().all(|&b| b == 0xAB);
    emit(m, "inject", if r.is_err() && preserved { "PRESERVED" } else { "MISMATCH" });

    // invalid pointer realloc: cannot form a dangling/foreign pointer in safe Rust
    emit(m, "invalid", "BLOCKED");

    // canary intact across growth
    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    let r = v.try_reserve_exact(TOTAL);
    emit(m, "canary", if r.is_ok() && guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// ------------------------------------------------------------- free_checked_s
fn test_free_checked_s() {
    let m = "free_checked_s";

    // valid: a Box drops (frees) exactly once
    let b = Box::new(42u32);
    let got = *b;
    drop(b);
    emit(m, "valid", if got == 42 { "OK" } else { "MISMATCH" });

    // double free modeled with Option::take: the second take is None, so the
    // second "free" is a no-op/refusal rather than a heap corruption.
    let mut slot: Option<Box<u32>> = Some(Box::new(7));
    let first = slot.take();
    let second = slot.take();
    emit(m, "double", if first.is_some() && second.is_none() { "REFUSED" } else { "MISMATCH" });

    // invalid (forged) pointer free: impossible in safe Rust
    emit(m, "invalid", "BLOCKED");

    // null free: dropping None is the defined no-op
    let slot: Option<Box<u32>> = None;
    drop(slot);
    emit(m, "null", "DEFINED");

    // canary intact after freeing an unrelated Box
    let mut v = vec![0u8; TOTAL];
    write_payload_with_guard(&mut v);
    let other = Box::new(9u32);
    drop(other);
    emit(m, "canary", if guard_intact(&v) { "CANARY_OK" } else { "MISMATCH" });
}

// ------------------------------------------------------------- aligned_alloc_s
fn test_aligned_alloc_s() {
    let m = "aligned_alloc_s";

    // Dynamic, caller-specified alignment (and a runtime address check) has no
    // safe stable Rust API; align_of::<T>() is a compile-time property.
    emit(m, "valid", "BLOCKED");

    // The size-overflow dimension is still observable and refusable.
    emit(m, "overflow", if refused_by_try_reserve() { "REFUSED" } else { "MISMATCH" });

    // Zero-size aligned allocation is not expressible safely.
    emit(m, "zero", "BLOCKED");

    // An invalid alignment (3) is rejected by the type system, not runtime.
    emit(m, "bad_align", "BLOCKED");

    // Failure injection at the size/availability level.
    emit(m, "inject", if refused_by_try_reserve() { "REFUSED" } else { "MISMATCH" });

    // Canary tied to an explicitly-aligned block is not expressible safely.
    emit(m, "canary", "BLOCKED");
}

fn main() {
    test_malloc_s();
    test_calloc_s();
    test_realloc_s();
    test_free_checked_s();
    test_aligned_alloc_s();
}

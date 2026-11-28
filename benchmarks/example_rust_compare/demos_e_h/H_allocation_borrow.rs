// Safe-Rust equivalent of demos/H_allocation_borrow.c.
//
// Observable behavior:  len=4 total=100
//
// No unsafe and no FFI.  The managed allocation is a `Vec<i32>` (owned heap
// buffer with a tracked length); the borrowed view is a shared slice `&[i32]`.
// The allocation is released automatically at scope exit by Vec's Drop.
//
// MECHANISM NON-EQUIVALENCE: C+ stores the length as *runtime allocator
// metadata* on a `managed int *` (`values.len` is read from the runtime's
// bookkeeping, and release happens via cplus_release/scope cleanup).  Rust
// stores the length in the `Vec` value itself and the borrow is checked
// statically.  The observable stdout is identical; the mechanisms are not.

fn sum(p: &[i32]) -> i32 {
    let mut total = 0;
    for &v in p {
        total += v;
    }
    total
}

fn main() {
    let mut values: Vec<i32> = vec![0; 4];

    for i in 0..4 {
        values[i] = ((i + 1) * 10) as i32;
    }

    let borrow: &[i32] = &values; // borrowed view, valid only while owner lives
    // The original creates the borrow before the writes (C+ permits this
    // because its borrow checking is advisory here); safe Rust requires the
    // shared borrow to start after the exclusive writes end.

    // The original reads `values.len` from metadata; Rust reads Vec::len().
    println!("len={} total={}", borrow.len(), sum(borrow));
}

// Safe-Rust equivalent of demos/B_bounds.c (runtime-detected out-of-bounds).
//
// The C+ demo uses a `volatile` index so the bad value is only known at run
// time; safe C+ emits "C+ error: index 9 is outside the bounds of 'samples'
// (length 4)" and aborts (SIGABRT, exit 134).  Idiomatic safe Rust expresses
// the same "fail before reaching the final line" behavior by indexing a
// fixed-size array: the bounds check panics (exit 101) with Rust's own
// diagnostic.  The diagnostic wording and exit status necessarily differ;
// see RESULTS.md for the exact byte-level comparison.
fn main() {
    let samples: [i32; 4] = [10, 20, 30, 40];
    // black_box keeps the bad index opaque to compile-time analysis, mirroring
    // the C+ `volatile` index (a violation visible only at run time).
    let bad_index: usize = std::hint::black_box(9);

    println!("valid: {}", samples[2]);
    // Safe Rust inserts the bounds check here; execution must not continue.
    println!("invalid: {}", samples[bad_index]);
    println!("this line must not be reached");
}

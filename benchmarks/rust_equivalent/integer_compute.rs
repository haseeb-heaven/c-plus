//! Rust equivalent of `benchmarks/integer_compute.c`.
//!
//! Deterministic stdout: `integer 6299863613973285121`

const ITERATIONS: usize = 100_000_000;

fn main() {
    let mut x: u64 = 1;
    for _ in 0..ITERATIONS {
        // C's unsigned overflow is defined modulo 2^64. Explicit wrapping ops
        // keep debug (-O0, overflow checks on) and release (-O) identical.
        x = x
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
    }
    println!("integer {}", x);
}

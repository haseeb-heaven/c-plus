//! Rust equivalent of `benchmarks/function_call.c`.
//!
//! Deterministic stdout: `call 2560`

const ITERATIONS: usize = 100_000_000;

// Mirrors `__attribute__((noinline))`: keep the call real, as in the C build.
#[inline(never)]
fn step(x: i32) -> i32 {
    x * 3 + 1
}

fn main() {
    let mut v: i32 = 0;
    for _ in 0..ITERATIONS {
        v = step(v & 1023);
    }
    println!("call {}", v);
}

// Paired benchmark: non-inlined function call overhead (Rust side).
use std::hint::black_box;

const ITERATIONS: i64 = 200_000_000;

#[inline(never)]
fn step(x: u64) -> u64 {
    x.wrapping_mul(6_364_136_223_846_793_005)
        .wrapping_add(1_442_695_040_888_963_407)
}

fn main() {
    let mut x = black_box(1u64);
    for _ in 0..ITERATIONS {
        x = step(x);
    }
    println!("call_chain {}", x);
}

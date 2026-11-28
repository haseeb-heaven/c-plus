// Paired benchmark: raw 64-bit LCG recurrence (Rust side of pairs/lcg.cp).
use std::hint::black_box;

const ITERATIONS: i64 = 200_000_000;

fn main() {
    let mut x = black_box(1u64); // stops constant folding
    for _ in 0..ITERATIONS {
        x = x
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
    }
    println!("lcg {}", x);
}

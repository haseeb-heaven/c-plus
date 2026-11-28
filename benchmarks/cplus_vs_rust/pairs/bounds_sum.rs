// Paired benchmark: bounds-checked table lookup (Rust side).
use std::hint::black_box;

const SLOTS: usize = 16;
const ITERATIONS: i64 = 200_000_000;

fn main() {
    let seed = black_box(3usize);
    let mut values = [0i64; SLOTS];
    for (i, slot) in values.iter_mut().enumerate() {
        *slot = (i + 3) as i64;
    }
    let mut total: i64 = 0;
    for _ in 0..ITERATIONS {
        total += values[seed & (SLOTS - 1)];
    }
    println!("bounds_sum {}", total);
}

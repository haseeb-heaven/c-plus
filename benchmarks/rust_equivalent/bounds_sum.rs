//! Rust equivalent of `benchmarks/bounds_sum.c`.
//!
//! Deterministic stdout: `bounds_sum 1350000000`

const SLOTS: usize = 16;
const ITERATIONS: usize = 60_000_000;

fn main() {
    let mut values = [0i64; SLOTS];
    for (i, v) in values.iter_mut().enumerate() {
        *v = (i as i64) * 3;
    }

    let mut total: i64 = 0;
    for i in 0..ITERATIONS {
        total += values[i & (SLOTS - 1)];
    }

    // C: sink = (int)(total & 0x7fffffff)
    let sink: i32 = (total & 0x7fff_ffff) as i32;
    println!("bounds_sum {}", sink);
}

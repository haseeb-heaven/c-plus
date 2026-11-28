// Rust equivalent of benchmarks/safety_compare/bench_modern.cp — the
// deterministic, PRNG-driven modern-stdlib workload (owned containers).
//
// Same operations, sizes, seed and checksum as the C+ harness:
//   * Vec<i32>:   N = 2,000,000 pushes of a fixed-seed xorshift32 stream masked
//     to 31 bits, then N bounds-checked reads at pseudo-random in-range indices,
//     summed into vsum.
//   * String:     start from "a"; M = 200,000 appends of random 1..4-letter
//     lowercase chunks, adding size() after each append into ssize_sum.
//   * checksum = vsum ^ (ssize_sum << 17) ^ N ^ (M << 32) (u64).
//   * stdout: CSV header + one data row, same field order as the C+ harness.
//
// Determinism: BENCH_SEED is fixed (0x9E3779B9) and never derived from time,
// so every run consumes the identical PRNG sequence and prints the same
// checksum. No RNG state is shared with the external timing harness.
//
// Build: rustc -O benchmarks/safety_compare/bench_modern.rs -o <out>
use std::hint::black_box;
use std::time::Instant;

const N: u32 = 2_000_000;
const M: u64 = 200_000;

/// Fixed seed from bench_modern.cp (golden-ratio increment). Never time-based.
const BENCH_SEED: u32 = 0x9E37_79B9;

/// xorshift32, byte-for-byte the same transition as next_u32() in bench_modern.cp.
struct Xorshift32 {
    state: u32,
}

impl Xorshift32 {
    fn new(seed: u32) -> Self {
        Self { state: seed }
    }

    fn next_u32(&mut self) -> u32 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        self.state = x;
        x
    }
}

fn main() {
    let mut rng = Xorshift32::new(BENCH_SEED);
    let mut vsum: i64 = 0;
    let mut ssize_sum: i64 = 0;
    let mut v: Vec<i32> = Vec::new();
    let mut s = String::from("a"); // constructed before the timed phases, as in C+

    let t0 = Instant::now();
    for _ in 0..N {
        let val = (rng.next_u32() & 0x7FFF_FFFF) as i32;
        v.push(val);
    }
    for _ in 0..N {
        let idx = (rng.next_u32() % N) as usize;
        vsum += v[idx] as i64;
    }
    let t1 = Instant::now();

    for _ in 0..M {
        let r = (rng.next_u32() % 4) + 1;
        let mut chunk = String::with_capacity(r as usize);
        for _ in 0..r {
            let c = (b'a' + (rng.next_u32() % 26) as u8) as char;
            chunk.push(c);
        }
        s.push_str(&chunk);
        ssize_sum += s.len() as i64;
    }
    let t2 = Instant::now();

    let vector_us = t1.duration_since(t0).as_secs_f64() * 1_000_000.0;
    let string_us = t2.duration_since(t1).as_secs_f64() * 1_000_000.0;
    let total_us = t2.duration_since(t0).as_secs_f64() * 1_000_000.0;

    let checksum: u64 = (vsum as u64)
        ^ ((ssize_sum as u64) << 17)
        ^ N as u64
        ^ (M << 32);

    println!("checksum,vector_us,string_us,total_us");
    println!(
        "{},{:.3},{:.3},{:.3}",
        black_box(checksum),
        vector_us,
        string_us,
        total_us
    );
}

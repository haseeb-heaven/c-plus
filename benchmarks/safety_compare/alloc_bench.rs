// Rust allocation-throughput comparison workload (safety_compare).
//
// Counterpart of alloc_bench.cp: identical operation count and identical
// value-based checksum. Each of the N = 200,000 iterations performs exactly
// one heap allocation (Vec<u8> of size 8..64) and one free (drop):
//
//   sz   = 8 + (i % 8) * 8        (8, 16, ..., 64 bytes)
//   byte = (i * 31 + 7) & 0xff
//   p    = vec![0u8; sz]           (one allocation)
//   checksum = checksum * 33 + sz + byte     (wrapping mod 2^64)
//   drop(p)                        (one free)
//
// black_box(&p) makes the allocation observable so LLVM cannot elide the
// allocate/free pair; it is the Rust equivalent of the C+ volatile sink.
// The timed region is just the loop; printing is outside it.
//
// Build (from the repo root):
//   rustc -O benchmarks/safety_compare/alloc_bench.rs \
//     -o build/bench_safety/rust_alloc_bench

use std::hint::black_box;
use std::time::Instant;

const N: usize = 200_000;

fn main() {
    let mut checksum: u64 = 0;
    let t0 = Instant::now();
    for i in 0..N {
        let sz = 8 + (i % 8) * 8;
        let byte = ((i as u64).wrapping_mul(31).wrapping_add(7)) & 0xff;
        let p = vec![0u8; sz];
        black_box(&p);
        checksum = checksum
            .wrapping_mul(33)
            .wrapping_add(sz as u64)
            .wrapping_add(byte);
        drop(p);
    }
    let elapsed = t0.elapsed();
    println!(
        "checksum={} elapsed_s={:.9} ops={}",
        black_box(checksum),
        elapsed.as_secs_f64(),
        N
    );
}

// Same workload as bench.cp, compiled with rustc -O.
use std::time::Instant;
fn main() {
    const N: i64 = 2_000_000;
    let mut x: i64 = 1; let mut y: i64 = 2; let mut acc: u64 = 0;
    let t0 = Instant::now();
    for i in 0..N {
        x = (x * 3 + y) & 0xffffff;
        y = (y + i * 7) & 0xffffff;
        acc = acc.wrapping_add((x ^ y) as u64);
    }
    println!("RUST {} {}", t0.elapsed().as_nanos(), acc);
    if acc == 0 { std::process::exit(1); }
}

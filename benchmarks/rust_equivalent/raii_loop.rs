//! Rust equivalent of `benchmarks/raii_loop.c`.
//!
//! Deterministic stdout: `raii_loop 125000000 0`
//!
//! The C+ build relies on an automatically inserted destructor; Rust's `Drop`
//! is the direct equivalent. The plain-C build calls `Acc_drop` by hand.

use std::sync::atomic::{AtomicI32, Ordering};

const ITERATIONS: usize = 25_000_000;

struct Acc {
    total: i32,
}

static DESTROYED: AtomicI32 = AtomicI32::new(0);

impl Drop for Acc {
    fn drop(&mut self) {
        if self.total == -1 {
            DESTROYED.fetch_add(1, Ordering::Relaxed);
        }
    }
}

fn acc_make(seed: i32) -> Acc {
    Acc { total: seed }
}

fn main() {
    let mut sum: i64 = 0;
    for i in 0..ITERATIONS {
        let mut a = acc_make((i & 7) as i32);
        a.total += (i & 3) as i32;
        sum += a.total as i64;
        // `a` is dropped automatically here (RAII), invoking `Acc::drop`.
    }
    println!("raii_loop {} {}", sum, DESTROYED.load(Ordering::Relaxed));
}

//! Rust equivalent of `benchmarks/alloc_churn.c` (plain malloc/free variant).
//!
//! Deterministic stdout: `alloc_churn 9000021000000`

use std::hint::black_box;

const ROUNDS: usize = 3_000_000;
const RUNTIME_SEED: i32 = 7;

struct Node {
    value: i32,
    next: Option<Box<Node>>,
}

fn main() {
    let mut total: i64 = 0;

    for r in 0..ROUNDS {
        // Two small heap objects per round, linked a -> b, then released.
        // Owning `b` through `a.next` means dropping `a` frees both, which is
        // the safe-Rust equivalent of the C build's free(b) then free(a).
        let b = Box::new(Node {
            value: (r as i32) + 1,
            next: None,
        });
        let a = Box::new(Node {
            value: (r as i32) + RUNTIME_SEED,
            next: Some(b),
        });

        total += a.value as i64 + a.next.as_ref().unwrap().value as i64;

        // `black_box` mirrors the `volatile escaped` sink: it makes the
        // allocation observable so the optimizer cannot fold the loop away.
        // Ownership ends here, so `a` and its linked `b` are freed.
        let _ = black_box(a);
    }

    println!("alloc_churn {}", total);
}

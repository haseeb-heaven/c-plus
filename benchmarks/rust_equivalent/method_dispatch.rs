//! Rust equivalent of `benchmarks/method_dispatch.c`.
//!
//! Deterministic stdout: `method_dispatch 75000000`

const ITERATIONS: usize = 50_000_000;

struct Counter {
    value: i32,
}

impl Counter {
    // C+ lowers `counter.add(x)` to `Counter_add(&counter, x)`, the call the
    // plain-C build writes by hand. Rust's `&mut self` method is equivalent.
    fn add(&mut self, amount: i32) {
        self.value += amount;
    }

    fn get(&self) -> i32 {
        self.value
    }
}

fn main() {
    let mut counter = Counter { value: 0 };
    for i in 0..ITERATIONS {
        counter.add((i & 3) as i32);
    }
    println!("method_dispatch {}", counter.get());
}

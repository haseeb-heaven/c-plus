// Safe-Rust equivalent of demos/F_concurrency.c.
//
// Observable behavior: three threads each perform 50,000 locked increments of
// a shared counter; the final value is printed.  Result is deterministic:
// counter=150000.
//
// No unsafe and no FFI.  `Arc<Mutex<i64>>` makes the shared access legal by
// construction, and `MutexGuard` is the RAII lock release that C+ models with
// its `Guard` resource + compiler-inserted `Guard_drop`.  The original's own
// diagnostics warn that its static checker cannot yet prove every shared
// mutation is locked; Rust's type system enforces it at compile time instead.

use std::sync::{Arc, Mutex, MutexGuard};
use std::thread;

/// Owning lock guard: unlocks on scope exit, mirroring C+ `Guard`/`Guard_drop`.
struct Guard<'a> {
    mutex: &'a Mutex<i64>,
    held: MutexGuard<'a, i64>,
}

impl Guard<'_> {
    fn increment(&mut self) {
        *self.held += 1;
    }
}

impl Drop for Guard<'_> {
    fn drop(&mut self) {
        // Releasing `held` unlocks the mutex; `mutex` is retained only to make
        // the owned relationship explicit.  No output, matching Guard_drop.
        let _ = self.mutex;
    }
}

fn worker(counter: Arc<Mutex<i64>>) {
    for _ in 0..50_000 {
        let mut guard = Guard {
            mutex: &counter,
            held: counter.lock().unwrap(),
        };
        guard.increment();
        // guard drops here -> deterministic unlock, even on early exit.
    }
}

fn main() {
    let counter = Arc::new(Mutex::new(0i64));
    let mut threads = Vec::new();
    for _ in 0..3 {
        let c = Arc::clone(&counter);
        threads.push(thread::spawn(move || worker(c)));
    }
    for t in threads {
        t.join().expect("worker panicked");
    }
    println!("counter={}", *counter.lock().unwrap());
}

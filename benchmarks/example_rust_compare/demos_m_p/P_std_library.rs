// Safe-Rust model of demos/P_std_library.cp.
//
// The C+ stdlib is a set of .hp wrappers lowered over libc/POSIX. Idiomatic
// Rust supplies the same roles with standard-library types:
//   string -> String, vector -> Vec, mutex/guard -> Mutex/MutexGuard.
// `print(...)` becomes a space-separated println, and `println()` a bare
// newline. No `unsafe`, no FFI.

use std::sync::Mutex;

macro_rules! cprint {
    ($($arg:expr),* $(,)?) => {
        // C+'s type-generic print(): space-separated values + one newline.
        println!("{}", [$($arg.to_string()),*].join(" "))
    };
}

fn main() {
    let name = String::from("C+");
    let mut scores: Vec<i32> = Vec::new();
    scores.push(10);
    scores.push(20);

    cprint!("language", name.as_str(), "scores", scores.len(), scores[1]);

    let lock = Mutex::new(());
    {
        let _held = lock.lock().unwrap(); // C+ `guard held = guard(&lock);`
        println!(); // C+ `println();`
    }
}

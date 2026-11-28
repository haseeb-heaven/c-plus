// Safe-Rust model of demos/N_range_for.c (range-based for).
//
// C+ lowers `for (auto v : arr)` to a counted loop bounded by the array's
// known length, inserting `__cplus_bounds_check` on each element access.
// Rust's iterator `for` loop over a fixed-size array / owned Vec expresses
// the same iteration with bounds safety guaranteed by the type system.
// No `unsafe`, no FFI.

#[allow(dead_code)] // the original declares this class but never uses it
struct Player {
    score: i32,
}

macro_rules! cprint {
    ($($arg:expr),* $(,)?) => {
        // C+'s type-generic print(): space-separated values + one newline.
        println!("{}", [$($arg.to_string()),*].join(" "))
    };
}

fn main() {
    let scores = [10, 20, 30];
    let mut total = 0;
    for &value in &scores {
        total += value;
    }
    cprint!("total", total);

    // `managed int *buffer = cplus_new_array(int, 3);` becomes an owned Vec
    // with a tracked length. The original range-for body is only `(void)i;`,
    // so it has no observable effect; the length print is the observable part.
    let buffer = vec![0i32; 3];
    for _i in &buffer {
        // no observable effect
    }
    cprint!("buffer.len", buffer.len());
}

// Safe-Rust model of demos/M_default_args.c (C+ default arguments).
//
// The C+ front end lowers `get_data()` to `get_data(0)`, `sum(1)` to
// `sum(1, 2, 3)` and `sum(1, 5)` to `sum(1, 5, 3)`. Rust has no default
// arguments, so the omitted arguments are modeled explicitly with `Option`
// plus two call-site helpers. No `unsafe`, no FFI.

macro_rules! cprint {
    ($($arg:expr),* $(,)?) => {
        // C+'s type-generic print(): space-separated values + one newline.
        println!("{}", [$($arg.to_string()),*].join(" "))
    };
}

static DATA: [i32; 4] = [10, 20, 30, 40];

// Mirrors `int *get_data(size_t size = 0)`. The lowered call passes 0, which
// is normalised to 1; the function still returns the same static array.
fn get_data(size: Option<usize>) -> &'static [i32; 4] {
    let mut size = size.unwrap_or(0);
    if size == 0 {
        size = 1;
    }
    let _ = size; // computed in the original, but not used to select data
    &DATA
}

fn get_data_default() -> &'static [i32; 4] {
    get_data(None)
}

// Mirrors `int sum(int a, int b = 2, int c = 3)`.
fn sum(a: i32, b: Option<i32>, c: Option<i32>) -> i32 {
    a + b.unwrap_or(2) + c.unwrap_or(3)
}

fn sum_one(a: i32) -> i32 {
    sum(a, None, None)
}

fn sum_two(a: i32, b: i32) -> i32 {
    sum(a, Some(b), None)
}

fn main() {
    let values = get_data_default();
    cprint!(
        "first",
        values[0],
        "default",
        sum_one(1),
        "partial",
        sum_two(1, 5)
    );
}

// Safe-Rust model of demos/O_extensions.cp + demos/O_extensions.hp.
//
// The .hp header declares `class Counter` with a constructor and methods and
// is lowered to plain C by the C+ compiler. Rust has no separate header or
// `class` keyword: a struct plus an `impl` block models the same constructor
// (Counter(10)), mutable method (add(5)) and getter (get()). No unsafe, no FFI.

macro_rules! cprint {
    ($($arg:expr),* $(,)?) => {
        // C+'s type-generic print(): space-separated values + one newline.
        println!("{}", [$($arg.to_string()),*].join(" "))
    };
}

struct Counter {
    value: i32,
}

impl Counter {
    fn new(start: i32) -> Self {
        Self { value: start }
    }

    fn add(&mut self, n: i32) {
        self.value += n;
    }

    fn get(&self) -> i32 {
        self.value
    }
}

fn main() {
    let mut counter = Counter::new(10);
    counter.add(5);
    cprint!("counter", counter.get());
}

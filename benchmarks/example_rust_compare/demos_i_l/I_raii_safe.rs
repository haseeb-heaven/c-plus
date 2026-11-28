// Safe-Rust equivalent of demos/I_raii_safe.c.
//
// Models the observable behavior of the C+ `resource struct Mark` plus its
// `Mark_drop` cleanup. In C+ the compiler injects Mark_drop(&m) at every scope
// exit, including the `continue` path and the early `return` path. Rust has no
// `resource` qualifier; the idiomatic analogue is the `Drop` trait, whose
// `drop` method the compiler calls when the value leaves scope (also on
// `continue` and on `return`). The printed drop order is therefore identical.
//
// rustc -O I_raii_safe.rs -o I_raii_safe

struct Mark {
    id: i32,
}

impl Drop for Mark {
    fn drop(&mut self) {
        if self.id != 0 {
            println!("drop {}", self.id);
            self.id = 0;
        }
    }
}

fn mark_make(id: i32) -> Mark {
    Mark { id }
}

fn early(id: i32) -> i32 {
    let m = mark_make(id);
    if id == 2 {
        return -1; // Rust runs Drop on m before returning, like C+ RAII.
    }
    let _ = &m; // mirror the C `(void)m;`, and keep m bound until scope exit
    id
}

fn main() {
    for i in 1..=3 {
        let m = mark_make(i);
        if i == 2 {
            continue; // Rust runs Drop on m before the next iteration.
        }
        let _ = &m; // mirror the C `(void)m;`
    }
    println!("early={}", early(2));
}

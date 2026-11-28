// Safe-Rust equivalent of demos/L_class_lifecycle.c.
//
// Models the observable behavior of the C+ `class Point` whose constructor
// lowers to Point_ctor() and whose `drop` method lowers to Point_drop(), called
// automatically when `p` leaves scope. Rust has no C-style ctor/dtor: the
// idiomatic analogue is an ordinary associated function `Point::new` plus the
// `Drop` trait hook. The two printed lines ("total 10", then "drop point 10")
// and their order are identical.
//
// rustc -O L_class_lifecycle.rs -o L_class_lifecycle

struct Point {
    x: i32,
    y: i32,
}

impl Point {
    fn new(a: i32, b: i32) -> Self {
        Point { x: a, y: b }
    }

    fn sum(&self) -> i32 {
        self.x + self.y
    }
}

impl Drop for Point {
    fn drop(&mut self) {
        println!("drop point {}", self.sum());
    }
}

fn main() {
    let p = Point::new(4, 6);
    println!("total {}", p.sum());
    // Rust calls <Point as Drop>::drop(&p) here, at scope exit, like C+.
}

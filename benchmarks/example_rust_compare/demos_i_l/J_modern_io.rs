// Safe-Rust equivalent of demos/J_modern_io.c.
//
// Models the observable behavior of C+'s format-free `print`/`scan`:
//   * print("literal") -> the literal on its own line (a trailing newline).
//   * print(k, v, k, v, ...) -> each argument printed in order, separated by a
//     single space, with a trailing newline; values are dispatched by type.
//   * scan(count, ratio, label) -> reads an int, a double and a bounded token
//     from stdin by type, with addresses taken internally (lvalue syntax).
//   * values.len is a C+ language-level array length property.
//
// Mechanism differences (recorded honestly): Rust has no variadic
// type-dispatching `print`/`scan`; `println!` uses a format string, stdin is
// parsed explicitly from whitespace-separated tokens, and array length is the
// `.len()` method rather than a language property. The byte-for-byte output
// for the deterministic input "7 2.5 demo\n" is nevertheless identical.
//
// rustc -O J_modern_io.rs -o J_modern_io

use std::io::Read;

fn main() {
    let mut input = String::new();
    std::io::stdin()
        .read_to_string(&mut input)
        .expect("read stdin");
    let tokens: Vec<&str> = input.split_whitespace().collect();

    let count: i32 = tokens[0].parse().expect("count is an int");
    let ratio: f64 = tokens[1].parse().expect("ratio is a double");
    let label: &str = tokens[2]; // bounded token, as C+ scan reads a char array
    let values = [10i32, 30, 50];

    println!("enter count ratio label:");
    println!(
        "count= {} ratio= {} label= {} values.len= {}",
        count,
        ratio,
        label,
        values.len()
    );
}

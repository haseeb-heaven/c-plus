use std::hint::black_box;
fn main() {
    let mut acc: i64 = 0;
    for i in 0..4000i64 { for j in 0..4000i64 { acc += i * j - j * i + 1; } }
    println!("{}", black_box(acc));
}

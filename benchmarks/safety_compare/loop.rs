use std::hint::black_box;
fn main() {
    let mut sum: i64 = 0;
    for i in 0..200_000_000i64 { sum += i; }
    println!("{}", black_box(sum));
}

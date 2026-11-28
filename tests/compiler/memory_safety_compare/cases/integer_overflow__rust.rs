fn main() {
    let a = std::hint::black_box(i32::MAX);
    let b = std::hint::black_box(1);
    println!("{}", a + b);
}

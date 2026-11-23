fn main() {
    let n = std::hint::black_box(32u32);
    println!("{}", 1i32 << n);
}

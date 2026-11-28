fn main() {
    let v = vec![10, 20, 30, 40];
    let idx = std::hint::black_box(7usize);
    println!("{}", v[idx]);
}

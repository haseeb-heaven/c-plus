// Rust equivalent of modern.cp: owned Vec + owned String doing the same work.
// rustc -O benchmarks/safety_compare/modern.rs -o <out>
use std::hint::black_box;

fn main() {
    let n = 2_000_000i32;
    let m = 200_000i32;

    let mut v: Vec<i32> = Vec::new();
    for i in 0..n {
        v.push(i);
    }
    let mut vsum: i64 = 0;
    for i in 0..n {
        vsum += v[i as usize] as i64;
    }

    let mut s = String::from("a");
    let mut ssize_sum: i64 = 0;
    for _ in 0..m {
        s.push_str("bbbbbbbbbb");
        ssize_sum += s.len() as i64;
    }

    println!("{}", black_box(vsum + ssize_sum));
}

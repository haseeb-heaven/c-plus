// Safe-Rust model of examples/utilities/text_tools/twc.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 twc.rs -o twc
use std::fs;

const MAX_BYTES: usize = 65536;

fn is_space(c: u8) -> bool {
    matches!(c, b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 2 {
        println!("TWC error usage");
        std::process::exit(2);
    }
    let data = match fs::read(&args[1]) {
        Ok(d) => d,
        Err(_) => {
            println!("TWC error cannot-open");
            std::process::exit(1);
        }
    };
    if data.len() > MAX_BYTES {
        println!("TWC refuse oversize");
        std::process::exit(1);
    }
    let mut lines: i64 = 0;
    let mut words: i64 = 0;
    let mut total: i64 = 0;
    let mut inword = false;
    let mut last = b'\n';
    for &c in &data {
        total += 1;
        if c == b'\n' {
            lines += 1;
        }
        if is_space(c) {
            inword = false;
        } else if !inword {
            words += 1;
            inword = true;
        }
        last = c;
    }
    if total > 0 && last != b'\n' {
        lines += 1;
    }
    println!("TWC {} {} {}", lines, words, total);
}

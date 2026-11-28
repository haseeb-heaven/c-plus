// Safe-Rust model of examples/utilities/text_tools/thead.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 thead.rs -o thead
use std::fs;

const LINE_CAP: usize = 256;
const MAX_N: i64 = 1000;

fn parse_n(text: &str) -> Option<usize> {
    if text.is_empty() {
        return None;
    }
    let bytes = text.as_bytes();
    let mut i = 0usize;
    let neg = bytes[0] == b'-';
    if neg || bytes[0] == b'+' {
        i += 1;
    }
    if i >= bytes.len() {
        return None;
    }
    let mut v: i64 = 0;
    while i < bytes.len() {
        let c = bytes[i];
        if c < b'0' || c > b'9' {
            return None;
        }
        v = v.checked_mul(10)?.checked_add((c - b'0') as i64)?;
        if v > i64::MAX / 10 {
            return None;
        }
        i += 1;
    }
    if neg {
        v = -v;
    }
    if v < 1 || v > MAX_N {
        return None;
    }
    Some(v as usize)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 3 {
        println!("THEAD error usage");
        std::process::exit(2);
    }
    let want = match parse_n(&args[2]) {
        Some(n) => n,
        None => {
            println!("THEAD refuse N");
            std::process::exit(1);
        }
    };
    let data = match fs::read(&args[1]) {
        Ok(d) => d,
        Err(_) => {
            println!("THEAD error cannot-open");
            std::process::exit(1);
        }
    };
    let mut printed = 0usize;
    let mut start = 0usize;
    loop {
        if printed >= want || start >= data.len() {
            break;
        }
        let end = data[start..]
            .iter()
            .position(|&b| b == b'\n')
            .map(|p| start + p)
            .unwrap_or(data.len());
        let mut raw = &data[start..end];
        if raw.last() == Some(&b'\r') {
            raw = &raw[..raw.len() - 1];
        }
        if raw.len() >= LINE_CAP {
            println!("THEAD refuse line-too-long");
            std::process::exit(1);
        }
        println!("THEAD {} {}", printed, String::from_utf8_lossy(raw));
        printed += 1;
        if end == data.len() {
            break;
        }
        start = end + 1;
    }
    println!("THEAD lines {}", printed);
}

// Safe-Rust model of examples/utilities/file_tools/fcat.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 fcat.rs -o fcat
use std::fs;

const LINE_CAP: usize = 128;
const MAX_LINES: usize = 64;
const MAX_BYTES: u64 = 2048;

fn refuse(reason: &str, code: i32) -> ! {
    println!("FCAT error {} code={}", reason, code);
    std::process::exit(1);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        println!("FCAT error usage");
        std::process::exit(2);
    }
    if args.len() != 2 {
        println!("FCAT error bad-args");
        std::process::exit(2);
    }
    let path = &args[1];
    let meta = match fs::symlink_metadata(path) {
        Ok(m) => m,
        Err(_) => refuse("missing-file", 109),
    };
    if !meta.is_file() {
        refuse("bad-path", 109);
    }
    let size = meta.len();
    if size > MAX_BYTES {
        refuse("file-too-large", 101);
    }
    let data = match fs::read(path) {
        Ok(d) => d,
        Err(_) => refuse("cannot-open", 109),
    };
    let mut lines: Vec<String> = Vec::new();
    let mut start = 0usize;
    loop {
        if start >= data.len() {
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
            refuse("file::read_line: line longer than the buffer", 101);
        }
        if lines.len() >= MAX_LINES {
            refuse("too-many-lines", 101);
        }
        lines.push(String::from_utf8_lossy(raw).into_owned());
        if end == data.len() {
            break;
        }
        start = end + 1;
    }
    for (i, line) in lines.iter().enumerate() {
        println!("FCAT {} {}", i, line);
    }
    println!("FCAT size {} lines {}", size, lines.len());
}

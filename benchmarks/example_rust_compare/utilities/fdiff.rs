// Safe-Rust model of examples/utilities/file_tools/fdiff.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 fdiff.rs -o fdiff
use std::fs;

const LINE_CAP: usize = 64;
const MAX_LINES: usize = 32;
const MAX_BYTES: u64 = 2048;

fn refuse(reason: &str, code: i32) -> ! {
    println!("FDIFF error {} code={}", reason, code);
    std::process::exit(1);
}

fn load(path: &str) -> Vec<String> {
    let meta = match fs::symlink_metadata(path) {
        Ok(m) => m,
        Err(_) => refuse("missing-file", 109),
    };
    if meta.len() > MAX_BYTES {
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
    lines
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        println!("FDIFF error usage");
        std::process::exit(2);
    }
    if args.len() != 3 {
        println!("FDIFF error bad-args");
        std::process::exit(2);
    }
    let a = load(&args[1]);
    let b = load(&args[2]);
    let max = a.len().max(b.len());
    let mut changes = 0;
    println!("FDiff --- a");
    println!("FDiff +++ b");
    println!("FDiff @@ -{} +{} @@", a.len(), b.len());
    for i in 0..max {
        match (a.get(i), b.get(i)) {
            (Some(x), Some(y)) if x == y => println!("FDiff  {}", x),
            (Some(x), Some(y)) => {
                println!("FDiff -{}", x);
                println!("FDiff +{}", y);
                changes += 2;
            }
            (Some(x), None) => {
                println!("FDiff -{}", x);
                changes += 1;
            }
            (None, Some(y)) => {
                println!("FDiff +{}", y);
                changes += 1;
            }
            (None, None) => {}
        }
    }
    println!("FDiff changes {}", changes);
}

// Safe-Rust model of examples/utilities/text_tools/ttr.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 ttr.rs -o ttr
use std::fs;

const LINE_CAP: usize = 256;
const SET_CAP: usize = 64;

fn read_lines(path: &str) -> Result<Vec<Vec<u8>>, &'static str> {
    let data = fs::read(path).map_err(|_| "cannot-open")?;
    let mut lines: Vec<Vec<u8>> = Vec::new();
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
            return Err("line-too-long");
        }
        lines.push(raw.to_vec());
        if end == data.len() {
            break;
        }
        start = end + 1;
    }
    Ok(lines)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 4 {
        println!("TTR error usage");
        std::process::exit(2);
    }
    let from = args[2].as_bytes();
    let to = args[3].as_bytes();
    if from.len() >= SET_CAP || from.len() != to.len() {
        println!("TTR refuse sets");
        std::process::exit(1);
    }
    let pairs: Vec<(u8, u8)> = from.iter().copied().zip(to.iter().copied()).collect();
    let lines = match read_lines(&args[1]) {
        Ok(l) => l,
        Err(reason) => {
            if reason == "line-too-long" {
                println!("TTR refuse line-too-long");
            } else {
                println!("TTR error cannot-open");
            }
            std::process::exit(1);
        }
    };
    let mut count = 0usize;
    for line in &lines {
        let mut out = Vec::with_capacity(line.len());
        for &c in line {
            let mut mapped = c;
            for &(f, t) in &pairs {
                if f == mapped {
                    mapped = t;
                    break;
                }
            }
            out.push(mapped);
        }
        println!("TTR {}", String::from_utf8_lossy(&out));
        count += 1;
    }
    println!("TTR lines {}", count);
}

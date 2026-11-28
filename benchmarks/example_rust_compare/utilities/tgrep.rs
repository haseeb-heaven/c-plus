// Safe-Rust model of examples/utilities/text_tools/tgrep.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 tgrep.rs -o tgrep
use std::fs;

const LINE_CAP: usize = 256;
const PAT_CAP: usize = 64;

fn glob_full(pat: &[u8], text: &[u8]) -> bool {
    let (pn, sn) = (pat.len(), text.len());
    let mut pi = 0usize;
    let mut si = 0usize;
    let mut star: isize = -1;
    let mut mark = 0usize;
    while si < sn {
        if pi < pn && (pat[pi] == b'.' || pat[pi] == text[si]) {
            pi += 1;
            si += 1;
        } else if pi < pn && pat[pi] == b'*' {
            star = pi as isize;
            pi += 1;
            mark = si;
        } else if star >= 0 {
            pi = (star as usize) + 1;
            mark += 1;
            si = mark;
        } else {
            return false;
        }
    }
    while pi < pn && pat[pi] == b'*' {
        pi += 1;
    }
    pi == pn
}

fn glob_search(pat: &[u8], line: &[u8]) -> bool {
    for start in 0..=line.len() {
        if glob_full(pat, &line[start..]) {
            return true;
        }
    }
    false
}

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
    if args.len() != 3 {
        println!("TGREP error usage");
        std::process::exit(2);
    }
    let pat = args[2].as_bytes();
    if pat.len() >= PAT_CAP {
        println!("TGREP refuse pattern");
        std::process::exit(1);
    }
    let lines = match read_lines(&args[1]) {
        Ok(l) => l,
        Err(reason) => {
            if reason == "line-too-long" {
                println!("TGREP refuse line-too-long");
            } else {
                println!("TGREP error cannot-open");
            }
            std::process::exit(1);
        }
    };
    let has_meta = pat.iter().any(|&c| c == b'.' || c == b'*');
    let mut idx = 0usize;
    let mut matches = 0usize;
    for line in &lines {
        let hit = if has_meta {
            glob_search(pat, line)
        } else if pat.is_empty() {
            true
        } else {
            line.windows(pat.len()).any(|w| w == pat)
        };
        if hit {
            println!("TGREP {} {}", idx, String::from_utf8_lossy(line));
            matches += 1;
        }
        idx += 1;
    }
    println!("TGREP matches {}", matches);
}

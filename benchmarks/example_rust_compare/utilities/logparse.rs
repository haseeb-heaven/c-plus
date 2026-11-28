// Safe-Rust model of examples/utilities/data_tools/logparse.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 logparse.rs -o logparse
use std::fs;

const MAX_LINES: usize = 256;

fn refuse(code: &str, rc: i32) -> ! {
    println!("LOG error {}", code);
    std::process::exit(rc);
}

fn known_level(s: &str) -> bool {
    matches!(s, "TRACE" | "DEBUG" | "INFO" | "WARN" | "ERROR" | "FATAL")
}

fn valid_ts(ts: &str) -> bool {
    let b = ts.as_bytes();
    if b.len() != 20 {
        return false;
    }
    let digits = |from: usize, count: usize| -> bool {
        b[from..from + count].iter().all(|c| c.is_ascii_digit())
    };
    digits(0, 4)
        && b[4] == b'-'
        && digits(5, 2)
        && b[7] == b'-'
        && digits(8, 2)
        && b[10] == b'T'
        && digits(11, 2)
        && b[13] == b':'
        && digits(14, 2)
        && b[16] == b':'
        && digits(17, 2)
        && b[19] == b'Z'
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 3 {
        refuse("usage", 2);
    }
    let filter = &args[2];
    let want_all = filter == "all";
    if !want_all && !known_level(filter) {
        refuse("bad-level", 1);
    }
    let data = match fs::read(&args[1]) {
        Ok(d) => d,
        Err(_) => refuse("cannot-open", 1),
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
        if raw.len() >= 255 {
            refuse("line-too-long", 1);
        }
        lines.push(String::from_utf8_lossy(raw).into_owned());
        if end == data.len() {
            break;
        }
        start = end + 1;
    }
    let mut shown = 0usize;
    let mut total = 0usize;
    for line in &lines {
        let b = line.as_bytes();
        if b.is_empty() {
            refuse("malformed", 1);
        }
        let sp1 = b.iter().position(|&c| c == b' ').unwrap_or(b.len());
        if sp1 != 20 {
            refuse("malformed", 1);
        }
        let ts = &line[..20];
        if !valid_ts(ts) {
            refuse("bad-timestamp", 1);
        }
        let mut sp2 = sp1 + 1;
        while sp2 < b.len() && b[sp2] == b' ' {
            sp2 += 1;
        }
        if sp2 >= b.len() {
            refuse("malformed", 1);
        }
        let mut i = 0usize;
        while sp2 + i < b.len() && b[sp2 + i] != b' ' {
            if i > 7 {
                refuse("malformed", 1);
            }
            i += 1;
        }
        let level = &line[sp2..sp2 + i];
        if !known_level(level) {
            refuse("bad-level", 1);
        }
        let mut msg = sp2 + i;
        while msg < b.len() && b[msg] == b' ' {
            msg += 1;
        }
        if msg >= b.len() {
            refuse("malformed", 1);
        }
        total += 1;
        if total > MAX_LINES {
            refuse("too-many-lines", 1);
        }
        if want_all || level == filter {
            println!("LOG match {} {}", shown, line);
            shown += 1;
        }
    }
    println!("LOG count {} {}", filter, shown);
}

// Safe-Rust model of examples/utilities/data_tools/csvstat.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 csvstat.rs -o csvstat
use std::fs;

const MAX_ROWS: usize = 64;
const NAME_CAP: usize = 64;

fn refuse(code: &str, rc: i32) -> ! {
    println!("CSV error {}", code);
    std::process::exit(rc);
}

fn read_lines(path: &str) -> Result<Vec<String>, &'static str> {
    let data = fs::read(path).map_err(|_| "cannot-open")?;
    if data.len() > 256 * 256 {
        return Err("line-too-long");
    }
    let mut out: Vec<String> = Vec::new();
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
            return Err("line-too-long");
        }
        out.push(String::from_utf8_lossy(raw).into_owned());
        if end == data.len() {
            break;
        }
        start = end + 1;
    }
    Ok(out)
}

fn parse_int(text: &str) -> Option<i64> {
    if text.is_empty() {
        return None;
    }
    let b = text.as_bytes();
    let mut i = 0usize;
    if b[0] == b'+' || b[0] == b'-' {
        i += 1;
    }
    if i >= b.len() {
        return None;
    }
    if !b[i..].iter().all(|c| c.is_ascii_digit()) {
        return None;
    }
    text.parse::<i64>().ok()
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 3 {
        refuse("usage", 2);
    }
    let cmd = args[2].as_str();
    if !matches!(cmd, "stats" | "sum" | "min" | "max" | "count") {
        refuse("bad-command", 1);
    }
    let lines = match read_lines(&args[1]) {
        Ok(l) => l,
        Err(reason) => refuse(reason, 1),
    };
    if lines.is_empty() {
        refuse("empty", 1);
    }
    if lines[0] != "name,value" {
        refuse("bad-header", 1);
    }
    let mut sum: i64 = 0;
    let mut mn: i64 = 0;
    let mut mx: i64 = 0;
    let mut count = 0usize;
    for line in lines.iter().skip(1) {
        let n = line.len();
        if n == 0 || line.as_bytes().iter().filter(|&&c| c == b',').count() != 1 {
            refuse("bad-row", 1);
        }
        let comma = line.as_bytes().iter().position(|&c| c == b',').unwrap();
        if comma == 0 || comma > NAME_CAP - 1 || comma + 1 >= n {
            refuse("bad-row", 1);
        }
        let cell = match parse_int(&line[comma + 1..]) {
            Some(v) => v,
            None => refuse("non-numeric", 1),
        };
        sum = match sum.checked_add(cell) {
            Some(v) => v,
            None => refuse("sum-overflow", 1),
        };
        if count == 0 {
            mn = cell;
            mx = cell;
        } else {
            if cell < mn {
                mn = cell;
            }
            if cell > mx {
                mx = cell;
            }
        }
        count += 1;
        if count > MAX_ROWS {
            refuse("too-many-rows", 1);
        }
    }
    if count == 0 {
        refuse("no-data", 1);
    }
    match cmd {
        "stats" => {
            println!("CSV count {}", count);
            println!("CSV sum {}", sum);
            println!("CSV min {}", mn);
            println!("CSV max {}", mx);
        }
        "sum" => println!("CSV sum {}", sum),
        "min" => println!("CSV min {}", mn),
        "max" => println!("CSV max {}", mx),
        _ => println!("CSV count {}", count),
    }
}

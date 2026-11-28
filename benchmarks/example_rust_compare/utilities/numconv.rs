// Safe-Rust model of examples/utilities/dev_tools/numconv.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 numconv.rs -o numconv
fn refuse(reason: &str) -> ! {
    println!("NUMCONV REFUSED {}", reason);
    std::process::exit(1);
}

fn base(name: &str) -> Option<u32> {
    match name {
        "dec" => Some(10),
        "hex" => Some(16),
        "bin" => Some(2),
        _ => None,
    }
}

fn parse(s: &str, b: u32) -> Result<u32, &'static str> {
    if s.is_empty() {
        return Err("invalid-digit");
    }
    let mut v: u32 = 0;
    for c in s.bytes() {
        let d = match c {
            b'0'..=b'9' => (c - b'0') as u32,
            b'a'..=b'f' => (c - b'a') as u32 + 10,
            b'A'..=b'F' => (c - b'A') as u32 + 10,
            _ => return Err("invalid-digit"),
        };
        if d >= b {
            return Err("invalid-digit");
        }
        v = match v.checked_mul(b).and_then(|x| x.checked_add(d)) {
            Some(x) => x,
            None => return Err("overflow"),
        };
    }
    Ok(v)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 4 {
        refuse("bad-args");
    }
    if args[1].len() >= 64 || args[2].len() >= 16 || args[3].len() >= 16 {
        refuse("input-too-long");
    }
    let from = match base(&args[2]) {
        Some(b) => b,
        None => refuse("unknown-base"),
    };
    let to = match base(&args[3]) {
        Some(b) => b,
        None => refuse("unknown-base"),
    };
    let v = match parse(&args[1], from) {
        Ok(v) => v,
        Err(reason) => refuse(reason),
    };
    let result = match to {
        10 => v.to_string(),
        16 => format!("{:x}", v),
        _ => format!("{:b}", v),
    };
    println!("NUMCONV {} {} {} {}", args[1], args[2], args[3], result);
}

// Safe-Rust model of examples/utilities/text_tools/tbase64.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 tbase64.rs -o tbase64
use std::fs;

const MAX_IN: usize = 4096;
const ALPHA: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const HEX: &[u8; 16] = b"0123456789abcdef";

fn val(c: u8) -> i32 {
    ALPHA.iter().position(|&x| x == c).map(|p| p as i32).unwrap_or(-1)
}

fn refuse(reason: &str) -> ! {
    println!("TBASE64 refuse {}", reason);
    std::process::exit(1);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 3 || (args[1] != "enc" && args[1] != "dec") {
        println!("TBASE64 error usage");
        std::process::exit(2);
    }
    let encoding = args[1] == "enc";
    let data = match fs::read(&args[2]) {
        Ok(d) => d,
        Err(_) => {
            println!("TBASE64 error cannot-open");
            std::process::exit(1);
        }
    };
    if data.len() > MAX_IN {
        println!("TBASE64 refuse input-too-large");
        std::process::exit(1);
    }
    if encoding {
        let mut out = String::new();
        let mut i = 0usize;
        while i < data.len() {
            let b0 = data[i] as u32;
            let b1 = if i + 1 < data.len() { data[i + 1] as u32 } else { 0 };
            let b2 = if i + 2 < data.len() { data[i + 2] as u32 } else { 0 };
            out.push(ALPHA[((b0 >> 2) & 63) as usize] as char);
            out.push(ALPHA[(((b0 & 3) << 4) | ((b1 >> 4) & 15)) as usize] as char);
            out.push(if i + 1 < data.len() {
                ALPHA[(((b1 & 15) << 2) | ((b2 >> 6) & 3)) as usize] as char
            } else {
                '='
            });
            out.push(if i + 2 < data.len() {
                ALPHA[(b2 & 63) as usize] as char
            } else {
                '='
            });
            i += 3;
        }
        println!("TBASE64 {}", out);
        return;
    }

    let mut n = data.len();
    while n > 0 && (data[n - 1] == b'\n' || data[n - 1] == b'\r') {
        n -= 1;
    }
    let token = &data[..n];
    if n == 0 || n % 4 != 0 {
        refuse("malformed");
    }
    let mut pads = 0usize;
    if token[n - 1] == b'=' {
        pads += 1;
    }
    if n >= 2 && token[n - 2] == b'=' {
        pads += 1;
    }
    if pads > 2 {
        refuse("malformed");
    }
    for (i, &c) in token.iter().enumerate() {
        if c == b'=' {
            if i < n - pads {
                refuse("malformed");
            }
        } else if val(c) < 0 {
            refuse("malformed");
        }
    }
    if pads == 1 {
        if (val(token[n - 2]) & 15) != 0 {
            refuse("malformed");
        }
    } else if pads == 2 && (val(token[n - 3]) & 3) != 0 {
        refuse("malformed");
    }
    let mut acc: i64 = 0;
    let mut bits = 0i32;
    let mut bytes: Vec<u8> = Vec::new();
    for &c in token {
        if c == b'=' {
            break;
        }
        acc = (acc << 6) | val(c) as i64;
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            bytes.push(((acc >> bits) & 255) as u8);
        }
    }
    let mut expected = (n / 4) * 3;
    if token[n - 1] == b'=' {
        expected -= 1;
    }
    if n >= 2 && token[n - 2] == b'=' {
        expected -= 1;
    }
    if bytes.len() != expected {
        refuse("malformed");
    }
    let mut hex = String::new();
    for &b in &bytes {
        hex.push(HEX[(b >> 4) as usize] as char);
        hex.push(HEX[(b & 15) as usize] as char);
    }
    println!("TBASE64 {} {}", bytes.len(), hex);
}

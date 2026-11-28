// Safe-Rust model of examples/expanded/math_2/checksum.cp
// No unsafe, no FFI, no external crates. Build: rustc -O --edition 2021 checksum.rs -o checksum
// CRC32 + Adler32 over bounded, fixed byte strings. C+ `print()` joins args with
// one space and appends a newline; string::append concatenates without spaces.

const CAP: usize = 128;
const CRC_POLY: u32 = 0xedb8_8320;
const ADLER_MOD: u32 = 65521;
const CRC_SEED: u32 = 0xffff_ffff;

fn cprint(parts: &[String]) {
    println!("{}", parts.join(" "));
}

fn crc32(data: &[u8]) -> u32 {
    let mut crc = CRC_SEED;
    for &b in data {
        crc ^= b as u32;
        for _ in 0..8 {
            let mask = 0u32.wrapping_sub(crc & 1);
            crc = (crc >> 1) ^ (CRC_POLY & mask);
        }
    }
    crc ^ CRC_SEED
}

fn adler32(data: &[u8]) -> u32 {
    let mut a = 1u32;
    let mut b = 0u32;
    for &byte in data {
        a = (a + byte as u32) % ADLER_MOD;
        b = (b + a) % ADLER_MOD;
    }
    (b << 16) | a
}

// Prints one case line; returns false when refused (input-too-long).
fn ck_case(text: &str) -> bool {
    let bytes = text.as_bytes();
    if bytes.len() > CAP {
        cprint(&[
            "CHECKSUM REFUSED code=".to_string(),
            "101".to_string(),
            "input-too-long".to_string(),
        ]);
        return false;
    }
    let hexc = format!("{:08x}", crc32(bytes));
    let hexa = format!("{:08x}", adler32(bytes));
    cprint(&[format!(
        "CHECKSUM len={} text={} crc32={} adler32={}",
        bytes.len(), text, hexc, hexa
    )]);
    true
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() == 1 {
        let inputs = [
            "",
            "123456789",
            "The quick brown fox jumps over the lazy dog",
            "abc",
        ];
        for t in &inputs {
            if !ck_case(t) {
                std::process::exit(1);
            }
        }
        cprint(&[
            "CHECKSUM OK count=4 cap=128 n=".to_string(),
            "4".to_string(),
        ]);
    } else if args.len() == 2 {
        let mode = &args[1];
        if mode.as_bytes().len() > 32 || mode.bytes().any(|b| b == 0) {
            cprint(&["CHECKSUM REFUSED code=101 bad-mode".to_string()]);
            std::process::exit(1);
        }
        if mode == "oversized" {
            let big = "A".repeat(CAP + 1);
            if !ck_case(&big) {
                std::process::exit(1);
            }
        } else {
            cprint(&[
                "CHECKSUM REFUSED code=".to_string(),
                "108".to_string(),
                "bad-mode".to_string(),
            ]);
            std::process::exit(1);
        }
    } else if args.len() == 3 {
        if args[1] != "text" {
            cprint(&[
                "CHECKSUM REFUSED code=".to_string(),
                "108".to_string(),
                "bad-mode".to_string(),
            ]);
            std::process::exit(1);
        }
        if !ck_case(&args[2]) {
            std::process::exit(1);
        }
    } else {
        cprint(&[
            "CHECKSUM REFUSED code=".to_string(),
            "108".to_string(),
            "usage".to_string(),
        ]);
        std::process::exit(1);
    }
}

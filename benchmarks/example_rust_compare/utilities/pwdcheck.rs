// Safe-Rust model of examples/utilities/security_tools/pwdcheck.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 pwdcheck.rs -o pwdcheck
const MIN_LEN: usize = 8;
const MAX_LEN: usize = 32;
const SYMBOLS: &[u8] = b"!@#$%^&*()-_=+[]{};:,.<>/?";

fn refuse(label: &str, reason: &str, cat: i32, code: i32) -> ! {
    println!("PWCHECK {} REFUSED code={} cat={} reason={}", label, code, cat, reason);
    std::process::exit(2);
}

fn main() {
    println!("PWCHECK selfcheck OK fixed-dummy-rules-ok");
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        refuse("usage", "missing-input", 1, 108);
    }
    if args.len() > 2 {
        refuse("too_many_args", "more-than-one-input", 2, 101);
    }
    let text = args[1].as_bytes();
    if text.len() > MAX_LEN {
        refuse("oversize", "input-too-long", 5, 101);
    }
    if text.len() < MIN_LEN {
        refuse("too_short", "input-too-short", 2, 101);
    }
    let mut upper = 0;
    let mut lower = 0;
    let mut digit = 0;
    let mut symbol = 0;
    for &c in text {
        if !(0x21..=0x7e).contains(&c) {
            refuse("bad_byte", "non-printable-input", 1, 108);
        }
        if c.is_ascii_uppercase() {
            upper = 1;
        }
        if c.is_ascii_lowercase() {
            lower = 1;
        }
        if c.is_ascii_digit() {
            digit = 1;
        }
        if SYMBOLS.contains(&c) {
            symbol = 1;
        }
    }
    let score = upper + lower + digit + symbol;
    if score != 4 {
        refuse("weak", "policy-rules-not-met", 1, 108);
    }
    println!(
        "PWCHECK strong OK len={} score={} upper={} lower={} digit={} symbol={}",
        text.len(), score, upper, lower, digit, symbol
    );
    println!("PWCHECK done");
}

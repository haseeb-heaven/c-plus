//! Safe-Rust model of tools/dual/hexdump/hexdump.cp.
//!
//! No `unsafe`: all input enters through std::fs::File::read into a fixed-size
//! stack byte, and the capacity check happens before any push into the Vec.
//! Refusals mirror the C+ driver exactly (stdout line + distinct exit code):
//!   bad-args 108/exit 2, cannot-open 109/exit 3,
//!   oversized 101/exit 4, io-error 109/exit 5.
//! Valid-input stdout is byte-identical to hexdump.c and hexdump.cp.

use std::env;
use std::fs::File;
use std::io::Read;
use std::process;

const CAP: usize = 256;
const LINE: usize = 16;
const HEX: &[u8; 16] = b"0123456789abcdef";

fn refuse(kind: &str, code: i32, exit_code: i32) -> ! {
    println!("HEXDUMP REFUSED {} code={}", kind, code);
    process::exit(exit_code);
}

fn render(data: &[u8]) -> String {
    let mut out = String::new();
    let mut offset = 0usize;
    while offset < data.len() {
        out.push_str(&format!("{:08x}  ", offset));
        let mut ascii = String::with_capacity(LINE);
        for k in 0..LINE {
            let idx = offset + k;
            if idx < data.len() {
                let b = data[idx];
                out.push(HEX[(b >> 4) as usize] as char);
                out.push(HEX[(b & 0x0f) as usize] as char);
                out.push(' ');
                ascii.push(if (0x20..=0x7e).contains(&b) { b as char } else { '.' });
            } else {
                out.push_str("   ");
            }
        }
        out.push_str("  |");
        out.push_str(&ascii);
        out.push_str("|\n");
        offset += LINE;
    }
    out
}

fn main() {
    let args: Vec<String> = env::args_os()
        .skip(1)
        .map(|s| s.into_string())
        .collect::<Result<Vec<_>, _>>()
        .unwrap_or_default();

    if args.len() != 1 || args[0].starts_with('-') {
        refuse("bad-args", 108, 2);
    }

    let mut file = match File::open(&args[0]) {
        Ok(f) => f,
        Err(_) => refuse("cannot-open", 109, 3),
    };

    let mut data: Vec<u8> = Vec::with_capacity(CAP);
    let mut one = [0u8; 1];
    loop {
        match file.read(&mut one) {
            Ok(0) => break,
            Ok(_) => {
                if data.len() >= CAP {
                    refuse("oversized", 101, 4);
                }
                data.push(one[0]);
            }
            Err(_) => refuse("io-error", 109, 5),
        }
    }

    print!("{}", render(&data));
}

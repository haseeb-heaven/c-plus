// Safe-Rust model of examples/utilities/dev_tools/hexdump.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 hexdump.rs -o hexdump
use std::fs;

const CAP: usize = 256;
const HEX: &[u8; 16] = b"0123456789abcdef";

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 2 {
        println!("HEXDUMP REFUSED bad-args");
        std::process::exit(1);
    }
    let data = match fs::read(&args[1]) {
        Ok(d) => d,
        Err(_) => {
            println!("HEXDUMP REFUSED cannot-open");
            std::process::exit(1);
        }
    };
    if data.len() > CAP {
        println!("HEXDUMP REFUSED oversized");
        std::process::exit(1);
    }
    println!("HEXDUMP {} bytes", data.len());
    let mut off = 0usize;
    while off < data.len() {
        let mut hex = String::new();
        let mut ascii = String::new();
        for k in 0..16usize {
            let idx = off + k;
            if idx < data.len() {
                let b = data[idx];
                hex.push(HEX[(b >> 4) as usize] as char);
                hex.push(HEX[(b & 15) as usize] as char);
                hex.push(' ');
                ascii.push(if (0x20..=0x7e).contains(&b) { b as char } else { '.' });
            } else {
                hex.push_str("   ");
            }
        }
        println!("HEXDUMP {:04x}  {} |{}|", off, hex, ascii);
        off += 16;
    }
}

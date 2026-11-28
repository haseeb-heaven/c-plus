// Safe-Rust model of examples/utilities/file_tools/fhash.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 fhash.rs -o fhash
use std::fs;

const FH_MAX: usize = 4096;
const OFFSET: u32 = 0x811c9dc5;
const PRIME: u32 = 0x01000193;

fn fnv(bytes: &[u8]) -> u32 {
    let mut h = OFFSET;
    for &b in bytes {
        h ^= b as u32;
        h = h.wrapping_mul(PRIME);
    }
    h
}

fn main() {
    // Built-in known-answer check: FNV-1a("hello") must match.
    if fnv(b"hello") != 0x4f9f2cab {
        println!("FHASH error selfcheck-failed code=101");
        std::process::exit(1);
    }
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        println!("FHASH error usage");
        std::process::exit(2);
    }
    if args.len() != 2 {
        println!("FHASH error bad-args");
        std::process::exit(2);
    }
    let path = &args[1];
    let meta = match fs::symlink_metadata(path) {
        Ok(m) => m,
        Err(_) => {
            println!("FHASH error missing-file code=109");
            std::process::exit(1);
        }
    };
    if !meta.is_file() {
        println!("FHASH error bad-path code=109");
        std::process::exit(1);
    }
    let size = meta.len() as usize;
    if size > FH_MAX {
        println!("FHASH error oversized code=101");
        std::process::exit(1);
    }
    let data = match fs::read(path) {
        Ok(d) => d,
        Err(_) => {
            println!("FHASH error cannot-open code=109");
            std::process::exit(1);
        }
    };
    println!("FHASH {:08x} {}", fnv(&data), data.len());
}

// Safe-Rust model of examples/utilities/file_tools/fcopy.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 fcopy.rs -o fcopy
use std::fs;

const CAP: usize = 1024;
const PATH_CAP: usize = 192;

fn refuse(reason: &str, code: i32) -> ! {
    println!("FCOPY error {} code={}", reason, code);
    std::process::exit(1);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        println!("FCOPY error usage");
        std::process::exit(2);
    }
    if args.len() != 3 {
        println!("FCOPY error bad-args");
        std::process::exit(2);
    }
    let src = &args[1];
    let dst = &args[2];
    if src.is_empty() {
        refuse("bad-path", 109);
    }
    let smeta = match fs::symlink_metadata(src) {
        Ok(m) => m,
        Err(_) => refuse("missing-file", 109),
    };
    if !smeta.is_file() {
        refuse("bad-path", 109);
    }
    let size = smeta.len() as usize;
    if size > CAP {
        refuse("oversized", 101);
    }
    if dst.is_empty() || dst.len() > PATH_CAP {
        refuse("bad-destination", 109);
    }
    if let Ok(m) = fs::symlink_metadata(dst) {
        if !m.is_file() {
            refuse("bad-destination", 109);
        }
    }
    let data = match fs::read(src) {
        Ok(d) => d,
        Err(_) => refuse("cannot-open", 109),
    };
    if fs::write(dst, &data).is_err() {
        refuse("cannot-open-destination", 109);
    }
    let dmeta = match fs::symlink_metadata(dst) {
        Ok(m) => m,
        Err(_) => refuse("destination-missing", 109),
    };
    if dmeta.len() as usize != size {
        refuse("destination-one-over", 101);
    }
    let back = match fs::read(dst) {
        Ok(d) => d,
        Err(_) => refuse("cannot-open", 109),
    };
    if back != data {
        refuse("verify-mismatch", 109);
    }
    println!("FCOPY copied {}", size);
    println!("FCOPY verified {}", size);
}

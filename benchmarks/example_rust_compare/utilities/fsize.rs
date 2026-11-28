// Safe-Rust model of examples/utilities/file_tools/fsize.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 fsize.rs -o fsize
use std::fs;
use std::path::Path;

const PATH_CAP: usize = 192;
const REL_CAP: usize = 128;
const MAX_ENTRIES: usize = 16;

fn refuse(reason: &str, code: i32) -> ! {
    println!("FSIZE error {} code={}", reason, code);
    std::process::exit(1);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        println!("FSIZE error usage");
        std::process::exit(2);
    }
    if args.len() - 2 > MAX_ENTRIES {
        println!("FSIZE error too-many-entries code=101");
        std::process::exit(1);
    }
    let dir = &args[1];
    if dir.is_empty() {
        refuse("bad-path", 109);
    }
    if dir.len() > PATH_CAP {
        refuse("path-too-long", 101);
    }
    let dmeta = match fs::symlink_metadata(dir) {
        Ok(m) => m,
        Err(_) => refuse("missing-dir", 109),
    };
    if !dmeta.is_dir() {
        refuse("not-a-directory", 109);
    }
    let mut sizes: Vec<u64> = Vec::new();
    for rel in &args[2..] {
        if rel.is_empty() || rel.len() > REL_CAP || dir.len() + 1 + rel.len() > PATH_CAP {
            refuse("path-too-long", 101);
        }
        let full = Path::new(dir).join(rel);
        let meta = match fs::symlink_metadata(&full) {
            Ok(m) => m,
            Err(_) => refuse("missing-entry", 109),
        };
        sizes.push(meta.len());
    }
    let mut total: u64 = 0;
    for (rel, size) in args[2..].iter().zip(sizes.iter()) {
        println!("FSIZE {} {}", rel, size);
        total = total.wrapping_add(*size);
    }
    println!("FSIZE count {} total {}", args.len() - 2, total);
}

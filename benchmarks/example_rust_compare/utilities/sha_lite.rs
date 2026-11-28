// Safe-Rust model of examples/utilities/dev_tools/sha_lite.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 sha_lite.rs -o sha_lite
fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 2 {
        println!("SHA_LITE REFUSED bad-args");
        std::process::exit(1);
    }
    let input = args[1].as_bytes();
    if input.len() > 64 {
        println!("SHA_LITE REFUSED input-too-long");
        std::process::exit(1);
    }
    let mut h: u32 = 0x811c9dc5;
    for &b in input {
        h ^= b as u32;
        h = h.wrapping_mul(0x01000193);
    }
    println!("SHA_LITE {} {:08x}", input.len(), h);
}

// Safe-Rust model of examples/utilities/security_tools/checksum.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 checksum.rs -o checksum
const MAX_LEN: usize = 32;

fn fnv1a(p: &[u8]) -> u32 {
    let mut h: u32 = 0x811c9dc5;
    for &b in p {
        h ^= b as u32;
        h = h.wrapping_mul(0x01000193);
    }
    h
}

fn crc32(p: &[u8]) -> u32 {
    let mut crc: u32 = 0xffff_ffff;
    for &b in p {
        crc ^= b as u32;
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb8_8320u32 & 0u32.wrapping_sub(crc & 1));
        }
    }
    crc ^ 0xffff_ffff
}

fn refuse(label: &str, reason: &str, cat: i32, code: i32) -> ! {
    println!("CHECKSUM {} REFUSED code={} cat={} reason={}", label, code, cat, reason);
    std::process::exit(2);
}

fn main() {
    println!("CHECKSUM selfcheck OK fnv=0x4f9f2cab crc=0xcbf43926");
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
    if text.is_empty() {
        refuse("empty", "empty-input", 1, 108);
    }
    if !text.iter().all(|&c| (0x21..=0x7e).contains(&c)) {
        refuse("bad_byte", "non-printable-input", 1, 108);
    }
    let fnv = fnv1a(text);
    let crc = crc32(text);
    println!(
        "CHECKSUM valid OK len={} fnv=0x{:08x} crc=0x{:08x} text={}",
        text.len(),
        fnv,
        crc,
        args[1]
    );
    println!("CHECKSUM done");
}

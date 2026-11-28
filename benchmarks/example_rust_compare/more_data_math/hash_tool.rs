// Safe-Rust model of examples/expanded/data_2/hash_tool.cp
// No unsafe, no FFI, no external crates. Build: rustc -O --edition 2021 hash_tool.rs -o hash_tool
// Deterministic bounded FNV-1a/32 hasher; identical argv yields byte-identical stdout.

const MAX_INPUTS: usize = 16;
const MAX_LEN: usize = 64;
const OFFSET: u32 = 0x811c9dc5;
const PRIME: u32 = 0x01000193;

fn refuse(reason: &str, code: i32) -> i32 {
    println!("HASH error code={} reason={}", code, reason);
    2
}

fn fnv1a(text: &[u8]) -> u32 {
    let mut h = OFFSET;
    for &b in text {
        h ^= b as u32;
        h = h.wrapping_mul(PRIME);
    }
    h
}

fn main() {
    // Built-in known-answer check must pass before any input is processed.
    if fnv1a(b"hello") != 0x4f9f2cab {
        std::process::exit(refuse("selfcheck-failed", 108));
    }

    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        std::process::exit(refuse("usage", 108));
    }
    let n = args.len() - 1;
    if n > MAX_INPUTS {
        std::process::exit(refuse("too-many-inputs", 101));
    }

    // Validate every token before printing the header: no partial transcript.
    let mut lens: Vec<usize> = Vec::with_capacity(n);
    for arg in &args[1..] {
        let bytes = arg.as_bytes();
        if bytes.is_empty() {
            std::process::exit(refuse("empty-input", 108));
        }
        if bytes.len() > MAX_LEN {
            std::process::exit(refuse("input-too-long", 101));
        }
        if bytes.iter().any(|&c| c < 0x21 || c > 0x7e) {
            std::process::exit(refuse("invalid-byte", 108));
        }
        lens.push(bytes.len());
    }

    println!(
        "HASH app=hash_tool alg=FNV-1a-32 offset=0x{:08x} prime=0x{:08x} max_len={} selfcheck=ok n={}",
        OFFSET, PRIME, MAX_LEN, n
    );
    let mut xor_all = 0u32;
    for (i, arg) in args[1..].iter().enumerate() {
        let value = fnv1a(arg.as_bytes());
        println!(
            "HASH {} len={} hash=0x{:08x} text={}",
            i, lens[i], value, arg
        );
        xor_all ^= value;
    }
    println!("HASH xor=0x{:08x}", xor_all);
    println!("HASH done");
}

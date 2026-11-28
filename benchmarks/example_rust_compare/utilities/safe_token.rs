// Safe-Rust model of examples/utilities/security_tools/safe_token.cp
// No unsafe, no FFI. Build: rustc -O --edition 2021 safe_token.rs -o safe_token
const MAX_INPUT: usize = 64;
const MAX_TOKENS: usize = 4;
const TOKEN_CAP: usize = 16;

fn refuse(label: &str, reason: &str, cat: i32, code: i32) -> ! {
    println!(
        "SAFETOKEN {} REFUSED code={} cat={} reason={}",
        label, code, cat, reason
    );
    std::process::exit(2);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        refuse("usage", "missing-input", 1, 108);
    }
    if args.len() > 2 {
        refuse("usage", "more-than-one-input", 2, 101);
    }
    let text = args[1].as_bytes();
    if text.len() > MAX_INPUT {
        refuse("oversize", "input-too-long", 5, 101);
    }
    if text.is_empty() {
        refuse("empty", "empty-input", 1, 108);
    }
    if !text.iter().all(|&c| (0x20..=0x7e).contains(&c)) {
        refuse("bad_byte", "non-printable-input", 1, 108);
    }
    // strtok_s(" ,;") skips runs of delimiters and returns non-empty tokens.
    let tokens: Vec<&[u8]> = text
        .split(|&c| c == b' ' || c == b',' || c == b';')
        .filter(|t| !t.is_empty())
        .collect();
    for tok in &tokens {
        if tok.len() > TOKEN_CAP {
            refuse("long_token", "token-too-long", 5, 101);
        }
    }
    if tokens.len() > MAX_TOKENS {
        refuse("many_tokens", "too-many-tokens", 5, 101);
    }
    if tokens.is_empty() {
        refuse("no_tokens", "no-tokens-found", 1, 108);
    }
    for (i, tok) in tokens.iter().enumerate() {
        println!(
            "SAFETOKEN token {} len={} text={}",
            i,
            tok.len(),
            String::from_utf8_lossy(tok)
        );
    }
    println!(
        "SAFETOKEN valid OK n={} remaining={}",
        tokens.len(),
        MAX_INPUT + 1 - text.len()
    );
    println!("SAFETOKEN done");
}

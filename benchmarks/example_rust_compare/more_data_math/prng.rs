// Safe-Rust model of examples/expanded/math_2/prng.cp
// No unsafe, no FFI, no external crates. Build: rustc -O --edition 2021 prng.rs -o prng
// Deterministic xorshift32 PRNG with capacity/overflow refusal. C+ `print()`
// joins args with one space and appends a newline.

const COUNT_CAP: u64 = 256;
const SEED_CAP: u64 = 4_294_967_295;
const BOUND_MASK: u32 = 0x7;
const ARG_CAP: usize = 64;

fn cprint(parts: &[String]) {
    println!("{}", parts.join(" "));
}

fn refused(code: i32, what: &str) -> i32 {
    cprint(&[
        "PRNG REFUSED code=".to_string(),
        code.to_string(),
        what.to_string(),
    ]);
    1
}

// Returns Ok(value) or Err((code, reason)).
fn parse_impl<'a>(text: &str, limit: u64, syntax: &'a str, overflow: &'a str) -> Result<u64, (i32, &'a str)> {
    let b = text.as_bytes();
    if b.len() > ARG_CAP || b.is_empty() {
        return Err((108, syntax));
    }
    let mut acc = 0u64;
    for &c in b {
        if c < b'0' || c > b'9' {
            return Err((108, syntax));
        }
        let d = (c - b'0') as u64;
        if d > limit || acc > (limit - d) / 10 {
            return Err((101, overflow));
        }
        acc = acc * 10 + d;
    }
    Ok(acc)
}

fn next(state: &mut u32) -> u32 {
    let mut x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    x
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let (seed, count) = if args.len() == 3 {
        let s = match parse_impl(&args[1], SEED_CAP, "seed-syntax", "seed-overflow") {
            Ok(v) => v,
            Err((c, w)) => std::process::exit(refused(c, w)),
        };
        let c = match parse_impl(&args[2], u64::MAX, "count-syntax", "count-overflow") {
            Ok(v) => v,
            Err((code, why)) => std::process::exit(refused(code, why)),
        };
        if c > COUNT_CAP {
            std::process::exit(refused(101, "count-too-large"));
        }
        (s, c)
    } else if args.len() == 1 {
        (305_419_896u64, 16u64)
    } else {
        std::process::exit(refused(108, "usage"));
    };

    let mut state = seed as u32;
    let mut values: Vec<u32> = Vec::with_capacity(count as usize);
    for _ in 0..count {
        values.push(next(&mut state));
    }

    let sseed = seed as u32;
    cprint(&[
        format!("PRNG seed={} count={} cap=256", sseed, values.len()),
    ]);
    let mut bounded_sum = 0u32;
    for (i, &raw) in values.iter().enumerate() {
        let b = raw & BOUND_MASK;
        bounded_sum = bounded_sum.wrapping_add(b);
        cprint(&[
            format!("PRNG {}", i),
            format!("raw={}", raw),
            format!("bounded={}", b),
        ]);
    }
    cprint(&[format!("PRNG sum={} OK", bounded_sum)]);

    // Replay from the same seed to prove reproducibility (no output on match).
    let mut replay = seed as u32;
    for &stored in &values {
        let r = next(&mut replay);
        if r != stored {
            std::process::exit(refused(108, "replay-mismatch"));
        }
    }
}

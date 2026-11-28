/* SAFE-Rust model of examples/secure_apps/ratelimit/ratelimit.cp
 *
 * Deterministic token-bucket rate limiter: same config validation, same
 * monotonic-tick request validation, same refill/debit state machine, same
 * REFUSED code = process exit status.  No wall clock, no unsafe, no FFI.
 */

use std::env;

const MAX_CAPACITY: i64 = 1_000_000;
const MAX_REFILL: i64 = 1_000_000;
const MAX_REQUESTS: usize = 64;

fn refusal(code: i32, name: &str, detail: &str) -> i32 {
    println!("REFUSED {} {} {}", code, name, detail);
    code
}

/* strtol-like checked decimal parse: leading ASCII space, optional sign,
 * at least one digit, nothing trailing, i64 range. */
fn parse_long(text: &str) -> Option<i64> {
    let b = text.as_bytes();
    let mut i = 0usize;
    while i < b.len() && matches!(b[i], b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c) {
        i += 1;
    }
    let start_digits = i;
    let neg = if i < b.len() && (b[i] == b'+' || b[i] == b'-') {
        let n = b[i] == b'-';
        i += 1;
        n
    } else {
        false
    };
    let mut value: i128 = 0;
    let mut digits = 0usize;
    while i < b.len() && b[i].is_ascii_digit() {
        value = value * 10 + (b[i] - b'0') as i128;
        if value > i64::MAX as i128 + 1 {
            return None;
        }
        digits += 1;
        i += 1;
    }
    if digits == 0 || i != b.len() || start_digits == b.len() {
        return None;
    }
    let signed = if neg { -value } else { value };
    if signed < i64::MIN as i128 || signed > i64::MAX as i128 {
        return None;
    }
    Some(signed as i64)
}

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    if args.len() < 3 {
        std::process::exit(refusal(1, "ARGS", "need capacity refill n [tick cost]..."));
    }

    let capacity = match parse_long(&args[0]) {
        Some(v) => v,
        None => std::process::exit(refusal(2, "PARSE", "capacity")),
    };
    let refill = match parse_long(&args[1]) {
        Some(v) => v,
        None => std::process::exit(refusal(2, "PARSE", "refill")),
    };
    let n_long = match parse_long(&args[2]) {
        Some(v) => v,
        None => std::process::exit(refusal(2, "PARSE", "n")),
    };

    if capacity <= 0 || capacity > MAX_CAPACITY {
        std::process::exit(refusal(3, "CAPACITY", "out of range"));
    }
    if refill <= 0 || refill > MAX_REFILL {
        std::process::exit(refusal(4, "REFILL", "out of range"));
    }
    if n_long < 0 || n_long > MAX_REQUESTS as i64 {
        std::process::exit(refusal(5, "COUNT", "out of range"));
    }
    let n = n_long as usize;

    if args.len() != 3 + 2 * n {
        std::process::exit(refusal(1, "ARGS", "tick/cost pair count mismatch"));
    }

    let mut ticks: Vec<i64> = Vec::with_capacity(n);
    let mut costs: Vec<i64> = Vec::with_capacity(n);
    for i in 0..n {
        let tick = match parse_long(&args[3 + 2 * i]) {
            Some(v) => v,
            None => std::process::exit(refusal(2, "PARSE", "tick")),
        };
        let cost = match parse_long(&args[4 + 2 * i]) {
            Some(v) => v,
            None => std::process::exit(refusal(2, "PARSE", "cost")),
        };
        if tick < 0 {
            std::process::exit(refusal(6, "TICK", "negative tick"));
        }
        if i > 0 && tick < ticks[i - 1] {
            std::process::exit(refusal(6, "TICK", "non-monotonic tick"));
        }
        if cost <= 0 || cost > capacity {
            std::process::exit(refusal(7, "COST", "out of range"));
        }
        ticks.push(tick);
        costs.push(cost);
    }

    /* Pre-pass: never emit a partial transcript followed by a refusal. */
    let mut levels: Vec<i64> = Vec::with_capacity(n);
    let mut afters: Vec<i64> = Vec::with_capacity(n);
    let mut allow: Vec<bool> = Vec::with_capacity(n);
    let mut tokens = capacity;
    let mut last_tick: i64 = 0;

    for i in 0..n {
        let elapsed = ticks[i] as i128 - last_tick as i128;
        let added = elapsed * refill as i128;
        if added < i64::MIN as i128 || added > i64::MAX as i128 {
            std::process::exit(refusal(8, "OVERFLOW", "refill multiply"));
        }
        let level = tokens as i128 + added;
        if level < i64::MIN as i128 || level > i64::MAX as i128 {
            std::process::exit(refusal(8, "OVERFLOW", "bucket add"));
        }
        let before = level.min(capacity as i128);
        let mut level = before;
        if level >= costs[i] as i128 {
            level -= costs[i] as i128;
            allow.push(true);
        } else {
            allow.push(false);
        }
        tokens = level as i64;
        levels.push(before as i64);
        afters.push(tokens);
        last_tick = ticks[i];
    }

    println!("SECURE_RL_CONFIG {} {} {}", capacity, refill, n);
    let mut allowed = 0i64;
    let mut denied = 0i64;
    for i in 0..n {
        let verdict = if allow[i] {
            allowed += 1;
            "ALLOW"
        } else {
            denied += 1;
            "DENY"
        };
        println!(
            "SECURE_RL_REQ {} {} {} {} {} {}",
            i, ticks[i], costs[i], levels[i], verdict, afters[i]
        );
    }
    println!("SECURE_RL_DONE {} {} {}", allowed, denied, tokens);
}

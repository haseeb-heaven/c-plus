// Safe-Rust model of examples/expanded/data_2/regex_lite.cp
// No unsafe, no FFI, no external crates. Build: rustc -O --edition 2021 regex_lite.rs -o regex_lite
// Deterministic wildcard matcher with a 256-step backtracking guard.

const MAX_PAT: usize = 64;
const MAX_TEXT: usize = 128;
const MAX_STEPS: i32 = 256;

fn refuse(reason: &str, code: i32) -> i32 {
    println!("REGEX error code={} reason={}", code, reason);
    2
}

fn validate(text: &[u8], limit: usize, pattern: bool) -> Result<usize, i32> {
    // -1 too long, -2 empty, -3 invalid byte.
    let n = text.len();
    if n == 0 {
        return Err(-2);
    }
    if n > limit {
        return Err(-1);
    }
    for &c in text {
        if pattern {
            let ok = (c >= b'a' && c <= b'z')
                || (c >= b'A' && c <= b'Z')
                || (c >= b'0' && c <= b'9')
                || c == b'*'
                || c == b'?'
                || c == b'.'
                || c == b'-'
                || c == b'_';
            if !ok {
                return Err(-3);
            }
        } else if c < 0x21 || c > 0x7e {
            return Err(-3);
        }
    }
    Ok(n)
}

// Returns 1 match, 0 no-match, -1 budget exhausted; plus (steps, backtracks).
fn rg_match(pattern: &[u8], text: &[u8]) -> (i32, i32, i32) {
    let m = pattern.len();
    let n = text.len();
    let (mut pi, mut ti) = (0usize, 0usize);
    let (mut star_p, mut star_t) = (0usize, 0usize);
    let mut has_star = false;
    let mut steps = 0i32;
    let mut backs = 0i32;
    let mut matched = true;
    while ti < n {
        steps += 1;
        if steps > MAX_STEPS {
            return (-1, steps, backs);
        }
        if pi < m && (pattern[pi] == text[ti] || pattern[pi] == b'?') {
            pi += 1;
            ti += 1;
        } else if pi < m && pattern[pi] == b'*' {
            star_p = pi;
            star_t = ti;
            has_star = true;
            pi += 1;
        } else if has_star {
            backs += 1;
            steps += 1;
            if steps > MAX_STEPS {
                return (-1, steps, backs);
            }
            star_t += 1;
            ti = star_t;
            pi = star_p + 1;
        } else {
            matched = false;
            break;
        }
    }
    while pi < m && pattern[pi] == b'*' {
        steps += 1;
        if steps > MAX_STEPS {
            return (-1, steps, backs);
        }
        pi += 1;
    }
    if pi != m {
        matched = false;
    }
    (if matched { 1 } else { 0 }, steps, backs)
}

const PATTERNS: [&str; 6] = ["*", "a?c", "a?c", "a*b", "a*b", "a*b?c"];
const TEXTS: [&str; 6] = ["anything", "abc", "ac", "aaab", "aaac", "aaabcc"];
const WANT: [i32; 6] = [1, 1, 0, 1, 0, 1];

fn run_pair(pat: &str, txt: &str) -> i32 {
    let pl = match validate(pat.as_bytes(), MAX_PAT, true) {
        Ok(v) => v,
        Err(-1) => return refuse("pattern-too-long", 101),
        Err(-3) => return refuse("invalid-pattern", 108),
        Err(_) => 0,
    };
    let tl = match validate(txt.as_bytes(), MAX_TEXT, false) {
        Ok(v) => v,
        Err(-1) => return refuse("text-too-long", 101),
        Err(-3) => return refuse("invalid-text", 108),
        Err(_) => 0,
    };
    if pl == 0 || tl == 0 {
        return refuse("empty-input", 108);
    }
    let (result, steps, backs) = rg_match(pat.as_bytes(), txt.as_bytes());
    if result == -1 {
        return refuse("step-budget-exhausted", 107);
    }
    println!(
        "REGEX pattern={} text={} match={} steps={} backtracks={} plen={} tlen={}",
        pat, txt, result, steps, backs, pl, tl
    );
    0
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() == 1 {
        let mut got = [0i32; 6];
        let mut steps = [0i32; 6];
        let mut backs = [0i32; 6];
        for i in 0..6 {
            let (r, s, b) = rg_match(PATTERNS[i].as_bytes(), TEXTS[i].as_bytes());
            if r == -1 {
                std::process::exit(refuse("step-budget-exhausted", 107));
            }
            got[i] = r;
            steps[i] = s;
            backs[i] = b;
        }
        for i in 0..6 {
            if got[i] != WANT[i] {
                std::process::exit(refuse("selfcheck-failed", 108));
            }
        }
        println!(
            "REGEX app=regex_lite max_pat={} max_text={} max_steps={} selfcheck=ok cases=6",
            MAX_PAT, MAX_TEXT, MAX_STEPS
        );
        for i in 0..6 {
            println!(
                "REGEX case {} pattern={} text={} match={} want={} ok=1 steps={} backtracks={}",
                i, PATTERNS[i], TEXTS[i], got[i], WANT[i], steps[i], backs[i]
            );
        }
        println!("REGEX done cases=6");
    } else if args.len() == 3 {
        let rc = run_pair(&args[1], &args[2]);
        if rc != 0 {
            std::process::exit(rc);
        }
        println!("REGEX done");
    } else {
        std::process::exit(refuse("usage", 108));
    }
}

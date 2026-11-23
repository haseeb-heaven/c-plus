// Safe-Rust model of examples/expanded/data_2/sort_tool.cp
// No unsafe, no FFI, no external crates. Build: rustc -O --edition 2021 sort_tool.rs -o sort_tool
// Deterministic stable sorter; identical argv yields byte-identical stdout.

const MAX_ITEMS: usize = 24;
const MAX_TOKEN: usize = 24;
const MAX_INT_DIGITS: usize = 7;
const INT_LIMIT: i64 = 1_000_000;

// C+ exit codes: BOUNDS=101, UNSUPPORTED=108.
fn refuse(reason: &str, code: i32) -> i32 {
    println!("SORT error code={} reason={}", code, reason);
    2
}

fn parse_int(arg: &str) -> Result<i32, i32> {
    // -1 invalid character, -2 magnitude overflow (mirrors st_parse_int).
    let b = arg.as_bytes();
    let n = b.len();
    if n == 0 || n > MAX_INT_DIGITS + 1 {
        return Err(-2);
    }
    let (neg, start) = if b[0] == b'-' { (true, 1usize) } else { (false, 0usize) };
    if start >= n {
        return Err(-1);
    }
    let mut val: i64 = 0;
    for &c in &b[start..] {
        if c < b'0' || c > b'9' {
            return Err(-1);
        }
        val = val * 10 + (c - b'0') as i64;
        if val > INT_LIMIT {
            return Err(-2);
        }
    }
    Ok(if neg { -(val as i32) } else { val as i32 })
}

fn cmp_str(a: &[u8], b: &[u8]) -> i32 {
    // Byte-wise strcmp semantics (inputs are printable ASCII).
    let mut i = 0;
    while i < a.len() && i < b.len() {
        if a[i] != b[i] {
            return if a[i] < b[i] { -1 } else { 1 };
        }
        i += 1;
    }
    if a.len() == b.len() { 0 } else if a.len() < b.len() { -1 } else { 1 }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 4 {
        std::process::exit(refuse("usage", 108));
    }
    let mode = &args[1];
    let order = &args[2];

    let mode_int = if mode == "int" {
        true
    } else if mode == "str" {
        false
    } else {
        std::process::exit(refuse("bad-mode", 108));
    };
    let desc = if order == "asc" {
        false
    } else if order == "desc" {
        true
    } else {
        std::process::exit(refuse("bad-order", 108));
    };

    let n = args.len() - 3;
    if n < 1 {
        std::process::exit(refuse("empty-list", 108));
    }
    if n > MAX_ITEMS {
        std::process::exit(refuse("too-many-items", 101));
    }

    let mut values: Vec<i32> = Vec::with_capacity(n);
    let mut texts: Vec<Vec<u8>> = Vec::with_capacity(n);
    for arg in &args[3..] {
        if mode_int {
            match parse_int(arg) {
                Ok(v) => values.push(v),
                Err(-1) => std::process::exit(refuse("invalid-integer", 108)),
                Err(_) => std::process::exit(refuse("integer-overflow", 101)),
            }
        } else {
            let bytes = arg.as_bytes();
            if bytes.is_empty() {
                std::process::exit(refuse("empty-token", 108));
            }
            if bytes.len() >= MAX_TOKEN {
                std::process::exit(refuse("token-too-long", 101));
            }
            if bytes.iter().any(|&c| c < 0x21 || c > 0x7e) {
                std::process::exit(refuse("invalid-string", 108));
            }
            texts.push(bytes.to_vec());
        }
    }

    fn less(mode_int: bool, desc: bool, values: &[i32], texts: &[Vec<u8>], a: usize, b: usize) -> bool {
        let c = if mode_int {
            if values[a] < values[b] { -1 } else if values[a] > values[b] { 1 } else { 0 }
        } else {
            cmp_str(&texts[a], &texts[b])
        };
        if desc { c > 0 } else { c < 0 }
    }

    let mut perm: Vec<usize> = (0..n).collect();
    let mut cmp_count = 0i32;
    for i in 1..n {
        let p = perm[i];
        let mut j = i;
        while j > 0 {
            cmp_count += 1;
            if !less(mode_int, desc, &values, &texts, p, perm[j - 1]) {
                break;
            }
            perm[j] = perm[j - 1];
            j -= 1;
        }
        perm[j] = p;
    }

    let mut sorted = true;
    let mut stable = true;
    for i in 1..n {
        let prev = perm[i - 1];
        let cur = perm[i];
        if less(mode_int, desc, &values, &texts, cur, prev) {
            sorted = false;
        }
        if !less(mode_int, desc, &values, &texts, cur, prev)
            && !less(mode_int, desc, &values, &texts, prev, cur)
        {
            if prev > cur {
                stable = false;
            }
        }
    }

    println!(
        "SORT app=sort_tool mode={} order={} n={} max_items={} max_token={}",
        if mode_int { "int" } else { "str" },
        if desc { "desc" } else { "asc" },
        n, MAX_ITEMS, MAX_TOKEN
    );
    let mut fp = 0i32;
    if mode_int {
        for i in 0..n {
            println!("SORT in {}={}", i, values[i]);
        }
        for i in 0..n {
            let at = perm[i];
            println!("SORT out {}={} src={}", i, values[at], at);
            fp += values[at] * (i as i32 + 1);
        }
    } else {
        for i in 0..n {
            println!("SORT in {}={}", i, String::from_utf8_lossy(&texts[i]));
        }
        for i in 0..n {
            let at = perm[i];
            println!("SORT out {}={} src={}", i, String::from_utf8_lossy(&texts[at]), at);
            fp += (texts[at][0] as i32) * (i as i32 + 1);
        }
    }
    let si = if sorted { 1 } else { 0 };
    let stab = if stable { 1 } else { 0 };
    println!("SORT check sorted={} stable={} cmp={}", si, stab, cmp_count);
    println!("SORT fp={}", fp);
    println!("SORT done");
}

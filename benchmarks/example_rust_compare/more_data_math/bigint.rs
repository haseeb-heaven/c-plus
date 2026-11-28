// Safe-Rust model of examples/expanded/math_2/bigint.cp
// No unsafe, no FFI, no external crates. Build: rustc -O --edition 2021 bigint.rs -o bigint
// Fixed-width (16 decimal digit) big integer arithmetic. C+ `print()` joins
// args with one space and appends a newline; string::append concatenates freely.

const CAP: usize = 16;
const BASE: i32 = 10;

type Fail = (&'static str, i32); // (reason, C+ exit code)

fn cprint(parts: &[String]) {
    println!("{}", parts.join(" "));
}

fn refused(f: Fail) -> i32 {
    cprint(&[
        "BIGINT REFUSED code=".to_string(),
        f.1.to_string(),
        f.0.to_string(),
    ]);
    1
}

// Little-endian base-10 digits.
fn load(text: &str) -> Result<Vec<i32>, Fail> {
    let b = text.as_bytes();
    if b.len() > CAP {
        return Err(("overflow-input", 101));
    }
    if b.is_empty() {
        return Err(("overflow-input", 101));
    }
    if b.iter().any(|&c| c < b'0' || c > b'9') {
        return Err(("digit-syntax", 101));
    }
    let mut d = Vec::with_capacity(b.len());
    for &c in b.iter().rev() {
        d.push((c - b'0') as i32);
    }
    Ok(d)
}

fn normalize(a: &mut [i32]) -> usize {
    let mut len = a.len();
    while len > 1 && a[len - 1] == 0 {
        len -= 1;
    }
    len
}

fn cmp(a: &[i32], b: &[i32]) -> i32 {
    if a.len() != b.len() {
        return if a.len() < b.len() { -1 } else { 1 };
    }
    for i in (0..a.len()).rev() {
        if a[i] != b[i] {
            return if a[i] < b[i] { -1 } else { 1 };
        }
    }
    0
}

fn add(a: &[i32], b: &[i32]) -> Result<Vec<i32>, Fail> {
    let max = a.len().max(b.len());
    let mut r = vec![0i32; max];
    let mut carry = 0i32;
    for i in 0..max {
        let mut s = carry;
        if i < a.len() { s += a[i]; }
        if i < b.len() { s += b[i]; }
        r[i] = s % BASE;
        carry = s / BASE;
    }
    if carry != 0 {
        if max + 1 > CAP {
            return Err(("overflow-result", 101));
        }
        r.push(carry);
    }
    let new_len = normalize(&mut r);
    r.truncate(new_len);
    Ok(r)
}

fn sub(a: &[i32], b: &[i32]) -> Result<Vec<i32>, Fail> {
    if cmp(a, b) < 0 {
        return Err(("underflow", 101));
    }
    let mut r = vec![0i32; a.len()];
    let mut borrow = 0i32;
    for i in 0..a.len() {
        let mut d = a[i] - borrow;
        if i < b.len() { d -= b[i]; }
        if d < 0 {
            d += BASE;
            borrow = 1;
        } else {
            borrow = 0;
        }
        r[i] = d;
    }
    let new_len = normalize(&mut r);
    r.truncate(new_len);
    Ok(r)
}

fn mul(a: &[i32], b: &[i32]) -> Result<Vec<i32>, Fail> {
    let total = a.len() + b.len();
    if total > CAP {
        return Err(("overflow-result", 101));
    }
    let mut r = vec![0i32; total];
    for i in 0..a.len() {
        let mut carry = 0i32;
        for j in 0..b.len() {
            let pos = i + j;
            if pos >= CAP {
                return Err(("overflow-result", 101));
            }
            let v = r[pos] + a[i] * b[j] + carry;
            r[pos] = v % BASE;
            carry = v / BASE;
        }
        let mut pos = i + b.len();
        while carry != 0 {
            if pos >= CAP {
                return Err(("overflow-result", 101));
            }
            let v = r[pos] + carry;
            r[pos] = v % BASE;
            carry = v / BASE;
            pos += 1;
        }
    }
    let new_len = normalize(&mut r);
    r.truncate(new_len);
    Ok(r)
}

fn to_text(d: &[i32]) -> String {
    d.iter().rev().map(|&x| (b'0' + x as u8) as char).collect()
}

fn bi_case(op: &str, sa: &str, sb: &str) -> Result<usize, Fail> {
    let a = load(sa)?;
    let b = load(sb)?;
    let (r, symbol) = match op {
        "add" => (add(&a, &b)?, " + "),
        "sub" => (sub(&a, &b)?, " - "),
        "mul" => (mul(&a, &b)?, " * "),
        _ => return Err(("bad-op", 108)),
    };
    let len = r.len();
    cprint(&[format!(
        "BIGINT {} {}{}{} = {}",
        op,
        to_text(&a),
        symbol,
        to_text(&b),
        to_text(&r)
    )]);
    Ok(len)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() <= 1 {
        let cases = [
            ("add", "999999999999999", "1"),
            ("add", "1234567890123456", "7"),
            ("sub", "1000000000000000", "1"),
            ("mul", "99999", "99999"),
            ("mul", "1234567", "89"),
        ];
        let mut count = 0usize;
        for (op, a, b) in &cases {
            match bi_case(op, a, b) {
                Ok(_) => count += 1,
                Err(f) => std::process::exit(refused(f)),
            }
        }
        cprint(&[
            "BIGINT OK count=5 cap=16 ops=".to_string(),
            count.to_string(),
        ]);
        return;
    }

    let mode = &args[1];
    if mode.as_bytes().len() > 32 || mode.bytes().any(|x| x == 0) {
        cprint(&["BIGINT REFUSED code=101 overflow-input".to_string()]);
        std::process::exit(1);
    }
    let outcome: Result<(), Fail> = match mode.as_str() {
        "input-overflow" => bi_case("add", "10000000000000000", "1").map(|_| ()),
        "result-overflow" => bi_case("add", "9999999999999999", "1").map(|_| ()),
        "sub-underflow" => bi_case("sub", "1", "2").map(|_| ()),
        "mul-overflow" => bi_case("mul", "999999999", "999999999").map(|_| ()),
        "bad-digit" => bi_case("add", "12x4", "1").map(|_| ()),
        _ => Err(("bad-mode", 108)),
    };
    match outcome {
        // C+ prints the accumulated line only in the no-argument branch;
        // explicit-mode success (not exercised by fixed inputs) stays silent.
        Ok(()) => {}
        Err(f) => std::process::exit(refused(f)),
    }
}

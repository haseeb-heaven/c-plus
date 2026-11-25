// Safe-Rust model of examples/expanded/apps_2/matrix.cp
// No unsafe, no FFI, no external crates. Deterministic small integer matrices.
// Build: rustc -O --edition 2021 matrix.rs -o matrix

const MAT_MAX_DIM: usize = 4;
const MAT_FIELD_CAP: usize = 48;
const MAT_DATA_CAP: usize = 512;
const MAT_VALUE_LIMIT: i64 = 1_000_000_000;
const MAT_SAFE: i128 = 4_000_000_000_000_000_000;

fn refuse(reason: &str) -> i32 {
    println!("MAT error {}", reason);
    1
}

fn parse_full(text: &str) -> Option<i64> {
    let t = text.trim_start_matches(|c: char| c.is_ascii_whitespace());
    if t.is_empty() {
        return None;
    }
    let (neg, digits) = match t.as_bytes()[0] {
        b'-' => (true, &t[1..]),
        b'+' => (false, &t[1..]),
        _ => (false, t),
    };
    if digits.is_empty() || !digits.bytes().all(|c| c.is_ascii_digit()) {
        return None;
    }
    let v = digits.parse::<i64>().ok()?;
    Some(if neg { -v } else { v })
}

fn parse_dim(text: &str) -> Result<usize, &'static str> {
    match parse_full(text) {
        Some(v) if v < 0 => Err("bad-dims"),
        Some(v) if v > MAT_MAX_DIM as i64 => Err("oversized-dims"),
        Some(v) if v < 1 => Err("bad-dims"),
        Some(v) => Ok(v as usize),
        None => Err("bad-dims"),
    }
}

fn parse_value(text: &str) -> Option<i64> {
    parse_full(text).filter(|&v| v >= -MAT_VALUE_LIMIT && v <= MAT_VALUE_LIMIT)
}

fn flat(data: &str, rows: usize, cols: usize) -> Option<Vec<i64>> {
    if data.is_empty() || data.len() + 1 > MAT_DATA_CAP {
        return None;
    }
    let fields: Vec<&str> = data.split(',').collect();
    let expected = rows * cols;
    if fields.len() != expected {
        return None;
    }
    let mut vals = Vec::with_capacity(expected);
    for f in fields {
        if f.is_empty() || f.len() + 1 > MAT_FIELD_CAP {
            return None;
        }
        vals.push(parse_value(f)?);
    }
    Some(vals)
}

fn load(rtext: &str, ctext: &str, data: &str) -> Result<(Vec<Vec<i64>>, usize, usize), &'static str> {
    let r = parse_dim(rtext)?;
    let c = parse_dim(ctext)?;
    let vals = flat(data, r, c).ok_or("bad-data")?;
    let mut m = vec![vec![0i64; c]; r];
    let mut k = 0;
    for i in 0..r {
        for j in 0..c {
            m[i][j] = vals[k];
            k += 1;
        }
    }
    Ok((m, r, c))
}

fn checked_mul(a: i64, b: i64) -> Option<i64> {
    let ua = (a as i128).unsigned_abs() as i128;
    let ub = (b as i128).unsigned_abs() as i128;
    if ua > MAT_SAFE || ub > MAT_SAFE {
        return None;
    }
    if ua == 0 || ub == 0 {
        return Some(0);
    }
    if ub > MAT_SAFE / ua {
        return None;
    }
    Some(a.wrapping_mul(b))
}

fn checked_add(a: i64, b: i64) -> Option<i64> {
    a.checked_add(b)
}

fn checked_sub(a: i64, b: i64) -> Option<i64> {
    if b == i64::MIN {
        return None;
    }
    checked_add(a, -b)
}

fn mul3(a: i64, b: i64, c: i64) -> Option<i64> {
    checked_mul(a, b).and_then(|ab| checked_mul(ab, c))
}

fn det3(m: &[Vec<i64>]) -> Option<i64> {
    let (a00, a01, a02) = (m[0][0], m[0][1], m[0][2]);
    let (a10, a11, a12) = (m[1][0], m[1][1], m[1][2]);
    let (a20, a21, a22) = (m[2][0], m[2][1], m[2][2]);
    let mut t = [0i64; 6];
    t[0] = mul3(a00, a11, a22)?;
    t[1] = mul3(a01, a12, a20)?;
    t[2] = mul3(a02, a10, a21)?;
    t[3] = mul3(a02, a11, a20)?;
    t[4] = mul3(a01, a10, a22)?;
    t[5] = mul3(a00, a12, a21)?;
    let mut acc = 0i64;
    acc = checked_add(acc, t[0])?;
    acc = checked_add(acc, t[1])?;
    acc = checked_add(acc, t[2])?;
    acc = checked_sub(acc, t[3])?;
    acc = checked_sub(acc, t[4])?;
    acc = checked_sub(acc, t[5])?;
    Some(acc)
}

fn submatrix3(m: &[Vec<i64>], skip_row: usize, skip_col: usize) -> Vec<Vec<i64>> {
    let rows: Vec<usize> = (1..4).filter(|&r| r != skip_row).collect();
    let cols: Vec<usize> = (1..4).filter(|&c| c != skip_col).collect();
    rows.iter()
        .map(|&r| cols.iter().map(|&c| m[r][c]).collect())
        .collect()
}

fn det(m: &[Vec<i64>], n: usize) -> Option<i64> {
    match n {
        1 => Some(m[0][0]),
        2 => {
            let term = checked_mul(m[0][0], m[1][1])?;
            let minor = checked_mul(m[0][1], m[1][0])?;
            checked_sub(term, minor)
        }
        3 => det3(m),
        _ => {
            // n == 4: cofactor expansion along the first row.
            let mut acc = 0i64;
            for j in 0..4 {
                let minor = det3(&submatrix3(m, 0, j))?;
                let term = checked_mul(m[0][j], minor)?;
                acc = if j % 2 == 0 {
                    checked_add(acc, term)?
                } else {
                    checked_sub(acc, term)?
                };
            }
            Some(acc)
        }
    }
}

fn print_matrix(op: &str, m: &[Vec<i64>]) {
    println!("MAT {} {} {}", op, m.len(), m[0].len());
    for (i, row) in m.iter().enumerate() {
        for (j, &v) in row.iter().enumerate() {
            println!("MAT {} {} {}", i, j, v);
        }
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        std::process::exit(refuse("usage"));
    }
    let op = args[1].as_str();

    if op == "det" || op == "determinant" {
        if args.len() != 5 {
            std::process::exit(refuse("bad-args"));
        }
        let (m, rows, cols) = match load(&args[2], &args[3], &args[4]) {
            Ok(v) => v,
            Err(reason) => std::process::exit(refuse(reason)),
        };
        if rows != cols {
            std::process::exit(refuse("not-square"));
        }
        let value = match det(&m, rows) {
            Some(v) => v,
            None => std::process::exit(refuse("overflow")),
        };
        println!("MAT determinant {} {}", rows, value);
        return;
    }

    if op != "add" && op != "multiply" {
        std::process::exit(refuse("unknown-op"));
    }
    if args.len() != 8 {
        std::process::exit(refuse("bad-args"));
    }
    let (a, r1, c1) = match load(&args[2], &args[3], &args[4]) {
        Ok(v) => v,
        Err(reason) => std::process::exit(refuse(reason)),
    };
    let (b, r2, c2) = match load(&args[5], &args[6], &args[7]) {
        Ok(v) => v,
        Err(reason) => std::process::exit(refuse(reason)),
    };

    if op == "add" {
        if r1 != r2 || c1 != c2 {
            std::process::exit(refuse("dim-mismatch"));
        }
        let mut out = vec![vec![0i64; c1]; r1];
        for i in 0..r1 {
            for j in 0..c1 {
                out[i][j] = match checked_add(a[i][j], b[i][j]) {
                    Some(v) => v,
                    None => std::process::exit(refuse("overflow")),
                };
            }
        }
        print_matrix("add", &out);
        return;
    }

    if c1 != r2 {
        std::process::exit(refuse("dim-mismatch"));
    }
    let mut out = vec![vec![0i64; c2]; r1];
    for i in 0..r1 {
        for j in 0..c2 {
            let mut acc = 0i64;
            for k in 0..c1 {
                let product = match checked_mul(a[i][k], b[k][j]) {
                    Some(v) => v,
                    None => std::process::exit(refuse("overflow")),
                };
                acc = match checked_add(acc, product) {
                    Some(v) => v,
                    None => std::process::exit(refuse("overflow")),
                };
            }
            out[i][j] = acc;
        }
    }
    print_matrix("multiply", &out);
}

// Safe-Rust model of examples/expanded/apps_2/csv_tool.cp
// No unsafe, no FFI, no external crates. Deterministic CSV column aggregate.
// Build: rustc -O --edition 2021 csv_tool.rs -o csv_tool

const CSV_LINE_CAP: usize = 256;
const CSV_FIELD_CAP: usize = 40;
const CSV_MAX_COLS: usize = 64;
const CSV_MAX_ROWS: i64 = 100_000;

fn refuse(reason: &str) -> i32 {
    println!("CSV error {}", reason);
    1
}

// strtol-like: full-string decimal integer, no overflow.
fn parse_long(text: &str) -> Option<i64> {
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

fn field_len(record: &str, index: usize) -> Option<usize> {
    let mut current = 0usize;
    let mut start = 0usize;
    for (i, ch) in record.bytes().chain(std::iter::once(b',')).enumerate() {
        if ch == b',' {
            if current == index {
                return Some(i - start);
            }
            current += 1;
            start = i + 1;
        }
    }
    None
}

fn field_at(record: &str, index: usize) -> Option<&str> {
    let mut current = 0usize;
    let mut start = 0usize;
    for (i, ch) in record.bytes().chain(std::iter::once(b',')).enumerate() {
        if ch == b',' {
            if current == index {
                return Some(&record[start..i]);
            }
            current += 1;
            start = i + 1;
        }
    }
    None
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 4 {
        println!("CSV error usage");
        std::process::exit(2);
    }
    let path = &args[1];
    let col_text = &args[2];
    let mode = &args[3];

    let col_long = match parse_long(col_text) {
        Some(v) if v >= 0 && (v as usize) < CSV_MAX_COLS => v as usize,
        _ => std::process::exit(refuse("column-out-of-range")),
    };
    let is_avg = match mode.as_str() {
        "sum" => false,
        "avg" => true,
        _ => std::process::exit(refuse("bad-mode")),
    };

    let data = match std::fs::read(path) {
        Ok(d) => d,
        Err(_) => std::process::exit(refuse("cannot-open")),
    };
    let text = match String::from_utf8(data) {
        Ok(t) => t,
        Err(_) => std::process::exit(refuse("io-error")),
    };

    // file.hp::read_line strips the line terminator; split accordingly.
    let mut lines: Vec<&str> = text.split('\n').collect();
    if lines.last() == Some(&"") {
        lines.pop(); // trailing newline does not create a final empty row
    }

    let mut rows: i64 = 0;
    let mut cols: usize = 0;
    let mut sum: i64 = 0;
    let mut reason: Option<&str> = None;

    for line in lines {
        let line = line.strip_suffix('\r').unwrap_or(line);
        if rows >= CSV_MAX_ROWS {
            reason = Some("too-many-rows");
            break;
        }
        if line.len() + 1 > CSV_LINE_CAP {
            reason = Some("line-too-long");
            break;
        }
        let row_cols = line.split(',').count();
        if row_cols < 1 || row_cols > CSV_MAX_COLS {
            reason = Some("malformed-row");
            break;
        }
        if rows == 0 {
            cols = row_cols;
            if col_long >= cols {
                reason = Some("column-out-of-range");
                break;
            }
        } else if row_cols != cols {
            reason = Some("malformed-row");
            break;
        }
        let value = match field_len(line, col_long)
            .filter(|&len| len > 0 && len + 1 <= CSV_FIELD_CAP)
            .and_then(|_| field_at(line, col_long))
            .and_then(parse_long)
        {
            Some(v) => v,
            None => {
                reason = Some("not-numeric");
                break;
            }
        };
        match sum.checked_add(value) {
            Some(s) => sum = s,
            None => {
                reason = Some("overflow");
                break;
            }
        }
        rows += 1;
    }

    if let Some(r) = reason {
        std::process::exit(refuse(r));
    }
    if rows == 0 || cols == 0 {
        std::process::exit(refuse("empty-input"));
    }

    if is_avg {
        if rows == 0 {
            std::process::exit(refuse("empty-input"));
        }
        println!("CSV avg {} {}", col_long, sum / rows);
    } else {
        println!("CSV sum {} {}", col_long, sum);
    }
    println!("CSV rows {}", rows);
}

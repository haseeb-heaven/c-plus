//! Safe-Rust equivalent of `examples/crt/scanf_s.c` (checked scanf_s).
//!
//! The C example is intentionally a compile-time API demonstration: it
//! includes `safe_format.h` but never calls `scanf_s`; it simply prints
//! `scanf_s: ok` and exits 0. The Rust equivalent preserves that observable
//! behaviour exactly and deliberately does NOT read stdin (doing so would
//! block / consume input and change the comparison result).
//!
//! Refusal modelling: the C `cplus_scanf_s` returns -1 for a null/non-literal
//! format and refuses unbounded `%s`/`%[` at runtime. Rust has no runtime
//! format string (conversion is an explicit bounded parser), so those refusal
//! paths are unrepresentable. The helper below models the callable API with
//! `Result<Option<i32>>`: Err = I/O error, None = EOF/no conversion (C's EOF),
//! Some(v) = one successful conversion. It is kept for API parity only.
//! No unsafe blocks, no FFI.

use std::io::{self, BufRead};
use std::process::ExitCode;

/// Bounded `%d` scan over text (None == no conversion / EOF).
#[allow(dead_code)]
fn scan_i32(text: &str) -> Option<i32> {
    let bytes = text.as_bytes();
    let mut i = 0usize;
    while i < bytes.len() && (bytes[i] as char).is_ascii_whitespace() {
        i += 1;
    }
    let mut sign: i64 = 1;
    if i < bytes.len() && (bytes[i] == b'+' || bytes[i] == b'-') {
        if bytes[i] == b'-' {
            sign = -1;
        }
        i += 1;
    }
    let start = i;
    let mut value: i64 = 0;
    while i < bytes.len() && bytes[i].is_ascii_digit() {
        value = value.saturating_mul(10).saturating_add((bytes[i] - b'0') as i64);
        i += 1;
    }
    if i == start {
        return None;
    }
    let signed = sign * value;
    if signed < i32::MIN as i64 || signed > i32::MAX as i64 {
        return None;
    }
    Some(signed as i32)
}

/// Safe analogue of `cplus_scanf_s("%d", &v)` reading one line from stdin.
/// Not called by `main`, matching the C example (API demonstration only).
#[allow(dead_code)]
fn scanf_i32_stdin() -> Result<Option<i32>, io::Error> {
    let stdin = io::stdin();
    let mut line = String::new();
    if stdin.lock().read_line(&mut line)? == 0 {
        return Ok(None);
    }
    Ok(scan_i32(&line))
}

fn main() -> ExitCode {
    println!("scanf_s: ok");
    ExitCode::SUCCESS
}

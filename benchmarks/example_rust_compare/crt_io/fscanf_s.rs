//! Safe-Rust equivalent of `examples/crt/fscanf_s.c` (checked fscanf_s).
//!
//! The C example opens a `wb+` temp file, writes "42" with `cplus_fprintf_s`,
//! rewinds, reads an int with `cplus_fscanf_s(f, "%d", &v)`, and prints
//! `fscanf_s: ok` only when 1 conversion succeeded and v == 42; otherwise 1.
//!
//! Refusal modelling: `cplus_fscanf_s` returns -1 for a null stream/format,
//! refuses non-literal formats and unbounded `%s`/`%[` at runtime. Rust has no
//! format-string value to validate (parsing is explicit and inherently
//! bounded), so those refusal paths are unrepresentable. The `Result` models
//! I/O failure; `Option` models EOF / no conversion (C's EOF / count 0).
//! No unsafe blocks, no FFI.

use std::fs::{self, File, OpenOptions};
use std::io::{self, BufReader, Read, Seek, SeekFrom, Write};
use std::path::Path;
use std::process::ExitCode;

/// `cplus_fopen_s(name, "wb+")`.
fn fopen_s_rw(path: &Path) -> Result<File, io::Error> {
    OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(true)
        .open(path)
}

/// Bounded `%d` scan: skip whitespace, optional sign, ASCII digits only.
/// Mimics C's `strtol`-based `%d` conversion; None == no conversion / EOF.
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

/// Safe analogue of `cplus_fscanf_s(f, "%d", &v)` over a reader.
fn fscanf_i32<R: Read>(r: R) -> Result<Option<i32>, io::Error> {
    let mut reader = BufReader::new(r);
    let mut text = String::new();
    reader.read_to_string(&mut text)?;
    Ok(scan_i32(&text))
}

fn main() -> ExitCode {
    let path = std::env::temp_dir().join("cplus_fscanf.tmp");
    let mut f = match fopen_s_rw(&path) {
        Ok(f) => f,
        Err(_) => return ExitCode::from(1),
    };
    if write!(f, "42").is_err() {
        drop(f);
        let _ = fs::remove_file(&path);
        return ExitCode::from(1);
    }
    if f.seek(SeekFrom::Start(0)).is_err() {
        drop(f);
        let _ = fs::remove_file(&path);
        return ExitCode::from(1);
    }
    let converted = fscanf_i32(&mut f).unwrap_or(None);
    drop(f);
    let _ = fs::remove_file(&path);
    match converted {
        Some(42) => {}
        _ => return ExitCode::from(1),
    }
    println!("fscanf_s: ok");
    ExitCode::SUCCESS
}

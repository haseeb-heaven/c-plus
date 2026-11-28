//! Safe-Rust equivalent of `examples/crt/fprintf_s.c` (checked fprintf_s).
//!
//! The C example opens a `wb+` temp file, writes "ok" into it via
//! `cplus_fprintf_s(f, "%s", "ok")`, closes, removes it, prints `fprintf_s: ok`
//! and exits 0.
//!
//! Refusal modelling: `cplus_fprintf_s` returns -1 for a null stream/format and
//! refuses non-literal formats / `%n` at runtime (via the `_checked` macro).
//! Rust's `write!` validates the format string at compile time and has no
//! runtime format-string value at all, so those refusal paths are
//! unrepresentable rather than silently accepted. An I/O write error maps to
//! `Result::Err` (the analogue of C's -1). No unsafe blocks, no FFI.

use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
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

fn main() -> ExitCode {
    let path = std::env::temp_dir().join("cplus_fprintf.tmp");
    let mut f = match fopen_s_rw(&path) {
        Ok(f) => f,
        Err(_) => return ExitCode::from(1),
    };
    // Safe analogue of `cplus_fprintf_s(f, "%s", "ok")`: writes "ok" to the
    // file (not stdout), exactly as vfprintf does in the C example.
    if write!(f, "{}", "ok").is_err() {
        drop(f);
        let _ = fs::remove_file(&path);
        return ExitCode::from(1);
    }
    drop(f);
    let _ = fs::remove_file(&path);
    println!("fprintf_s: ok");
    ExitCode::SUCCESS
}

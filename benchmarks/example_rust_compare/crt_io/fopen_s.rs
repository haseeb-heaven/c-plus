//! Safe-Rust equivalent of `examples/crt/fopen_s.c` (checked fopen_s).
//!
//! The C example opens a temp file with `cplus_fopen_s(name, "wb")`, closes it,
//! removes it, prints `fopen_s: ok` and exits 0. A failed open returns 1 with
//! no stdout output.
//!
//! Refusal modelling: `cplus_fopen_s` returns NULL for a null name/mode or a
//! full live-stream table. In safe Rust a null `&str`/`Path` cannot exist and
//! there is no global stream table, so the only modelled failure is an I/O
//! open error, returned as `Result::Err` (the analogue of C's NULL). This file
//! uses only safe std APIs: no unsafe blocks, no FFI, no raw pointers.

use std::fs::{self, File, OpenOptions};
use std::io;
use std::path::Path;
use std::process::ExitCode;

/// Safe analogue of `cplus_fopen_s(name, "wb")`.
/// `Err` models the C NULL return; no `unsafe`/FFI involved.
fn fopen_s_write(path: &Path) -> Result<File, io::Error> {
    OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(path)
}

fn main() -> ExitCode {
    let path = std::env::temp_dir().join("cplus_fopen_s.tmp");
    let file = match fopen_s_write(&path) {
        Ok(f) => f,
        Err(_) => return ExitCode::from(1),
    };
    // Rust closes deterministically on drop; a double-close is unrepresentable.
    drop(file);
    let _ = fs::remove_file(&path);
    println!("fopen_s: ok");
    ExitCode::SUCCESS
}

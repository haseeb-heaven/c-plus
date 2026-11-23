//! Safe-Rust equivalent of `examples/crt/fclose_s.c` (checked fclose_s).
//!
//! The C example opens a temp file, checks that `cplus_fclose_s(f) == 0`,
//! removes the file, prints `fclose_s: ok` and exits 0. Any failure returns 1.
//!
//! Refusal modelling: `cplus_fclose_s` returns EOF for a null stream or a
//! double close (tracked via a global closed-stream table). Safe Rust makes a
//! double close impossible because the `File` is consumed by value; there is
//! no null reference. The C "close failed" branch is modelled with `Result`:
//! `sync_all()` forces an I/O error to surface before the file is dropped.
//! No unsafe blocks, no FFI.

use std::fs::{self, File, OpenOptions};
use std::io;
use std::path::Path;
use std::process::ExitCode;

fn fopen_s_write(path: &Path) -> Result<File, io::Error> {
    OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(path)
}

/// Safe analogue of `cplus_fclose_s(stream)`.
/// Ownership makes double-close unrepresentable; `sync_all` surfaces a close
/// error as `Result::Err` (the analogue of the C non-zero return).
fn fclose_s(file: File) -> Result<(), io::Error> {
    file.sync_all()?;
    drop(file);
    Ok(())
}

fn main() -> ExitCode {
    let path = std::env::temp_dir().join("cplus_fclose_s.tmp");
    let file = match fopen_s_write(&path) {
        Ok(f) => f,
        Err(_) => return ExitCode::from(1),
    };
    match fclose_s(file) {
        Ok(()) => {}
        Err(_) => return ExitCode::from(1),
    }
    let _ = fs::remove_file(&path);
    println!("fclose_s: ok");
    ExitCode::SUCCESS
}

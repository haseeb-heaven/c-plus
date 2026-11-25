//! Safe-Rust equivalent of `examples/crt/fwrite_s.c` (checked fwrite_s).
//!
//! The C example opens a `wb` temp file, writes "abc" via
//! `cplus_fwrite_s("abc", 3, 1, 3, f)`, checks that 3 items were written,
//! removes the file, prints `fwrite_s: ok` and exits 0; otherwise 1.
//!
//! Refusal modelling: as in C, zero `size`/`count` (or an empty buffer) yields
//! 0, and the count is clamped to `bmax / size` where `bmax` is `buf.len()`.
//! No unsafe blocks, no FFI.

use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::path::Path;
use std::process::ExitCode;

/// `cplus_fopen_s(name, "wb")`.
fn fopen_s_write(path: &Path) -> Result<File, io::Error> {
    OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .open(path)
}

/// Safe analogue of `cplus_fwrite_s(buf, bmax, size, count, stream)`.
fn fwrite_s<W: Write>(w: &mut W, buf: &[u8], size: usize, count: usize) -> Result<usize, io::Error> {
    if size == 0 || count == 0 || buf.is_empty() {
        return Ok(0);
    }
    let n = count.min(buf.len() / size);
    w.write_all(&buf[..n * size])?;
    Ok(n)
}

fn main() -> ExitCode {
    let path = std::env::temp_dir().join("cplus_fwrite_s.tmp");
    let mut f = match fopen_s_write(&path) {
        Ok(f) => f,
        Err(_) => return ExitCode::from(1),
    };
    let n = fwrite_s(&mut f, b"abc", 1, 3).unwrap_or(0);
    drop(f);
    let _ = fs::remove_file(&path);
    if n != 3 {
        return ExitCode::from(1);
    }
    println!("fwrite_s: ok");
    ExitCode::SUCCESS
}

//! Safe-Rust equivalent of `examples/crt/fread_s.c` (checked fread_s).
//!
//! The C example opens a `wb+` temp file, writes "abc" via `cplus_fwrite_s`,
//! rewinds, reads up to 3 bytes via `cplus_fread_s(buf, 8, 1, 3)`, and prints
//! `fread_s: ok` only when 3 items were read and buf == "abc". Otherwise 1.
//!
//! Refusal modelling: the C helpers return 0 for a null buffer/stream or a
//! zero size/count, and clamp `count` to `bmax / size`. A Rust `&[u8]` slice is
//! inherently bounded, so `buf.len()` is the C `bmax`; zero `size`/`count` or
//! an empty buffer returns `Ok(0)` (matching C's 0). No unsafe blocks / FFI.

use std::fs::{self, File, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
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

/// Safe analogue of `cplus_fwrite_s(buf, bmax, size, count, stream)`;
/// `buf.len()` supplies the inherent `bmax`.
fn fwrite_s<W: Write>(w: &mut W, buf: &[u8], size: usize, count: usize) -> Result<usize, io::Error> {
    if size == 0 || count == 0 || buf.is_empty() {
        return Ok(0);
    }
    let n = count.min(buf.len() / size);
    w.write_all(&buf[..n * size])?;
    Ok(n)
}

/// Safe analogue of `cplus_fread_s(buf, bmax, size, count, stream)`.
/// Returns the number of complete items read (like `fread`).
fn fread_s<R: Read>(r: &mut R, buf: &mut [u8], size: usize, count: usize) -> Result<usize, io::Error> {
    if size == 0 || count == 0 || buf.is_empty() {
        return Ok(0);
    }
    let n = count.min(buf.len() / size);
    let want = n * size;
    let mut filled = 0usize;
    while filled < want {
        match r.read(&mut buf[filled..want]) {
            Ok(0) => break,
            Ok(k) => filled += k,
            Err(ref e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(e),
        }
    }
    Ok(filled / size)
}

fn main() -> ExitCode {
    let path = std::env::temp_dir().join("cplus_fread_s.tmp");
    let mut f = match fopen_s_rw(&path) {
        Ok(f) => f,
        Err(_) => return ExitCode::from(1),
    };
    if fwrite_s(&mut f, b"abc", 1, 3).unwrap_or(0) != 3 {
        drop(f);
        let _ = fs::remove_file(&path);
        return ExitCode::from(1);
    }
    if f.seek(SeekFrom::Start(0)).is_err() {
        drop(f);
        let _ = fs::remove_file(&path);
        return ExitCode::from(1);
    }
    let mut buf = [0u8; 8];
    let n = fread_s(&mut f, &mut buf, 1, 3).unwrap_or(0);
    drop(f);
    let _ = fs::remove_file(&path);
    if n != 3 || &buf[..3] != b"abc" {
        return ExitCode::from(1);
    }
    println!("fread_s: ok");
    ExitCode::SUCCESS
}

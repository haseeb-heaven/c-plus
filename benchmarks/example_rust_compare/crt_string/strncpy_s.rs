// Safe-Rust equivalent of examples/crt/strncpy_s.c.
//
// cplus_strncpy_s(dest, dmax, src, slen) copies at most min(slen, strlen(src))
// bytes from src and always NUL-terminates; it returns 0 on success and a
// positive safeclib constraint code on violation. Here `dmax` is the explicit
// capacity, `slen` the copy bound, and `Result` models refusal. No unsafe/FFI.
use std::process::ExitCode;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum CopyError {
    NullSource,
    ZeroCapacity,
    NotEnoughSpace,
    InvalidBoundary,
}

/// Model of `cplus_strncpy_s(dest, dmax, src, slen)`.
fn strncpy_s(dmax: usize, src: Option<&str>, slen: usize) -> Result<String, CopyError> {
    let src = src.ok_or(CopyError::NullSource)?;
    if dmax == 0 {
        return Err(CopyError::ZeroCapacity);
    }
    if slen == 0 {
        // safeclib: slen == 0 writes a NUL into dest and reports EOK.
        return Ok(String::new());
    }
    let n = slen.min(src.len());
    // str::get avoids a panic if n lands inside a UTF-8 code point; C works on
    // raw bytes, this safe port refuses such a split instead.
    let prefix = src.get(..n).ok_or(CopyError::InvalidBoundary)?;
    if prefix.len() + 1 > dmax {
        return Err(CopyError::NotEnoughSpace);
    }
    Ok(prefix.to_owned())
}

fn main() -> ExitCode {
    // char d[8]; cplus_strncpy_s(d, sizeof(d), "abcdef", 3)
    let d = match strncpy_s(8, Some("abcdef"), 3) {
        Ok(s) => s,
        Err(_) => return ExitCode::from(1),
    };
    // strcmp(d, "abc") != 0 -> return 1
    if d != "abc" {
        return ExitCode::from(1);
    }

    println!("strncpy_s: ok");
    ExitCode::SUCCESS
}

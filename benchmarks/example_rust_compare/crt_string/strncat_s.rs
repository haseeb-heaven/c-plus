// Safe-Rust equivalent of examples/crt/strncat_s.c.
//
// cplus_strncat_s(dest, dmax, src, slen) appends at most slen bytes of src and
// NUL-terminates; 0 on success, positive safeclib constraint code on violation
// (dest cleared). `dmax` is an explicit capacity, `Result` models refusal,
// `String` owns the buffer. No unsafe, no FFI.
use std::process::ExitCode;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ConcatError {
    NullSource,
    ZeroCapacity,
    NotEnoughSpace,
    InvalidBoundary,
}

/// Model of `cplus_strncat_s(dest, dmax, src, slen)`.
fn strncat_s(
    dmax: usize,
    dest: &mut String,
    src: Option<&str>,
    slen: usize,
) -> Result<(), ConcatError> {
    let src = src.ok_or(ConcatError::NullSource)?;
    if dmax == 0 {
        return Err(ConcatError::ZeroCapacity);
    }
    let n = slen.min(src.len());
    let appendix = src.get(..n).ok_or(ConcatError::InvalidBoundary)?;
    if dest.len() + appendix.len() + 1 > dmax {
        dest.clear(); // safeclib clears dest on a runtime-constraint violation
        return Err(ConcatError::NotEnoughSpace);
    }
    dest.push_str(appendix);
    Ok(())
}

fn main() -> ExitCode {
    // char d[16] = "hi"; cplus_strncat_s(d, sizeof(d), "world", 2)
    let mut d = String::from("hi");
    if strncat_s(16, &mut d, Some("world"), 2).is_err() {
        return ExitCode::from(1);
    }
    if d != "hiwo" {
        return ExitCode::from(1);
    }

    println!("strncat_s: ok");
    ExitCode::SUCCESS
}

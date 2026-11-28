// Safe-Rust equivalent of examples/crt/strcmp_s.c.
//
// cplus_strcmp_s(dest, dmax, src, &indicator) returns 0 on success and writes
// strcmp's sign to *indicator (-1/0/+1); a NULL dest is a constraint violation
// (ESNULLP = 400) with a nonzero return. `Option<&str>` models the NULL pointer
// and `Result<i32, _>` models the return code plus out-parameter. No unsafe/FFI.
use std::cmp::Ordering;
use std::process::ExitCode;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum CmpError {
    NullDest,
    NullSource,
    ZeroCapacity,
}

/// Model of `cplus_strcmp_s(dest, dmax, src, &indicator)` -> `Ok(indicator)`.
fn strcmp_s(dest: Option<&str>, dmax: usize, src: Option<&str>) -> Result<i32, CmpError> {
    let dest = dest.ok_or(CmpError::NullDest)?;
    let src = src.ok_or(CmpError::NullSource)?;
    if dmax == 0 {
        return Err(CmpError::ZeroCapacity);
    }
    // safeclib compares at most dmax characters of each string.
    let a: String = dest.chars().take(dmax).collect();
    let b: String = src.chars().take(dmax).collect();
    Ok(match a.cmp(&b) {
        Ordering::Less => -1,
        Ordering::Equal => 0,
        Ordering::Greater => 1,
    })
}

fn main() -> ExitCode {
    // cplus_strcmp_s("abc", 3, "abc", &indicator): success, indicator == 0
    match strcmp_s(Some("abc"), 3, Some("abc")) {
        Ok(indicator) if indicator == 0 => {}
        _ => return ExitCode::from(1),
    }

    // cplus_strcmp_s("abc", 3, "abd", &indicator): indicator < 0
    match strcmp_s(Some("abc"), 3, Some("abd")) {
        Ok(indicator) if indicator < 0 => {}
        _ => return ExitCode::from(1),
    }

    // cplus_strcmp_s(NULL, 3, "abd", &indicator): constraint -> must fail.
    if strcmp_s(None, 3, Some("abd")).is_ok() {
        return ExitCode::from(1);
    }

    println!("strcmp_s: ok");
    ExitCode::SUCCESS
}

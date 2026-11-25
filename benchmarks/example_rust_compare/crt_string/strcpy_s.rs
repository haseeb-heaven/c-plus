// Safe-Rust equivalent of examples/crt/strcpy_s.c.
//
// C contract (stdlib/safe/safe_string.h, safeclib-backed):
//   cplus_strcpy_s(dest, dmax, src) -> 0 on success; nonzero constraint code
//   (e.g. ESNOSPC = 406) when strlen(src) + 1 > dmax, and dest is cleared.
// The `dmax` byte capacity is passed explicitly; `Result` models the refusal.
// `String` replaces the fixed C char buffer. No unsafe, no FFI.
use std::process::ExitCode;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum CopyError {
    NullSource,
    ZeroCapacity,
    NotEnoughSpace,
}

/// Model of `cplus_strcpy_s(dest, dmax, src)`. `None` src models a C NULL.
fn strcpy_s(dmax: usize, src: Option<&str>) -> Result<String, CopyError> {
    let src = src.ok_or(CopyError::NullSource)?;
    if dmax == 0 {
        return Err(CopyError::ZeroCapacity);
    }
    // The C buffer needs room for the NUL terminator.
    if src.len() + 1 > dmax {
        return Err(CopyError::NotEnoughSpace);
    }
    Ok(src.to_owned())
}

fn main() -> ExitCode {
    // char name[32]; cplus_strcpy_s(name, sizeof(name), "C+")
    let name = match strcpy_s(32, Some("C+")) {
        Ok(s) => s,
        Err(_) => return ExitCode::from(1),
    };
    // strcmp(name, "C+") != 0 -> return 1
    if name != "C+" {
        return ExitCode::from(1);
    }

    // char small[4]; cplus_strcpy_s(small, sizeof(small), "Too long")
    // C expects the constraint violation (8 bytes + NUL > 4), so the C test
    // `== 0` must be false.
    if strcpy_s(4, Some("Too long")).is_ok() {
        return ExitCode::from(1);
    }

    println!("strcpy_s: ok");
    ExitCode::SUCCESS
}

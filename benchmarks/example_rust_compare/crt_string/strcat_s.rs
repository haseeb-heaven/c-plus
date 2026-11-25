// Safe-Rust equivalent of examples/crt/strcat_s.c.
//
// cplus_strcat_s(dest, dmax, src) requires
// strlen(dest) + strlen(src) + 1 <= dmax, returns 0 on success and a positive
// constraint code on violation (dest is cleared on violation). `dmax` is an
// explicit capacity, `Result` models refusal, `String` owns the buffer.
// No unsafe, no FFI.
use std::process::ExitCode;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ConcatError {
    NullSource,
    ZeroCapacity,
    NotEnoughSpace,
}

/// Model of `cplus_strcat_s(dest, dmax, src)`; mutates dest only on success.
fn strcat_s(dmax: usize, dest: &mut String, src: Option<&str>) -> Result<(), ConcatError> {
    let src = src.ok_or(ConcatError::NullSource)?;
    if dmax == 0 {
        return Err(ConcatError::ZeroCapacity);
    }
    // Room for the existing text, the appended text and the NUL terminator.
    if dest.len() + src.len() + 1 > dmax {
        dest.clear(); // safeclib clears dest on a runtime-constraint violation
        return Err(ConcatError::NotEnoughSpace);
    }
    dest.push_str(src);
    Ok(())
}

fn main() -> ExitCode {
    // char d[16] = "hi"; cplus_strcat_s(d, sizeof(d), "!")
    let mut d = String::from("hi");
    if strcat_s(16, &mut d, Some("!")).is_err() {
        return ExitCode::from(1);
    }
    if d != "hi!" {
        return ExitCode::from(1);
    }

    // cplus_strcat_s(d, 4, "world") -- 3 + 5 + 1 > 4, so C expects refusal.
    if strcat_s(4, &mut d, Some("world")).is_ok() {
        return ExitCode::from(1);
    }

    println!("strcat_s: ok");
    ExitCode::SUCCESS
}

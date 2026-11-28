// Safe-Rust equivalent of examples/crt/strnlen_s.c.
//
// cplus_strnlen_s(str, smax) is the non-errno_t bounded length query: it
// returns min(strlen(str), smax), and 0 for a NULL pointer (not a constraint
// violation). `Option<&str>` models NULL; no unsafe, no FFI.
use std::process::ExitCode;

/// Model of `cplus_strnlen_s(str, smax)`.
fn strnlen_s(s: Option<&str>, smax: usize) -> usize {
    match s {
        None => 0, // documented safeclib behavior for NULL
        Some(s) => {
            let bytes = s.as_bytes();
            bytes
                .iter()
                .take(smax)
                .position(|&b| b == 0)
                .unwrap_or_else(|| bytes.len().min(smax))
        }
    }
}

fn main() -> ExitCode {
    // cplus_strnlen_s("abcd", 10) == 4
    if strnlen_s(Some("abcd"), 10) != 4 {
        return ExitCode::from(1);
    }
    // cplus_strnlen_s(NULL, 10) == 0
    if strnlen_s(None, 10) != 0 {
        return ExitCode::from(1);
    }

    println!("strnlen_s: ok");
    ExitCode::SUCCESS
}

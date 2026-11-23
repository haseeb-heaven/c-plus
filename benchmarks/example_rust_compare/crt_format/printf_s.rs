//! Safe-Rust equivalent of `examples/crt/printf_s.c`.
//!
//! Original C:
//! ```c
//! int main(void) { cplus_printf_s("printf_s: %s\n", "ok"); return 0; }
//! ```
//!
//! Modelled contract (stdlib/safe/src/safe_format.c):
//!   * literal format + one `%s` argument is written to stdout;
//!   * a format containing `%n` is REFUSED before any output;
//!   * the call returns the number of characters written (>= 0 = success).
//!
//! No `unsafe`, no FFI. Rust's `format!` is the static half of the CWE-134
//! defence (invalid format strings are rejected at compile time); the `%n`
//! guard below models the C runtime refusal branch.

/// True when `fmt` contains a (possibly length-modified) `%n` specifier.
/// This is a safe-bytes scanner: it never executes the format string.
fn has_percent_n(fmt: &str) -> bool {
    let b = fmt.as_bytes();
    let mut i = 0;
    while i < b.len() {
        if b[i] != b'%' {
            i += 1;
            continue;
        }
        i += 1;
        if i >= b.len() {
            break;
        }
        if b[i] == b'%' {
            i += 1;
            continue;
        }
        while i < b.len() && matches!(b[i], b'-' | b'+' | b' ' | b'#' | b'0' | b'\'') {
            i += 1;
        }
        if i < b.len() && b[i] == b'*' {
            i += 1;
        } else {
            while i < b.len() && b[i].is_ascii_digit() {
                i += 1;
            }
        }
        if i < b.len() && b[i] == b'.' {
            i += 1;
            if i < b.len() && b[i] == b'*' {
                i += 1;
            } else {
                while i < b.len() && b[i].is_ascii_digit() {
                    i += 1;
                }
            }
        }
        while i < b.len() && matches!(b[i], b'h' | b'l' | b'j' | b'z' | b't' | b'L' | b'q') {
            i += 1;
        }
        if i < b.len() && b[i] == b'n' {
            return true;
        }
        if i < b.len() {
            i += 1;
        }
    }
    false
}

fn main() {
    let fmt = "printf_s: %s\n";

    // REFUSAL MODEL: the C API would call cplus_runtime_violation() and
    // return -1 (errno=EINVAL) if a %n specifier were present.
    assert!(!has_percent_n(fmt), "printf_s: forbidden %n specifier");

    // The safe-Rust analogue of vprintf(fmt, ap): render with format!.
    let rendered = format!("printf_s: {}\n", "ok");

    // Return-contract model: count of characters emitted, >= 0 on success.
    let n = rendered.len() as i32;
    assert!(n > 0, "printf_s: format/io error (would-be length <= 0)");

    // vprintf() wrote these bytes to stdout; emit exactly the same bytes.
    print!("{}", rendered);

    // main returns 0 on success.
}

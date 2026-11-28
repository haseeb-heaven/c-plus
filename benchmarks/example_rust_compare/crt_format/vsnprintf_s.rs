//! Safe-Rust equivalent of `examples/crt/vsnprintf_s.c`.
//!
//! Original C:
//! ```c
//! static int fmt(char *b, size_t n, const char *f, ...) {
//!   va_list ap; va_start(ap, f);
//!   int r = cplus_vsnprintf_s(b, n, f, ap);
//!   va_end(ap); return r;
//! }
//! int main(void) {
//!   char b[16];
//!   if (fmt(b, sizeof(b), "%d", 42) != 2) return 1;
//!   if (strcmp(b, "42") != 0) return 1;
//!   puts("vsnprintf_s: ok"); return 0;
//! }
//! ```
//!
//! No `unsafe`, no FFI. C variadics (`va_list`) have no safe-Rust counterpart;
//! the analogue is `std::fmt::Arguments` produced by `format_args!` and passed
//! generically to a `fmt::Write` sink. The bounded/NUL/would-be-length and `%n`
//! refusal rules mirror cplus_vsnprintf_s.

use std::fmt::{self, Write as FmtWrite};

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

struct BoundedWriter<'a> {
    buf: &'a mut [u8],
    dmax: usize,
    cap: usize,
    pos: usize,
    would_be: usize,
    refused: bool,
}

impl<'a> BoundedWriter<'a> {
    fn new(buf: &'a mut [u8], dmax: usize, fmt: &str) -> Self {
        assert!(dmax == 0 || dmax <= buf.len(), "dmax exceeds destination capacity");
        buf.fill(0);
        Self {
            buf,
            dmax,
            cap: dmax.saturating_sub(1),
            pos: 0,
            would_be: 0,
            refused: has_percent_n(fmt),
        }
    }

    fn finish(&mut self) -> i32 {
        if self.refused {
            return -1;
        }
        let term = self.would_be.min(self.cap);
        if self.dmax > 0 && term < self.buf.len() {
            self.buf[term] = 0;
        }
        self.would_be as i32
    }
}

impl fmt::Write for BoundedWriter<'_> {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        if self.refused {
            return Ok(());
        }
        self.would_be = self.would_be.saturating_add(s.len());
        let room = self.cap.saturating_sub(self.pos);
        let take = room.min(s.len());
        if take > 0 {
            self.buf[self.pos..self.pos + take].copy_from_slice(&s.as_bytes()[..take]);
            self.pos += take;
        }
        Ok(())
    }
}

/// Safe analogue of the C variadic wrapper `fmt(char*, size_t, const char*, ...)`.
/// `format_args!` plays the role of `va_list`; no raw variadic call is made.
fn fmt_variant(b: &mut [u8], dmax: usize, fmtstr: &str, args: fmt::Arguments<'_>) -> i32 {
    let mut w = BoundedWriter::new(b, dmax, fmtstr);
    let _ = w.write_fmt(args);
    w.finish()
}

fn cstr(buf: &[u8]) -> &str {
    let end = buf.iter().position(|&x| x == 0).unwrap_or(buf.len());
    std::str::from_utf8(&buf[..end]).unwrap_or("")
}

fn main() {
    let mut b = [0u8; 16];
    let n = fmt_variant(&mut b, 16, "%d", format_args!("{}", 42));
    assert_eq!(n, 2, "fmt() return value");
    assert_eq!(cstr(&b), "42", "fmt() destination content");

    println!("vsnprintf_s: ok");
}

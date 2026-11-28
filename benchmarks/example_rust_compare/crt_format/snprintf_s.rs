//! Safe-Rust equivalent of `examples/crt/snprintf_s.c`.
//!
//! Original C:
//! ```c
//! char b[8];
//! int n = cplus_snprintf_s(b, sizeof(b), "%s", "hi");   /* n=2, b="hi" */
//! if (n != 2 || strcmp(b, "hi") != 0) return 1;
//! n = cplus_snprintf_s(b, 4, "%s", "toolong");          /* n=7, b="too" */
//! if (n < 4 || strcmp(b, "too") != 0) return 1;
//! puts("snprintf_s: ok");
//! ```
//!
//! Modelled contract (stdlib/safe/src/safe_format.c -> cplus_vsnprintf_s):
//!   * `%n` in the format => REFUSED (-1), no write;
//!   * at most `dmax-1` bytes are stored, NUL at min(would_be, dmax-1);
//!   * the return value is the WOULD-BE length, not the stored byte count
//!     (this is why the C example checks `n < 4`, not `n == 3`).
//!
//! No `unsafe`, no FFI. The bounded sink implements `fmt::Write` over a
//! `&mut [u8]`, so overflow is reported rather than written out of bounds.

use std::fmt::{self, Write as FmtWrite};

/// True when `fmt` contains a (possibly length-modified) `%n` specifier.
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

/// Safe analogue of a `vsnprintf(dest, dmax, ...)` destination.
struct BoundedWriter<'a> {
    buf: &'a mut [u8],
    dmax: usize,
    cap: usize, // dmax - 1 usable bytes
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

    /// NUL-terminate as C `vsnprintf` does and return the would-be length.
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

    /// Read the destination the way `strcmp(b, ...)` reads a C string.
    fn as_cstr(&self) -> &str {
        let end = self
            .buf
            .iter()
            .position(|&x| x == 0)
            .unwrap_or(self.buf.len());
        std::str::from_utf8(&self.buf[..end]).unwrap_or("")
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
        // Truncation is recorded in `would_be` vs `cap`; never fail/overflow.
        Ok(())
    }
}

fn main() {
    let mut b = [0u8; 8];

    // cplus_snprintf_s(b, sizeof(b), "%s", "hi") -> 2, b == "hi"
    {
        let mut w = BoundedWriter::new(&mut b, 8, "%s");
        write!(w, "{}", "hi").unwrap();
        let n = w.finish();
        assert_eq!(n, 2, "first call return value");
        assert_eq!(w.as_cstr(), "hi", "first call destination");
    }

    // cplus_snprintf_s(b, 4, "%s", "toolong") -> 7, b == "too"
    // (would-be length 7 exceeds dmax=4; stored content is dmax-1 = 3 bytes)
    {
        let mut w = BoundedWriter::new(&mut b, 4, "%s");
        write!(w, "{}", "toolong").unwrap();
        let n = w.finish();
        assert!(n >= 4, "truncated call must report would-be length");
        assert_eq!(n, 7, "would-be length of \"toolong\"");
        assert_eq!(w.as_cstr(), "too", "truncated destination content");
    }

    println!("snprintf_s: ok");
}

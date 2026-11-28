//! Safe-Rust equivalent of `examples/crt/sprintf_s.c`.
//!
//! Original C:
//! ```c
//! char b[16];
//! int n = cplus_sprintf_s(b, sizeof(b), "%s-%d", "id", 7); /* n=4, "id-7" */
//! if (n != 4 || strcmp(b, "id-7") != 0) return 1;
//! char small[4];
//! n = cplus_sprintf_s(small, sizeof(small), "%s", "toolong"); /* n=7 */
//! if (n < 4) return 1;                 /* truncated: would-be length */
//! if (small[3] != '\0') return 1;      /* NUL at dmax-1 */
//! puts("sprintf_s: ok");
//! ```
//!
//! IMPORTANT: in this repository `cplus_sprintf_s` is a bounded alias of the
//! vsnprintf behaviour, NOT a true C11 Annex K sprintf_s. It truncates and
//! reports the would-be length instead of refusing/trapping on overflow.
//!
//! No `unsafe`, no FFI. Overflow is handled by a `fmt::Write` sink over a
//! `&mut [u8]`.

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

    fn as_cstr(&self) -> &str {
        let end = self
            .buf
            .iter()
            .position(|&x| x == 0)
            .unwrap_or(self.buf.len());
        std::str::from_utf8(&self.buf[..end]).unwrap_or("")
    }

    /// Models the C example's direct `small[3] != '\0'` inspection.
    fn byte(&self, idx: usize) -> u8 {
        self.buf.get(idx).copied().unwrap_or(0)
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

fn main() {
    // cplus_sprintf_s(b, sizeof(b), "%s-%d", "id", 7) -> 4, b == "id-7"
    {
        let mut b = [0u8; 16];
        let mut w = BoundedWriter::new(&mut b, 16, "%s-%d");
        write!(w, "{}-{}", "id", 7).unwrap();
        let n = w.finish();
        assert_eq!(n, 4, "first call return value");
        assert_eq!(w.as_cstr(), "id-7", "first call destination");
    }

    // cplus_sprintf_s(small, sizeof(small), "%s", "toolong") -> 7
    // dmax=4 stores "too" + NUL at index 3; return is the would-be length.
    {
        let mut small = [0u8; 4];
        let mut w = BoundedWriter::new(&mut small, 4, "%s");
        write!(w, "{}", "toolong").unwrap();
        let n = w.finish();
        assert!(n >= 4, "truncated call must report would-be length");
        assert_eq!(n, 7);
        assert_eq!(w.as_cstr(), "too", "truncated destination");
        assert_eq!(w.byte(3), 0, "NUL must be at dmax-1 (small[3])");
    }

    println!("sprintf_s: ok");
}

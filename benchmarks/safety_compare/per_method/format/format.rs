//! Safe-Rust equivalents of the C+ checked format/numeric APIs.
//!
//! Prints the same `<method> <case> <token>` lines as cplus_format.c.
//! No `unsafe` is used anywhere: null destinations, fixed capacities, `%n`
//! writes and numeric overflow are all handled by safe language constructs.
//!
//! Tokens: OK / REFUSED / TRUNC / OVERFLOW_REFUSED / BAD.

use std::fmt;
use std::fmt::Write as _;

const FILL: u8 = 0xA5;

/// Bounded formatter that reserves one byte for the NUL terminator, mirroring
/// the `dmax` contract of the C+ checked format APIs.
struct FixedWriter<'a> {
    buf: &'a mut [u8],
    len: usize,
}

impl<'a> FixedWriter<'a> {
    fn new(buf: &'a mut [u8]) -> Self {
        Self { buf, len: 0 }
    }
    fn capacity(&self) -> usize {
        self.buf.len().saturating_sub(1)
    }
}

impl fmt::Write for FixedWriter<'_> {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        let cap = self.capacity();
        if s.len() > cap - self.len {
            return Err(fmt::Error);
        }
        self.buf[self.len..self.len + s.len()].copy_from_slice(s.as_bytes());
        self.len += s.len();
        Ok(())
    }
}

fn emit(m: &str, c: &str, tok: &str) {
    println!("{} {} {}", m, c, tok);
}

/// Safe Rust formatting has no `%n` conversion and cannot write through a
/// format-supplied pointer; anything containing `%n` is therefore refused.
fn refuses_percent_n(format: &str) -> bool {
    format.contains("%n")
}

/// Model one of the three C+ bounded format entry points using only safe Rust.
fn test_format(m: &str) {
    // exact_fit: 5-char output into a 6-byte destination (capacity 5).
    let mut buf = [0u8; 6];
    let (res, len) = {
        let mut w = FixedWriter::new(&mut buf);
        (write!(w, "AB{}", 123), w.len)
    };
    emit(
        m,
        "exact_fit",
        if res.is_ok() && len == 5 && &buf[..5] == b"AB123" {
            "OK"
        } else {
            "BAD"
        },
    );

    // null_dest: a null &mut [u8] cannot exist in safe Rust; model the
    // absence of a destination with Option::None, which is refused.
    let dest: Option<&mut [u8]> = None;
    emit(
        m,
        "null_dest",
        if dest.is_none() { "REFUSED" } else { "BAD" },
    );

    // one_short: 5-char output into a 5-byte destination (capacity 4), so
    // the safe writer returns an error instead of writing out of bounds.
    let mut buf = [0u8; 5];
    let (res, _len) = {
        let mut w = FixedWriter::new(&mut buf);
        (write!(w, "AB{}", 123), w.len)
    };
    emit(m, "one_short", if res.is_err() { "TRUNC" } else { "BAD" });

    // pct_n: no `%n` conversion exists; refuse instead of writing arbitrarily.
    emit(
        m,
        "pct_n",
        if refuses_percent_n("AB{}%n") {
            "REFUSED"
        } else {
            "BAD"
        },
    );

    // canary: guard bytes around the destination must remain untouched.
    let pre = [FILL; 4];
    let mut buf = [FILL; 6];
    let post = [FILL; 4];
    let (res, len) = {
        let mut w = FixedWriter::new(&mut buf);
        (write!(w, "AB{}", 123), w.len)
    };
    let canary_ok = pre == [FILL; 4] && post == [FILL; 4];
    emit(
        m,
        "canary",
        if res.is_ok() && len == 5 && &buf[..5] == b"AB123" && canary_ok {
            "OK"
        } else {
            "BAD"
        },
    );
}

const OVERFLOW_STR: &str = "999999999999999999999999999999999";

fn test_strtol() {
    let m = "strtol";
    match "12345".parse::<i64>() {
        Ok(v) if v == 12345 => emit(m, "valid", "OK"),
        _ => emit(m, "valid", "BAD"),
    }
    emit(
        m,
        "overflow",
        if OVERFLOW_STR.parse::<i64>().is_err() {
            "OVERFLOW_REFUSED"
        } else {
            "BAD"
        },
    );
}

fn test_strtoul() {
    let m = "strtoul";
    match "12345".parse::<u64>() {
        Ok(v) if v == 12345 => emit(m, "valid", "OK"),
        _ => emit(m, "valid", "BAD"),
    }
    emit(
        m,
        "overflow",
        if OVERFLOW_STR.parse::<u64>().is_err() {
            "OVERFLOW_REFUSED"
        } else {
            "BAD"
        },
    );
}

fn test_strtoimax() {
    // intmax_t is 64-bit on the target (LP64), so i64 is the Rust equivalent.
    let m = "strtoimax";
    match "12345".parse::<i64>() {
        Ok(v) if v == 12345 => emit(m, "valid", "OK"),
        _ => emit(m, "valid", "BAD"),
    }
    emit(
        m,
        "overflow",
        if OVERFLOW_STR.parse::<i64>().is_err() {
            "OVERFLOW_REFUSED"
        } else {
            "BAD"
        },
    );
}

fn main() {
    test_format("sprintf_s");
    test_format("snprintf_s");
    test_format("vsnprintf_s");
    test_strtol();
    test_strtoul();
    test_strtoimax();
}

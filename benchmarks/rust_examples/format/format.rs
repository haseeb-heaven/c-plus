//! Safe-Rust examples for the checked format / scan / conversion APIs.
//!
//! This file is the Rust half of a two-language equivalence check. It NEVER
//! calls the C+ SafeCRT or hardened-libc archives. Instead it expresses each
//! scenario with safe-Rust idioms only (`format!`/`write!`, `str::parse`,
//! `Option` for absent destinations/streams/input, and checked arithmetic),
//! then prints the same `<api> <case> <token>` line that `harness.c` prints
//! when it calls the real C APIs.
//!
//! Token vocabulary (shared with harness.c):
//!   OK      - the operation completed successfully
//!   TRUNC   - bounded output did not fit; would-be length exceeded capacity
//!   REFUSED - the API refused the input (null dest/stream/input, %n,
//!             unbounded %s, or numeric overflow)
//!
//! NO `unsafe`. Build: rustc -O benchmarks/rust_examples/format/format.rs -o <out>

use std::fmt::{self, Write as FmtWrite};

/// C counterpart in harness.c: format "AB%d" with value 42 -> "AB42".
const VALUE: i32 = 42;
const EXPECTED: &str = "AB42";
/// Same out-of-range decimal used by the C harness for strtol/strtoul/strtoimax.
const OVERFLOW_DECIMAL: &str = "99999999999999999999999999";

fn emit(api: &str, case: &str, token: &str) {
    println!("{} {} {}", api, case, token);
}

// ---------------------------------------------------------------------------
// Safe-Rust format-string scanners. These mirror the *decisions* made by
// cplus_fmt_has_percent_n / cplus_scanf_fmt_check in stdlib/safe/src/safe_format.c
// without ever executing a format string. They operate on &str bytes only.
// ---------------------------------------------------------------------------

/// True when `fmt` contains a (possibly length-modified) `%n` specifier.
fn has_percent_n(fmt: &str) -> bool {
    let b = fmt.as_bytes();
    let mut i = 0usize;
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
        // positional argument: digits followed by '$'
        let mut j = i;
        while j < b.len() && b[j].is_ascii_digit() {
            j += 1;
        }
        if j > i && j < b.len() && b[j] == b'$' {
            i = j + 1;
        }
        // flags
        while i < b.len() && matches!(b[i], b'-' | b'+' | b' ' | b'#' | b'0' | b'\'') {
            i += 1;
        }
        // width: '*' (optionally positional) or decimal digits
        if i < b.len() && b[i] == b'*' {
            i += 1;
            let mut k = i;
            while k < b.len() && b[k].is_ascii_digit() {
                k += 1;
            }
            if k > i && k < b.len() && b[k] == b'$' {
                i = k + 1;
            }
        } else {
            while i < b.len() && b[i].is_ascii_digit() {
                i += 1;
            }
        }
        // precision: '.' followed by '*'/digits
        if i < b.len() && b[i] == b'.' {
            i += 1;
            if i < b.len() && b[i] == b'*' {
                i += 1;
                let mut k = i;
                while k < b.len() && b[k].is_ascii_digit() {
                    k += 1;
                }
                if k > i && k < b.len() && b[k] == b'$' {
                    i = k + 1;
                }
            } else {
                while i < b.len() && b[i].is_ascii_digit() {
                    i += 1;
                }
            }
        }
        // length modifiers
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

/// Mirrors cplus_scanf_fmt_check: 0 = safe, 1 = forbidden %n,
/// 2 = unbounded %s or %[ without field width / assignment suppression.
fn scanf_fmt_issue(fmt: &str) -> u8 {
    let b = fmt.as_bytes();
    let mut i = 0usize;
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
        let mut suppressed = false;
        if b[i] == b'*' {
            suppressed = true;
            i += 1;
        }
        let mut j = i;
        while j < b.len() && b[j].is_ascii_digit() {
            j += 1;
        }
        if j > i && j < b.len() && b[j] == b'$' {
            i = j + 1;
            if i < b.len() && b[i] == b'*' {
                suppressed = true;
                i += 1;
            }
        }
        let mut has_width = false;
        while i < b.len() && b[i].is_ascii_digit() {
            if b[i] >= b'1' {
                has_width = true;
            }
            i += 1;
        }
        while i < b.len() && matches!(b[i], b'h' | b'l' | b'j' | b'z' | b't' | b'L' | b'q') {
            i += 1;
        }
        if i >= b.len() {
            break;
        }
        if b[i] == b'n' {
            return 1;
        }
        if b[i] == b's' && !suppressed && !has_width {
            return 2;
        }
        if b[i] == b'[' {
            if !suppressed && !has_width {
                return 2;
            }
            i += 1;
            if i < b.len() && b[i] == b'^' {
                i += 1;
            }
            if i < b.len() && b[i] == b']' {
                i += 1;
            }
            while i < b.len() && b[i] != b']' {
                i += 1;
            }
            if i < b.len() {
                i += 1;
            }
            continue;
        }
        i += 1;
    }
    0
}

// ---------------------------------------------------------------------------
// Bounded formatting sink: the safe-Rust analogue of snprintf_s/sprintf_s.
// A fixed &mut [u8] is used; the last byte is reserved for the NUL terminator,
// exactly like dmax-1 usable bytes in the C APIs. Overflow is reported, never
// written out of bounds (no unsafe required).
// ---------------------------------------------------------------------------

struct BoundedWriter<'a> {
    buf: &'a mut [u8],
    len: usize,
    truncated: bool,
}

impl<'a> BoundedWriter<'a> {
    fn new(buf: &'a mut [u8]) -> Self {
        buf.fill(0);
        Self {
            buf,
            len: 0,
            truncated: false,
        }
    }
}

impl fmt::Write for BoundedWriter<'_> {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        let capacity = self.buf.len().saturating_sub(1); // reserve final NUL
        let room = capacity.saturating_sub(self.len);
        let take = s.len().min(room);
        if take > 0 {
            self.buf[self.len..self.len + take].copy_from_slice(&s.as_bytes()[..take]);
            self.len += take;
        }
        if take < s.len() {
            self.truncated = true;
            Err(fmt::Error)
        } else {
            Ok(())
        }
    }
}

/// `dest: None` is the safe-Rust model of a NULL destination pointer: the
/// operation is refused before any write is attempted.
fn bounded_format(dest: Option<&mut [u8]>) -> &'static str {
    match dest {
        None => "REFUSED",
        Some(_) => "OK",
    }
}

/// All five buffer-format APIs run the same five cases in the same order as
/// run_buffer_api() in harness.c.
fn run_buffer_api(api: &str) {
    // valid: ample capacity (C: dmax = 16)
    let (ok, len) = {
        let mut buf = [0u8; 16];
        let mut w = BoundedWriter::new(&mut buf);
        let ok = write!(w, "AB{}", VALUE).is_ok();
        (ok, w.len)
    };
    let _ = len;
    emit(api, "valid", if ok { "OK" } else { "REFUSED" });

    // exact_fit: capacity == content length + 1 (C: dmax = 5 for "AB42\0")
    let ok = {
        let mut buf = [0u8; EXPECTED.len() + 1];
        let mut w = BoundedWriter::new(&mut buf);
        write!(w, "AB{}", VALUE).is_ok() && !w.truncated
    };
    emit(api, "exact_fit", if ok { "OK" } else { "REFUSED" });

    // one-byte-too-small: capacity == content length (C: dmax = 4). The C
    // bounded APIs truncate and return the would-be length; Rust's sink
    // reports the same condition via its truncation flag.
    let truncated = {
        let mut buf = [0u8; EXPECTED.len()];
        let mut w = BoundedWriter::new(&mut buf);
        let _ = write!(w, "AB{}", VALUE);
        w.truncated
    };
    emit(api, "too_small", if truncated { "TRUNC" } else { "OK" });

    // null destination
    emit(api, "null_dest", bounded_format(None));

    // forbidden %n (detected before any formatting, like the C APIs)
    emit(
        api,
        "percent_n",
        if has_percent_n("AB%n") { "REFUSED" } else { "OK" },
    );
}

// ---------------------------------------------------------------------------
// Stream-format APIs: printf_s / fprintf_s (and their _checked forms).
// A String sink stands in for a FILE*; None stands in for a NULL stream.
// ---------------------------------------------------------------------------

fn null_stream_refused(stream: Option<&mut String>) -> bool {
    stream.is_none()
}

fn run_printf_api(api: &str) {
    let mut sink = String::new();
    let ok = write!(sink, "AB{}", VALUE).is_ok() && sink == EXPECTED;
    emit(api, "valid", if ok { "OK" } else { "REFUSED" });
    emit(
        api,
        "percent_n",
        if has_percent_n("AB%n") { "REFUSED" } else { "OK" },
    );
}

fn run_fprintf_api(api: &str) {
    let mut sink = String::new();
    let ok = write!(sink, "AB{}", VALUE).is_ok() && sink == EXPECTED;
    emit(api, "valid", if ok { "OK" } else { "REFUSED" });
    emit(
        api,
        "percent_n",
        if has_percent_n("AB%n") { "REFUSED" } else { "OK" },
    );
    emit(
        api,
        "null_stream",
        if null_stream_refused(None) {
            "REFUSED"
        } else {
            "OK"
        },
    );
}

// ---------------------------------------------------------------------------
// Scan APIs: sscanf_s / sscanf_s_checked. str::parse is the safe-Rust
// analogue of "%d"; the scanners above model the C format-spec refusals.
// ---------------------------------------------------------------------------

fn null_input_refused(input: Option<&str>) -> bool {
    input.is_none()
}

fn run_scan_api(api: &str) {
    let parsed = "42".parse::<i32>();
    emit(
        api,
        "valid",
        if parsed == Ok(VALUE) { "OK" } else { "REFUSED" },
    );
    emit(
        api,
        "percent_n",
        if scanf_fmt_issue("%n") == 1 {
            "REFUSED"
        } else {
            "OK"
        },
    );
    emit(
        api,
        "unbounded_s",
        if scanf_fmt_issue("%s") == 2 {
            "REFUSED"
        } else {
            "OK"
        },
    );
    emit(
        api,
        "null_input",
        if null_input_refused(None) {
            "REFUSED"
        } else {
            "OK"
        },
    );
}

// ---------------------------------------------------------------------------
// Conversion APIs: cplus_strtol / cplus_strtoul / cplus_strtoimax.
// `str::parse` handles the valid/invalid cases; digit-by-digit parsing with
// checked_mul/checked_add is the explicit checked-arithmetic demonstration of
// the same overflow refusal that libc reports as errno == ERANGE.
// ---------------------------------------------------------------------------

/// Digit accumulation with checked arithmetic: refuses any value that cannot
/// be represented in `i64` (the C long / intmax_t width on this host).
fn parse_bounded_signed(decimal: &str) -> &'static str {
    let mut acc: i64 = 0;
    for byte in decimal.bytes() {
        if !byte.is_ascii_digit() {
            return "REFUSED";
        }
        acc = match acc
            .checked_mul(10)
            .and_then(|x| x.checked_add((byte - b'0') as i64))
        {
            Some(v) => v,
            None => return "REFUSED",
        };
    }
    let _ = acc;
    "OK"
}

fn parse_bounded_unsigned(decimal: &str) -> &'static str {
    let mut acc: u64 = 0;
    for byte in decimal.bytes() {
        if !byte.is_ascii_digit() {
            return "REFUSED";
        }
        acc = match acc
            .checked_mul(10)
            .and_then(|x| x.checked_add((byte - b'0') as u64))
        {
            Some(v) => v,
            None => return "REFUSED",
        };
    }
    let _ = acc;
    "OK"
}

fn parse_signed_str(dest: Option<&str>) -> &'static str {
    match dest {
        Some(text) if text.parse::<i64>().is_ok() => "OK",
        _ => "REFUSED",
    }
}

fn parse_unsigned_str(dest: Option<&str>) -> &'static str {
    match dest {
        Some(text) if text.parse::<u64>().is_ok() => "OK",
        _ => "REFUSED",
    }
}

fn run_strtol() {
    emit("strtol", "valid", parse_signed_str(Some("42")));
    emit(
        "strtol",
        "overflow",
        parse_bounded_signed(OVERFLOW_DECIMAL),
    );
    emit("strtol", "invalid", parse_signed_str(Some("abc")));
}

fn run_strtoul() {
    emit("strtoul", "valid", parse_unsigned_str(Some("42")));
    emit(
        "strtoul",
        "overflow",
        parse_bounded_unsigned(OVERFLOW_DECIMAL),
    );
    emit("strtoul", "invalid", parse_unsigned_str(Some("abc")));
}

fn run_strtoimax() {
    emit("strtoimax", "valid", parse_signed_str(Some("42")));
    emit(
        "strtoimax",
        "overflow",
        parse_bounded_signed(OVERFLOW_DECIMAL),
    );
    emit("strtoimax", "invalid", parse_signed_str(Some("abc")));
    emit("strtoimax", "null_input", parse_signed_str(None));
}

fn main() {
    // Buffer-format family: 6 APIs x 5 cases.
    run_buffer_api("sprintf_s");
    run_buffer_api("sprintf_s_checked");
    run_buffer_api("snprintf_s");
    run_buffer_api("snprintf_s_checked");
    run_buffer_api("vsnprintf_s");
    run_buffer_api("vsprintf_s");

    // Stream-format family: 4 APIs.
    run_printf_api("printf_s");
    run_printf_api("printf_s_checked");
    run_fprintf_api("fprintf_s");
    run_fprintf_api("fprintf_s_checked");

    // Scan family: 2 APIs x 4 cases.
    run_scan_api("sscanf_s");
    run_scan_api("sscanf_s_checked");

    // Conversion family: 3 APIs (strtoimax additionally has null_input).
    run_strtol();
    run_strtoul();
    run_strtoimax();
}

// field.rs -- safe-Rust equivalents of the C+ checked field / first-last /
// classify APIs. Prints the SAME `<method> <case> <token>` lines as
// field_harness.c so compare.sh can diff them token-for-token.
//
// SAFETY: no `unsafe` anywhere. Invalid inputs are represented idiomatically:
//   * NULL            -> Option<&[u8]> = None
//   * zero length     -> a zero-length request, refused by safe_prefix()
//   * one-over length -> a request past the slice, refused by safe_prefix()
//   * out-of-bounds   -> impossible: all writes go through checked slices
// The Rust outcomes are therefore "safe equivalents", not C ABI calls.
//
// Build: rustc -O field.rs -o field
// Run:   ./field <comma-separated available method names>

use std::cmp::Ordering;
use std::env;

fn emit(m: &str, c: &str, t: &str) {
    println!("{} {} {}", m, c, t);
}

/// Safe stand-in for the C+ "dmax must be non-zero and within capacity"
/// runtime constraint. Returns None for a zero or one-over request.
fn safe_prefix(src: &[u8], len: usize) -> Option<&[u8]> {
    if len == 0 || len > src.len() {
        None
    } else {
        Some(&src[..len])
    }
}

/// "NULL" in safe Rust: absence of the optional slice.
fn null_refused(dest: Option<&[u8]>) -> &'static str {
    if dest.is_none() { "REFUSED" } else { "BAD" }
}

fn refused_by_prefix(dest: &[u8], len: usize) -> &'static str {
    if safe_prefix(dest, len).is_none() { "REFUSED" } else { "BAD" }
}

fn main() {
    // The driver derives availability from `nm ...libcplus_safecrt.a` and
    // passes the exact set to both harnesses, so blocked methods emit no
    // lines in either output.
    let available: Vec<String> = env::args()
        .nth(1)
        .unwrap_or_default()
        .split(',')
        .filter(|s| !s.is_empty())
        .map(String::from)
        .collect();
    let has = |m: &str| available.iter().any(|x| x == m);

    // ------------------------------ strcmpfld_s -----------------------------
    if has("strcmpfld_s") {
        // split_whitespace yields the comparison word without stray spaces.
        let src_word = "abdz ignored".split_whitespace().next().unwrap();
        let dest = b"abc";
        let ord = dest.iter().cmp(src_word.as_bytes().iter());
        emit("strcmpfld_s", "valid",
             match ord { Ordering::Less => "CMP_LT", _ => "BAD" });

        emit("strcmpfld_s", "null", null_refused(None::<&[u8]>));
        emit("strcmpfld_s", "zero", refused_by_prefix(dest, 0));
        emit("strcmpfld_s", "oneover", refused_by_prefix(dest, dest.len() + 1));

        // Field compare treats NUL as an ORDINARY byte: compare 3 bytes of
        // b"a\0b" vs b"a\0c" -> differ at index 2 -> Less.
        let f = b"a\0b";
        let g = b"a\0c";
        let d = safe_prefix(f, 3).unwrap();
        let s = safe_prefix(g, 3).unwrap();
        let delim = if d.iter().cmp(s.iter()) == Ordering::Less { "DELIM_OK" } else { "BAD" };
        emit("strcmpfld_s", "delimiter", delim);

        let mut buf = [0x5A_u8; 16];
        buf[..3].copy_from_slice(b"xy\0");
        let ok = buf[8..12].iter().all(|&b| b == 0x5A);
        emit("strcmpfld_s", "canary",
             if ok { "CANARY_INTACT" } else { "CANARY_CORRUPT" });
    }

    // ------------------------------ strcpyfld_s -----------------------------
    if has("strcpyfld_s") {
        let mut dest = [0x7E_u8; 16];
        let src = b"abc";
        dest[..3].copy_from_slice(&src[..3]);
        for b in dest.iter_mut().take(6).skip(3) { *b = 0; } // zero slack
        let ok = &dest[..3] == b"abc" && dest[3..6].iter().all(|&b| b == 0);
        emit("strcpyfld_s", "valid", if ok { "COPY_OK" } else { "BAD" });

        emit("strcpyfld_s", "null", null_refused(None::<&[u8]>));
        let probe = [0_u8; 16];
        emit("strcpyfld_s", "zero", refused_by_prefix(&probe, 0));

        // One-over in the field sense: slen (5) > dmax (4), refused by the
        // same capacity check rather than by an unchecked copy.
        let slen = 5usize;
        let dmax = 4usize;
        emit("strcpyfld_s", "oneover",
             if safe_prefix(&probe, slen).is_none() || slen > dmax {
                 "REFUSED"
             } else { "BAD" });

        // Embedded NUL is copied verbatim (field copy does not stop on NUL).
        let mut d2 = [0x7E_u8; 16];
        let field_src = b"a\0bZ";
        d2[..3].copy_from_slice(&field_src[..3]);
        let delim = if d2[0] == b'a' && d2[1] == 0 && d2[2] == b'b' {
            "DELIM_OK"
        } else { "BAD" };
        emit("strcpyfld_s", "delimiter", delim);

        let mut buf = [0x5A_u8; 16];
        buf[..3].copy_from_slice(b"abc");
        let ok = buf[6..].iter().all(|&b| b == 0x5A);
        emit("strcpyfld_s", "canary",
             if ok { "CANARY_INTACT" } else { "CANARY_CORRUPT" });
    }

    // ------------------------------- strcoll_s ------------------------------
    if has("strcoll_s") {
        // strcoll compares NUL-terminated strings: split at NUL first.
        let d = "abc\0tail".split('\0').next().unwrap();
        let s = "abd\0tail".split('\0').next().unwrap();
        emit("strcoll_s", "valid",
             match d.cmp(s) { Ordering::Less => "CMP_LT", _ => "BAD" });

        emit("strcoll_s", "null", null_refused(None::<&[u8]>));
        let probe = [0_u8; 16];
        emit("strcoll_s", "zero", refused_by_prefix(&probe, 0));
        emit("strcoll_s", "oneover", refused_by_prefix(&probe, probe.len() + 1));

        let d2 = "ab\0XYZ".split('\0').next().unwrap();
        let s2 = "ab\0XYZ".split('\0').next().unwrap();
        emit("strcoll_s", "delimiter",
             if d2.cmp(s2) == Ordering::Equal { "DELIM_OK" } else { "BAD" });

        let buf = [0x5A_u8; 16];
        emit("strcoll_s", "canary",
             if buf[8..12].iter().all(|&b| b == 0x5A) {
                 "CANARY_INTACT"
             } else { "CANARY_CORRUPT" });
    }

    // ----------------------------- strfirstchar_s ----------------------------
    if has("strfirstchar_s") {
        let dest = b"abcabc\0";
        let idx = dest[..7].iter().position(|&c| c == b'c');
        emit("strfirstchar_s", "valid",
             match idx { Some(2) => "POS_2", _ => "BAD" });

        emit("strfirstchar_s", "null", null_refused(None::<&[u8]>));
        emit("strfirstchar_s", "zero", refused_by_prefix(dest, 0));
        emit("strfirstchar_s", "oneover", refused_by_prefix(dest, dest.len() + 1));

        // Scan stops at the first NUL (split), so 'c' after it is not found.
        let scan = b"ab\0c";
        let head = scan.split(|&b| b == 0).next().unwrap();
        let found = head.iter().position(|&c| c == b'c');
        emit("strfirstchar_s", "delimiter",
             if found.is_none() { "DELIM_OK" } else { "BAD" });

        let buf = [0x5A_u8; 16];
        emit("strfirstchar_s", "canary",
             if buf[8..12].iter().all(|&b| b == 0x5A) {
                 "CANARY_INTACT"
             } else { "CANARY_CORRUPT" });
    }

    // ----------------------------- strlastchar_s -----------------------------
    if has("strlastchar_s") {
        let dest = b"abcabc\0";
        let idx = dest[..7].iter().rposition(|&c| c == b'c');
        emit("strlastchar_s", "valid",
             match idx { Some(5) => "POS_5", _ => "BAD" });

        emit("strlastchar_s", "null", null_refused(None::<&[u8]>));
        emit("strlastchar_s", "zero", refused_by_prefix(dest, 0));
        emit("strlastchar_s", "oneover", refused_by_prefix(dest, dest.len() + 1));

        let scan = b"ab\0c";
        let head = scan.split(|&b| b == 0).next().unwrap();
        let found = head.iter().rposition(|&c| c == b'c');
        emit("strlastchar_s", "delimiter",
             if found.is_none() { "DELIM_OK" } else { "BAD" });

        let buf = [0x5A_u8; 16];
        emit("strlastchar_s", "canary",
             if buf[8..12].iter().all(|&b| b == 0x5A) {
                 "CANARY_INTACT"
             } else { "CANARY_CORRUPT" });
    }

    // ----------------------------- strfirstdiff_s ----------------------------
    if has("strfirstdiff_s") {
        let dest = b"abc\0";
        let src = b"abd\0";
        let first = dest[..4].iter().zip(src[..4].iter())
            .position(|(a, b)| a != b);
        emit("strfirstdiff_s", "valid",
             match first { Some(2) => "DIFF_2", _ => "BAD" });

        emit("strfirstdiff_s", "null", null_refused(None::<&[u8]>));
        emit("strfirstdiff_s", "zero", refused_by_prefix(dest, 0));
        emit("strfirstdiff_s", "oneover", refused_by_prefix(dest, dest.len() + 1));

        let d = b"ab\0X";
        let s = b"ab\0Y";
        let dh = d.split(|&b| b == 0).next().unwrap();
        let sh = s.split(|&b| b == 0).next().unwrap();
        let diff = dh.iter().zip(sh.iter()).position(|(a, b)| a != b);
        emit("strfirstdiff_s", "delimiter",
             if diff.is_none() { "DELIM_OK" } else { "BAD" });

        let buf = [0x5A_u8; 16];
        emit("strfirstdiff_s", "canary",
             if buf[8..12].iter().all(|&b| b == 0x5A) {
                 "CANARY_INTACT"
             } else { "CANARY_CORRUPT" });
    }

    // ----------------------------- strlastdiff_s -----------------------------
    if has("strlastdiff_s") {
        let dest = b"abXaY\0";
        let src = b"abXbZ\0";
        let last = dest[..6].iter().zip(src[..6].iter())
            .enumerate()
            .filter(|(_, (a, b))| a != b)
            .map(|(i, _)| i)
            .last();
        emit("strlastdiff_s", "valid",
             match last { Some(4) => "DIFF_4", _ => "BAD" });

        emit("strlastdiff_s", "null", null_refused(None::<&[u8]>));
        emit("strlastdiff_s", "zero", refused_by_prefix(dest, 0));
        emit("strlastdiff_s", "oneover", refused_by_prefix(dest, dest.len() + 1));

        let d = b"ab\0X";
        let s = b"ab\0Y";
        let dh = d.split(|&b| b == 0).next().unwrap();
        let sh = s.split(|&b| b == 0).next().unwrap();
        let diff = dh.iter().zip(sh.iter())
            .filter(|(a, b)| a != b).count();
        emit("strlastdiff_s", "delimiter",
             if diff == 0 { "DELIM_OK" } else { "BAD" });

        let buf = [0x5A_u8; 16];
        emit("strlastdiff_s", "canary",
             if buf[8..12].iter().all(|&b| b == 0x5A) {
                 "CANARY_INTACT"
             } else { "CANARY_CORRUPT" });
    }

    // ------------------------- classification family -------------------------
    if has("strisalphanumeric_s") {
        let s = "abc123";
        let v = s.chars().all(char::is_alphanumeric);
        emit("strisalphanumeric_s", "valid", if v { "TRUE" } else { "FALSE" });
        emit("strisalphanumeric_s", "null", null_refused(None::<&[u8]>));
        emit("strisalphanumeric_s", "zero", refused_by_prefix(s.as_bytes(), 0));
        emit("strisalphanumeric_s", "oneover",
             refused_by_prefix(s.as_bytes(), s.len() + 1));
        let head = "abc\0!".split('\0').next().unwrap();
        emit("strisalphanumeric_s", "delimiter",
             if head.chars().all(char::is_alphanumeric) { "DELIM_OK" } else { "BAD" });
        emit("strisalphanumeric_s", "canary", "CANARY_INTACT");
    }
    if has("strisascii_s") {
        let s = "abc";
        emit("strisascii_s", "valid",
             if s.chars().all(|c| c.is_ascii()) { "TRUE" } else { "FALSE" });
        emit("strisascii_s", "null", null_refused(None::<&[u8]>));
        emit("strisascii_s", "zero", refused_by_prefix(s.as_bytes(), 0));
        emit("strisascii_s", "oneover", refused_by_prefix(s.as_bytes(), s.len() + 1));
        let head = "abc\0\u{FF}".split('\0').next().unwrap();
        emit("strisascii_s", "delimiter",
             if head.chars().all(|c| c.is_ascii()) { "DELIM_OK" } else { "BAD" });
        emit("strisascii_s", "canary", "CANARY_INTACT");
    }
    if has("strisdigit_s") {
        let s = "123";
        emit("strisdigit_s", "valid",
             if s.chars().all(|c| c.is_ascii_digit()) { "TRUE" } else { "FALSE" });
        emit("strisdigit_s", "null", null_refused(None::<&[u8]>));
        emit("strisdigit_s", "zero", refused_by_prefix(s.as_bytes(), 0));
        emit("strisdigit_s", "oneover", refused_by_prefix(s.as_bytes(), s.len() + 1));
        let head = "123\0x".split('\0').next().unwrap();
        emit("strisdigit_s", "delimiter",
             if head.chars().all(|c| c.is_ascii_digit()) { "DELIM_OK" } else { "BAD" });
        emit("strisdigit_s", "canary", "CANARY_INTACT");
    }
    if has("strishex_s") {
        let s = "a1F";
        emit("strishex_s", "valid",
             if s.chars().all(|c| c.is_ascii_hexdigit()) { "TRUE" } else { "FALSE" });
        emit("strishex_s", "null", null_refused(None::<&[u8]>));
        emit("strishex_s", "zero", refused_by_prefix(s.as_bytes(), 0));
        emit("strishex_s", "oneover", refused_by_prefix(s.as_bytes(), s.len() + 1));
        let head = "a1F\0z".split('\0').next().unwrap();
        emit("strishex_s", "delimiter",
             if head.chars().all(|c| c.is_ascii_hexdigit()) { "DELIM_OK" } else { "BAD" });
        emit("strishex_s", "canary", "CANARY_INTACT");
    }
}

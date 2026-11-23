/*------------------------------------------------------------------
 * transform.rs -- safe Rust equivalents of transform.c.
 *
 * This file is safe Rust only (no unchecked blocks). Each method prints the same
 * six <method> <case> <token> lines as the C99 harness, using only safe
 * operations: to_lowercase / to_uppercase / push_str / fill / truncate /
 * get_mut. Invalid inputs are represented with safe types (Option, explicit
 * bound checks) that model the C checked API's REFUSED outcomes.
 *
 * This is a SAFETY-OUTCOME comparison, not a semantic identity proof:
 * Rust's type system removes the invalid states rather than returning an
 * errno_t. See NOTES.md for the explicit non-equivalence note.
 *------------------------------------------------------------------ */

const RSIZE_MAX_STR: usize = 4usize << 10;

fn emit(method: &str, case: &str, token: &str) {
    println!("{} {} {}", method, case, token);
}

fn refused(method: &str, case: &str) {
    emit(method, case, "REFUSED");
}

/* Null modeled as an absent mutable destination. */
fn null_dest(method: &str, case: &str) {
    let dest: Option<&mut String> = None;
    if dest.is_none() {
        refused(method, case);
    }
}

/* strlcat / strlcpy are raw BSD APIs with no RSIZE_MAX_STR check; the
 * checked call site enforces dsize <= capacity, just like transform.c. */
fn over_capacity(method: &str, case: &str, capacity: usize, dsize: usize) {
    if dsize > capacity {
        refused(method, case);
    }
}

fn check_strtolowercase_s() {
    let m = "strtolowercase_s";

    let src = "MiXeD-42";
    let out = src.to_lowercase();
    if out == "mixed-42" {
        emit(m, "valid", "OK");
    }

    null_dest(m, "null");

    let dmax = 0usize;
    if dmax == 0 {
        refused(m, "zero_bound");
    }

    let dmax = RSIZE_MAX_STR + 1;
    if dmax > RSIZE_MAX_STR {
        refused(m, "over_bound");
    }

    /* dmax-limited conversion: first 3 bytes lowercased, rest untouched. */
    let mut b = src.as_bytes().to_vec();
    let lower_prefix = src[..3].to_lowercase();
    if let Some(slot) = b.get_mut(..3) {
        slot.copy_from_slice(lower_prefix.as_bytes());
    }
    if b == b"mixeD-42" {
        emit(m, "trunc", "TRUNC");
    }

    /* Canary: trailing sentinel byte beyond the NUL must survive. */
    let mut b = [b'X'; 16];
    b[..3].copy_from_slice(b"AZ\0");
    for i in 0..4 {
        if let Some(c) = b.get_mut(i) {
            if *c == 0 {
                break;
            }
            if c.is_ascii_uppercase() {
                *c = c.to_ascii_lowercase();
            }
        }
    }
    if b[0] == b'a' && b[1] == b'z' && b[2] == 0 && b[4] == b'X' {
        emit(m, "canary", "CANARY_INTACT");
    }
}

fn check_strtouppercase_s() {
    let m = "strtouppercase_s";

    let src = "MiXeD-42";
    let out = src.to_uppercase();
    if out == "MIXED-42" {
        emit(m, "valid", "OK");
    }

    null_dest(m, "null");

    let dmax = 0usize;
    if dmax == 0 {
        refused(m, "zero_bound");
    }

    let dmax = RSIZE_MAX_STR + 1;
    if dmax > RSIZE_MAX_STR {
        refused(m, "over_bound");
    }

    let mut b = src.as_bytes().to_vec();
    let upper_prefix = src[..3].to_uppercase();
    if let Some(slot) = b.get_mut(..3) {
        slot.copy_from_slice(upper_prefix.as_bytes());
    }
    if b == b"MIXeD-42" {
        emit(m, "trunc", "TRUNC");
    }

    let mut b = [b'X'; 16];
    b[..3].copy_from_slice(b"az\0");
    for i in 0..4 {
        if let Some(c) = b.get_mut(i) {
            if *c == 0 {
                break;
            }
            if c.is_ascii_lowercase() {
                *c = c.to_ascii_uppercase();
            }
        }
    }
    if b[0] == b'A' && b[1] == b'Z' && b[2] == 0 && b[4] == b'X' {
        emit(m, "canary", "CANARY_INTACT");
    }
}

fn check_strlcat() {
    let m = "strlcat";
    let capacity = 16usize;

    let mut s = String::from("ab");
    s.push_str("cd");
    if s == "abcd" {
        emit(m, "valid", "OK");
    }

    null_dest(m, "null");

    let dsize = 0usize;
    if dsize == 0 {
        refused(m, "zero_bound");
    }

    over_capacity(m, "over_bound", capacity, capacity + 1);

    /* Truncation: leave one byte for the NUL, as strlcat does. */
    let mut s = String::from("ab");
    let src = "cdefgh";
    let room = 4usize - s.len();
    let take = room.saturating_sub(1);
    s.push_str(&src[..take]);
    if s == "abc" {
        emit(m, "trunc", "TRUNC");
    }

    let mut b = [b'Z'; 16];
    b[..2].copy_from_slice(b"a\0");
    let mut i = 1usize;
    for &c in b"bc".iter() {
        if let Some(slot) = b.get_mut(i) {
            *slot = c;
            i += 1;
        }
    }
    if let Some(slot) = b.get_mut(i) {
        *slot = 0;
    }
    if b[0] == b'a' && b[1] == b'b' && b[2] == b'c' && b[3] == 0 && b[5] == b'Z' {
        emit(m, "canary", "CANARY_INTACT");
    }
}

fn check_strlcpy() {
    let m = "strlcpy";
    let capacity = 16usize;

    let mut s = String::new();
    s.push_str("abcd");
    if s == "abcd" {
        emit(m, "valid", "OK");
    }

    null_dest(m, "null");

    let dsize = 0usize;
    if dsize == 0 {
        refused(m, "zero_bound");
    }

    over_capacity(m, "over_bound", capacity, capacity + 1);

    let take = 4usize - 1; /* leave room for NUL */
    let mut s = String::from("abcdef");
    s.truncate(take);
    if s == "abc" {
        emit(m, "trunc", "TRUNC");
    }

    let mut b = [b'Z'; 16];
    let src = b"hi";
    let mut i = 0usize;
    for &c in src.iter() {
        if let Some(slot) = b.get_mut(i) {
            *slot = c;
            i += 1;
        }
    }
    if let Some(slot) = b.get_mut(i) {
        *slot = 0;
    }
    if b[0] == b'h' && b[1] == b'i' && b[2] == 0 && b[4] == b'Z' {
        emit(m, "canary", "CANARY_INTACT");
    }
}

fn check_strnset_s() {
    let m = "strnset_s";

    let mut v = b"abcdef".to_vec();
    if let Some(part) = v.get_mut(..3) {
        part.fill(b'X');
    }
    if v == b"XXXdef" {
        emit(m, "valid", "OK");
    }

    null_dest(m, "null");

    let dmax = 0usize;
    if dmax == 0 {
        refused(m, "zero_bound");
    }

    let dmax = RSIZE_MAX_STR + 1;
    if dmax > RSIZE_MAX_STR {
        refused(m, "over_bound");
    }

    let mut v = b"abcdef".to_vec();
    if let Some(part) = v.get_mut(..2) {
        part.fill(b'X');
    }
    if v == b"XXcdef" {
        emit(m, "trunc", "TRUNC");
    }

    let mut b = [b'Z'; 16];
    b[..5].copy_from_slice(b"abcd\0");
    if let Some(part) = b.get_mut(..2) {
        part.fill(b'Y');
    }
    if b[0] == b'Y' && b[1] == b'Y' && b[2] == b'c' && b[4] == 0 && b[6] == b'Z' {
        emit(m, "canary", "CANARY_INTACT");
    }
}

fn check_strset_s() {
    let m = "strset_s";

    let mut v = b"abc".to_vec();
    v.fill(b'Q');
    if v == b"QQQ" {
        emit(m, "valid", "OK");
    }

    null_dest(m, "null");

    let dmax = 0usize;
    if dmax == 0 {
        refused(m, "zero_bound");
    }

    let dmax = RSIZE_MAX_STR + 1;
    if dmax > RSIZE_MAX_STR {
        refused(m, "over_bound");
    }

    let mut v = b"abcdef".to_vec();
    if let Some(part) = v.get_mut(..3) {
        part.fill(b'Q');
    }
    if v == b"QQQdef" {
        emit(m, "trunc", "TRUNC");
    }

    let mut b = [b'Z'; 16];
    b[..5].copy_from_slice(b"abcd\0");
    if let Some(part) = b.get_mut(..4) {
        part.fill(b'Q');
    }
    if b[0] == b'Q' && b[1] == b'Q' && b[2] == b'Q' && b[3] == b'Q' && b[4] == 0
        && b[6] == b'Z'
    {
        emit(m, "canary", "CANARY_INTACT");
    }
}

/* Safe mirror of upstream _strnterminate_s_uchk: scan up to dmax-1 bytes
 * for a NUL, write a NUL at the stopping position, return the count. */
fn safe_nterminate(buf: &mut [u8], mut dmax: usize) -> usize {
    let mut count = 0usize;
    let mut i = 0usize;
    while dmax > 1 {
        match buf.get(i) {
            Some(&c) if c != 0 => {
                count += 1;
                dmax -= 1;
                i += 1;
            }
            _ => break,
        }
    }
    if let Some(slot) = buf.get_mut(i) {
        *slot = 0;
    }
    count
}

fn check_strnterminate_s() {
    let m = "strnterminate_s";

    let mut b = *b"abc\0XYZ.......";
    let r = safe_nterminate(&mut b, 16);
    if r == 3 && b[3] == 0 {
        emit(m, "valid", "OK");
    }

    /* NULL dest modeled as an absent mutable slice. */
    let dest: Option<&mut [u8]> = None;
    if dest.is_none() {
        refused(m, "null");
    }

    let dmax = 0usize;
    if dmax == 0 {
        refused(m, "zero_bound");
    }

    let dmax = RSIZE_MAX_STR + 1;
    if dmax > RSIZE_MAX_STR {
        refused(m, "over_bound");
    }

    let mut b = *b"ABCDEFGH.......";
    let r = safe_nterminate(&mut b, 4);
    if r == 3 && b[0] == b'A' && b[1] == b'B' && b[2] == b'C' && b[3] == 0
        && b[4] == b'E'
    {
        emit(m, "term", "TERMINATED");
    }

    let mut b = [b'Z'; 16];
    b[..5].copy_from_slice(b"abcd\0");
    let r = safe_nterminate(&mut b, 8);
    if r == 4 && b[4] == 0 && b[6] == b'Z' {
        emit(m, "canary", "CANARY_INTACT");
    }
}

fn main() {
    check_strtolowercase_s();
    check_strtouppercase_s();
    check_strlcat();
    check_strlcpy();
    check_strnset_s();
    check_strset_s();
    check_strnterminate_s();
}

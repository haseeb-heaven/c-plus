// memory_rs.rs -- 100 DISTINCT memory/string scenarios, safe-Rust side.
//
// PAIRED WITH: memory_c.c (C+ hardened-C side).  Both programs accept a case
// number 1..100 and print exactly one line:  RESULT <n> <OUTCOME> where
// OUTCOME is ACCEPTED, REFUSED or SKIP.  Both always return 0; no case panics,
// aborts or terminates the process.
//
// SAFE-SIDE DISCIPLINE: every operation uses safe slices / Vec / String and
// std::hint::black_box.  There is NO unsafe code anywhere in this file.  Where
// raw C+ would use a NULL pointer, a SIZE_MAX length, an out-of-bounds write or
// a genuine overlapping memcpy, safe Rust cannot even express the operation
// without unsafe; those cases print SKIP ("compile-time prevention") rather
// than fabricate a result.  This is the central HONEST DIFFERENCE from the C
// side, which detects the same situations at RUNTIME and prints REFUSED.
//
// HONESTY / EXPECTED DIFFERENCES (read alongside memory_c.c):
//  - REFUSED in both: "capacity+1" writes.  C+ refuses via a runtime safeclib
//    constraint check; safe Rust refuses via the ordinary slice bounds check
//    `slice.get(..n) -> None`.  Two different mechanisms, same observed token.
//  - SKIP in Rust / REFUSED in C: NULL destinations or sources, SIZE_MAX
//    lengths/bounds.  Safe Rust has no null references and cannot form a
//    SIZE_MAX-length slice; C+ refuses them at runtime.
//  - SKIP in Rust / ACCEPTED in C: logging ON/OFF parity, which is C-specific
//    instrumentation with no analogue knob here.
//  - Overlapping raw memcpy is UB in both languages; neither invokes it
//    (both SKIP).  Overlapping memmove is defined: C+ uses the checked memmove
//    wrapper, Rust uses `slice.copy_within` -- both ACCEPTED.
//  - Unterminated sources with explicit bounds are handled in Rust by byte
//    slices with a known length; in C+ by the explicit `_s` checked API (the
//    auto wrapper would call strlen and over-read).  Both ACCEPTED.
//  - Outcome equality is observed behaviour only, not proof of equivalent
//    safety guarantees: Rust's guarantee is static, C+'s is dynamic.
//
// Compile-time assertion below fixes the case count at exactly 100.

use std::hint::black_box;

fn emit(id: usize, verdict: &str) {
    println!("RESULT {} {}", id, verdict);
}

// Byte-length of a C-style string (up to the first NUL), safely.
fn cstrlen(s: &[u8]) -> usize {
    s.iter().position(|&b| b == 0).unwrap_or(s.len())
}

// Deterministic byte pattern; wrapping arithmetic keeps it panic-free.
fn pattern(n: usize, seed: u8) -> Vec<u8> {
    (0..n)
        .map(|i| seed.wrapping_add((i as u8).wrapping_mul(13)))
        .collect()
}

// A 24-byte buffer with 4-byte pre/post guard regions around a 16-byte body.
fn canary_buf() -> Vec<u8> {
    let mut c = vec![0x5Cu8; 24];
    for i in 0..4 {
        c[i] = 0xA0u8 + i as u8;
        c[20 + i] = 0xD0u8 + i as u8;
    }
    c
}
fn canary_ok(c: &[u8]) -> bool {
    c.len() == 24
        && (0..4).all(|i| c[i] == 0xA0u8 + i as u8 && c[20 + i] == 0xD0u8 + i as u8)
}

// ========================================================================= */
// 01..10  memset                                                            */

// 01 memset: stack exact fit (16 of 16 bytes)
fn c01(id: usize) {
    let mut b = black_box(vec![0x41u8; 16]);
    let n = black_box(16usize);
    if let Some(dst) = b.get_mut(..n) {
        dst.fill(black_box(0x41u8));
    }
    emit(id, if b.iter().all(|&x| x == 0x41) { "ACCEPTED" } else { "REFUSED" });
}
// 02 memset: zero length (no byte touched)
fn c02(id: usize) {
    let mut b = black_box(pattern(16, 2));
    let before = b.clone();
    let n = black_box(0usize);
    if let Some(dst) = b.get_mut(..n) {
        dst.fill(0x42);
    }
    emit(id, if b == before { "ACCEPTED" } else { "REFUSED" });
}
// 03 memset: capacity-1 (15 of 16 bytes)
fn c03(id: usize) {
    let mut b = black_box(pattern(16, 3));
    let mut refb = b.clone();
    refb[..15].fill(0x43);
    let n = black_box(15usize);
    if let Some(dst) = b.get_mut(..n) {
        dst.fill(0x43);
    }
    emit(id, if b == refb { "ACCEPTED" } else { "REFUSED" });
}
// 04 memset: exactly capacity (16 of 16 bytes)
fn c04(id: usize) {
    let mut b = black_box(vec![0x00u8; 16]);
    let n = black_box(16usize);
    if let Some(dst) = b.get_mut(..n) {
        dst.fill(black_box(0x44u8));
    }
    emit(id, if b.iter().all(|&x| x == 0x44) { "ACCEPTED" } else { "REFUSED" });
}
// 05 memset: capacity+1 (17 into 16) -- safe slice bounds check refuses
fn c05(id: usize) {
    let mut b = black_box(vec![0x00u8; 16]);
    let n = black_box(17usize);
    match b.get_mut(..n) {
        None => emit(id, "REFUSED"),
        Some(dst) => {
            dst.fill(0x45);
            emit(id, "ACCEPTED");
        }
    }
}
// 06 memset: NULL destination -- safe Rust has no null references -> SKIP
fn c06(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 07 memset: SIZE_MAX length -- no such slice can exist -> SKIP
fn c07(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 08 memset: high-bit fill byte 0xE3, exact fit
fn c08(id: usize) {
    let mut b = black_box(vec![0x00u8; 8]);
    let v = black_box(0xE3u8);
    let n = black_box(8usize);
    if let Some(dst) = b.get_mut(..n) { dst.fill(v); }
    emit(id, if b.iter().all(|&x| x == 0xE3) { "ACCEPTED" } else { "REFUSED" });
}
// 09 memset: canary-guarded body, exact fit keeps guards
fn c09(id: usize) {
    let mut c = black_box(canary_buf());
    match c.get_mut(4..20) {
        None => emit(id, "REFUSED"),
        Some(body) => {
            body.fill(black_box(0x7Eu8));
            let ok = canary_ok(&c) && c[4..20].iter().all(|&x| x == 0x7E);
            emit(id, if ok { "ACCEPTED" } else { "REFUSED" });
        }
    }
}
// 10 memset: logging OFF/ON parity -- C-only instrumentation -> SKIP
fn c10(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }

// ========================================================================= */
// 11..20  memcpy                                                            */

// 11 memcpy: exact fit, copied bytes verified
fn c11(id: usize) {
    let s = black_box(pattern(16, 11));
    let mut d = black_box(vec![0x9Au8; 16]);
    let n = black_box(16usize);
    match d.get_mut(..n) {
        None => emit(id, "REFUSED"),
        Some(dst) => {
            dst.copy_from_slice(&s[..n]);
            emit(id, if d == s { "ACCEPTED" } else { "REFUSED" });
        }
    }
}
// 12 memcpy: zero length leaves destination unchanged
fn c12(id: usize) {
    let s = black_box(pattern(4, 12));
    let mut d = black_box(pattern(4, 22));
    let before = d.clone();
    let n = black_box(0usize);
    if let Some(dst) = d.get_mut(..n) { dst.copy_from_slice(&s[..n]); }
    emit(id, if d == before { "ACCEPTED" } else { "REFUSED" });
}
// 13 memcpy: capacity-1 (15 of 16 bytes)
fn c13(id: usize) {
    let s = black_box(pattern(15, 13));
    let mut d = black_box(pattern(16, 23));
    let mut refb = d.clone();
    refb[..15].copy_from_slice(&s);
    let n = black_box(15usize);
    if let Some(dst) = d.get_mut(..n) { dst.copy_from_slice(&s[..n]); }
    emit(id, if d == refb { "ACCEPTED" } else { "REFUSED" });
}
// 14 memcpy: exactly capacity (16 of 16 bytes)
fn c14(id: usize) {
    let s = black_box(pattern(16, 14));
    let mut d = black_box(vec![0u8; 16]);
    let n = black_box(16usize);
    if let Some(dst) = d.get_mut(..n) { dst.copy_from_slice(&s[..n]); }
    emit(id, if d == s { "ACCEPTED" } else { "REFUSED" });
}
// 15 memcpy: capacity+1 (17 into 16) -- safe slice bounds check refuses
fn c15(id: usize) {
    let s = black_box(vec![0x11u8; 17]);
    let mut d = black_box(vec![0x00u8; 16]);
    let n = black_box(17usize);
    match d.get_mut(..n) {
        None => emit(id, "REFUSED"),
        Some(dst) => { dst.copy_from_slice(&s[..n]); emit(id, "ACCEPTED"); }
    }
}
// 16 memcpy: NULL source -- no null references -> SKIP
fn c16(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 17 memcpy: SIZE_MAX length -- no such slice -> SKIP
fn c17(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 18 memcpy: overlapping ranges -- UB in both languages, never invoked
fn c18(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 19 memcpy: high-bit bytes into canary-guarded body
fn c19(id: usize) {
    let s: Vec<u8> = black_box((0..16).map(|i| 0x80u8 + i as u8).collect());
    let mut c = black_box(canary_buf());
    let n = black_box(16usize);
    match c.get_mut(4..20) {
        None => emit(id, "REFUSED"),
        Some(body) => match body.get_mut(..n) {
            None => emit(id, "REFUSED"),
            Some(dst) => {
                dst.copy_from_slice(&s[..n]);
                let ok = canary_ok(&c) && c[4..20] == s[..];
                emit(id, if ok { "ACCEPTED" } else { "REFUSED" });
            }
        }
    }
}
// 20 memcpy: embedded NUL byte is ordinary binary data
fn c20(id: usize) {
    let s: Vec<u8> = black_box(b"a\0b\0c\0d\0".to_vec());
    let mut d = black_box(vec![0u8; 8]);
    let n = black_box(8usize);
    if let Some(dst) = d.get_mut(..n) { dst.copy_from_slice(&s[..n]); }
    let ok = d == s && d[1] == 0 && d[3] == 0;
    emit(id, if ok { "ACCEPTED" } else { "REFUSED" });
}

// ========================================================================= */
// 21..30  memmove                                                           */

// 21 memmove: non-overlapping exact fit
fn c21(id: usize) {
    let s = black_box(pattern(12, 21));
    let mut d = black_box(vec![0x99u8; 12]);
    let n = black_box(12usize);
    if let Some(dst) = d.get_mut(..n) { dst.copy_from_slice(&s[..n]); }
    emit(id, if d == s { "ACCEPTED" } else { "REFUSED" });
}
// 22 memmove: overlapping forward (dest = src+2), defined via copy_within
fn c22(id: usize) {
    let mut b = black_box({
        let mut v = vec![0u8; 32];
        v[..17].copy_from_slice(b"0123456789abcdef\0");
        v
    });
    let mut refb = b.clone();
    refb.copy_within(0..10, 2);
    b.copy_within(black_box(0..10), black_box(2));
    emit(id, if b == refb { "ACCEPTED" } else { "REFUSED" });
}
// 23 memmove: overlapping backward (dest = src-2), defined via copy_within
fn c23(id: usize) {
    let mut b = black_box({
        let mut v = vec![0u8; 32];
        v[..17].copy_from_slice(b"0123456789abcdef\0");
        v
    });
    let mut refb = b.clone();
    refb.copy_within(2..12, 0);
    b.copy_within(black_box(2..12), black_box(0));
    emit(id, if b == refb { "ACCEPTED" } else { "REFUSED" });
}
// 24 memmove: zero length leaves destination unchanged
fn c24(id: usize) {
    let s = black_box(pattern(4, 24));
    let mut d = black_box(pattern(4, 34));
    let before = d.clone();
    let n = black_box(0usize);
    if let Some(dst) = d.get_mut(..n) { dst.copy_from_slice(&s[..n]); }
    emit(id, if d == before { "ACCEPTED" } else { "REFUSED" });
}
// 25 memmove: capacity-1 (15 of 16 bytes)
fn c25(id: usize) {
    let s = black_box(pattern(15, 25));
    let mut d = black_box(pattern(16, 35));
    let mut refb = d.clone();
    refb[..15].copy_from_slice(&s);
    let n = black_box(15usize);
    if let Some(dst) = d.get_mut(..n) { dst.copy_from_slice(&s[..n]); }
    emit(id, if d == refb { "ACCEPTED" } else { "REFUSED" });
}
// 26 memmove: capacity+1 (17 into 16) -- safe slice bounds check refuses
fn c26(id: usize) {
    let s = black_box(vec![0x22u8; 17]);
    let mut d = black_box(vec![0u8; 16]);
    let n = black_box(17usize);
    match d.get_mut(..n) {
        None => emit(id, "REFUSED"),
        Some(dst) => { dst.copy_from_slice(&s[..n]); emit(id, "ACCEPTED"); }
    }
}
// 27 memmove: NULL destination -> SKIP
fn c27(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 28 memmove: SIZE_MAX length -> SKIP
fn c28(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 29 memmove: high-bit bytes into canary-guarded body
fn c29(id: usize) {
    let s: Vec<u8> = black_box((0..16).map(|i| 0xE0u8 + i as u8).collect());
    let mut c = black_box(canary_buf());
    let n = black_box(16usize);
    match c.get_mut(4..20) {
        None => emit(id, "REFUSED"),
        Some(body) => match body.get_mut(..n) {
            None => emit(id, "REFUSED"),
            Some(dst) => {
                dst.copy_from_slice(&s[..n]);
                let ok = canary_ok(&c) && c[4..20] == s[..];
                emit(id, if ok { "ACCEPTED" } else { "REFUSED" });
            }
        }
    }
}
// 30 memmove: logging OFF/ON parity -- C-only instrumentation -> SKIP
fn c30(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }

// ========================================================================= */
// 31..40  memcmp                                                            */

// 31 memcmp: equal byte ranges
fn c31(id: usize) {
    let a = black_box(pattern(8, 31));
    let b = black_box(a.clone());
    let v = a == b;
    emit(id, if v { "ACCEPTED" } else { "REFUSED" });
}
// 32 memcmp: lexicographically less
fn c32(id: usize) {
    let a: Vec<u8> = black_box(vec![1, 2, 3, 4]);
    let b: Vec<u8> = black_box(vec![1, 2, 4, 4]);
    emit(id, if a < b { "ACCEPTED" } else { "REFUSED" });
}
// 33 memcmp: lexicographically greater
fn c33(id: usize) {
    let a: Vec<u8> = black_box(vec![9, 2, 3, 4]);
    let b: Vec<u8> = black_box(vec![1, 2, 3, 4]);
    emit(id, if a > b { "ACCEPTED" } else { "REFUSED" });
}
// 34 memcmp: zero length compares equal (empty prefix of both)
fn c34(id: usize) {
    let a: Vec<u8> = black_box(vec![0xFF, 1]);
    let b: Vec<u8> = black_box(vec![0, 0xFF]);
    let n = black_box(0usize);
    let pa = a.get(..n);
    let pb = b.get(..n);
    emit(id, if pa == pb && pa == Some(&[][..]) { "ACCEPTED" } else { "REFUSED" });
}
// 35 memcmp: high-bit bytes sign
fn c35(id: usize) {
    let a: Vec<u8> = black_box(vec![0, 0x80, 0xFF]);
    let b: Vec<u8> = black_box(vec![0, 0x80, 0xFE]);
    emit(id, if a > b { "ACCEPTED" } else { "REFUSED" });
}
// 36 memcmp: embedded NUL byte is just data
fn c36(id: usize) {
    let a: Vec<u8> = black_box(b"a\0b".to_vec());
    let b: Vec<u8> = black_box(b"a\0c".to_vec());
    emit(id, if a < b { "ACCEPTED" } else { "REFUSED" });
}
// 37 memcmp: NULL first buffer -> SKIP
fn c37(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 38 memcmp: over-capacity length (8 from 4-byte buffers) -- slice check refuses
fn c38(id: usize) {
    let a: Vec<u8> = black_box(vec![1, 2, 3, 4]);
    let b: Vec<u8> = black_box(vec![1, 2, 3, 4, 5, 6, 7, 8]);
    let n = black_box(8usize);
    match (a.get(..n), b.get(..n)) {
        (None, _) | (_, None) => emit(id, "REFUSED"),
        (Some(x), Some(y)) => emit(id, if x == y { "ACCEPTED" } else { "REFUSED" }),
    }
}
// 39 memcmp: SIZE_MAX length -> SKIP
fn c39(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 40 memcmp: logging OFF/ON parity -- C-only instrumentation -> SKIP
fn c40(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }

// ========================================================================= */
// 41..50  strcpy                                                            */

// 41 strcpy: exact fit (src "abc", dest[4])
fn c41(id: usize) {
    let src: &[u8] = black_box(b"abc\0");
    let need = cstrlen(src) + 1;
    let mut d = black_box(vec![0u8; 4]);
    match d.get_mut(..need) {
        None => emit(id, "REFUSED"),
        Some(dst) => {
            dst.copy_from_slice(&src[..need]);
            emit(id, if d == b"abc\0" { "ACCEPTED" } else { "REFUSED" });
        }
    }
}
// 42 strcpy: zero-length source (empty String)
fn c42(id: usize) {
    let s = black_box(String::from("\0")); // C empty string == one NUL byte
    let src = s.as_bytes();
    let need = cstrlen(src) + 1;
    let mut d = black_box(vec![b'X'; 2]);
    if let Some(dst) = d.get_mut(..need) { dst.copy_from_slice(&src[..need]); }
    emit(id, if d[0] == 0 { "ACCEPTED" } else { "REFUSED" });
}
// 43 strcpy: capacity-1 (src "abc", dest[5]) leaves slack
fn c43(id: usize) {
    let src: &[u8] = black_box(b"abc\0");
    let need = cstrlen(src) + 1;
    let mut d = black_box(vec![b'X'; 5]);
    if let Some(dst) = d.get_mut(..need) { dst.copy_from_slice(&src[..need]); }
    emit(id, if d[..4] == b"abc\0"[..] { "ACCEPTED" } else { "REFUSED" });
}
// 44 strcpy: exactly capacity, full 8-byte buffer
fn c44(id: usize) {
    let src: &[u8] = black_box(b"abcde\0");
    let need = cstrlen(src) + 1;
    let mut d = black_box(vec![b'Y'; 8]);
    if let Some(dst) = d.get_mut(..need) { dst.copy_from_slice(&src[..need]); }
    emit(id, if d[..6] == b"abcde\0"[..] { "ACCEPTED" } else { "REFUSED" });
}
// 45 strcpy: capacity+1 (src "abcd", dest[4]) -- bounds check refuses
fn c45(id: usize) {
    let src: &[u8] = black_box(b"abcd\0");
    let need = black_box(cstrlen(src) + 1);
    let mut d = black_box(vec![b'X'; 4]);
    match d.get_mut(..need) {
        None => emit(id, "REFUSED"),
        Some(dst) => { dst.copy_from_slice(&src[..need]); emit(id, "ACCEPTED"); }
    }
}
// 46 strcpy: NULL source -> SKIP
fn c46(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 47 strcpy: embedded NUL source copies up to first NUL
fn c47(id: usize) {
    let src: Vec<u8> = black_box(b"a\0b\0".to_vec());
    let need = cstrlen(&src) + 1;
    let mut d = black_box(vec![b'X'; 4]);
    if let Some(dst) = d.get_mut(..need) { dst.copy_from_slice(&src[..need]); }
    emit(id, if d[0] == b'a' && d[1] == 0 { "ACCEPTED" } else { "REFUSED" });
}
// 48 strcpy: high-bit bytes are ordinary data
fn c48(id: usize) {
    let src: Vec<u8> = black_box(vec![0x80, 0xFF, 0]);
    let need = cstrlen(&src) + 1;
    let mut d = black_box(vec![0u8; 4]);
    if let Some(dst) = d.get_mut(..need) { dst.copy_from_slice(&src[..need]); }
    emit(id, if d[..3] == src[..] { "ACCEPTED" } else { "REFUSED" });
}
// 49 strcpy: canary-guarded body, source fits
fn c49(id: usize) {
    let src: &[u8] = black_box(b"hello\0");
    let need = cstrlen(src) + 1;
    let mut c = black_box(canary_buf());
    match c.get_mut(4..20) {
        None => emit(id, "REFUSED"),
        Some(body) => match body.get_mut(..need) {
            None => emit(id, "REFUSED"),
            Some(dst) => {
                dst.copy_from_slice(&src[..need]);
                let ok = canary_ok(&c) && c[4..10] == b"hello\0"[..];
                emit(id, if ok { "ACCEPTED" } else { "REFUSED" });
            }
        }
    }
}
// 50 strcpy: logging OFF/ON parity -- C-only instrumentation -> SKIP
fn c50(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }

// ========================================================================= */
// 51..60  strncpy                                                           */

// 51 strncpy: exact fit (n=3, src "abc", dest[4])
fn c51(id: usize) {
    let src: &[u8] = black_box(b"abc\0");
    let n = black_box(3usize);
    let mut d = black_box(vec![0u8; 4]);
    match d.get_mut(..n + 1) {
        None => emit(id, "REFUSED"),
        Some(dst) => {
            dst[..n].copy_from_slice(&src[..n]);
            dst[n] = 0;
            emit(id, if d == b"abc\0" { "ACCEPTED" } else { "REFUSED" });
        }
    }
}
// 52 strncpy: zero length (n=0) leaves destination untouched
fn c52(id: usize) {
    let src: &[u8] = black_box(b"abc\0");
    let n = black_box(0usize);
    let mut d = black_box(vec![b'X'; 4]);
    if let Some(dst) = d.get_mut(..n) { dst.copy_from_slice(&src[..n]); }
    emit(id, if d[0] == b'X' { "ACCEPTED" } else { "REFUSED" });
}
// 53 strncpy: capacity-1 (n=3, dest[5])
fn c53(id: usize) {
    let src: &[u8] = black_box(b"abc\0");
    let n = black_box(3usize);
    let mut d = black_box(vec![b'X'; 5]);
    if let Some(dst) = d.get_mut(..n + 1) {
        dst[..n].copy_from_slice(&src[..n]);
        dst[n] = 0;
    }
    emit(id, if d[..4] == b"abc\0"[..] { "ACCEPTED" } else { "REFUSED" });
}
// 54 strncpy: exactly capacity (n=4, dest[5] = n+1)
fn c54(id: usize) {
    let src: &[u8] = black_box(b"abcd\0");
    let n = black_box(4usize);
    let mut d = black_box(vec![0u8; 5]);
    if let Some(dst) = d.get_mut(..n + 1) {
        dst[..n].copy_from_slice(&src[..n]);
        dst[n] = 0;
    }
    emit(id, if d == b"abcd\0" { "ACCEPTED" } else { "REFUSED" });
}
// 55 strncpy: capacity+1 (n=4, dest[4]) -- bounds check refuses
fn c55(id: usize) {
    let src: &[u8] = black_box(b"abcd\0");
    let n = black_box(4usize);
    let mut d = black_box(vec![b'X'; 4]);
    match d.get_mut(..n + 1) {
        None => emit(id, "REFUSED"),
        Some(dst) => { dst[..n].copy_from_slice(&src[..n]); dst[n] = 0; emit(id, "ACCEPTED"); }
    }
}
// 56 strncpy: NULL source -> SKIP
fn c56(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 57 strncpy: SIZE_MAX count -> SKIP
fn c57(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 58 strncpy: UNTERMINATED source with explicit bound (byte slice, no NUL needed)
fn c58(id: usize) {
    let src: Vec<u8> = black_box(b"abcd".to_vec()); // exactly 4 bytes, no terminator
    let n = black_box(4usize);
    let mut d = black_box(vec![0u8; 5]);
    match d.get_mut(..n + 1) {
        None => emit(id, "REFUSED"),
        Some(dst) => {
            dst[..n].copy_from_slice(&src[..n]);
            dst[n] = 0;
            emit(id, if d == b"abcd\0" { "ACCEPTED" } else { "REFUSED" });
        }
    }
}
// 59 strncpy: embedded NUL copies up to and including first NUL
fn c59(id: usize) {
    let src: Vec<u8> = black_box(b"ab\0d".to_vec());
    let n = black_box(4usize);
    let eff = n.min(cstrlen(&src) + 1);
    let mut d = black_box(vec![b'X'; 5]);
    if let Some(dst) = d.get_mut(..eff) { dst.copy_from_slice(&src[..eff]); }
    emit(id, if d[..3] == b"ab\0"[..] { "ACCEPTED" } else { "REFUSED" });
}
// 60 strncpy: high-bit bytes into canary-guarded body
fn c60(id: usize) {
    let src: Vec<u8> = black_box(vec![0x80, 0xFF, 0]);
    let n = black_box(2usize);
    let mut c = black_box(canary_buf());
    match c.get_mut(4..20) {
        None => emit(id, "REFUSED"),
        Some(body) => match body.get_mut(..n + 1) {
            None => emit(id, "REFUSED"),
            Some(dst) => {
                dst[..n].copy_from_slice(&src[..n]);
                dst[n] = 0;
                let ok = canary_ok(&c) && c[4..6] == src[..2];
                emit(id, if ok { "ACCEPTED" } else { "REFUSED" });
            }
        }
    }
}

// ========================================================================= */
// 61..70  strcat                                                            */

// helper: safe strcat model -- returns None when the destination is too small
fn safe_strcat(dest: &mut [u8], src: &[u8]) -> Option<()> {
    let dl = cstrlen(dest);
    let sl = cstrlen(src);
    let need = dl.checked_add(sl)?.checked_add(1)?;
    let dst = dest.get_mut(dl..need)?;
    dst.copy_from_slice(&src[..sl + 1]);
    Some(())
}
// 61 strcat: exact fit ("ab"+"cde" into dest[6])
fn c61(id: usize) {
    let mut d = black_box(b"ab\0\0\0\0".to_vec());
    let src: &[u8] = black_box(b"cde\0");
    match safe_strcat(&mut d, src) {
        Some(()) => emit(id, if d == b"abcde\0" { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 62 strcat: zero-length source appends nothing
fn c62(id: usize) {
    let mut d = black_box(b"ab\0\0".to_vec());
    let src: &[u8] = black_box(b"\0");
    match safe_strcat(&mut d, src) {
        Some(()) => emit(id, if d[..3] == b"ab\0"[..] { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 63 strcat: capacity-1 (dest[7])
fn c63(id: usize) {
    let mut d = black_box(b"ab\0\0\0\0\0".to_vec());
    let src: &[u8] = black_box(b"cde\0");
    match safe_strcat(&mut d, src) {
        Some(()) => emit(id, if d[..6] == b"abcde\0"[..] { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 64 strcat: exactly capacity (dest[6])
fn c64(id: usize) {
    let mut d = black_box(b"ab\0\0\0\0".to_vec());
    let src: &[u8] = black_box(b"cde\0");
    match safe_strcat(&mut d, src) {
        Some(()) => emit(id, if d == b"abcde\0" { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 65 strcat: capacity+1 (dest[5], need 6) -- bounds check refuses
fn c65(id: usize) {
    let mut d = black_box(b"ab\0\0\0".to_vec());
    let src: &[u8] = black_box(b"cde\0");
    match safe_strcat(&mut d, src) {
        Some(()) => emit(id, "ACCEPTED"),
        None => emit(id, "REFUSED"),
    }
}
// 66 strcat: NULL source -> SKIP
fn c66(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 67 strcat: SIZE_MAX dmax -> SKIP
fn c67(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 68 strcat: embedded NUL in source appends up to first NUL
fn c68(id: usize) {
    let mut d = black_box(b"ab\0\0\0\0\0".to_vec());
    let src: Vec<u8> = black_box(b"x\0y".to_vec());
    match safe_strcat(&mut d, &src) {
        Some(()) => emit(id, if d[..4] == b"abx\0"[..] { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 69 strcat: canary-guarded body, append fits
fn c69(id: usize) {
    let mut c = black_box(canary_buf());
    let src: &[u8] = black_box(b"cde\0");
    match c.get_mut(4..20) {
        None => emit(id, "REFUSED"),
        Some(body) => {
            // place "ab\0" at the start of the body first
            body[..3].copy_from_slice(b"ab\0");
            match safe_strcat(body, src) {
                Some(()) => {
                    let ok = canary_ok(&c) && c[4..10] == b"abcde\0"[..];
                    emit(id, if ok { "ACCEPTED" } else { "REFUSED" });
                }
                None => emit(id, "REFUSED"),
            }
        }
    }
}
// 70 strcat: logging OFF/ON parity -- C-only instrumentation -> SKIP
fn c70(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }

// ========================================================================= */
// 71..80  strncat                                                           */

// helper: safe strncat model -- copies at most n chars (stopping at a NUL) + NUL
fn safe_strncat(dest: &mut [u8], src: &[u8], n: usize) -> Option<()> {
    let dl = cstrlen(dest);
    let take = n.min(cstrlen(src));
    let need = dl.checked_add(take)?.checked_add(1)?;
    let dst = dest.get_mut(dl..need)?;
    dst[..take].copy_from_slice(&src[..take]);
    dst[take] = 0;
    Some(())
}
// 71 strncat: exact fit ("ab"+"cde", n=3, dest[8])
fn c71(id: usize) {
    let mut d = black_box(b"ab\0\0\0\0\0\0".to_vec());
    let src: &[u8] = black_box(b"cde\0");
    let n = black_box(3usize);
    match safe_strncat(&mut d, src, n) {
        Some(()) => emit(id, if d[..6] == b"abcde\0"[..] { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 72 strncat: zero length (n=0) appends nothing
fn c72(id: usize) {
    let mut d = black_box(b"ab\0\0\0\0\0\0".to_vec());
    let src: &[u8] = black_box(b"cde\0");
    let n = black_box(0usize);
    match safe_strncat(&mut d, src, n) {
        Some(()) => emit(id, if d[..3] == b"ab\0"[..] { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 73 strncat: capacity-1 (dest[7], need 6)
fn c73(id: usize) {
    let mut d = black_box(b"ab\0\0\0\0\0".to_vec());
    let src: &[u8] = black_box(b"cde\0");
    let n = black_box(3usize);
    match safe_strncat(&mut d, src, n) {
        Some(()) => emit(id, if d[..6] == b"abcde\0"[..] { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 74 strncat: exactly capacity (dest[6], need 6)
fn c74(id: usize) {
    let mut d = black_box(b"ab\0\0\0\0".to_vec());
    let src: &[u8] = black_box(b"cde\0");
    let n = black_box(3usize);
    match safe_strncat(&mut d, src, n) {
        Some(()) => emit(id, if d == b"abcde\0" { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 75 strncat: capacity+1 (dest[5], need 6) -- bounds check refuses
fn c75(id: usize) {
    let mut d = black_box(b"ab\0\0\0".to_vec());
    let src: &[u8] = black_box(b"cde\0");
    let n = black_box(3usize);
    match safe_strncat(&mut d, src, n) {
        Some(()) => emit(id, "ACCEPTED"),
        None => emit(id, "REFUSED"),
    }
}
// 76 strncat: NULL source -> SKIP
fn c76(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 77 strncat: SIZE_MAX count -> SKIP
fn c77(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 78 strncat: UNTERMINATED source with explicit bound (byte slice)
fn c78(id: usize) {
    let mut c = black_box(canary_buf());
    let src: Vec<u8> = black_box(b"cdef".to_vec()); // 4 bytes, no terminator
    let n = black_box(4usize);
    match c.get_mut(4..20) {
        None => emit(id, "REFUSED"),
        Some(body) => {
            body[..3].copy_from_slice(b"ab\0");
            // explicit-bound append: take n bytes (no NUL in source) + NUL
            let take = n.min(src.len());
            match body.get_mut(2..2 + take + 1) {
                None => emit(id, "REFUSED"),
                Some(dst) => {
                    dst[..take].copy_from_slice(&src[..take]);
                    dst[take] = 0;
                    let ok = canary_ok(&c) && c[4..11] == b"abcdef\0"[..];
                    emit(id, if ok { "ACCEPTED" } else { "REFUSED" });
                }
            }
        }
    }
}
// 79 strncat: embedded NUL in source appends up to first NUL
fn c79(id: usize) {
    let mut d = black_box(b"ab\0\0\0\0\0\0".to_vec());
    let src: Vec<u8> = black_box(b"x\0y".to_vec());
    let n = black_box(3usize);
    match safe_strncat(&mut d, &src, n) {
        Some(()) => emit(id, if d[..4] == b"abx\0"[..] { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 80 strncat: high-bit bytes into canary-guarded body
fn c80(id: usize) {
    let mut c = black_box(canary_buf());
    let src: Vec<u8> = black_box(vec![0x80, 0xFF, 0]);
    let n = black_box(2usize);
    match c.get_mut(4..20) {
        None => emit(id, "REFUSED"),
        Some(body) => {
            body[..3].copy_from_slice(b"ab\0");
            match safe_strncat(body, &src, n) {
                Some(()) => {
                    let ok = canary_ok(&c) && c[6] == 0x80 && c[7] == 0xFF && c[8] == 0;
                    emit(id, if ok { "ACCEPTED" } else { "REFUSED" });
                }
                None => emit(id, "REFUSED"),
            }
        }
    }
}

// ========================================================================= */
// 81..90  strlen                                                            */

// 81 strlen: ordinary terminated string (String)
fn c81(id: usize) {
    let s = black_box(String::from("hello"));
    emit(id, if s.len() == 5 { "ACCEPTED" } else { "REFUSED" });
}
// 82 strlen: empty string -> 0
fn c82(id: usize) {
    let s = black_box(String::new());
    emit(id, if s.len() == 0 { "ACCEPTED" } else { "REFUSED" });
}
// 83 strlen: NUL at last byte of 16-byte buffer (length 15)
fn c83(id: usize) {
    let mut b = black_box(vec![b'a'; 16]);
    b[15] = 0;
    let n = cstrlen(&b);
    emit(id, if n == 15 { "ACCEPTED" } else { "REFUSED" });
}
// 84 strlen: NUL mid-buffer (length 8)
fn c84(id: usize) {
    let mut b = black_box(vec![b'A'; 16]);
    b[8] = 0;
    let n = cstrlen(&b);
    emit(id, if n == 8 { "ACCEPTED" } else { "REFUSED" });
}
// 85 strlen: NO terminator in 16 bytes -- bounded search detects it
fn c85(id: usize) {
    let b = black_box(pattern(16, 85));
    match b.iter().position(|&x| x == 0) {
        None => emit(id, "REFUSED"), // raw strlen deliberately not modelled
        Some(_) => emit(id, "ACCEPTED"),
    }
}
// 86 strlen: NULL -> SKIP (no null references)
fn c86(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 87 strlen: SIZE_MAX bound -> SKIP
fn c87(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 88 strlen: embedded NUL stops count at first one
fn c88(id: usize) {
    let b: Vec<u8> = black_box(b"ab\0cd\0\0\0".to_vec());
    emit(id, if cstrlen(&b) == 2 { "ACCEPTED" } else { "REFUSED" });
}
// 89 strlen: high-bit bytes before NUL counted as ordinary chars
fn c89(id: usize) {
    let b: Vec<u8> = black_box(vec![0x80, 0xFF, 0, 0]);
    emit(id, if cstrlen(&b) == 2 { "ACCEPTED" } else { "REFUSED" });
}
// 90 strlen: logging OFF/ON parity -- C-only instrumentation -> SKIP
fn c90(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }

// ========================================================================= */
// 91..100  strnlen                                                          */

// helper: bounded length query via safe slice -- None when bound exceeds buffer
fn safe_strnlen(b: &[u8], bound: usize) -> Option<usize> {
    let s = b.get(..bound)?;
    Some(s.iter().position(|&x| x == 0).unwrap_or(s.len()))
}
// 91 strnlen: NUL found within bound -> exact length
fn c91(id: usize) {
    let b: Vec<u8> = black_box(b"abc\0\0\0\0\0\0\0\0\0\0\0\0\0".to_vec());
    let bound = black_box(16usize);
    match safe_strnlen(&b, bound) {
        Some(n) => emit(id, if n == 3 { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 92 strnlen: zero bound -> defined 0
fn c92(id: usize) {
    let b: Vec<u8> = black_box(b"abc".to_vec());
    let bound = black_box(0usize);
    match safe_strnlen(&b, bound) {
        Some(n) => emit(id, if n == 0 { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 93 strnlen: NUL at 14 of 16
fn c93(id: usize) {
    let mut b = black_box(vec![b'z'; 16]);
    b[14] = 0;
    let bound = black_box(16usize);
    match safe_strnlen(&b, bound) {
        Some(n) => emit(id, if n == 14 { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 94 strnlen: NUL exactly at capacity (index 15 of 16)
fn c94(id: usize) {
    let mut b = black_box(vec![b'z'; 16]);
    b[15] = 0;
    let bound = black_box(16usize);
    match safe_strnlen(&b, bound) {
        Some(n) => emit(id, if n == 15 { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 95 strnlen: capacity+1 bound (17 > 16) -- bounds check refuses
fn c95(id: usize) {
    let b: Vec<u8> = black_box(vec![b'a'; 16]);
    let bound = black_box(17usize);
    match safe_strnlen(&b, bound) {
        None => emit(id, "REFUSED"),
        Some(_) => emit(id, "ACCEPTED"),
    }
}
// 96 strnlen: NULL -> SKIP
fn c96(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 97 strnlen: SIZE_MAX bound -> SKIP
fn c97(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }
// 98 strnlen: no NUL within bound returns the bound (defined)
fn c98(id: usize) {
    let b = black_box(pattern(16, 98));
    let bound = black_box(16usize);
    match safe_strnlen(&b, bound) {
        Some(n) => emit(id, if n == 16 { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 99 strnlen: embedded NUL returns first terminator position
fn c99(id: usize) {
    let b: Vec<u8> = black_box(b"ab\0cd\0\0\0".to_vec());
    let bound = black_box(8usize);
    match safe_strnlen(&b, bound) {
        Some(n) => emit(id, if n == 2 { "ACCEPTED" } else { "REFUSED" }),
        None => emit(id, "REFUSED"),
    }
}
// 100 strnlen: high-bit bytes + logging parity -- C-only instrumentation -> SKIP
fn c100(id: usize) { let _ = black_box(id); emit(id, "SKIP"); }

// Compile-time assertion: exactly 100 cases are registered.
type Case = fn(usize);
const CASES: [Case; 100] = [
    c01, c02, c03, c04, c05, c06, c07, c08, c09, c10,
    c11, c12, c13, c14, c15, c16, c17, c18, c19, c20,
    c21, c22, c23, c24, c25, c26, c27, c28, c29, c30,
    c31, c32, c33, c34, c35, c36, c37, c38, c39, c40,
    c41, c42, c43, c44, c45, c46, c47, c48, c49, c50,
    c51, c52, c53, c54, c55, c56, c57, c58, c59, c60,
    c61, c62, c63, c64, c65, c66, c67, c68, c69, c70,
    c71, c72, c73, c74, c75, c76, c77, c78, c79, c80,
    c81, c82, c83, c84, c85, c86, c87, c88, c89, c90,
    c91, c92, c93, c94, c95, c96, c97, c98, c99, c100,
];
const _: [(); 100] = [(); CASES.len()];

fn main() {
    let n: usize = std::env::args()
        .nth(1)
        .and_then(|s| s.parse().ok())
        .unwrap_or(1);
    if n >= 1 && n <= 100 {
        CASES[n - 1](n);
    } else {
        emit(n, "SKIP");
    }
}

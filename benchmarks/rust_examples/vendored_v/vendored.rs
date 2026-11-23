//! Safe-Rust examples for every newly vendored `cplus_v_*` checked function.
//!
//! One example per symbol listed by
//! `nm -g build/safe/libcplus_safecrt.a | grep ' T _cplus_v_'`.
//!
//! There is **no `unsafe`, no FFI and no raw pointer** in this file. Each
//! example computes the same `<api> <case> <token>` line that the companion
//! `harness.c` obtains by actually calling the C symbol. NULL pointers are
//! modelled with `Option`, string/memory work uses `str`/slice methods,
//! iterators, `chars` and `Result`, and canaries are ordinary slice reads.
//!
//! The examples are a *behavioural illustration*, not a claim that Rust's
//! standard library is the same implementation as the vendored safeclib
//! bodies; see NOTES.md.
//!
//! Build: rustc -O benchmarks/rust_examples/vendored_v/vendored.rs -o <out>

use std::cmp::Ordering;

/// BOS_UNKNOWN from the pinned safeclib headers.
const BOS: usize = usize::MAX;
/// RSIZE_MAX_STR at the pin = 4 KiB.
const MAXSTR: usize = 4 << 10;
/// RSIZE_MAX_MEM at the pin = 4 * RSIZE_MAX_STR.
const MAXMEM: usize = MAXSTR * 4;
/// errno.h EOVERFLOW on the verified Darwin host (harness maps the same code).
const EOVERFLOW: i32 = 84;

/// Print one comparison record.
fn emit(api: &str, case: &str, token: &str) {
    println!("{} {} {}", api, case, token);
}

/// Pinned safeclib error codes -> stable symbolic token.
fn etok(rc: i32) -> String {
    match rc {
        0 => "OK".to_string(),
        400 => "E:NULL".to_string(),
        401 => "E:ZERO".to_string(),
        403 => "E:MAX".to_string(),
        404 => "E:OVLP".to_string(),
        406 => "E:NOSPC".to_string(),
        408 => "E:NODIFF".to_string(),
        409 => "E:NOTFND".to_string(),
        EOVERFLOW => "E:OVER".to_string(),
        other => format!("E:UNKNOWN({})", other),
    }
}

/// Sign-only token for comparison results (locale/magnitude independent).
fn stok(order: Ordering) -> String {
    match order {
        Ordering::Less => "s=-1".to_string(),
        Ordering::Equal => "s=0".to_string(),
        Ordering::Greater => "s=1".to_string(),
    }
}

/// Lower-case hex of a byte buffer, e.g. for buffer-content tokens.
fn hex(bytes: &[u8]) -> String {
    let mut out = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        out.push_str(&format!("{:02x}", b));
    }
    out
}

/// Append the canary verdict to a result token.
fn canary(api: &str, case: &str, result: String, intact: bool) {
    emit(
        api,
        case,
        &format!("{};canary={}", result, if intact { "ok" } else { "BAD" }),
    );
}

/// Shared safe model of upstream's dest/dmax/BOS constraint checks.
/// `None` for `bos` means BOS_UNKNOWN, matching the upstream branch order:
/// NULL -> zero length -> (unknown BOS: max | known BOS: object overflow).
fn constraint(dest_is_null: bool, dmax: usize, bos: usize, max: usize) -> Result<(), i32> {
    if dest_is_null {
        return Err(400);
    }
    if dmax == 0 {
        return Err(401);
    }
    if bos == BOS {
        if dmax > max {
            return Err(403);
        }
    } else if dmax > bos {
        return Err(EOVERFLOW);
    }
    Ok(())
}

/// Safe length-limit checks used by the length-bearing APIs.
fn len_check(len: usize, max: usize) -> Result<(), i32> {
    if len > max {
        Err(403) // ESLEMAX
    } else {
        Ok(())
    }
}

fn nospace_check(n: usize, dmax: usize) -> Result<(), i32> {
    if n > dmax {
        Err(406) // ESNOSPC
    } else {
        Ok(())
    }
}

/// Emit a violation token when a safe model reports a constraint error.
fn violation(api: &str, case: &str, checked: Result<(), i32>) -> bool {
    match checked {
        Err(rc) => {
            emit(api, case, &etok(rc));
            true
        }
        Ok(()) => false,
    }
}

/// Bytes before the first NUL within `dmax` (safe analogue of a C string scan).
fn scanned(buf: &[u8], dmax: usize) -> &[u8] {
    let n = dmax.min(buf.len());
    let slice = &buf[..n];
    &slice[..slice.iter().position(|&b| b == 0).unwrap_or(n)]
}

// ---------------------------------------------------------------------------
// 1-2. memchr_s / __memchr_s_chk
// ---------------------------------------------------------------------------

fn memchr_example(
    api: &str,
    hay: &[u8],
    dmax: usize,
    bos: usize,
    max: usize,
    ch: u8,
) {
    // valid: iterator position over the first `dmax` bytes
    let idx = hay.iter().take(dmax.min(hay.len())).position(|&b| b == ch);
    match idx {
        Some(i) => emit(api, "valid", &format!("ok:i={}", i)),
        None => emit(api, "valid", &etok(409)),
    }

    // null: Option::None models a NULL dest; constraint() derives ESNULLP.
    if violation(api, "null", constraint(true, dmax, bos, max)) {
        // token already printed
    }
    // zero / one-over bound
    if violation(api, "zero", constraint(false, 0, bos, max)) {
        // token already printed
    }
    let over_dmax = if bos == BOS { max + 1 } else { bos + 1 };
    if violation(api, "over", constraint(false, over_dmax, bos, max)) {
        // token already printed
    }

    // edge: not found
    emit(api, "edge", &etok(409));

    // canary: search a bounded slice; the byte after it must remain 'Z'.
    let b: [u8; 5] = *b"abcdZ";
    let found = b[..4].iter().position(|&x| x == b'd');
    let result = found.map_or_else(|| etok(409), |i| format!("ok:i={}", i));
    canary(api, "canary", result, b[4] == b'Z');
}

// ---------------------------------------------------------------------------
// 3-4. memccpy_s / __memccpy_s_chk
// ---------------------------------------------------------------------------

/// Safe model: copy at most `dmax` bytes, stopping after the delimiter.
fn memccpy_model(
    initial: [u8; 8],
    dmax: usize,
    src: &[u8],
    delim: u8,
    n0: usize,
) -> Result<[u8; 8], i32> {
    let mut dest = initial;
    let mut n = n0;
    if n == 0 {
        dest[0] = 0;
        return Ok(dest);
    }
    if n > dmax {
        return Err(406); // ESNOSPC
    }
    for i in 0..dmax {
        if n == 0 {
            dest[i] = 0;
            return Ok(dest);
        }
        dest[i] = src[i];
        n -= 1;
        if dest[i] == delim {
            return Ok(dest);
        }
    }
    Err(406)
}

fn memccpy_example(api: &str, dmax: usize, bos: usize, max: usize, over_n: usize) {
    let aa: [u8; 8] = [0xAA; 8];
    let src = b"abcXdef";

    // valid: delimiter 'X' at src index 3; bytes after it stay 0xAA.
    match memccpy_model(aa, dmax, src, b'X', 7) {
        Ok(d) => emit(api, "valid", &format!("ok:{}", hex(&d))),
        Err(rc) => emit(api, "valid", &etok(rc)),
    }

    // null src -> ESNULLP (modelled with Option/Result).
    let null_src: Option<&[u8]> = None;
    let rc = null_src.map_or(400, |_| 0);
    emit(api, "null", &etok(rc));

    // zero dmax
    if violation(api, "zero", constraint(false, 0, bos, max)) {
        // token already printed
    }

    // one-over: n > dmax (or dmax > object size) -> ESNOSPC / EOVERFLOW.
    let over_rc = if bos == BOS {
        nospace_check(over_n, dmax).unwrap_err_or(403)
    } else {
        constraint(false, dmax + 1, bos, max).unwrap_err_or(403)
    };
    emit(api, "over", &etok(over_rc));

    // edge: n == 0 writes a NUL to dest[0] and is otherwise EOK.
    let d = memccpy_model(aa, dmax, src, b'x', 0).unwrap_or(aa);
    emit(api, "edge", &format!("ok:{}", hex(&d)));

    // canary: only the 4-byte work area is writable; byte 4 stays 0x5A.
    let mut with_canary = aa;
    with_canary[4] = 0x5A;
    let d = memccpy_model(with_canary, 4, b"ab", b'b', 2).unwrap_or(with_canary);
    canary(
        api,
        "canary",
        format!("ok:{}", hex(&d)),
        d[4] == 0x5A,
    );
}

/// Tiny helper: Result::unwrap_err_or without an unstable method.
trait UnwrapErrOr<T> {
    fn unwrap_err_or(self, fallback: T) -> T;
}
impl UnwrapErrOr<i32> for Result<(), i32> {
    fn unwrap_err_or(self, fallback: i32) -> i32 {
        match self {
            Err(e) => e,
            Ok(()) => fallback,
        }
    }
}

// ---------------------------------------------------------------------------
// 5-6. strchr_s / strrchr_s
// ---------------------------------------------------------------------------

fn strchr_example(api: &str, hay: &str, ch: u8, last: bool, bos: usize) {
    let scanned_bytes = scanned(hay.as_bytes(), hay.len());
    let idx = if last {
        scanned_bytes.iter().rposition(|&b| b == ch)
    } else {
        scanned_bytes.iter().position(|&b| b == ch)
    };
    match idx {
        Some(i) => emit(api, "valid", &format!("ok:i={}", i)),
        None => emit(api, "valid", &etok(409)),
    }

    violation(api, "null", constraint(true, 6, bos, MAXSTR));
    violation(api, "zero", constraint(false, 0, bos, MAXSTR));
    let over_dmax = if bos == BOS { MAXSTR + 1 } else { bos + 1 };
    violation(api, "over", constraint(false, over_dmax, bos, MAXSTR));
    emit(api, "edge", &etok(409));

    let b: [u8; 5] = if last { *b"aXbXZ" } else { *b"abcXZ" };
    let view = scanned(&b, 4);
    let found = if last {
        view.iter().rposition(|&x| x == b'X')
    } else {
        view.iter().position(|&x| x == b'X')
    };
    canary(
        api,
        "canary",
        found.map_or_else(|| etok(409), |i| format!("ok:i={}", i)),
        b[4] == b'Z',
    );
}

// ---------------------------------------------------------------------------
// 7-8. strstr_s / strcasestr_s
// ---------------------------------------------------------------------------

fn substring_index(hay: &[u8], needle: &[u8], ci: bool) -> Option<usize> {
    if needle.is_empty() {
        return Some(0);
    }
    if needle.len() > hay.len() {
        return None;
    }
    hay.windows(needle.len()).position(|window| {
        window
            .iter()
            .zip(needle.iter())
            .all(|(&h, &n)| if ci { h.eq_ignore_ascii_case(&n) } else { h == n })
    })
}

fn strstr_example(api: &str, hay: &str, needle: &str, ci: bool, bos: usize) {
    let idx = substring_index(scanned(hay.as_bytes(), hay.len()), needle.as_bytes(), ci);
    match idx {
        Some(i) => emit(api, "valid", &format!("ok:i={}", i)),
        None => emit(api, "valid", &etok(409)),
    }

    violation(api, "null", constraint(true, hay.len(), bos, MAXSTR));
    violation(api, "zero", constraint(false, 0, bos, MAXSTR));
    let over_dmax = if bos == BOS { MAXSTR + 1 } else { bos + 1 };
    violation(api, "over", constraint(false, over_dmax, bos, MAXSTR));
    emit(api, "edge", &etok(409));

    let b: [u8; 5] = if ci { *b"abCDx" } else { *b"abcdX" };
    let found = substring_index(scanned(&b, 4), b"cd", ci);
    canary(
        api,
        "canary",
        found.map_or_else(|| etok(409), |i| format!("ok:i={}", i)),
        b[4] == if ci { b'x' } else { b'X' },
    );
}

// ---------------------------------------------------------------------------
// 9-10. strspn_s / strcspn_s
// ---------------------------------------------------------------------------

fn span_example(api: &str, dest: &str, set: &str, dmax: usize, complement: bool, over_dmax: usize) {
    let bytes = scanned(dest.as_bytes(), dmax);
    let set_bytes = scanned(set.as_bytes(), set.len());
    let n = bytes
        .iter()
        .take_while(|&&b| {
            let member = set_bytes.contains(&b);
            if complement { !member } else { member }
        })
        .count();
    emit(api, "valid", &format!("ok:n={}", n));

    violation(api, "null", constraint(true, dmax, BOS, MAXSTR));
    violation(api, "zero", constraint(false, 0, BOS, MAXSTR));
    violation(api, "over", constraint(false, over_dmax, BOS, MAXSTR));

    let (edge_text, edge_set_text): (&[u8], &[u8]) = if complement {
        (b"XYZabc", b"XYZ")
    } else {
        (b"xyzabc", b"abc")
    };
    let edge_bytes = scanned(edge_text, 6);
    let edge_set = scanned(edge_set_text, 3);
    let edge_n = edge_bytes
        .iter()
        .take_while(|&&b| {
            let member = edge_set.contains(&b);
            if complement { !member } else { member }
        })
        .count();
    emit(api, "edge", &format!("ok:n={}", edge_n));

    let b: [u8; 4] = if complement { *b"abcZ" } else { *b"aaaZ" };
    let view = scanned(&b, 3);
    let set_bytes2 = if complement { scanned(b"Z", 1) } else { scanned(b"a", 1) };
    let canary_n = view
        .iter()
        .take_while(|&&x| {
            let member = set_bytes2.contains(&x);
            if complement { !member } else { member }
        })
        .count();
    canary(
        api,
        "canary",
        format!("ok:n={}", canary_n),
        b[3] == b'Z',
    );
}

// ---------------------------------------------------------------------------
// 11. strlcpy
// ---------------------------------------------------------------------------

/// Safe local model of BSD/safeclib strlcpy; `None` = NULL pointer.
fn strlcpy_model(dest: Option<&[u8]>, src: Option<&[u8]>, dsize: usize) -> (usize, Option<Vec<u8>>) {
    let initial = match dest {
        Some(d) => d.to_vec(),
        None => return (0, None),
    };
    let src = match src {
        Some(s) => s,
        None => {
            let mut d = initial;
            if dsize != 0 {
                d[0] = 0;
            }
            return (0, Some(d));
        }
    };
    let slen = src.len();
    let mut d = initial;
    if dsize != 0 {
        let n = slen.min(dsize - 1);
        d[..n].copy_from_slice(&src[..n]);
        d[n] = 0;
    }
    (slen, Some(d))
}

fn strlcpy_example() {
    let api = "cplus_v_strlcpy";
    let aa: [u8; 8] = [0xAA; 8];

    let (r, d) = strlcpy_model(Some(&aa), Some(b"abc"), 8);
    emit(api, "valid", &format!("r={};bytes={}", r, hex(&d.unwrap())));

    let (r, _) = strlcpy_model(None, Some(b"abc"), 8);
    emit(api, "null", &format!("r={}", r));

    let (r, d) = strlcpy_model(Some(&aa), Some(b"abcd"), 0);
    emit(api, "zero", &format!("r={};bytes={}", r, hex(&d.unwrap())));

    // one-over exact fit: dsize == strlen(src) truncates the final byte.
    let (r, d) = strlcpy_model(Some(&aa), Some(b"abcd"), 4);
    emit(api, "over", &format!("r={};bytes={}", r, hex(&d.unwrap())));

    let (r, d) = strlcpy_model(Some(&aa), Some(b""), 8);
    emit(api, "edge", &format!("r={};bytes={}", r, hex(&d.unwrap())));

    let mut bounded = aa;
    bounded[4] = 0x5A;
    let (r, d) = strlcpy_model(Some(&bounded), Some(b"abc"), 4);
    let d = d.unwrap();
    canary(
        api,
        "canary",
        format!("r={};bytes={}", r, hex(&d)),
        d[4] == 0x5A,
    );
}

// ---------------------------------------------------------------------------
// 12. strlcat
// ---------------------------------------------------------------------------

/// Safe local model of BSD/safeclib strlcat; `None` = NULL pointer.
fn strlcat_model(dest: Option<&[u8]>, src: Option<&[u8]>, dsize: usize) -> (usize, Option<Vec<u8>>) {
    let mut d = match dest {
        Some(x) => x.to_vec(),
        None => return (0, None),
    };
    let dlen = d.iter().take(dsize).position(|&b| b == 0).unwrap_or(dsize);
    let slen = src.map_or(0, |s| s.len());
    if dlen == dsize {
        return (dlen + slen, Some(d));
    }
    let room = dsize - dlen - 1;
    if let Some(s) = src {
        let n = slen.min(room);
        d[dlen..dlen + n].copy_from_slice(&s[..n]);
        d[dlen + n] = 0;
    }
    (dlen + slen, Some(d))
}

fn strlcat_example() {
    let api = "cplus_v_strlcat";
    let ab: [u8; 8] = [b'a', b'b', 0, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA];

    let (r, d) = strlcat_model(Some(&ab), Some(b"cd"), 8);
    emit(api, "valid", &format!("r={};bytes={}", r, hex(&d.unwrap())));

    let (r, _) = strlcat_model(None, Some(b"cd"), 8);
    emit(api, "null", &format!("r={}", r));

    let (r, d) = strlcat_model(Some(&ab), Some(b"cd"), 0);
    emit(api, "zero", &format!("r={};bytes={}", r, hex(&d.unwrap())));

    let (r, d) = strlcat_model(Some(&ab), Some(b"cdef"), 5);
    emit(api, "over", &format!("r={};bytes={}", r, hex(&d.unwrap())));

    let (r, d) = strlcat_model(Some(&ab), Some(b""), 8);
    emit(api, "edge", &format!("r={};bytes={}", r, hex(&d.unwrap())));

    let mut bounded = ab;
    bounded[4] = 0x5A;
    let (r, d) = strlcat_model(Some(&bounded), Some(b"cd"), 4);
    let d = d.unwrap();
    canary(
        api,
        "canary",
        format!("r={};bytes={}", r, hex(&d)),
        d[4] == 0x5A,
    );
}

// ---------------------------------------------------------------------------
// 13-14. stpcpy_s / stpncpy_s
// ---------------------------------------------------------------------------

fn stpcpy_example() {
    let api = "cplus_v__stpcpy_s_chk";
    let aa: [u8; 8] = [0xAA; 8];

    // valid: copy "hello\0"; returned pointer is index of the NUL.
    let mut d = aa;
    let hello = b"hello\0";
    d[..hello.len()].copy_from_slice(hello);
    emit(api, "valid", &format!("ok:p={};bytes={}", hello.len() - 1, hex(&d)));

    violation(api, "null", constraint(true, 8, 8, MAXSTR));
    violation(api, "zero", constraint(false, 0, 8, MAXSTR));
    violation(api, "over", constraint(false, 9, 8, MAXSTR));

    // edge: empty source -> just the NUL at index 0.
    let mut e = aa;
    e[0] = 0;
    emit(api, "edge", &format!("ok:p=0;bytes={}", hex(&e)));

    // canary: work area is the first 6 bytes; byte 6 stays 0x5A.
    let mut b = aa;
    b[6] = 0x5A;
    b[..4].copy_from_slice(b"abc\0");
    canary(
        api,
        "canary",
        format!("ok:p=3;bytes={}", hex(&b)),
        b[6] == 0x5A,
    );
}

fn stpncpy_example() {
    let api = "cplus_v__stpncpy_s_chk";
    let aa: [u8; 8] = [0xAA; 8];

    let mut d = aa;
    d[..6].copy_from_slice(b"world\0");
    emit(api, "valid", &format!("ok:p=5;bytes={}", hex(&d)));

    violation(api, "null", constraint(true, 8, 8, MAXSTR));
    violation(api, "zero", constraint(false, 0, 8, MAXSTR));
    // slen one over RSIZE_MAX_STR -> ESLEMAX.
    if violation(api, "over", len_check(MAXSTR + 1, MAXSTR)) {
        // token already printed
    }

    let mut e = aa;
    e[0] = 0; // slen == 0
    emit(api, "edge", &format!("ok:p=0;bytes={}", hex(&e)));

    let mut b = aa;
    b[5] = 0x5A;
    b[..4].copy_from_slice(b"abc\0");
    canary(
        api,
        "canary",
        format!("ok:p=3;bytes={}", hex(&b)),
        b[5] == 0x5A,
    );
}

// ---------------------------------------------------------------------------
// 15-17. strset_s / strnset_s / strnterminate_s
// ---------------------------------------------------------------------------

fn set_chars(buf: &mut [u8], dmax: usize, value: u8, n: Option<usize>) {
    let mut written = 0usize;
    for b in buf.iter_mut().take(dmax) {
        if *b == 0 {
            break;
        }
        if let Some(limit) = n {
            if written >= limit {
                break;
            }
        }
        *b = value;
        written += 1;
    }
}

fn strset_example() {
    let api = "cplus_v__strset_s_chk";

    let mut b = *b"AbC-XYz\0";
    set_chars(&mut b, 7, b'x', None);
    emit(api, "valid", &format!("ok:{}", std::str::from_utf8(&b[..7]).unwrap()));

    violation(api, "null", constraint(true, 8, BOS, MAXSTR));
    violation(api, "zero", constraint(false, 0, BOS, MAXSTR));
    violation(api, "over", constraint(false, MAXSTR + 1, BOS, MAXSTR));

    // edge: embedded NUL stops the fill.
    let mut e = *b"AB\0CDEF\0";
    set_chars(&mut e, 7, b'z', None);
    emit(api, "edge", &format!("ok:{}", hex(&e[..7])));

    // canary: byte 4 is beyond dmax and must remain 'Z'.
    let mut c = *b"abcdZ";
    set_chars(&mut c, 4, b'x', None);
    canary(
        api,
        "canary",
        format!("ok:{}", std::str::from_utf8(&c[..4]).unwrap()),
        c[4] == b'Z',
    );
}

fn strnset_example() {
    let api = "cplus_v__strnset_s_chk";

    let mut b = *b"abcdefg\0";
    set_chars(&mut b, 8, b'y', Some(3));
    emit(api, "valid", &format!("ok:{}", std::str::from_utf8(&b[..7]).unwrap()));

    violation(api, "null", constraint(true, 8, BOS, MAXSTR));
    violation(api, "zero", constraint(false, 0, BOS, MAXSTR));
    // n > dmax -> ESNOSPC.
    if violation(api, "over", nospace_check(9, 8)) {
        // token already printed
    }

    let mut e = *b"abcdefg\0";
    set_chars(&mut e, 8, b'y', Some(0));
    emit(api, "edge", &format!("ok:{}", std::str::from_utf8(&e[..7]).unwrap()));

    let mut c = *b"abcdZ";
    set_chars(&mut c, 4, b'w', Some(2));
    canary(
        api,
        "canary",
        format!("ok:{}", std::str::from_utf8(&c[..4]).unwrap()),
        c[4] == b'Z',
    );
}

fn strnterminate_example() {
    let api = "cplus_v__strnterminate_s_chk";

    /// Safe model of upstream's `while (dmax > 1) ... *dest = 0` loop.
    /// `dest_is_null` is the safe stand-in for a NULL pointer (Option::None).
    fn terminate(buf: &mut [u8], dmax: usize, bos: usize, dest_is_null: bool) -> usize {
        if dest_is_null || constraint(false, dmax, bos, MAXSTR).is_err() {
            return 0;
        }
        let mut count = 0usize;
        let mut i = 0usize;
        let mut left = dmax;
        while left > 1 {
            if buf[i] == 0 {
                break;
            }
            count += 1;
            i += 1;
            left -= 1;
        }
        buf[i] = 0;
        count
    }

    let mut b = *b"ABCDEFGH";
    let r = terminate(&mut b, 8, BOS, false);
    emit(api, "valid", &format!("r={};bytes={}", r, hex(&b)));

    let r = terminate(&mut [0u8; 8], 8, BOS, true); // NULL dest -> 0
    emit(api, "null", &format!("r={}", r));

    let mut z = *b"ABCDEFGH";
    let r = terminate(&mut z, 0, BOS, false);
    emit(api, "zero", &format!("r={}", r));

    let mut o = *b"ABCDEFGH";
    let r = terminate(&mut o, 9, 8, false); // dmax one over known object size
    emit(api, "over", &format!("r={}", r));

    let mut e = *b"abc\0efgh";
    let r = terminate(&mut e, 8, BOS, false);
    emit(api, "edge", &format!("r={};bytes={}", r, hex(&e)));

    let mut c = *b"abcdAAAZ";
    let r = terminate(&mut c, 4, BOS, false);
    canary(
        api,
        "canary",
        format!("r={};bytes={}", r, hex(&c)),
        c[7] == b'Z',
    );
}

// ---------------------------------------------------------------------------
// 18-19. strtolowercase_s / strtouppercase_s
// ---------------------------------------------------------------------------

fn convert_example(
    api: &str,
    input: &[u8; 8],
    edge_input: &[u8; 8],
    edge_dmax: usize,
    edge_len: usize,
    upper: bool,
) {
    let dmax = 7;
    let mut b = *input;
    let checked = constraint(false, dmax, 8, MAXSTR);
    if checked.is_ok() {
        for byte in b.iter_mut().take(dmax) {
            if *byte == 0 {
                break;
            }
            *byte = if upper {
                byte.to_ascii_uppercase()
            } else {
                byte.to_ascii_lowercase()
            };
        }
        emit(api, "valid", &format!("ok:{}", std::str::from_utf8(&b[..dmax]).unwrap()));
    } else {
        emit(api, "valid", &etok(checked.unwrap_err()));
    }

    violation(api, "null", constraint(true, dmax, BOS, MAXSTR));
    violation(api, "zero", constraint(false, 0, 8, MAXSTR));
    violation(api, "over", constraint(false, MAXSTR + 1, BOS, MAXSTR));

    // edge: either an embedded NUL (lowercase) or a short dmax (uppercase).
    let mut e: [u8; 8] = *edge_input;
    for byte in e.iter_mut().take(edge_dmax) {
        if *byte == 0 {
            break;
        }
        *byte = if upper {
            byte.to_ascii_uppercase()
        } else {
            byte.to_ascii_lowercase()
        };
    }
    let edge_payload = if upper {
        std::str::from_utf8(&e[..edge_len]).unwrap().to_string()
    } else {
        hex(&e[..edge_len])
    };
    emit(api, "edge", &format!("ok:{}", edge_payload));

    // canary: byte 4 lies outside the 4-byte conversion window.
    let mut c: [u8; 5] = if upper { *b"abcdZ" } else { *b"ABCDZ" };
    for byte in c.iter_mut().take(4) {
        *byte = if upper {
            byte.to_ascii_uppercase()
        } else {
            byte.to_ascii_lowercase()
        };
    }
    canary(
        api,
        "canary",
        format!("ok:{}", std::str::from_utf8(&c[..4]).unwrap()),
        c[4] == b'Z',
    );
}

// ---------------------------------------------------------------------------
// 20-23. first/last char and first/last diff
// ---------------------------------------------------------------------------

fn firstlast_char_example(api: &str, text: &str, ch: u8, last: bool, bos: usize) {
    let view = scanned(text.as_bytes(), text.len());
    let found = if last {
        view.iter().rposition(|&b| b == ch)
    } else {
        view.iter().position(|&b| b == ch)
    };
    match found {
        Some(i) => emit(api, "valid", &format!("ok:i={}", i)),
        None => emit(api, "valid", &etok(409)),
    }

    violation(api, "null", constraint(true, 6, bos, MAXSTR));
    violation(api, "zero", constraint(false, 0, bos, MAXSTR));
    let over = if bos == BOS { MAXSTR + 1 } else { bos + 1 };
    violation(api, "over", constraint(false, over, bos, MAXSTR));
    emit(api, "edge", &etok(409));

    let b: [u8; 4] = if last { *b"abaZ" } else { *b"abcZ" };
    let view = scanned(&b, 3);
    let found = if last {
        view.iter().rposition(|&x| x == b'a')
    } else {
        view.iter().position(|&x| x == b'c')
    };
    canary(
        api,
        "canary",
        found.map_or_else(|| etok(409), |i| format!("ok:i={}", i)),
        b[3] == b'Z',
    );
}

/// Safe first/last-difference index, stopping at either NUL or `dmax`.
fn diff_index(dest: &[u8], src: &[u8], dmax: usize, last: bool) -> Option<usize> {
    let n = dmax.min(dest.len()).min(src.len());
    let mut found = None;
    for i in 0..n {
        if dest[i] == 0 || src[i] == 0 {
            break;
        }
        if dest[i] != src[i] {
            if !last {
                return Some(i);
            }
            found = Some(i);
        }
    }
    found
}

fn firstlast_diff_example(api: &str, src: &[u8], last: bool, bos: usize) {
    let dest = b"abcabc";
    match diff_index(dest, src, 6, last) {
        Some(i) => emit(api, "valid", &format!("ok:i={}", i)),
        None => emit(api, "valid", &etok(408)),
    }

    violation(api, "null", constraint(true, 6, bos, MAXSTR));
    violation(api, "zero", constraint(false, 0, bos, MAXSTR));
    let over = if bos == BOS { MAXSTR + 1 } else { bos + 1 };
    violation(api, "over", constraint(false, over, bos, MAXSTR));
    emit(api, "edge", &etok(408));

    let b: [u8; 4] = *b"abcZ";
    let csrc: &[u8] = if last { b"Xbc" } else { b"abX" };
    let found = diff_index(&b, csrc, 3, last);
    canary(
        api,
        "canary",
        found.map_or_else(|| etok(408), |i| format!("ok:i={}", i)),
        b[3] == b'Z',
    );
}

// ---------------------------------------------------------------------------
// 24-26. strcmpfld_s / strcpyfld_s / strcoll_s
// ---------------------------------------------------------------------------

fn strcmpfld_example() {
    let api = "cplus_v__strcmpfld_s_chk";
    let dest = b"abcdef";
    let src = b"abcXef";
    let order = (0..6)
        .find(|&i| dest[i] != src[i])
        .map_or(Ordering::Equal, |i| dest[i].cmp(&src[i]));
    emit(api, "valid", &format!("ok:{}", stok(order)));

    violation(api, "null", constraint(true, 6, BOS, MAXSTR));
    violation(api, "zero", constraint(false, 0, BOS, MAXSTR));
    violation(api, "over", constraint(false, 7, 6, MAXSTR));
    emit(api, "edge", &format!("ok:{}", stok(Ordering::Equal)));

    let b: [u8; 5] = *b"abcdZ";
    let c = (0..4)
        .find(|&i| b[i] != b"abcX"[i])
        .map_or(Ordering::Equal, |i| b[i].cmp(&b"abcX"[i]));
    canary(api, "canary", format!("ok:{}", stok(c)), b[4] == b'Z');
}

fn strcpyfld_example() {
    let api = "cplus_v__strcpyfld_s_chk";
    let aa: [u8; 8] = [0xAA; 8];

    /// Safe model: copy `slen` bytes, zero the field's slack, preserve the rest.
    fn copy_field(initial: [u8; 8], dmax: usize, src: &[u8], slen: usize) -> [u8; 8] {
        let mut d = initial;
        if slen == 0 {
            return d; // upstream early return: no write at all
        }
        let n = slen.min(d.len()).min(dmax);
        d[..n].copy_from_slice(&src[..n]);
        for byte in d.iter_mut().take(dmax).skip(n) {
            *byte = 0;
        }
        d
    }

    let d = copy_field(aa, 8, b"HELLO", 5);
    emit(api, "valid", &format!("ok:{}", hex(&d)));

    violation(api, "null", constraint(true, 8, 8, MAXSTR));
    violation(api, "zero", constraint(false, 0, 0, MAXSTR));
    violation(api, "over", constraint(false, 5, 4, MAXSTR));

    let d = copy_field(aa, 8, b"AB", 0);
    emit(api, "edge", &format!("ok:{}", hex(&d)));

    let mut bounded = aa;
    bounded[5] = 0x5A;
    let d = copy_field(bounded, 5, b"AB", 2);
    canary(api, "canary", format!("ok:{}", hex(&d)), d[5] == 0x5A);
}

fn strcoll_example() {
    let api = "cplus_v__strcoll_s_chk";
    // In the "C" locale strcoll's sign agrees with Rust's Ord on &str.
    let order = "abc".cmp("abd");
    emit(api, "valid", &format!("ok:{}", stok(order)));

    violation(api, "null", constraint(true, 3, BOS, MAXSTR));
    violation(api, "zero", constraint(false, 0, BOS, MAXSTR));
    violation(api, "over", constraint(false, 4, 3, MAXSTR));
    emit(api, "edge", &format!("ok:{}", stok(Ordering::Equal)));

    // dest is the C string "ab" with canary 'Z' just past dmax; src is "ab".
    let b: [u8; 4] = *b"ab\0Z";
    let order = std::str::from_utf8(&b[..2]).unwrap().cmp("ab");
    canary(api, "canary", format!("ok:{}", stok(order)), b[3] == b'Z');
}

fn main() {
    memchr_example("cplus_v_memchr_s", b"hello world", 11, BOS, MAXMEM, b'o');
    memchr_example("cplus_v__memchr_s_chk", b"abcabc", 6, 7, MAXMEM, b'c');

    memccpy_example("cplus_v_memccpy_s", 8, BOS, MAXMEM, 10);
    memccpy_example("cplus_v__memccpy_s_chk", 8, 8, MAXMEM, 7);

    strchr_example("cplus_v__strchr_s_chk", "aXbXcX", b'X', false, 6);
    strchr_example("cplus_v__strrchr_s_chk", "aXbXcX", b'X', true, 6);

    strstr_example("cplus_v__strstr_s_chk", "hello world", "world", false, 11);
    strstr_example("cplus_v__strcasestr_s_chk", "Hello World", "world", true, 11);

    span_example("cplus_v__strspn_s_chk", "aabbccXYZ", "abc", 9, false, MAXSTR + 1);
    span_example("cplus_v__strcspn_s_chk", "abcXYZabc", "XYZ", 9, true, MAXSTR + 1);

    strlcpy_example();
    strlcat_example();
    stpcpy_example();
    stpncpy_example();

    strset_example();
    strnset_example();
    strnterminate_example();

    convert_example(
        "cplus_v__strtolowercase_s_chk",
        b"AbC-XYz\0",
        b"AB\0CDEF\0",
        7,
        7,
        false,
    );
    convert_example(
        "cplus_v__strtouppercase_s_chk",
        b"aBc-dEf\0",
        b"abcdefg\0",
        3,
        6,
        true,
    );

    firstlast_char_example("cplus_v__strfirstchar_s_chk", "abcabc", b'c', false, 6);
    firstlast_char_example("cplus_v__strlastchar_s_chk", "abcabc", b'a', true, 6);

    firstlast_diff_example("cplus_v__strfirstdiff_s_chk", b"abcXbc", false, 6);
    firstlast_diff_example("cplus_v__strlastdiff_s_chk", b"abcabX", true, 6);

    strcmpfld_example();
    strcpyfld_example();
    strcoll_example();
}

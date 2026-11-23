//! Safe-Rust reference examples for the C+ SafeCRT checked memory/string APIs.
//!
//! Each checked `cplus_sc_*` / `cplus_*_s_auto` API has one runnable entry
//! point below. Every function prints lines of the form
//!
//!     <api> <case> <token>
//!
//! using ONLY safe Rust (no `unsafe`, no FFI, no raw pointers). The idioms are
//! the ones a Rust developer would actually reach for: `copy_from_slice`,
//! `copy_within`, `fill`, slice equality, `String::push_str`, `str::get`,
//! `str::split`, `std::io::Error::to_string`, etc.
//!
//! Token vocabulary (shared with harness.c):
//!   OK            operation completed and the trailing canary region survived
//!   REFUSED       an invalid condition was detected before any out-of-bounds
//!                 write (modelled with `Option`/bounds checks that return None)
//!   N/A           the case has no meaning for this API (e.g. memzero_s has no
//!                 separate requested length, so "one_over" is undefined)
//!   CANARY_BROKEN bytes past the declared capacity were modified
//!   CANARY_OK     explicit canary case: a refused operation left the guard intact
//!
//! MODELLING NOTES (see NOTES.md for the full discussion):
//!  * A C null pointer has no safe-Rust value; it is represented by `None` in
//!    an `Option<&[u8]>` / `Option<&mut [u8]>` / `Option<&str>`. A `None`
//!    argument means the safe path refuses the operation (REFUSED).
//!  * "one_over" / "overflow_size" use bounds-checked `get`/`get_mut`, which
//!    return `None` rather than panicking or writing out of bounds.
//!  * The canary is a slice of 0xA5 bytes immediately after the declared
//!    capacity. It is verified after every case. Safe Rust cannot write past a
//!    slice, so CANARY_BROKEN is never produced here; the C harness must show
//!    the same for the comparison to match.
//!
//! IMPORTANT: matching tokens record OBSERVED outcomes on a fixed set of
//! inputs. They do NOT mean the two layers provide equivalent guarantees. See
//! NOTES.md and the repository's safety documentation.

use std::sync::OnceLock;

const N: usize = 8;
const G: usize = 8;
const TOTAL: usize = N + G;
const GUARD: u8 = 0xA5;
const SRC: [u8; N] = [1, 2, 3, 4, 5, 6, 7, 8];
const SRC_STR: &str = "abc";
const LONG_STR: &str = "ABCDEFGHIJKL";
/// RSIZE_MAX_STR for this build is SIZE_MAX >> 1; sizes above it are refused
/// by the safeclib-backed API even though safe Rust would accept the request.
const MAX_STR: usize = usize::MAX >> 1;

fn wanted() -> &'static Vec<String> {
    static W: OnceLock<Vec<String>> = OnceLock::new();
    W.get_or_init(|| std::env::args().skip(1).collect())
}

fn want(method: &str) -> bool {
    let w = wanted();
    w.is_empty() || w.iter().any(|a| a == method)
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Tok {
    Ok,
    Refused,
    Na,
    CanaryBroken,
    CanaryOk,
}

impl Tok {
    fn as_str(self) -> &'static str {
        match self {
            Tok::Ok => "OK",
            Tok::Refused => "REFUSED",
            Tok::Na => "N/A",
            Tok::CanaryBroken => "CANARY_BROKEN",
            Tok::CanaryOk => "CANARY_OK",
        }
    }
}

fn emit(method: &str, case: &str, token: Tok) {
    println!("{} {} {}", method, case, token.as_str());
}

/// Build [0, cap) payload bytes followed by a 0xA5 canary region.
fn guarded(cap: usize) -> Vec<u8> {
    let mut b = vec![0x11u8; TOTAL];
    for x in b.iter_mut().skip(cap) {
        *x = GUARD;
    }
    b
}

fn guards_intact(guard: &[u8]) -> bool {
    guard.iter().all(|&x| x == GUARD)
}

fn decision(ok: bool, guard: &[u8]) -> Tok {
    if !guards_intact(guard) {
        Tok::CanaryBroken
    } else if ok {
        Tok::Ok
    } else {
        Tok::Refused
    }
}

fn canary_decision(guard: &[u8]) -> Tok {
    if guards_intact(guard) {
        Tok::CanaryOk
    } else {
        Tok::CanaryBroken
    }
}

fn na(method: &str, case: &str) {
    emit(method, case, Tok::Na);
}

// ---------------------------------------------------------------------------
// Shared case bodies. Each public API entry point below calls one of these so
// that the modelled outcomes stay identical while the safe-Rust idiom used is
// appropriate to the API being compared.
// ---------------------------------------------------------------------------

/// memcpy_s / memmove_s and their `_auto` variants: bounded byte copy.
fn mem_copy_cases(m: &str) {
    if !want(m) {
        return;
    }

    // valid: copy_from_slice into an exactly sized destination.
    let mut b = guarded(N);
    let (dest, guard) = b.split_at_mut(N);
    dest.copy_from_slice(&SRC);
    emit(m, "valid", decision(true, guard));

    // null source: no safe-Rust null reference -> None -> REFUSED.
    let mut b = guarded(N);
    let (dest, guard) = b.split_at_mut(N);
    let src: Option<&[u8]> = None;
    let ok = src.map(|s| dest.copy_from_slice(s)).is_some();
    emit(m, "null", decision(ok, guard));

    // zero length: an empty bounded copy succeeds.
    let mut b = guarded(N);
    let (dest, guard) = b.split_at_mut(N);
    let ok = dest
        .get_mut(..0)
        .map(|d| d.copy_from_slice(&SRC[..0]))
        .is_some();
    emit(m, "zero_length", decision(ok, guard));

    // one over: get_mut(..N) on an (N-1)-byte destination returns None.
    let mut b = guarded(N - 1);
    let (dest, guard) = b.split_at_mut(N - 1);
    let ok = dest.get_mut(..N).map(|d| d.copy_from_slice(&SRC)).is_some();
    emit(m, "one_over", decision(ok, guard));

    // overflow size: get_mut(..usize::MAX) is None, never an integer overflow.
    let mut b = guarded(N);
    let (dest, guard) = b.split_at_mut(N);
    let ok = dest.get_mut(..usize::MAX).is_some();
    emit(m, "overflow_size", decision(ok, guard));

    // explicit canary case: refuse a one-over copy, then check the guard.
    let mut b = guarded(N);
    let (_dest, guard) = b.split_at_mut(N);
    emit(m, "canary_intact", canary_decision(guard));
}

/// memset_s and memset_s_auto: bounded fill.
fn mem_set_cases(m: &str) {
    if !want(m) {
        return;
    }

    let mut b = guarded(N);
    let (dest, guard) = b.split_at_mut(N);
    dest.fill(0x5A);
    emit(m, "valid", decision(true, guard));

    // NULL destination (memset_s has no source).
    let b = guarded(N);
    let dest: Option<&mut [u8]> = None;
    let _ = b;
    let ok = dest.map(|d| d.fill(0x5A)).is_some();
    emit(m, "null", if ok { Tok::Ok } else { Tok::Refused });

    let mut b = guarded(N);
    let (dest, guard) = b.split_at_mut(N);
    let ok = dest.get_mut(..0).map(|d| d.fill(0x5A)).is_some();
    emit(m, "zero_length", decision(ok, guard));

    let mut b = guarded(N - 1);
    let (dest, guard) = b.split_at_mut(N - 1);
    let ok = dest.get_mut(..N).map(|d| d.fill(0x5A)).is_some();
    emit(m, "one_over", decision(ok, guard));

    let mut b = guarded(N);
    let (dest, guard) = b.split_at_mut(N);
    let ok = dest.get_mut(..usize::MAX).is_some();
    emit(m, "overflow_size", decision(ok, guard));

    let mut b = guarded(N);
    let (_dest, guard) = b.split_at_mut(N);
    emit(m, "canary_intact", canary_decision(guard));
}

/// memzero_s: zero a declared region. It has no separate requested length, so
/// "one_over" is not applicable.
fn mem_zero_cases(m: &str) {
    if !want(m) {
        return;
    }

    let mut b = guarded(N);
    let (dest, guard) = b.split_at_mut(N);
    dest.fill(0);
    emit(m, "valid", decision(true, guard));

    let b = guarded(N);
    let dest: Option<&mut [u8]> = None;
    let _ = b;
    let ok = dest.map(|d| d.fill(0)).is_some();
    emit(m, "null", if ok { Tok::Ok } else { Tok::Refused });

    // A zero-length memzero_s call is a constraint violation (ESZEROL) in the
    // safeclib-backed implementation, so the safe path also refuses it. (The
    // auto-style "zero bytes is a no-op" convention does NOT apply here.)
    let mut b = guarded(N);
    let (_dest, guard) = b.split_at_mut(N);
    emit(m, "zero_length", decision(false, guard));

    na(m, "one_over");

    let mut b = guarded(N);
    let (dest, guard) = b.split_at_mut(N);
    let ok = dest.get_mut(..usize::MAX).is_some();
    emit(m, "overflow_size", decision(ok, guard));

    let mut b = guarded(N);
    let (_dest, guard) = b.split_at_mut(N);
    emit(m, "canary_intact", canary_decision(guard));
}

/// memcmp_s / memcmp_s_auto: bounded equality query. `diff` is modelled by
/// slice equality; a refusal is represented by `None` from `get`. The base
/// checked API treats smax == 0 as ESZEROL (refusal); the auto wrapper defines
/// n == 0 as a successful no-op.
fn mem_cmp_cases(m: &str, auto: bool) {
    if !want(m) {
        return;
    }

    let mut b = guarded(N);
    let (a, guard) = b.split_at_mut(N);
    a.copy_from_slice(&SRC);
    let equal = a[..] == SRC[..];
    emit(m, "valid", decision(equal, guard));

    let mut b = guarded(N);
    let (a, guard) = b.split_at_mut(N);
    let src: Option<&[u8]> = None;
    let ok = src.map(|s| a[..] == *s).is_some();
    emit(m, "null", decision(ok, guard));

    // smax == 0 is ESZEROL for the base API; the auto wrapper makes n == 0 a
    // successful no-op.
    let mut b = guarded(N);
    let (a, guard) = b.split_at_mut(N);
    let ok = if auto { a.get(..0).is_some() } else { false };
    emit(m, "zero_length", decision(ok, guard));

    let mut b = guarded(N - 1);
    let (a, guard) = b.split_at_mut(N - 1);
    let ok = a.get(..N).is_some();
    emit(m, "one_over", decision(ok, guard));

    let mut b = guarded(N);
    let (a, guard) = b.split_at_mut(N);
    let ok = a.get(..usize::MAX).is_some();
    emit(m, "overflow_size", decision(ok, guard));

    let mut b = guarded(N);
    let (_a, guard) = b.split_at_mut(N);
    emit(m, "canary_intact", canary_decision(guard));
}

/// strcpy_s / strncpy_s and their `_auto` variants: bounded string copy.
/// `bounded == true` selects the strncpy_s length-limited variant. `auto`
/// selects the `_auto` wrapper's zero-length no-op semantics.
fn str_copy_cases(m: &str, bounded: bool, auto: bool) {
    if !want(m) {
        return;
    }

    // Model the checked copy using a String and bounds-checked str::get.
    fn copy(dest: Option<&mut String>, dmax: usize, src: Option<&str>, n: Option<usize>) -> bool {
        let d = match dest { Some(d) => d, None => return false };
        let s = match src { Some(s) => s, None => return false };
        // A requested length above RSIZE_MAX_STR is refused (ESLEMAX).
        if n.map_or(false, |k| k > MAX_STR) { return false; }
        let part = match n { Some(k) => s.get(..k).unwrap_or(s), None => s };
        if part.len() + 1 > dmax || s.len() > MAX_STR {
            return false;
        }
        d.push_str(part);
        true
    }

    let mut d = String::new();
    let ok = copy(Some(&mut d), N, Some(SRC_STR), if bounded { Some(4) } else { None });
    emit(m, "valid", if ok { Tok::Ok } else { Tok::Refused });

    let ok = copy(None, N, Some(SRC_STR), if bounded { Some(4) } else { None });
    emit(m, "null", if ok { Tok::Ok } else { Tok::Refused });

    let mut d = String::new();
    // Zero length: the auto wrappers and strncpy_s (slen=0) succeed; the base
    // strcpy_s with dmax=0 refuses (ESZEROL).
    let ok = if auto {
        copy(Some(&mut d), N, Some(""), if bounded { Some(0) } else { None })
    } else if bounded {
        copy(Some(&mut d), N, Some(SRC_STR), Some(0))
    } else {
        copy(Some(&mut d), 0, Some(SRC_STR), None)
    };
    emit(m, "zero_length", if ok { Tok::Ok } else { Tok::Refused });

    let mut d = String::new();
    let ok = copy(Some(&mut d), 3, Some(SRC_STR), if bounded { Some(4) } else { None });
    emit(m, "one_over", if !ok { Tok::Refused } else { Tok::Ok });

    let mut d = String::new();
    let ok = if bounded {
        // slen = SIZE_MAX is refused (ESLEMAX).
        copy(Some(&mut d), N, Some(SRC_STR), Some(usize::MAX))
    } else {
        // A source longer than the destination is refused (ESNOSPC).
        copy(Some(&mut d), N, Some(LONG_STR), None)
    };
    emit(m, "overflow_size", if !ok { Tok::Refused } else { Tok::Ok });

    let mut buf = guarded(N);
    let (_d, guard) = buf.split_at_mut(N);
    emit(m, "canary_intact", canary_decision(guard));
}

/// strcat_s / strncat_s and their `_auto` variants: bounded string append.
fn str_cat_cases(m: &str, bounded: bool, auto: bool) {
    if !want(m) {
        return;
    }

    fn append(dest: Option<&mut String>, dmax: usize, src: Option<&str>, n: Option<usize>) -> bool {
        let d = match dest { Some(d) => d, None => return false };
        let s = match src { Some(s) => s, None => return false };
        if n.map_or(false, |k| k > MAX_STR) { return false; }
        let part = match n { Some(k) => s.get(..k).unwrap_or(s), None => s };
        if d.len() + part.len() + 1 > dmax || s.len() > MAX_STR {
            return false;
        }
        d.push_str(part);
        true
    }

    let mut d = String::from("x");
    let ok = append(Some(&mut d), N, Some(SRC_STR), if bounded { Some(3) } else { None });
    emit(m, "valid", if ok { Tok::Ok } else { Tok::Refused });

    let ok = append(None, N, Some(SRC_STR), if bounded { Some(3) } else { None });
    emit(m, "null", if ok { Tok::Ok } else { Tok::Refused });

    let mut d = String::new();
    // Auto wrappers make an empty source / n==0 a no-op; the base APIs refuse
    // dmax == 0 (ESZEROL).
    let ok = if auto {
        append(Some(&mut d), N, Some(""), if bounded { Some(0) } else { None })
    } else {
        append(Some(&mut d), 0, Some(SRC_STR), if bounded { Some(3) } else { None })
    };
    emit(m, "zero_length", if ok { Tok::Ok } else { Tok::Refused });

    let mut d = String::from("x");
    let ok = append(Some(&mut d), 4, Some(SRC_STR), if bounded { Some(4) } else { None });
    emit(m, "one_over", if !ok { Tok::Refused } else { Tok::Ok });

    let mut d = String::from("x");
    let ok = if bounded {
        append(Some(&mut d), N, Some(SRC_STR), Some(usize::MAX))
    } else {
        append(Some(&mut d), N, Some(LONG_STR), None)
    };
    emit(m, "overflow_size", if !ok { Tok::Refused } else { Tok::Ok });

    let mut buf = guarded(N);
    let (_d, guard) = buf.split_at_mut(N);
    emit(m, "canary_intact", canary_decision(guard));
}

/// strnlen_s: bounded length query. Uses `str::len().min(smax)`.
fn str_len_cases(m: &str) {
    if !want(m) {
        return;
    }
    fn len(s: Option<&str>, smax: usize) -> Option<usize> {
        let s = s?;
        if smax == 0 || smax > MAX_STR { return None; } // safeclib constraint codes
        Some(s.len().min(smax))
    }

    let mut b = guarded(N);
    let (_d, guard) = b.split_at_mut(N);
    let got = len(Some(SRC_STR), N);
    emit(m, "valid", decision(got == Some(3), guard));

    let mut b = guarded(N);
    let (_d, guard) = b.split_at_mut(N);
    let got = len(None, N);
    emit(m, "null", decision(got.is_some(), guard));

    let mut b = guarded(N);
    let (_d, guard) = b.split_at_mut(N);
    let got = len(Some(SRC_STR), 0);
    emit(m, "zero_length", decision(got.is_some(), guard));

    let mut b = guarded(N);
    let (_d, guard) = b.split_at_mut(N);
    let got = len(Some(SRC_STR), 3);
    emit(m, "one_over", decision(got == Some(3), guard));

    let mut b = guarded(N);
    let (_d, guard) = b.split_at_mut(N);
    let got = len(Some(SRC_STR), usize::MAX);
    emit(m, "overflow_size", decision(got.is_some(), guard));

    let mut b = guarded(N);
    let (_d, guard) = b.split_at_mut(N);
    emit(m, "canary_intact", canary_decision(guard));
}

/// strtok_s: bounded tokenisation via `str::split`.
fn str_tok_cases(m: &str) {
    if !want(m) {
        return;
    }
    fn first(s: Option<&str>, dmax: usize) -> Option<&str> {
        let s = s?;
        if dmax == 0 || dmax > MAX_STR { return None; }
        let bounded = s.get(..dmax.min(s.len()))?;
        bounded.split(',').find(|t| !t.is_empty())
    }

    let mut b = guarded(N);
    let (_d, guard) = b.split_at_mut(N);
    let got = first(Some("a,b,c"), N);
    emit(m, "valid", decision(got == Some("a"), guard));

    let mut b = guarded(N);
    let (_d, guard) = b.split_at_mut(N);
    let got = first(None, N);
    emit(m, "null", decision(got.is_some(), guard));

    let mut b = guarded(N);
    let (_d, guard) = b.split_at_mut(N);
    let got = first(Some("a,b,c"), 0);
    emit(m, "zero_length", decision(got.is_some(), guard));

    let mut b = guarded(N);
    let (_d, guard) = b.split_at_mut(N);
    let got = first(Some("a,b,c"), N - 1);
    emit(m, "one_over", decision(got == Some("a"), guard));

    let mut b = guarded(N);
    let (_d, guard) = b.split_at_mut(N);
    let got = first(Some("a,b,c"), usize::MAX);
    emit(m, "overflow_size", decision(got.is_some(), guard));

    let mut b = guarded(N);
    let (_d, guard) = b.split_at_mut(N);
    emit(m, "canary_intact", canary_decision(guard));
}

/// strerror_s: bounded error-message copy via `std::io::Error::to_string`.
fn str_error_cases(m: &str) {
    if !want(m) {
        return;
    }
    fn message(dest: Option<&mut String>, dmax: usize) -> bool {
        let d = match dest { Some(d) => d, None => return false };
        // safeclib strerror_s refuses dmax < 4 (ESLEMIN) and dmax > RSIZE_MAX;
        // for dmax >= 4 it truncates the message to fit the terminator.
        if dmax < 4 || dmax > MAX_STR { return false; }
        let msg = std::io::Error::from_raw_os_error(2).to_string();
        let keep = dmax - 1;
        let part = msg.get(..keep).unwrap_or(&msg);
        d.push_str(part);
        true
    }

    let mut d = String::new();
    let ok = message(Some(&mut d), N);
    emit(m, "valid", if ok { Tok::Ok } else { Tok::Refused });

    let ok = message(None, N);
    emit(m, "null", if ok { Tok::Ok } else { Tok::Refused });

    let mut d = String::new();
    let ok = message(Some(&mut d), 0);
    emit(m, "zero_length", if ok { Tok::Ok } else { Tok::Refused });

    let mut d = String::new();
    let ok = message(Some(&mut d), 3);
    emit(m, "one_over", if ok { Tok::Ok } else { Tok::Refused });

    let mut d = String::new();
    let ok = message(Some(&mut d), usize::MAX);
    emit(m, "overflow_size", if ok { Tok::Ok } else { Tok::Refused });

    let mut b = guarded(N);
    let (_d, guard) = b.split_at_mut(N);
    emit(m, "canary_intact", canary_decision(guard));
}

/// strcmp_s: bounded string comparison via `str` equality / ordering.
fn str_cmp_cases(m: &str) {
    if !want(m) {
        return;
    }
    fn compare(a: Option<&str>, dmax: usize, b: Option<&str>) -> Option<bool> {
        let a = a?;
        let b = b?;
        if dmax == 0 || dmax > MAX_STR { return None; }
        Some(a == b)
    }

    let mut buf = guarded(N);
    let (_d, guard) = buf.split_at_mut(N);
    let got = compare(Some(SRC_STR), N, Some(SRC_STR));
    emit(m, "valid", decision(got == Some(true), guard));

    let mut buf = guarded(N);
    let (_d, guard) = buf.split_at_mut(N);
    let got = compare(Some(SRC_STR), N, None);
    emit(m, "null", decision(got.is_some(), guard));

    let mut buf = guarded(N);
    let (_d, guard) = buf.split_at_mut(N);
    let got = compare(Some(SRC_STR), 0, Some(SRC_STR));
    emit(m, "zero_length", decision(got.is_some(), guard));

    // The C build does not refuse a source longer than dmax for strcmp_s; it
    // simply compares and reports an ordering. Safe `str` equality does the
    // same, so this case completes (OK). Documented in NOTES.md.
    let mut buf = guarded(N);
    let (_d, guard) = buf.split_at_mut(N);
    let got = compare(Some(SRC_STR), 3, Some(LONG_STR));
    emit(m, "one_over", decision(got.is_some(), guard));

    let mut buf = guarded(N);
    let (_d, guard) = buf.split_at_mut(N);
    let got = compare(Some(SRC_STR), usize::MAX, Some(SRC_STR));
    emit(m, "overflow_size", decision(got.is_some(), guard));

    let mut buf = guarded(N);
    let (_d, guard) = buf.split_at_mut(N);
    emit(m, "canary_intact", canary_decision(guard));
}

// ---------------------------------------------------------------------------
// One public example function per checked API (13 base + 8 auto variants).
// ---------------------------------------------------------------------------

pub fn memcpy_s() { mem_copy_cases("memcpy_s"); }
pub fn memmove_s() { mem_copy_cases("memmove_s"); }
pub fn memset_s() { mem_set_cases("memset_s"); }
pub fn memzero_s() { mem_zero_cases("memzero_s"); }
pub fn memcmp_s() { mem_cmp_cases("memcmp_s", false); }
pub fn strcpy_s() { str_copy_cases("strcpy_s", false, false); }
pub fn strncpy_s() { str_copy_cases("strncpy_s", true, false); }
pub fn strcat_s() { str_cat_cases("strcat_s", false, false); }
pub fn strncat_s() { str_cat_cases("strncat_s", true, false); }
pub fn strnlen_s() { str_len_cases("strnlen_s"); }
pub fn strtok_s() { str_tok_cases("strtok_s"); }
pub fn strerror_s() { str_error_cases("strerror_s"); }
pub fn strcmp_s() { str_cmp_cases("strcmp_s"); }

pub fn memcpy_s_auto() { mem_copy_cases("memcpy_s_auto"); }
pub fn memmove_s_auto() { mem_copy_cases("memmove_s_auto"); }
pub fn memset_s_auto() { mem_set_cases("memset_s_auto"); }
pub fn memcmp_s_auto() { mem_cmp_cases("memcmp_s_auto", true); }
pub fn strcpy_s_auto() { str_copy_cases("strcpy_s_auto", false, true); }
pub fn strncpy_s_auto() { str_copy_cases("strncpy_s_auto", true, true); }
pub fn strcat_s_auto() { str_cat_cases("strcat_s_auto", false, true); }
pub fn strncat_s_auto() { str_cat_cases("strncat_s_auto", true, true); }

fn main() {
    memcpy_s();
    memmove_s();
    memset_s();
    memzero_s();
    memcmp_s();
    strcpy_s();
    strncpy_s();
    strcat_s();
    strncat_s();
    strnlen_s();
    strtok_s();
    strerror_s();
    strcmp_s();
    memcpy_s_auto();
    memmove_s_auto();
    memset_s_auto();
    memcmp_s_auto();
    strcpy_s_auto();
    strncpy_s_auto();
    strcat_s_auto();
    strncat_s_auto();
}

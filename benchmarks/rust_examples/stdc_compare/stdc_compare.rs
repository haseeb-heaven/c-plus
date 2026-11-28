//! Safe-Rust counterpart to `harness.c` for the ordinary-spelling C+ checked
//! layer (`-include stdlib/safe/safe_c.h`).
//!
//! Each function prints lines of the form
//!
//!     <api> <case> <token>
//!
//! using ONLY safe Rust: no `unsafe`, no FFI, no raw pointers. The idioms are
//! the ones a Rust developer would actually use: `copy_from_slice`, `fill`,
//! slice equality, `get`/`get_mut`, `String::push_str`, `Vec::try_reserve_exact`,
//! `Option`, `drop`.
//!
//! Token vocabulary (shared with harness.c):
//!   OK            operation completed; the separate guard object survived
//!   REFUSED       invalid condition detected before any out-of-bounds write
//!   N/A           the case has no meaning for this API (pure allocators)
//!   CANARY_OK     explicit case: the guard region is intact
//!   CANARY_BROKEN bytes past the declared capacity were modified (never
//!                 producible in safe Rust; the C side must also be clean)
//!
//! MODELLING NOTES (full discussion in NOTES.md):
//!  * A C null pointer has no safe-Rust value; it is `None` in an
//!    `Option<&[u8]>` / `Option<&mut [u8]>` / `Option<&str>`. `None` means the
//!    operation is refused.
//!  * "one_over" / "huge_n" use bounds-checked `get`/`get_mut` (and
//!    `try_reserve_exact`) which return `None`/`Err` instead of panicking or
//!    writing out of bounds.
//!  * Allocation-failure injection on the C side is modelled by a `force_fail`
//!    flag that produces `None`; real huge allocations use `try_reserve_exact`,
//!    a genuine checked return.
//!  * The canary is a SEPARATE 0xA5 array, mirroring the C harness. Safe Rust
//!    cannot write past a slice, so CANARY_BROKEN is never emitted here.
//!
//! IMPORTANT: matching tokens record OBSERVED outcomes on a fixed input set.
//! They do NOT mean the two layers provide equivalent guarantees. The C layer
//! performs runtime checks over caller-supplied extents; Rust's guarantees are
//! static and its refusals are `None`/`Err`. No Rust-equivalence or
//! 100%-safety claim is made. See NOTES.md.

use std::sync::OnceLock;

const N: usize = 8;
const G: usize = 8;
const GUARD: u8 = 0xA5;
const SRC_STR: &str = "abc";
const LONG_STR: &str = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
/// RSIZE_MAX_MEM / RSIZE_MAX_STR for this build is SIZE_MAX >> 1.
const MAX_SIZE: usize = usize::MAX >> 1;

fn wanted() -> &'static Vec<String> {
    static W: OnceLock<Vec<String>> = OnceLock::new();
    W.get_or_init(|| std::env::args().skip(1).collect())
}

fn want(api: &str) -> bool {
    let w = wanted();
    w.is_empty() || w.iter().any(|a| a == api)
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Tok {
    Ok,
    Refused,
    Na,
    CanaryOk,
    CanaryBroken,
}

impl Tok {
    fn as_str(self) -> &'static str {
        match self {
            Tok::Ok => "OK",
            Tok::Refused => "REFUSED",
            Tok::Na => "N/A",
            Tok::CanaryOk => "CANARY_OK",
            Tok::CanaryBroken => "CANARY_BROKEN",
        }
    }
}

fn emit(api: &str, case: &str, token: Tok) {
    println!("{} {} {}", api, case, token.as_str());
}

fn emit_na(api: &str, case: &str) {
    emit(api, case, Tok::Na);
}

fn guard() -> [u8; G] {
    [GUARD; G]
}

fn guard_intact(g: &[u8]) -> bool {
    g.iter().all(|&b| b == GUARD)
}

fn decision(ok: bool, g: &[u8]) -> Tok {
    if !guard_intact(g) {
        Tok::CanaryBroken
    } else if ok {
        Tok::Ok
    } else {
        Tok::Refused
    }
}

fn emit_canary(api: &str, case: &str, g: &[u8]) {
    emit(api, case, if guard_intact(g) { Tok::CanaryOk } else { Tok::CanaryBroken });
}

// ---------------------------------------------------------------------------
// Memory APIs: memcpy / memmove / memset / memcmp
// ---------------------------------------------------------------------------

/// Bounded byte copy. `get_mut`/`get` refuse an out-of-range request by
/// returning `None` rather than writing out of bounds. `copy_from_slice` is the
/// ordinary idiom; for genuinely overlapping regions Rust offers
/// `slice::copy_within`, which is the memmove-equivalent safe method.
fn copy_bytes(dest: Option<&mut [u8]>, src: Option<&[u8]>, n: usize) -> bool {
    let d = match dest { Some(d) => d, None => return false };
    let s = match src { Some(s) => s, None => return false };
    if n > MAX_SIZE { return false; }
    match (d.get_mut(..n), s.get(..n)) {
        (Some(d), Some(s)) => { d.copy_from_slice(s); true }
        _ => false,
    }
}

fn mem_copy_cases(api: &str) {
    if !want(api) { return; }
    let src: [u8; N] = core::array::from_fn(|i| (i + 1) as u8);

    let g = guard();
    let mut d = [0u8; N];
    let ok = copy_bytes(Some(&mut d), Some(&src), N);
    emit(api, "valid", decision(ok, &g));

    let g = guard();
    let ok = copy_bytes(None, Some(&src), N);
    emit(api, "null", decision(ok, &g));

    let g = guard();
    let mut d = [0u8; N];
    let ok = copy_bytes(Some(&mut d), Some(&src), 0);
    emit(api, "zero_length", decision(ok, &g));

    let g = guard();
    let mut small = [0u8; N - 1];
    let ok = copy_bytes(Some(&mut small), Some(&src), N);
    emit(api, "one_over", decision(ok, &g));

    let g = guard();
    let mut d = [0u8; N];
    let ok = copy_bytes(Some(&mut d), Some(&src), usize::MAX);
    emit(api, "huge_n", decision(ok, &g));

    let g = guard();
    let mut d = [0u8; N];
    let _ = copy_bytes(Some(&mut d), Some(&src), N);
    emit_canary(api, "canary", &g);
}

fn set_bytes(dest: Option<&mut [u8]>, value: u8, n: usize) -> bool {
    let d = match dest { Some(d) => d, None => return false };
    if n > MAX_SIZE { return false; }
    match d.get_mut(..n) {
        Some(d) => { d.fill(value); true }
        None => false,
    }
}

fn memset_cases() {
    let api = "memset";
    if !want(api) { return; }

    let g = guard();
    let mut d = [0u8; N];
    let ok = set_bytes(Some(&mut d), 0x5A, N);
    emit(api, "valid", decision(ok, &g));

    let g = guard();
    let ok = set_bytes(None, 0x5A, N);
    emit(api, "null", decision(ok, &g));

    let g = guard();
    let mut d = [0u8; N];
    let ok = set_bytes(Some(&mut d), 0x5A, 0);
    emit(api, "zero_length", decision(ok, &g));

    let g = guard();
    let mut small = [0u8; N - 1];
    let ok = set_bytes(Some(&mut small), 0x5A, N);
    emit(api, "one_over", decision(ok, &g));

    let g = guard();
    let mut d = [0u8; N];
    let ok = set_bytes(Some(&mut d), 0x5A, usize::MAX);
    emit(api, "huge_n", decision(ok, &g));

    let g = guard();
    let mut d = [0u8; N];
    let _ = set_bytes(Some(&mut d), 0x5A, N);
    emit_canary(api, "canary", &g);
}

fn cmp_bytes(a: Option<&[u8]>, b: Option<&[u8]>, n: usize) -> Option<bool> {
    let a = a?;
    let b = b?;
    if n > MAX_SIZE { return None; }
    let x = a.get(..n)?;
    let y = b.get(..n)?;
    Some(x == y)
}

fn memcmp_cases() {
    let api = "memcmp";
    if !want(api) { return; }
    let src: [u8; N] = core::array::from_fn(|i| (i + 1) as u8);
    let d = src;

    let g = guard();
    let got = cmp_bytes(Some(&d), Some(&src), N);
    emit(api, "valid", decision(got == Some(true), &g));

    let g = guard();
    let got = cmp_bytes(None, Some(&src), N);
    emit(api, "null", decision(got.is_some(), &g));

    let g = guard();
    let got = cmp_bytes(Some(&d), Some(&src), 0);
    emit(api, "zero_length", decision(got == Some(true), &g));

    let g = guard();
    let small = [0u8; N - 1];
    let got = cmp_bytes(Some(&small), Some(&src), N);
    emit(api, "one_over", decision(got.is_some(), &g));

    let g = guard();
    let got = cmp_bytes(Some(&d), Some(&src), usize::MAX);
    emit(api, "huge_n", decision(got.is_some(), &g));

    let g = guard();
    let _ = cmp_bytes(Some(&d), Some(&src), N);
    emit_canary(api, "canary", &g);
}

// ---------------------------------------------------------------------------
// String APIs: strcpy / strncpy / strcat / strncat
// ---------------------------------------------------------------------------

/// Model a capacity-checked copy into a String. `dmax` is the declared
/// destination capacity (including the NUL terminator). `n = Some(_)` selects
/// the length-limited (`strncpy`-style) variant.
fn str_copy(
    dest: Option<&mut String>,
    dmax: usize,
    src: Option<&str>,
    n: Option<usize>,
) -> bool {
    let d = match dest { Some(d) => d, None => return false };
    let s = match src { Some(s) => s, None => return false };
    if let Some(k) = n {
        if k > MAX_SIZE { return false; }
    }
    let part = match n { Some(k) => s.get(..k).unwrap_or(s), None => s };
    if part.len() + 1 > dmax { return false; }
    d.clear();
    d.push_str(part);
    true
}

/// Model a capacity-checked append. `dest` is the current string content.
fn str_append(
    dest: Option<&mut String>,
    dmax: usize,
    src: Option<&str>,
    n: Option<usize>,
) -> bool {
    let d = match dest { Some(d) => d, None => return false };
    let s = match src { Some(s) => s, None => return false };
    if let Some(k) = n {
        if k > MAX_SIZE { return false; }
    }
    let part = match n { Some(k) => s.get(..k).unwrap_or(s), None => s };
    if d.len() + part.len() + 1 > dmax { return false; }
    d.push_str(part);
    true
}

fn str_copy_cases(api: &str, limited: bool) {
    if !want(api) { return; }
    let lim = if limited { Some(4) } else { None };

    let g = guard();
    let mut d = String::new();
    let ok = str_copy(Some(&mut d), N, Some(SRC_STR), lim);
    emit(api, "valid", decision(ok, &g));

    let g = guard();
    let ok = str_copy(None, N, Some(SRC_STR), lim);
    emit(api, "null", decision(ok, &g));

    let g = guard();
    let mut d = String::new();
    let ok = if limited {
        // strncpy: n == 0 is a successful no-op in this checked layer.
        str_copy(Some(&mut d), N, Some(SRC_STR), Some(0))
    } else {
        // strcpy: "zero length" means an empty source string.
        str_copy(Some(&mut d), N, Some(""), None)
    };
    emit(api, "zero_length", decision(ok, &g));

    let g = guard();
    let mut d = String::new();
    let ok = str_copy(Some(&mut d), 3, Some(SRC_STR), lim);
    emit(api, "one_over", decision(ok, &g));

    let g = guard();
    let mut d = String::new();
    let ok = if limited {
        str_copy(Some(&mut d), N, Some(SRC_STR), Some(usize::MAX))
    } else {
        str_copy(Some(&mut d), N, Some(LONG_STR), None)
    };
    emit(api, "huge_n", decision(ok, &g));

    let g = guard();
    let mut d = String::new();
    let _ = str_copy(Some(&mut d), N, Some(SRC_STR), lim);
    emit_canary(api, "canary", &g);
}

fn str_cat_cases(api: &str, limited: bool) {
    if !want(api) { return; }
    let lim = if limited { Some(3) } else { None };

    let g = guard();
    let mut d = String::from("x");
    let ok = str_append(Some(&mut d), N, Some(SRC_STR), lim);
    emit(api, "valid", decision(ok, &g));

    let g = guard();
    let ok = str_append(None, N, Some(SRC_STR), lim);
    emit(api, "null", decision(ok, &g));

    let g = guard();
    let mut d = String::from("x");
    let ok = if limited {
        str_append(Some(&mut d), N, Some(SRC_STR), Some(0))
    } else {
        str_append(Some(&mut d), N, Some(""), None)
    };
    emit(api, "zero_length", decision(ok, &g));

    let g = guard();
    let mut d = String::from("x");
    let ok = str_append(Some(&mut d), 4, Some(SRC_STR), if limited { Some(4) } else { None });
    emit(api, "one_over", decision(ok, &g));

    let g = guard();
    let mut d = String::from("x");
    let ok = if limited {
        str_append(Some(&mut d), N, Some(SRC_STR), Some(usize::MAX))
    } else {
        str_append(Some(&mut d), N, Some(LONG_STR), None)
    };
    emit(api, "huge_n", decision(ok, &g));

    let g = guard();
    let mut d = String::from("x");
    let _ = str_append(Some(&mut d), N, Some(SRC_STR), lim);
    emit_canary(api, "canary", &g);
}

// ---------------------------------------------------------------------------
// Allocator APIs: malloc / calloc / realloc / free
// ---------------------------------------------------------------------------

/// `force_fail = true` models the C harness's injected NULL return. Otherwise
/// `try_reserve_exact` is a genuine safe-Rust checked allocation: it returns
/// `Err` on size overflow / capacity overflow instead of aborting.
fn simulated_alloc(size: usize, force_fail: bool) -> Option<Vec<u8>> {
    if force_fail { return None; }
    let mut v = Vec::new();
    v.try_reserve_exact(size).ok()?;
    v.resize(size, 0);
    Some(v)
}

/// Model realloc: on failure the original buffer is preserved (None), exactly
/// as `realloc_s` leaves the old block live.
fn simulated_realloc(old: &[u8], new_size: usize, force_fail: bool) -> Option<Vec<u8>> {
    let mut fresh = simulated_alloc(new_size, force_fail)?;
    let keep = old.len().min(fresh.len());
    fresh[..keep].copy_from_slice(&old[..keep]);
    Some(fresh)
}

fn alloc_cases(api: &str) {
    if !want(api) { return; }
    let g = guard();

    let p = simulated_alloc(N, false);
    emit(api, "valid", decision(p.is_some(), &g));
    drop(p);

    let q = simulated_alloc(N, true);
    emit(api, "null", decision(q.is_some(), &g));
    drop(q);

    // C contract: a zero-size request yields a defined 1-byte tracked block.
    let z = simulated_alloc(0, false);
    emit(api, "zero_length", decision(z.is_some(), &g));
    drop(z);

    emit_na(api, "one_over");

    let huge = simulated_alloc(usize::MAX, false);
    emit(api, "huge_n", decision(huge.is_some(), &g));
    drop(huge);

    let c = simulated_alloc(N, false);
    emit_canary(api, "canary", &g);
    drop(c);
}

fn calloc_cases() {
    let api = "calloc";
    if !want(api) { return; }
    let g = guard();

    // count * size with a zero operand is a defined 1-byte zeroed block.
    let p = simulated_alloc(N * 1, false);
    emit(api, "valid", decision(p.is_some(), &g));
    drop(p);

    let q = simulated_alloc(N * 1, true);
    emit(api, "null", decision(q.is_some(), &g));
    drop(q);

    let z = simulated_alloc(0, false);
    emit(api, "zero_length", decision(z.is_some(), &g));
    drop(z);

    emit_na(api, "one_over");

    // SIZE_MAX * 2 overflows; try_reserve_exact reports Err (no panic).
    let huge = usize::MAX.checked_mul(2).and_then(|n| simulated_alloc(n, false));
    emit(api, "huge_n", decision(huge.is_some(), &g));
    drop(huge);

    let c = simulated_alloc(N, false);
    emit_canary(api, "canary", &g);
    drop(c);
}

fn realloc_cases() {
    let api = "realloc";
    if !want(api) { return; }
    let g = guard();

    let old = [0x11u8; N];
    let grown = simulated_realloc(&old, N * 2, false);
    emit(api, "valid", decision(grown.is_some(), &g));
    drop(grown);

    let failed = simulated_realloc(&old, N * 2, true);
    emit(api, "null", decision(failed.is_some(), &g));
    drop(failed);

    let zero = simulated_realloc(&old, 0, false);
    emit(api, "zero_length", decision(zero.is_some(), &g));
    drop(zero);

    emit_na(api, "one_over");

    let huge = simulated_realloc(&old, usize::MAX, false);
    emit(api, "huge_n", decision(huge.is_some(), &g));
    drop(huge);

    let same = simulated_realloc(&old, N, false);
    emit_canary(api, "canary", &g);
    drop(same);
}

fn free_cases() {
    let api = "free";
    if !want(api) { return; }
    let g = guard();

    let p = simulated_alloc(N, false);
    drop(p); // free of a live allocation succeeds
    emit(api, "valid", decision(true, &g));

    drop(None::<Vec<u8>>); // free(NULL) is a no-op
    emit(api, "null", decision(true, &g));

    emit_na(api, "zero_length");
    emit_na(api, "one_over");
    emit_na(api, "huge_n");

    let p = simulated_alloc(N, false);
    drop(p);
    emit_canary(api, "canary", &g);
}

// ---------------------------------------------------------------------------
// Public example entry points (one per ordinary C API).
// ---------------------------------------------------------------------------

pub fn memcpy_() { mem_copy_cases("memcpy"); }
pub fn memmove_() { mem_copy_cases("memmove"); }
pub fn memset_() { memset_cases(); }
pub fn memcmp_() { memcmp_cases(); }
pub fn strcpy_() { str_copy_cases("strcpy", false); }
pub fn strncpy_() { str_copy_cases("strncpy", true); }
pub fn strcat_() { str_cat_cases("strcat", false); }
pub fn strncat_() { str_cat_cases("strncat", true); }
pub fn malloc_() { alloc_cases("malloc"); }
pub fn calloc_() { calloc_cases(); }
pub fn realloc_() { realloc_cases(); }
pub fn free_() { free_cases(); }

fn main() {
    memcpy_();
    memmove_();
    memset_();
    memcmp_();
    strcpy_();
    strncpy_();
    strcat_();
    strncat_();
    malloc_();
    calloc_();
    realloc_();
    free_();
}

//! Safe-Rust side of the per-method memory checked-API comparison.
//!
//! Prints the same "<method> <case> <token>" lines as the C harnesses.
//! NO unsafe code: only copy_from_slice, copy_within, fill, slice equality,
//! get/get_mut and get_many_mut are used.
//!
//! Modelling notes (see NOTES.md):
//!  * A C null pointer has no safe-Rust value. It is represented by `None`
//!    in an `Option<&[u8]>` / `Option<&mut [u8]>`; the safe path refuses it.
//!  * Bounds violations use `get`/`get_mut`, which return `None` instead of
//!    writing out of bounds. `get(..usize::MAX)` is `None`, never an overflow.
//!  * The memcpy overlap constraint is modelled with `RefCell::try_borrow_mut`,
//!    a real safe API that returns `Err` when a second mutable view would
//!    alias the same byte. memmove overlap uses `copy_within`, which explicitly
//!    supports overlapping ranges and therefore succeeds.
//!  * Guards are bytes placed immediately after the declared destination
//!    capacity; they must survive every refused operation.

use std::cell::RefCell;
use std::fmt;

const N: usize = 8;
const G: usize = 8;
const TOTAL: usize = N + G;
const GUARD: u8 = 0xA5;
const SRC: [u8; N] = [1, 2, 3, 4, 5, 6, 7, 8];

#[derive(Clone, Copy, PartialEq, Eq)]
enum Token {
    Ok,
    Refused,
    Na,
    CanaryBroken,
}

impl fmt::Display for Token {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            Token::Ok => "OK",
            Token::Refused => "REFUSED",
            Token::Na => "N/A",
            Token::CanaryBroken => "CANARY_BROKEN",
        };
        f.write_str(s)
    }
}

fn emit(method: &str, case: &str, token: Token) {
    println!("{} {} {}", method, case, token);
}

/// Build a guarded buffer: [0, guard_start) is payload, [guard_start, TOTAL)
/// is the guard region. Mirrors the C harness layout.
fn guarded(guard_start: usize) -> Vec<u8> {
    let mut buf = vec![0x11u8; TOTAL];
    for b in buf.iter_mut().skip(guard_start) {
        *b = GUARD;
    }
    buf
}

fn guards_intact(guard: &[u8]) -> bool {
    guard.iter().all(|&b| b == GUARD)
}

fn token(op_ok: bool, guards_ok: bool) -> Token {
    if !guards_ok {
        Token::CanaryBroken
    } else if op_ok {
        Token::Ok
    } else {
        Token::Refused
    }
}

fn memcpy_s_cases() {
    const M: &str = "memcpy_s";

    // 1) valid exact-fit: copy_from_slice into an exactly sized destination.
    let mut buf = guarded(N);
    let (dest, guard) = buf.split_at_mut(N);
    dest.copy_from_slice(&SRC);
    emit(M, "valid_exact_fit", token(true, guards_intact(guard)));

    // 2) null source -> None (safe Rust has no null reference) -> REFUSED.
    let mut buf = guarded(N);
    let (dest, guard) = buf.split_at_mut(N);
    let null_src: Option<&[u8]> = None;
    let op = null_src.map(|s| dest.copy_from_slice(s));
    emit(
        M,
        "null_src",
        token(op.is_some(), guards_intact(guard)),
    );

    // 3) destination one byte too small: get_mut(..N) on an (N-1)-byte dest.
    let mut buf = guarded(N - 1);
    let (dest, guard) = buf.split_at_mut(N - 1);
    let op = dest.get_mut(..N).map(|d| d.copy_from_slice(&SRC[..N]));
    emit(
        M,
        "dest_one_byte_too_small",
        token(op.is_some(), guards_intact(guard)),
    );

    // 4) SIZE_MAX length: get_mut(..usize::MAX) is None, never an overflow.
    let mut buf = guarded(N);
    let (dest, guard) = buf.split_at_mut(N);
    let op = dest
        .get_mut(..usize::MAX)
        .map(|d| d.copy_from_slice(&SRC[..usize::MAX]));
    emit(
        M,
        "size_max_length",
        token(op.is_some(), guards_intact(guard)),
    );

    // 5) overlap: two mutable views aliasing the same byte are refused at
    // runtime by RefCell::try_borrow_mut (returns Err, not a second &mut).
    let mut buf = guarded(N);
    let (_dest, guard) = buf.split_at_mut(N);
    let shared = RefCell::new(0x11u8);
    let live_borrow = shared.borrow();
    let alias = shared.try_borrow_mut();
    let op_ok = alias.is_ok();
    drop(live_borrow);
    emit(M, "overlap", token(op_ok, guards_intact(guard)));
}

fn memmove_s_cases() {
    const M: &str = "memmove_s";

    let mut buf = guarded(N);
    let (dest, guard) = buf.split_at_mut(N);
    dest.copy_from_slice(&SRC);
    emit(M, "valid_exact_fit", token(true, guards_intact(guard)));

    let mut buf = guarded(N);
    let (dest, guard) = buf.split_at_mut(N);
    let null_src: Option<&[u8]> = None;
    let op = null_src.map(|s| dest.copy_from_slice(s));
    emit(M, "null_src", token(op.is_some(), guards_intact(guard)));

    let mut buf = guarded(N - 1);
    let (dest, guard) = buf.split_at_mut(N - 1);
    let op = dest.get_mut(..N).map(|d| d.copy_from_slice(&SRC[..N]));
    emit(
        M,
        "dest_one_byte_too_small",
        token(op.is_some(), guards_intact(guard)),
    );

    let mut buf = guarded(N);
    let (dest, guard) = buf.split_at_mut(N);
    let op = dest
        .get_mut(..usize::MAX)
        .map(|d| d.copy_from_slice(&SRC[..usize::MAX]));
    emit(
        M,
        "size_max_length",
        token(op.is_some(), guards_intact(guard)),
    );

    // copy_within is the safe, overlap-capable memmove analogue.
    let mut buf = guarded(N);
    let (dest, guard) = buf.split_at_mut(N);
    dest.copy_within(1..N, 0);
    emit(M, "overlap", token(true, guards_intact(guard)));
}

fn memset_s_cases() {
    const M: &str = "memset_s";

    let mut buf = guarded(N);
    let (dest, guard) = buf.split_at_mut(N);
    dest.fill(0x5A);
    emit(M, "valid_exact_fit", token(true, guards_intact(guard)));

    let mut buf = guarded(N);
    let (_dest, guard) = buf.split_at_mut(N);
    let null_dest: Option<&mut [u8]> = None;
    let op = null_dest.map(|d| d.fill(0x5A));
    emit(M, "null_dest", token(op.is_some(), guards_intact(guard)));

    let mut buf = guarded(N - 1);
    let (dest, guard) = buf.split_at_mut(N - 1);
    let op = dest.get_mut(..N).map(|d| d.fill(0x5A));
    emit(
        M,
        "dest_one_byte_too_small",
        token(op.is_some(), guards_intact(guard)),
    );

    let mut buf = guarded(N);
    let (dest, guard) = buf.split_at_mut(N);
    let op = dest.get_mut(..usize::MAX).map(|d| d.fill(0x5A));
    emit(
        M,
        "size_max_length",
        token(op.is_some(), guards_intact(guard)),
    );

    emit(M, "overlap", Token::Na);
}

fn memcmp_s_cases() {
    const M: &str = "memcmp_s";

    let mut buf = guarded(N);
    let (a, guard) = buf.split_at_mut(N);
    let _equal = a[..] == SRC[..];
    emit(M, "valid_exact_fit", token(true, guards_intact(guard)));

    let mut buf = guarded(N);
    let (a, guard) = buf.split_at_mut(N);
    let null_src: Option<&[u8]> = None;
    let op = null_src.map(|s| a[..] == *s);
    emit(M, "null_src", token(op.is_some(), guards_intact(guard)));

    let mut buf = guarded(N - 1);
    let (a, guard) = buf.split_at_mut(N - 1);
    let op = a.get(..N).map(|s| *s == SRC[..N]);
    emit(
        M,
        "dest_one_byte_too_small",
        token(op.is_some(), guards_intact(guard)),
    );

    let mut buf = guarded(N);
    let (a, guard) = buf.split_at_mut(N);
    let op = a
        .get(..usize::MAX)
        .and_then(|s| SRC.get(..usize::MAX).map(|t| *s == *t));
    emit(
        M,
        "size_max_length",
        token(op.is_some(), guards_intact(guard)),
    );

    // Two shared (read-only) overlapping slices are allowed in safe Rust.
    let buf = guarded(N);
    let left = &buf[..N - 1];
    let right = &buf[1..N];
    let _equal = left == right;
    emit(M, "overlap", token(true, guards_intact(&buf[N..TOTAL])));
}

fn main() {
    memcpy_s_cases();
    memmove_s_cases();
    memset_s_cases();
    memcmp_s_cases();
}

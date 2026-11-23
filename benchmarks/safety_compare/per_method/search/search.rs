// Safe-Rust outcome harness paired with harness.c (per-method search APIs).
// Build: rustc -O search.rs -o harness_rs
//
// No `unsafe` block, no raw pointers, no FFI.  Every line of stdout has the
// exact form:  <method> <case> <token>
//
// These are *safe equivalents* of the C+ checked contracts, not a claim of
// identical guarantees: Rust makes null/context-less calls unrepresentable and
// carries slice lengths implicitly, so the refusal cases below are modelled
// with explicit Options/guards.  See NOTES.md for the non-equivalence note.

use std::cmp::Ordering;

// CPLUS_SC_UPSTREAM_RSIZE_MAX_MEM from <cplus_safecrt_upstream.h> (16384).
const LIMIT: usize = 4 << 12;
const CTX_MAGIC: u32 = 0x5A5A_5A5A;
const CANARY_I: i32 = 0x5A5A_A5A5_u32 as i32;
const CANARY_C: u8 = 0xA5;

#[derive(Clone, Copy)]
struct Ctx {
    magic: u32,
}

fn emit(method: &str, case_name: &str, token: &str) {
    println!("{} {} {}", method, case_name, token);
}

// ---------------------------------------------------------------------
// bsearch_s analogue: checked wrapper around slice::binary_search_by.
// The Option parameters model C's nullable arguments; count/elem_size are
// explicit so that the RSIZE_MAX_MEM contract can be exercised without
// allocating an oversized object.
// ---------------------------------------------------------------------
enum BsResult {
    Found(usize),
    NotFound,
    Refused,
}

fn cmp_ctx(probe: &i32, key: &i32, ctx: &Ctx) -> Ordering {
    // A comparator that did not receive the context must never report a
    // match.  Rust closures capture context lexically, so "lost context" is
    // simulated with a bogus Ctx value (see the context-lost case).
    if ctx.magic != CTX_MAGIC {
        return Ordering::Greater;
    }
    probe.cmp(key)
}

fn bsearch_checked(
    key: Option<&i32>,
    base: Option<&[i32]>,
    count: usize,
    elem_size: usize,
    compar: Option<fn(&i32, &i32, &Ctx) -> Ordering>,
    ctx: Option<&Ctx>,
) -> BsResult {
    if count != 0 && (key.is_none() || base.is_none() || compar.is_none()) {
        return BsResult::Refused; // models ESNULLP
    }
    if count > LIMIT || elem_size > LIMIT {
        return BsResult::Refused; // models ESLEMAX
    }
    let base = base.unwrap_or(&[]);
    // C+ checks only the RSIZE_MAX_MEM ceiling; it has no object-size check.
    // Rust slices carry their length, so an over-long count is refused here.
    if count > base.len() {
        return BsResult::Refused;
    }
    let key = match key {
        Some(k) => k,
        None => return BsResult::NotFound, // zero-count empty search
    };
    let cmp = compar.expect("nonzero count requires a comparator");
    let bad_ctx = Ctx { magic: 0 };
    let ctx = ctx.unwrap_or(&bad_ctx);
    match base[..count].binary_search_by(|probe| cmp(probe, key, ctx)) {
        Ok(i) => BsResult::Found(i),
        Err(_) => BsResult::NotFound,
    }
}

fn run_bsearch_cases() {
    let a: [i32; 5] = [1, 3, 5, 7, 9];
    let good = Ctx { magic: CTX_MAGIC };
    let seven = 7;
    let six = 6;

    match bsearch_checked(Some(&seven), Some(&a), 5, 4, Some(cmp_ctx), Some(&good)) {
        BsResult::Found(3) => emit("bsearch_s", "found", "FOUND"),
        _ => emit("bsearch_s", "found", "BAD_FOUND"),
    }
    match bsearch_checked(Some(&six), Some(&a), 5, 4, Some(cmp_ctx), Some(&good)) {
        BsResult::NotFound => emit("bsearch_s", "not-found", "NOTFOUND"),
        _ => emit("bsearch_s", "not-found", "BAD_NOTFOUND"),
    }
    match bsearch_checked(None, Some(&a), 5, 4, Some(cmp_ctx), Some(&good)) {
        BsResult::Refused => emit("bsearch_s", "null-key", "REFUSED"),
        _ => emit("bsearch_s", "null-key", "BAD_REFUSAL"),
    }
    match bsearch_checked(Some(&seven), None, 5, 4, Some(cmp_ctx), Some(&good)) {
        BsResult::Refused => emit("bsearch_s", "null-base", "REFUSED"),
        _ => emit("bsearch_s", "null-base", "BAD_REFUSAL"),
    }
    match bsearch_checked(Some(&seven), Some(&a), 5, 4, None, Some(&good)) {
        BsResult::Refused => emit("bsearch_s", "null-compar", "REFUSED"),
        _ => emit("bsearch_s", "null-compar", "BAD_REFUSAL"),
    }
    match bsearch_checked(None, Some(&[]), 0, 4, None, None) {
        BsResult::NotFound => emit("bsearch_s", "zero-nmemb", "EMPTY"),
        _ => emit("bsearch_s", "zero-nmemb", "BAD_EMPTY"),
    }
    match bsearch_checked(Some(&seven), Some(&a), LIMIT + 1, 4, Some(cmp_ctx), Some(&good)) {
        BsResult::Refused => emit("bsearch_s", "over-nmemb", "REFUSED"),
        _ => emit("bsearch_s", "over-nmemb", "BAD_REFUSAL"),
    }
    match bsearch_checked(Some(&seven), Some(&a), 5, LIMIT + 1, Some(cmp_ctx), Some(&good)) {
        BsResult::Refused => emit("bsearch_s", "over-size", "REFUSED"),
        _ => emit("bsearch_s", "over-size", "BAD_REFUSAL"),
    }
    match bsearch_checked(Some(&seven), Some(&a), 5, 4, Some(cmp_ctx), Some(&good)) {
        BsResult::Found(3) => emit("bsearch_s", "context-ok", "CTX_OK"),
        _ => emit("bsearch_s", "context-ok", "BAD_CTX"),
    }
    match bsearch_checked(Some(&seven), Some(&a), 5, 4, Some(cmp_ctx), None) {
        BsResult::NotFound => emit("bsearch_s", "context-lost", "CTX_LOST"),
        _ => emit("bsearch_s", "context-lost", "BAD_CTX"),
    }

    let mut b = [0i32; 7];
    b[0] = CANARY_I;
    b[7 - 1] = CANARY_I;
    b[1..6].copy_from_slice(&a);
    let five = 5;
    match bsearch_checked(Some(&five), Some(&b[1..6]), 5, 4, Some(cmp_ctx), Some(&good)) {
        BsResult::Found(2) if b[0] == CANARY_I && b[6] == CANARY_I => {
            emit("bsearch_s", "canary", "INTACT")
        }
        _ => emit("bsearch_s", "canary", "BAD_CANARY"),
    }
}

// ---------------------------------------------------------------------
// memrchr_s analogue: checked wrapper around iter().rposition().
// ---------------------------------------------------------------------
enum MrResult {
    Found(usize),
    NotFound,
    Refused,
}

fn memrchr_checked(
    dest: Option<&[u8]>,
    dmax: usize,
    ch: Option<u16>,
    resultp: Option<&mut Option<usize>>,
) -> MrResult {
    let out = match resultp {
        Some(p) => p,
        None => return MrResult::Refused, // models ESNULLP on resultp
    };
    *out = None;
    let d = match dest {
        Some(d) => d,
        None => return MrResult::Refused, // models ESNULLP on dest
    };
    if dmax == 0 {
        return MrResult::Refused; // models ESZEROL
    }
    if dmax > LIMIT {
        return MrResult::Refused; // models ESLEMAX
    }
    let c = match ch {
        Some(c) if c <= u8::MAX as u16 => c as u8,
        _ => return MrResult::Refused, // models ESLEMAX on ch > UCHAR_MAX
    };
    // Rust slices carry their length; C+ has no matching object-size check.
    if dmax > d.len() {
        return MrResult::Refused;
    }
    match d[..dmax].iter().rposition(|&x| x == c) {
        Some(i) => {
            *out = Some(i);
            MrResult::Found(i)
        }
        None => MrResult::NotFound,
    }
}

fn run_memrchr_cases() {
    let hay: [u8; 6] = [b'a', b'b', b'X', b'a', b'b', b'Y'];

    let mut out: Option<usize> = Some(999);
    match memrchr_checked(Some(&hay), hay.len(), Some(b'X' as u16), Some(&mut out)) {
        MrResult::Found(2) if out == Some(2) => emit("memrchr_s", "found", "FOUND"),
        _ => emit("memrchr_s", "found", "BAD_FOUND"),
    }

    let mut out: Option<usize> = Some(999);
    match memrchr_checked(Some(&hay), hay.len(), Some(b'Z' as u16), Some(&mut out)) {
        MrResult::NotFound if out.is_none() => emit("memrchr_s", "not-found", "NOTFOUND"),
        _ => emit("memrchr_s", "not-found", "BAD_NOTFOUND"),
    }

    let mut out: Option<usize> = Some(999);
    match memrchr_checked(Some(&hay), hay.len(), Some(b'a' as u16), Some(&mut out)) {
        MrResult::Found(3) => emit("memrchr_s", "last-occurrence", "IDX3"),
        _ => emit("memrchr_s", "last-occurrence", "BAD_INDEX"),
    }

    // A null resultp is unrepresentable in Rust; the Option model passes None.
    match memrchr_checked(Some(&hay), hay.len(), Some(b'X' as u16), None) {
        MrResult::Refused => emit("memrchr_s", "null-resultp", "REFUSED"),
        _ => emit("memrchr_s", "null-resultp", "BAD_REFUSAL"),
    }

    let mut out: Option<usize> = Some(999);
    match memrchr_checked(None, hay.len(), Some(b'X' as u16), Some(&mut out)) {
        MrResult::Refused if out.is_none() => emit("memrchr_s", "null-dest", "REFUSED"),
        _ => emit("memrchr_s", "null-dest", "BAD_REFUSAL"),
    }

    let mut out: Option<usize> = Some(999);
    match memrchr_checked(Some(&hay), 0, Some(b'X' as u16), Some(&mut out)) {
        MrResult::Refused if out.is_none() => emit("memrchr_s", "zero-dmax", "REFUSED"),
        _ => emit("memrchr_s", "zero-dmax", "BAD_REFUSAL"),
    }

    let mut out: Option<usize> = Some(999);
    match memrchr_checked(Some(&hay), LIMIT + 1, Some(b'X' as u16), Some(&mut out)) {
        MrResult::Refused if out.is_none() => emit("memrchr_s", "over-dmax", "REFUSED"),
        _ => emit("memrchr_s", "over-dmax", "BAD_REFUSAL"),
    }

    let mut out: Option<usize> = Some(999);
    match memrchr_checked(Some(&hay), hay.len(), Some(256), Some(&mut out)) {
        MrResult::Refused if out.is_none() => emit("memrchr_s", "ch-over", "REFUSED"),
        _ => emit("memrchr_s", "ch-over", "BAD_REFUSAL"),
    }

    let mut b = [CANARY_C; 8];
    b[1..7].copy_from_slice(&hay);
    let mut out: Option<usize> = Some(999);
    match memrchr_checked(Some(&b[1..7]), 6, Some(b'X' as u16), Some(&mut out)) {
        MrResult::Found(2) if b[0] == CANARY_C && b[7] == CANARY_C => {
            emit("memrchr_s", "canary", "INTACT")
        }
        _ => emit("memrchr_s", "canary", "BAD_CANARY"),
    }
}

// ---------------------------------------------------------------------
// memchr_s analogue: checked wrapper around iter().position().
// ---------------------------------------------------------------------
enum McResult {
    Found(usize),
    NotFound,
    Refused,
}

fn memchr_checked(
    dest: Option<&[u8]>,
    dmax: usize,
    ch: Option<u16>,
    resultp: Option<&mut Option<usize>>,
) -> McResult {
    let out = match resultp {
        Some(p) => p,
        None => return McResult::Refused,
    };
    *out = None;
    let d = match dest {
        Some(d) => d,
        None => return McResult::Refused,
    };
    if dmax == 0 || dmax > LIMIT {
        return McResult::Refused;
    }
    let c = match ch {
        Some(c) if c <= u8::MAX as u16 => c as u8,
        _ => return McResult::Refused,
    };
    if dmax > d.len() {
        return McResult::Refused;
    }
    match d[..dmax].iter().position(|&x| x == c) {
        Some(i) => {
            *out = Some(i);
            McResult::Found(i)
        }
        None => McResult::NotFound,
    }
}

fn run_memchr_cases() {
    let hay: [u8; 6] = [b'a', b'b', b'X', b'a', b'b', b'Y'];

    let mut out: Option<usize> = Some(999);
    match memchr_checked(Some(&hay), hay.len(), Some(b'X' as u16), Some(&mut out)) {
        McResult::Found(2) if out == Some(2) => emit("memchr_s", "found", "FOUND"),
        _ => emit("memchr_s", "found", "BAD_FOUND"),
    }

    let mut out: Option<usize> = Some(999);
    match memchr_checked(Some(&hay), hay.len(), Some(b'Z' as u16), Some(&mut out)) {
        McResult::NotFound if out.is_none() => emit("memchr_s", "not-found", "NOTFOUND"),
        _ => emit("memchr_s", "not-found", "BAD_NOTFOUND"),
    }

    let mut out: Option<usize> = Some(999);
    match memchr_checked(Some(&hay), hay.len(), Some(b'a' as u16), Some(&mut out)) {
        McResult::Found(0) => emit("memchr_s", "first-occurrence", "IDX0"),
        _ => emit("memchr_s", "first-occurrence", "BAD_INDEX"),
    }

    match memchr_checked(Some(&hay), hay.len(), Some(b'X' as u16), None) {
        McResult::Refused => emit("memchr_s", "null-resultp", "REFUSED"),
        _ => emit("memchr_s", "null-resultp", "BAD_REFUSAL"),
    }

    let mut out: Option<usize> = Some(999);
    match memchr_checked(None, hay.len(), Some(b'X' as u16), Some(&mut out)) {
        McResult::Refused if out.is_none() => emit("memchr_s", "null-dest", "REFUSED"),
        _ => emit("memchr_s", "null-dest", "BAD_REFUSAL"),
    }

    let mut out: Option<usize> = Some(999);
    match memchr_checked(Some(&hay), 0, Some(b'X' as u16), Some(&mut out)) {
        McResult::Refused if out.is_none() => emit("memchr_s", "zero-dmax", "REFUSED"),
        _ => emit("memchr_s", "zero-dmax", "BAD_REFUSAL"),
    }

    let mut out: Option<usize> = Some(999);
    match memchr_checked(Some(&hay), LIMIT + 1, Some(b'X' as u16), Some(&mut out)) {
        McResult::Refused if out.is_none() => emit("memchr_s", "over-dmax", "REFUSED"),
        _ => emit("memchr_s", "over-dmax", "BAD_REFUSAL"),
    }

    let mut out: Option<usize> = Some(999);
    match memchr_checked(Some(&hay), hay.len(), Some(256), Some(&mut out)) {
        McResult::Refused if out.is_none() => emit("memchr_s", "ch-over", "REFUSED"),
        _ => emit("memchr_s", "ch-over", "BAD_REFUSAL"),
    }

    let mut b = [CANARY_C; 8];
    b[1..7].copy_from_slice(&hay);
    let mut out: Option<usize> = Some(999);
    match memchr_checked(Some(&b[1..7]), 6, Some(b'X' as u16), Some(&mut out)) {
        McResult::Found(2) if b[0] == CANARY_C && b[7] == CANARY_C => {
            emit("memchr_s", "canary", "INTACT")
        }
        _ => emit("memchr_s", "canary", "BAD_CANARY"),
    }
}

fn main() {
    run_bsearch_cases();
    run_memrchr_cases();
    run_memchr_cases();
}

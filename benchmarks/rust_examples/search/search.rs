// Safe-Rust examples for the checked search APIs exercised by harness.c.
// Build: rustc -O search.rs -o out/harness_rs
//
// SAFETY / NO-FFI CONTRACT:
//   no `unsafe`, no raw pointers, no extern blocks, no libc calls.
// Every stdout line has the exact form:  <api> <case> <token>
// The four safe-Rust idioms required here are all exercised:
//   binary_search_by  -> bsearch_s analogue
//   iter().rposition  -> memrchr_s / strrchr_s analogues
//   iter().position   -> memchr_s / strchr_s analogues
//   slice/str contains-> strchr_s / strstr_s outcome checks
//   sort_by           -> qsort_s analogue (prepares the sorted input)
//
// These are SAFE EQUIVALENTS of the C+ checked contracts, not a claim of
// identical guarantees: Rust makes null arguments unrepresentable and carries
// slice lengths implicitly, so refusals are modelled with Option/guards.
// qsort_s has no linkable symbol (only a macro in safeclib), so no token is
// emitted for it; see NOTES.md for the non-equivalence statement.

use std::cmp::Ordering;

// CPLUS_SC_UPSTREAM_RSIZE_MAX_MEM (16384) and RSIZE_MAX_STR (4096).
const LIM_MEM: usize = 4 << 12;
const LIM_STR: usize = 4 << 10;
const CTX_MAGIC: u32 = 0x5A5A_5A5A;
const CANARY_I: i32 = 0x5A5A_A5A5_u32 as i32;
const CANARY_C: u8 = b'~'; // printable ASCII, keeps str buffers valid UTF-8

#[derive(Clone, Copy)]
struct Ctx {
    magic: u32,
}

fn emit(api: &str, case_name: &str, token: &str) {
    println!("{} {} {}", api, case_name, token);
}

// =====================================================================
// bsearch_s analogue: sort_by + slice::binary_search_by.
// The Option parameters model C's nullable arguments; count/elem_size are
// explicit so the RSIZE_MAX_MEM contract can be exercised without actually
// allocating an oversized object.
// =====================================================================
enum BsResult {
    Found(usize),
    NotFound,
    Refused,
}

fn cmp_ctx(probe: &i32, key: &i32, ctx: &Ctx) -> Ordering {
    // A comparator that did not receive its context refuses to match.
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
    // models ESNULLP (nmemb == 0 is a clean empty search in safeclib)
    if count != 0 && (key.is_none() || base.is_none() || compar.is_none()) {
        return BsResult::Refused;
    }
    if count > LIM_MEM || elem_size > LIM_MEM {
        return BsResult::Refused; // models ESLEMAX
    }
    let base = base.unwrap_or(&[]);
    // Rust slices carry their length; an over-long count is refused here.
    if count > base.len() {
        return BsResult::Refused;
    }
    let key = match key {
        Some(k) => k,
        None => return BsResult::NotFound,
    };
    let cmp = compar.expect("nonzero count requires a comparator");
    let bad_ctx = Ctx { magic: 0 };
    let ctx = ctx.unwrap_or(&bad_ctx);
    match base[..count].binary_search_by(|probe| cmp(probe, key, ctx)) {
        Ok(i) => BsResult::Found(i),
        Err(_) => BsResult::NotFound,
    }
}

fn run_bsearch() {
    // sort_by is the safe-Rust analogue of qsort_s (which is BLOCKED on the
    // C side). The sorted result equals [1, 3, 5, 7, 9], so indices match.
    let mut a = [9, 3, 7, 1, 5];
    a.sort_by(|x, y| x.cmp(y));

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
    match bsearch_checked(Some(&seven), Some(&a), 0, 4, Some(cmp_ctx), Some(&good)) {
        BsResult::NotFound => emit("bsearch_s", "zero-nmemb", "EMPTY"),
        _ => emit("bsearch_s", "zero-nmemb", "BAD_EMPTY"),
    }
    match bsearch_checked(Some(&seven), Some(&a), LIM_MEM + 1, 4, Some(cmp_ctx), Some(&good)) {
        BsResult::Refused => emit("bsearch_s", "over-nmemb", "REFUSED"),
        _ => emit("bsearch_s", "over-nmemb", "BAD_REFUSAL"),
    }
    match bsearch_checked(Some(&seven), Some(&a), 5, LIM_MEM + 1, Some(cmp_ctx), Some(&good)) {
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
    b[6] = CANARY_I;
    b[1..6].copy_from_slice(&a);
    let five = 5;
    match bsearch_checked(Some(&five), Some(&b[1..6]), 5, 4, Some(cmp_ctx), Some(&good)) {
        BsResult::Found(2) if b[0] == CANARY_I && b[6] == CANARY_I => {
            emit("bsearch_s", "canary", "INTACT")
        }
        _ => emit("bsearch_s", "canary", "BAD_CANARY"),
    }
}

// =====================================================================
// memrchr_s analogue: iter().rposition()
// =====================================================================
enum RResult {
    Found(usize),
    NotFound,
    Refused,
}

fn memrchr_checked(
    dest: Option<&[u8]>,
    dmax: usize,
    ch: Option<u16>,
    resultp: Option<&mut Option<usize>>,
) -> RResult {
    let out = match resultp {
        Some(p) => p,
        None => return RResult::Refused,
    };
    *out = None;
    let d = match dest {
        Some(d) => d,
        None => return RResult::Refused,
    };
    if dmax == 0 || dmax > LIM_MEM {
        return RResult::Refused;
    }
    let c = match ch {
        Some(c) if c <= u8::MAX as u16 => c as u8,
        _ => return RResult::Refused,
    };
    if dmax > d.len() {
        return RResult::Refused;
    }
    match d[..dmax].iter().rposition(|&x| x == c) {
        Some(i) => {
            *out = Some(i);
            RResult::Found(i)
        }
        None => RResult::NotFound,
    }
}

fn run_memrchr() {
    let hay: [u8; 6] = [b'a', b'b', b'X', b'a', b'b', b'Y'];

    let mut out = Some(999usize);
    match memrchr_checked(Some(&hay), hay.len(), Some(b'X' as u16), Some(&mut out)) {
        RResult::Found(2) if out == Some(2) => emit("memrchr_s", "found", "FOUND"),
        _ => emit("memrchr_s", "found", "BAD_FOUND"),
    }
    let mut out = Some(999usize);
    match memrchr_checked(Some(&hay), hay.len(), Some(b'Z' as u16), Some(&mut out)) {
        RResult::NotFound if out.is_none() => emit("memrchr_s", "not-found", "NOTFOUND"),
        _ => emit("memrchr_s", "not-found", "BAD_NOTFOUND"),
    }
    let mut out = Some(999usize);
    match memrchr_checked(Some(&hay), hay.len(), Some(b'a' as u16), Some(&mut out)) {
        RResult::Found(3) => emit("memrchr_s", "last-occurrence", "IDX3"),
        _ => emit("memrchr_s", "last-occurrence", "BAD_INDEX"),
    }
    match memrchr_checked(Some(&hay), hay.len(), Some(b'X' as u16), None) {
        RResult::Refused => emit("memrchr_s", "null-resultp", "REFUSED"),
        _ => emit("memrchr_s", "null-resultp", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match memrchr_checked(None, hay.len(), Some(b'X' as u16), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("memrchr_s", "null-dest", "REFUSED"),
        _ => emit("memrchr_s", "null-dest", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match memrchr_checked(Some(&hay), 0, Some(b'X' as u16), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("memrchr_s", "zero-dmax", "REFUSED"),
        _ => emit("memrchr_s", "zero-dmax", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match memrchr_checked(Some(&hay), LIM_MEM + 1, Some(b'X' as u16), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("memrchr_s", "over-dmax", "REFUSED"),
        _ => emit("memrchr_s", "over-dmax", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match memrchr_checked(Some(&hay), hay.len(), Some(256), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("memrchr_s", "ch-over", "REFUSED"),
        _ => emit("memrchr_s", "ch-over", "BAD_REFUSAL"),
    }

    let mut b = [CANARY_C; 8];
    b[1..7].copy_from_slice(&hay);
    let mut out = Some(999usize);
    match memrchr_checked(Some(&b[1..7]), 6, Some(b'X' as u16), Some(&mut out)) {
        RResult::Found(2) if b[0] == CANARY_C && b[7] == CANARY_C => {
            emit("memrchr_s", "canary", "INTACT")
        }
        _ => emit("memrchr_s", "canary", "BAD_CANARY"),
    }
}

// =====================================================================
// memchr_s analogue: iter().position() + slice::contains
// =====================================================================
fn memchr_checked(
    dest: Option<&[u8]>,
    dmax: usize,
    ch: Option<u16>,
    resultp: Option<&mut Option<usize>>,
) -> RResult {
    let out = match resultp {
        Some(p) => p,
        None => return RResult::Refused,
    };
    *out = None;
    let d = match dest {
        Some(d) => d,
        None => return RResult::Refused,
    };
    if dmax == 0 || dmax > LIM_MEM {
        return RResult::Refused;
    }
    let c = match ch {
        Some(c) if c <= u8::MAX as u16 => c as u8,
        _ => return RResult::Refused,
    };
    if dmax > d.len() {
        return RResult::Refused;
    }
    // Demonstrate safe-Rust `contains` alongside `position`.
    let contains_match = d[..dmax].contains(&c);
    match d[..dmax].iter().position(|&x| x == c) {
        Some(i) if contains_match => {
            *out = Some(i);
            RResult::Found(i)
        }
        None if !contains_match => RResult::NotFound,
        _ => RResult::Refused,
    }
}

fn run_memchr() {
    let hay: [u8; 6] = [b'a', b'b', b'X', b'a', b'b', b'Y'];

    let mut out = Some(999usize);
    match memchr_checked(Some(&hay), hay.len(), Some(b'X' as u16), Some(&mut out)) {
        RResult::Found(2) if out == Some(2) => emit("memchr_s", "found", "FOUND"),
        _ => emit("memchr_s", "found", "BAD_FOUND"),
    }
    let mut out = Some(999usize);
    match memchr_checked(Some(&hay), hay.len(), Some(b'Z' as u16), Some(&mut out)) {
        RResult::NotFound if out.is_none() => emit("memchr_s", "not-found", "NOTFOUND"),
        _ => emit("memchr_s", "not-found", "BAD_NOTFOUND"),
    }
    let mut out = Some(999usize);
    match memchr_checked(Some(&hay), hay.len(), Some(b'a' as u16), Some(&mut out)) {
        RResult::Found(0) => emit("memchr_s", "first-occurrence", "IDX0"),
        _ => emit("memchr_s", "first-occurrence", "BAD_INDEX"),
    }
    match memchr_checked(Some(&hay), hay.len(), Some(b'X' as u16), None) {
        RResult::Refused => emit("memchr_s", "null-resultp", "REFUSED"),
        _ => emit("memchr_s", "null-resultp", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match memchr_checked(None, hay.len(), Some(b'X' as u16), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("memchr_s", "null-dest", "REFUSED"),
        _ => emit("memchr_s", "null-dest", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match memchr_checked(Some(&hay), 0, Some(b'X' as u16), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("memchr_s", "zero-dmax", "REFUSED"),
        _ => emit("memchr_s", "zero-dmax", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match memchr_checked(Some(&hay), LIM_MEM + 1, Some(b'X' as u16), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("memchr_s", "over-dmax", "REFUSED"),
        _ => emit("memchr_s", "over-dmax", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match memchr_checked(Some(&hay), hay.len(), Some(256), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("memchr_s", "ch-over", "REFUSED"),
        _ => emit("memchr_s", "ch-over", "BAD_REFUSAL"),
    }

    let mut b = [CANARY_C; 8];
    b[1..7].copy_from_slice(&hay);
    let mut out = Some(999usize);
    match memchr_checked(Some(&b[1..7]), 6, Some(b'X' as u16), Some(&mut out)) {
        RResult::Found(2) if b[0] == CANARY_C && b[7] == CANARY_C => {
            emit("memchr_s", "canary", "INTACT")
        }
        _ => emit("memchr_s", "canary", "BAD_CANARY"),
    }
}

// =====================================================================
// strchr_s analogue: slice::contains + iter().position()
// =====================================================================
fn strchr_checked(
    dest: Option<&[u8]>,
    dmax: usize,
    ch: Option<u16>,
    resultp: Option<&mut Option<usize>>,
) -> RResult {
    let out = match resultp {
        Some(p) => p,
        None => return RResult::Refused,
    };
    *out = None;
    let d = match dest {
        Some(d) => d,
        None => return RResult::Refused,
    };
    if dmax == 0 || dmax > LIM_STR {
        return RResult::Refused;
    }
    let c = match ch {
        Some(c) if c <= u8::MAX as u16 => c as u8,
        _ => return RResult::Refused,
    };
    if dmax > d.len() {
        return RResult::Refused;
    }
    let contains_match = d[..dmax].contains(&c);
    match d[..dmax].iter().position(|&x| x == c) {
        Some(i) if contains_match => {
            *out = Some(i);
            RResult::Found(i)
        }
        None if !contains_match => RResult::NotFound,
        _ => RResult::Refused,
    }
}

fn run_strchr() {
    let hay = b"abXabY"; // length 6

    let mut out = Some(999usize);
    match strchr_checked(Some(hay), hay.len(), Some(b'X' as u16), Some(&mut out)) {
        RResult::Found(2) if out == Some(2) => emit("strchr_s", "found", "FOUND"),
        _ => emit("strchr_s", "found", "BAD_FOUND"),
    }
    let mut out = Some(999usize);
    match strchr_checked(Some(hay), hay.len(), Some(b'Z' as u16), Some(&mut out)) {
        RResult::NotFound if out.is_none() => emit("strchr_s", "not-found", "NOTFOUND"),
        _ => emit("strchr_s", "not-found", "BAD_NOTFOUND"),
    }
    let mut out = Some(999usize);
    match strchr_checked(None, hay.len(), Some(b'X' as u16), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("strchr_s", "null-dest", "REFUSED"),
        _ => emit("strchr_s", "null-dest", "BAD_REFUSAL"),
    }
    match strchr_checked(Some(hay), hay.len(), Some(b'X' as u16), None) {
        RResult::Refused => emit("strchr_s", "null-resultp", "REFUSED"),
        _ => emit("strchr_s", "null-resultp", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match strchr_checked(Some(hay), 0, Some(b'X' as u16), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("strchr_s", "zero-dmax", "REFUSED"),
        _ => emit("strchr_s", "zero-dmax", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match strchr_checked(Some(hay), LIM_STR + 1, Some(b'X' as u16), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("strchr_s", "over-dmax", "REFUSED"),
        _ => emit("strchr_s", "over-dmax", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match strchr_checked(Some(hay), hay.len(), Some(256), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("strchr_s", "ch-over", "REFUSED"),
        _ => emit("strchr_s", "ch-over", "BAD_REFUSAL"),
    }

    let mut b = [CANARY_C; 9];
    b[7] = CANARY_C;
    b[1..7].copy_from_slice(hay);
    let mut out = Some(999usize);
    match strchr_checked(Some(&b[1..7]), 6, Some(b'X' as u16), Some(&mut out)) {
        RResult::Found(2) if b[0] == CANARY_C && b[7] == CANARY_C => {
            emit("strchr_s", "canary", "INTACT")
        }
        _ => emit("strchr_s", "canary", "BAD_CANARY"),
    }
}

// =====================================================================
// strrchr_s analogue: iter().rposition()
// =====================================================================
fn strrchr_checked(
    dest: Option<&[u8]>,
    dmax: usize,
    ch: Option<u16>,
    resultp: Option<&mut Option<usize>>,
) -> RResult {
    let out = match resultp {
        Some(p) => p,
        None => return RResult::Refused,
    };
    *out = None;
    let d = match dest {
        Some(d) => d,
        None => return RResult::Refused,
    };
    if dmax == 0 || dmax > LIM_STR {
        return RResult::Refused;
    }
    let c = match ch {
        Some(c) if c <= u8::MAX as u16 => c as u8,
        _ => return RResult::Refused,
    };
    if dmax > d.len() {
        return RResult::Refused;
    }
    match d[..dmax].iter().rposition(|&x| x == c) {
        Some(i) => {
            *out = Some(i);
            RResult::Found(i)
        }
        None => RResult::NotFound,
    }
}

fn run_strrchr() {
    let hay = b"abXabY"; // 'a' first@0 last@3

    let mut out = Some(999usize);
    match strrchr_checked(Some(hay), hay.len(), Some(b'X' as u16), Some(&mut out)) {
        RResult::Found(2) if out == Some(2) => emit("strrchr_s", "found", "FOUND"),
        _ => emit("strrchr_s", "found", "BAD_FOUND"),
    }
    let mut out = Some(999usize);
    match strrchr_checked(Some(hay), hay.len(), Some(b'Z' as u16), Some(&mut out)) {
        RResult::NotFound if out.is_none() => emit("strrchr_s", "not-found", "NOTFOUND"),
        _ => emit("strrchr_s", "not-found", "BAD_NOTFOUND"),
    }
    let mut out = Some(999usize);
    match strrchr_checked(Some(hay), hay.len(), Some(b'a' as u16), Some(&mut out)) {
        RResult::Found(3) => emit("strrchr_s", "last-occurrence", "IDX3"),
        _ => emit("strrchr_s", "last-occurrence", "BAD_INDEX"),
    }
    let mut out = Some(999usize);
    match strrchr_checked(None, hay.len(), Some(b'X' as u16), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("strrchr_s", "null-dest", "REFUSED"),
        _ => emit("strrchr_s", "null-dest", "BAD_REFUSAL"),
    }
    match strrchr_checked(Some(hay), hay.len(), Some(b'X' as u16), None) {
        RResult::Refused => emit("strrchr_s", "null-resultp", "REFUSED"),
        _ => emit("strrchr_s", "null-resultp", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match strrchr_checked(Some(hay), 0, Some(b'X' as u16), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("strrchr_s", "zero-dmax", "REFUSED"),
        _ => emit("strrchr_s", "zero-dmax", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match strrchr_checked(Some(hay), LIM_STR + 1, Some(b'X' as u16), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("strrchr_s", "over-dmax", "REFUSED"),
        _ => emit("strrchr_s", "over-dmax", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match strrchr_checked(Some(hay), hay.len(), Some(256), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("strrchr_s", "ch-over", "REFUSED"),
        _ => emit("strrchr_s", "ch-over", "BAD_REFUSAL"),
    }

    let mut b = [CANARY_C; 9];
    b[7] = CANARY_C;
    b[1..7].copy_from_slice(hay);
    let mut out = Some(999usize);
    match strrchr_checked(Some(&b[1..7]), 6, Some(b'a' as u16), Some(&mut out)) {
        RResult::Found(3) if b[0] == CANARY_C && b[7] == CANARY_C => {
            emit("strrchr_s", "canary", "INTACT")
        }
        _ => emit("strrchr_s", "canary", "BAD_CANARY"),
    }
}

// =====================================================================
// strstr_s analogue: str::contains + str::find (byte offsets match C).
// =====================================================================
fn strstr_checked(
    dest: Option<&[u8]>,
    dmax: usize,
    src: Option<&[u8]>,
    slen: usize,
    resultp: Option<&mut Option<usize>>,
) -> RResult {
    let out = match resultp {
        Some(p) => p,
        None => return RResult::Refused,
    };
    *out = None;
    let d = match dest {
        Some(d) => d,
        None => return RResult::Refused,
    };
    let s = match src {
        Some(s) => s,
        None => return RResult::Refused,
    };
    if dmax == 0 || dmax > LIM_STR || slen == 0 || slen > LIM_STR {
        return RResult::Refused;
    }
    if dmax > d.len() || slen > s.len() {
        return RResult::Refused;
    }
    // All byte buffers used here are printable ASCII, so this is safe.
    let hay = std::str::from_utf8(&d[..dmax]).unwrap();
    let needle = std::str::from_utf8(&s[..slen]).unwrap();
    if hay.contains(needle) {
        let i = hay.find(needle).unwrap();
        *out = Some(i);
        RResult::Found(i)
    } else {
        RResult::NotFound
    }
}

fn run_strstr() {
    let hay = b"abXcdY"; // "Xc" at index 2
    let sub = b"Xc";
    let miss = b"ZZ";

    let mut out = Some(999usize);
    match strstr_checked(Some(hay), hay.len(), Some(sub), sub.len(), Some(&mut out)) {
        RResult::Found(2) if out == Some(2) => emit("strstr_s", "found", "FOUND"),
        _ => emit("strstr_s", "found", "BAD_FOUND"),
    }
    let mut out = Some(999usize);
    match strstr_checked(Some(hay), hay.len(), Some(miss), miss.len(), Some(&mut out)) {
        RResult::NotFound if out.is_none() => emit("strstr_s", "not-found", "NOTFOUND"),
        _ => emit("strstr_s", "not-found", "BAD_NOTFOUND"),
    }
    let mut out = Some(999usize);
    match strstr_checked(None, hay.len(), Some(sub), sub.len(), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("strstr_s", "null-dest", "REFUSED"),
        _ => emit("strstr_s", "null-dest", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match strstr_checked(Some(hay), hay.len(), None, sub.len(), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("strstr_s", "null-src", "REFUSED"),
        _ => emit("strstr_s", "null-src", "BAD_REFUSAL"),
    }
    match strstr_checked(Some(hay), hay.len(), Some(sub), sub.len(), None) {
        RResult::Refused => emit("strstr_s", "null-resultp", "REFUSED"),
        _ => emit("strstr_s", "null-resultp", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match strstr_checked(Some(hay), 0, Some(sub), sub.len(), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("strstr_s", "zero-dmax", "REFUSED"),
        _ => emit("strstr_s", "zero-dmax", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match strstr_checked(Some(hay), LIM_STR + 1, Some(sub), sub.len(), Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("strstr_s", "over-dmax", "REFUSED"),
        _ => emit("strstr_s", "over-dmax", "BAD_REFUSAL"),
    }
    let mut out = Some(999usize);
    match strstr_checked(Some(hay), hay.len(), Some(sub), 0, Some(&mut out)) {
        RResult::Refused if out.is_none() => emit("strstr_s", "zero-slen", "REFUSED"),
        _ => emit("strstr_s", "zero-slen", "BAD_REFUSAL"),
    }

    let mut b = [CANARY_C; 9];
    b[7] = CANARY_C;
    b[1..7].copy_from_slice(hay);
    let mut out = Some(999usize);
    match strstr_checked(Some(&b[1..7]), 6, Some(sub), sub.len(), Some(&mut out)) {
        RResult::Found(2) if b[0] == CANARY_C && b[7] == CANARY_C => {
            emit("strstr_s", "canary", "INTACT")
        }
        _ => emit("strstr_s", "canary", "BAD_CANARY"),
    }
}

fn main() {
    run_bsearch();
    run_memrchr();
    run_memchr();
    run_strchr();
    run_strrchr();
    run_strstr();
}
